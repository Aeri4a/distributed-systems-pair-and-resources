#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mpi.h>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PairQueue.hpp"

enum Role {
    GEO,
    ART,
};

enum State {
    REST, // G+A
    WAIT_FOR_QUEUE, // G
    WAIT_FOR_PAIR, // A
    LOOK_FOR_PAIR, // G
    PAIRED, // G+A
    WAIT_FOR_RESOURCE, // A
    IN_SECTION, // A
};

enum MessageType {
    REQ_QUEUE, // G->G
    ACK_QUEUE, // G->G
    AVAILABLE, // A->G
    PAIR, // AG->AG
    RELEASE_PAIR, // G->AG
    REQ_RES, // A->A
    ACK_RES, // A->A
    UNPAIR, // A->G
};

struct RecvResult {
    MPI_Status status;
    int msg_length;
    int msg_buffer[2];
    bool received;
};

constexpr int REQ_QUEUE_LEN = 1;
constexpr int ACK_QUEUE_LEN = 1;
constexpr int AVAILABLE_LEN = 1;
constexpr int PAIR_LEN = 1;
constexpr int RELEASE_PAIR_LEN = 2;
constexpr int REQ_RES_LEN = 1;
constexpr int ACK_RES_LEN = 2;
constexpr int UNPAIR_LEN = 1;

int resource_count = 10;

int world_size, world_rank, name_length;
char processor_name[MPI_MAX_PROCESSOR_NAME];
int send_message_buf[2]{};

std::vector<int> time_vector, used_resources;
std::vector<Role> process_roles;

int role_geo_count = 0;
int role_art_count = 0;

std::unordered_set<int> artist_ranks;

std::random_device rd;
std::mt19937 gen(rd());

[[noreturn]] void geoengineer();
[[noreturn]] void artist();

RecvResult receive_blocking();
RecvResult receive_nonblocking();
void update_time_vector(const RecvResult& recv_result, std::vector<int>& time_vector, int world_rank);
bool is_pairing_allowed(std::size_t available_size, const PairQueue& pair_queue, int world_rank);
std::unordered_map<int, int> fill_map_with_artists(const std::vector<Role>& process_roles, int size);
int find_least_paired_process(const std::unordered_map<int, int>& pair_count_history,
                              std::unordered_set<int>& available_artists);

int main(int argc, char** argv) {
    if (argc > 1) {
        try {
            resource_count = std::stoi(argv[1]);
        }
        catch (const std::invalid_argument& e) {
            fprintf(stderr, "Invalid argument: %s\n", e.what());
            return EXIT_FAILURE;
        }
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Get_processor_name(processor_name, &name_length);

    if (world_size < 2) {
        fprintf(stderr, "Too few processes! Exiting\n");
        return EXIT_FAILURE;
    }

    time_vector.resize(world_size, 0);
    used_resources.resize(world_size, 0);
    process_roles.resize(world_size, GEO);

    printf("(%d) [%d] Process started of %d (%s)\n", world_rank, time_vector[world_rank], world_size, processor_name);

    std::uniform_int_distribution<> role_dist(1, world_size - 1);

    if (world_rank == 0) {
        role_geo_count = role_dist(gen);
        role_art_count = world_size - role_geo_count;
        for (int i = 0; i < role_art_count; i++) {
            process_roles[i] = ART;
        }
        std::shuffle(process_roles.begin(), process_roles.end(), gen);

        printf("GEO COUNT: %d, ART COUNT: %d\n", role_geo_count, role_art_count);
        printf("(%d) [%d] Roles generated, my role: %s\n", world_rank, time_vector[world_rank],
               process_roles[0] == GEO ? "GEO" : "ART");
    }

    MPI_Bcast(process_roles.data(), world_size, MPI_INT, 0, MPI_COMM_WORLD);
    const Role role = process_roles[world_rank];
    if (world_rank != 0) {
        printf("(%d) [%d] Received role: %s\n", world_rank, time_vector[world_rank], role == GEO ? "GEO" : "ART");
    }

    for (int i = 0; i < world_size; i++) {
        if (process_roles[i] == ART) {
            artist_ranks.insert(i);
        }
    }

    switch (role) {
    case GEO: {
        geoengineer();
    }
    case ART: {
        artist();
    }
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}

[[noreturn]] void geoengineer() {
    // Base structures
    PairQueue pair_queue;
    std::unordered_set<int> available_artists;
    std::unordered_map<int, int> pair_count_history = fill_map_with_artists(process_roles, world_size);
    // edge case where we send to all and all respond with not us, we have to then resend again to all PAIR
    // this below
    std::unordered_set<int> pair_requested; // dunno now if necessary
    int process_to_pair = -1;

    std::uniform_int_distribution<> wait_dist(1, 5);

    int rest_wait_seconds = wait_dist(gen);

    State state = REST;

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        switch (state) {
        case REST: {
            printf("(G%d) [%d] Changed status to: REST (%ds)\n", world_rank, time_vector[world_rank],
                   rest_wait_seconds);

            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            while (duration.count() < rest_wait_seconds) {
                const auto result = receive_nonblocking();
                if (result.received) {
                    const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                    const int message_source = result.status.MPI_SOURCE;
                    int rec_timestamp = result.msg_buffer[0];

                    update_time_vector(result, time_vector, world_rank);

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(message_source, rec_timestamp);

                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, message_source, ACK_QUEUE, MPI_COMM_WORLD);
                        break;
                    }
                    case AVAILABLE: {
                        available_artists.insert(message_source);
                        break;
                    }
                    case RELEASE_PAIR: {
                        pair_queue.remove(message_source);
                        // with who ART out of queue
                        int busy_artist = result.msg_buffer[1];
                        available_artists.erase(busy_artist);
                        break;
                    }
                    default:
                        break;
                    }
                }

                stop = std::chrono::high_resolution_clock::now();
                duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            }

            state = WAIT_FOR_QUEUE;
            break;
        }
        case WAIT_FOR_QUEUE: {
            printf("(G%d) [%d] Changed status to: WAIT_FOR_QUEUE\n", world_rank, time_vector[world_rank]);
            int ack_counter = 0;

            pair_queue.add(world_rank, time_vector[world_rank]);
            for (int i = 0; i < world_size; i++) {
                if (i != world_rank && process_roles[i] == GEO) {
                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, REQ_QUEUE_LEN, MPI_INT, i, REQ_QUEUE, MPI_COMM_WORLD);
                }
            }
            printf("(G%d) [%d] Sent all REQ FOR QUEUE\n", world_rank, time_vector[world_rank]);

            while (ack_counter < role_geo_count - 1) {
                const auto result = receive_blocking();

                const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                const int message_source = result.status.MPI_SOURCE;
                int rec_timestamp = result.msg_buffer[0];

                update_time_vector(result, time_vector, world_rank);

                switch (message_type) {
                case REQ_QUEUE: {
                    pair_queue.add(message_source, rec_timestamp);

                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, message_source, ACK_QUEUE, MPI_COMM_WORLD);
                    break;
                }
                case ACK_QUEUE: {
                    ack_counter++;
                    break;
                }
                case AVAILABLE: {
                    available_artists.insert(message_source);
                    break;
                }
                case RELEASE_PAIR: {
                    pair_queue.remove(message_source);
                    // with who ART out of queue
                    int busy_artist = result.msg_buffer[1];
                    available_artists.erase(busy_artist);

                    break;
                }
                default:
                    break;
                }
            }

            state = LOOK_FOR_PAIR;
            break;
        }
        case LOOK_FOR_PAIR: {
            printf("(G%d) [%d] Changed status to: LOOK_FOR_PAIR\n", world_rank, time_vector[world_rank]);
            int best_candidate = -1;

            // wait for available artist
            while (!is_pairing_allowed(available_artists.size(), pair_queue, world_rank)) {
                const auto result = receive_blocking();

                const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                const int message_source = result.status.MPI_SOURCE;
                int rec_timestamp = result.msg_buffer[0];

                update_time_vector(result, time_vector, world_rank);

                switch (message_type) {
                case REQ_QUEUE: {
                    pair_queue.add(message_source, rec_timestamp);

                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, message_source, ACK_QUEUE, MPI_COMM_WORLD);
                    break;
                }
                case AVAILABLE: {
                    available_artists.insert(message_source);
                    break;
                }
                case RELEASE_PAIR: {
                    pair_queue.remove(message_source);
                    // with who ART out of queue
                    int busy_artist = result.msg_buffer[1];
                    available_artists.erase(busy_artist);
                    // ART source not possible here
                    break;
                }
                default:
                    break;
                }
            }

            printf("(G%d) [%d] Allow to pair (because of cut)\n", world_rank, time_vector[world_rank]);
            // allowed for available artist
            // if process is in cut then try to send request for pairing

            bool is_first_match = true;
            while (process_to_pair == -1) {
                if (is_first_match && !available_artists.empty()) {
                    // best_candidate == -1 means first try failed
                    // first try
                    best_candidate = find_least_paired_process(pair_count_history, available_artists);
                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, best_candidate, PAIR, MPI_COMM_WORLD);
                    // pair_requested.insert(best_candidate);
                    printf("(G%d) [%d] Send pair to best artist (%d)\n", world_rank, time_vector[world_rank],
                           best_candidate);

                    is_first_match = false;
                }

                const auto result = receive_blocking();

                const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                const int message_source = result.status.MPI_SOURCE;
                int rec_timestamp = result.msg_buffer[0];

                update_time_vector(result, time_vector, world_rank);

                switch (message_type) {
                case REQ_QUEUE: {
                    pair_queue.add(message_source, rec_timestamp);

                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, message_source, ACK_QUEUE, MPI_COMM_WORLD);
                    break;
                }
                case AVAILABLE: {
                    available_artists.insert(message_source);
                    if (!is_first_match) {
                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, message_source, PAIR, MPI_COMM_WORLD);
                        // pair_requested.insert(status.MPI_SOURCE);
                    }
                    break;
                }
                case RELEASE_PAIR: {
                    int artist_pair = result.msg_buffer[1];
                    // if someone else then just remove them from queue and available
                    pair_queue.remove(message_source);
                    available_artists.erase(artist_pair);

                    // check if best cand is with me or not
                    if (artist_pair == best_candidate) {

                        // second try to all
                        for (auto art : available_artists) {
                            // send to all available **despite him**
                            // if (art == best_candidate)
                            //     continue;

                            send_message_buf[0] = ++time_vector[world_rank];
                            MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, art, PAIR, MPI_COMM_WORLD);
                            // pair_requested.insert(art);
                        }

                        best_candidate = -1;
                        printf("(G%d) [%d] Best pair failed, sending to all\n", world_rank, time_vector[world_rank]);
                    }
                    break;
                }
                case PAIR: {
                    process_to_pair = result.status.MPI_SOURCE;
                    pair_count_history[process_to_pair]++;

                    printf("(G%d) [%d] Paired with process (%d)\n", world_rank, time_vector[world_rank],
                           process_to_pair);
                    // inform all
                    for (int i = 0; i < world_size; i++) {
                        if (i != world_rank) {
                            send_message_buf[0] = ++time_vector[world_rank];
                            send_message_buf[1] = process_to_pair;
                            MPI_Send(send_message_buf, RELEASE_PAIR_LEN, MPI_INT, i, RELEASE_PAIR, MPI_COMM_WORLD);
                        }
                    }

                    break;
                }
                default:
                    break;
                }
            }

            state = PAIRED;
            break;
        }
        case PAIRED: {
            const auto result = receive_blocking();

            const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
            const int message_source = result.status.MPI_SOURCE;
            int rec_timestamp = result.msg_buffer[0];

            update_time_vector(result, time_vector, world_rank);

            switch (message_type) {
            case REQ_QUEUE: {
                pair_queue.add(message_source, rec_timestamp);

                send_message_buf[0] = ++time_vector[world_rank];
                MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, message_source, ACK_QUEUE, MPI_COMM_WORLD);
                break;
            }
            case AVAILABLE: {
                available_artists.insert(message_source);
                break;
            }
            case RELEASE_PAIR: {
                pair_queue.remove(message_source);
                // with who ART out of queue
                int busy_artist = result.msg_buffer[1];
                available_artists.erase(busy_artist);
                break;
            }
            case UNPAIR: {
                if (message_source != process_to_pair)
                    break;

                printf("(G%d) [%d] Unpaired with %d\n", world_rank, time_vector[world_rank], process_to_pair);

                // RESET HARD
                // available_artists.clear();
                // pair_queue = PairQueue();
                // pair_count_history.clear();
                // pair_requested.clear();
                process_to_pair = -1;
                state = REST;
                break;
            }
            default:
                break;
            }
        }
        default:
            break;
        }
    }
}

[[noreturn]] void artist() {
    // (PROCESS_ID/IS_PAIR_SEND)
    std::list<std::pair<int, bool>> pair_wait_queue;
    std::list<int> resource_queue;
    int process_to_pair = -1;

    std::uniform_int_distribution<> wait_dist(1, 5);

    const auto rest_wait_seconds = wait_dist(gen);
    const auto section_wait_seconds = wait_dist(gen);

    std::uniform_int_distribution<> resource_dist(1, resource_count - 1);
    int requested_resources = 0;

    State state = REST;

    auto start = std::chrono::high_resolution_clock::now();

    while (true) {
        switch (state) {
        case REST: {
            printf("(A%d) [%d] Changed status to: REST (%ds)\n", world_rank, time_vector[world_rank],
                   rest_wait_seconds);

            start = std::chrono::high_resolution_clock::now();
            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            while (duration.count() < rest_wait_seconds) {
                const auto result = receive_nonblocking();
                if (result.received) {
                    const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                    const int message_source = result.status.MPI_SOURCE;
                    // int rec_timestamp = result.msg_buffer[0];

                    update_time_vector(result, time_vector, world_rank);

                    switch (message_type) {
                    case REQ_RES: {
                        send_message_buf[0] = ++time_vector[world_rank];
                        send_message_buf[1] = resource_count;
                        MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                        break;
                    }
                    case ACK_RES: {
                        used_resources[message_source] -= result.msg_buffer[1];
                        break;
                    }
                    default:
                        break;
                    }
                }

                stop = std::chrono::high_resolution_clock::now();
                duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            }

            state = WAIT_FOR_PAIR;
            break;
        }
        case WAIT_FOR_PAIR: {
            printf("(A%d) [%d] Changed status to: WAIT_FOR_PAIR\n", world_rank, time_vector[world_rank]);

            // "broadcast" to all geo
            for (int i = 0; i < world_size; i++) {
                if (process_roles[i] == GEO) {
                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, AVAILABLE_LEN, MPI_INT, i, AVAILABLE, MPI_COMM_WORLD);
                }
            }

            while (process_to_pair == -1) {
                // analyze queue
                if (!pair_wait_queue.empty() && !pair_wait_queue.front().second) {
                    send_message_buf[0] = ++time_vector[world_rank];
                    MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, pair_wait_queue.front().first, PAIR, MPI_COMM_WORLD);
                    pair_wait_queue.front().second = true;
                }

                // messages...
                const auto result = receive_blocking();

                const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                const int message_source = result.status.MPI_SOURCE;
                update_time_vector(result, time_vector, world_rank);

                switch (message_type) {
                case PAIR: {
                    pair_wait_queue.emplace_back(message_source, false);
                    break;
                }
                case RELEASE_PAIR: {
                    // it is not from "our" geo
                    if (pair_wait_queue.empty()) {
                        break;
                    }

                    if (message_source != pair_wait_queue.front().first) {
                        // check if in our queue and if then delete
                        for (auto it = pair_wait_queue.begin(); it != pair_wait_queue.end(); ++it) {
                            if (it->first == message_source) {
                                pair_wait_queue.erase(it);
                                break;
                            }
                        }
                    }
                    else {
                        // geo we want
                        // if with us "happy end"
                        const int artist_pair = result.msg_buffer[1];
                        if (artist_pair == world_rank) {
                            process_to_pair = message_source;
                            pair_wait_queue.clear(); // could send before ?
                            printf("(A%d) [%d] Paired with process (%d)\n", world_rank, time_vector[world_rank],
                                   process_to_pair);
                        }
                        else {
                            // someone else
                            pair_wait_queue.pop_front();
                        }
                    }
                    break;
                }
                case REQ_RES: {
                    send_message_buf[0] = ++time_vector[world_rank];
                    send_message_buf[1] = resource_count;
                    MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                    break;
                }
                case ACK_RES: {
                    used_resources[message_source] -= result.msg_buffer[1];
                    break;
                }
                default:
                    break;
                }
            }

            state = WAIT_FOR_RESOURCE;
            break;
        }
        case WAIT_FOR_RESOURCE: {
            requested_resources = resource_dist(gen);
            for (int i = 0; i < used_resources.size(); i++) {
                if (i == world_rank || artist_ranks.find(i) == artist_ranks.end()) {
                    continue;
                }
                used_resources[i] += resource_count;
                send_message_buf[0] = ++time_vector[world_rank];
                MPI_Send(send_message_buf, REQ_RES_LEN, MPI_INT, i, REQ_RES, MPI_COMM_WORLD);
            }

            printf("(A%d) [%d] Changed status to: WAIT_FOR_RESOURCE (%d resources)\n", world_rank,
                   time_vector[world_rank], requested_resources);

            int resource_sum = std::accumulate(used_resources.begin(), used_resources.end(), 0);
            while (resource_sum + requested_resources > resource_count) {
                const auto result = receive_blocking();

                const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                const int message_source = result.status.MPI_SOURCE;
                const int rec_timestamp = result.msg_buffer[0];
                const int own_timestamp = time_vector[world_rank];

                update_time_vector(result, time_vector, world_rank);

                switch (message_type) {
                case REQ_RES: {
                    bool other_has_priority = rec_timestamp < own_timestamp ||
                        (rec_timestamp == own_timestamp && message_source < world_rank);
                    if (other_has_priority ||
                        std::find(resource_queue.begin(), resource_queue.end(), message_source) !=
                            resource_queue.end()) {
                        send_message_buf[0] = ++time_vector[world_rank];
                        send_message_buf[1] = resource_count;
                        MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                    }
                    else {
                        send_message_buf[0] = ++time_vector[world_rank];
                        send_message_buf[1] = resource_count - requested_resources;
                        MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                        resource_queue.emplace_back(message_source);
                    }

                    break;
                }
                case ACK_RES: {
                    used_resources[message_source] -= result.msg_buffer[1];

                    resource_sum = std::accumulate(used_resources.begin(), used_resources.end(), 0);
                    break;
                }
                default:
                    break;
                }
            }

            state = IN_SECTION;
            break;
        }
        case IN_SECTION: {
            printf("(A%d) [%d] Switching to IN_SECTION (%d resources, %ds)\n", world_rank, time_vector[world_rank],
                   requested_resources, section_wait_seconds);

            start = std::chrono::high_resolution_clock::now();
            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);

            while (duration.count() < section_wait_seconds) {
                const auto result = receive_nonblocking();
                if (result.received) {
                    const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
                    const int message_source = result.status.MPI_SOURCE;

                    update_time_vector(result, time_vector, world_rank);

                    switch (message_type) {
                    case REQ_RES: {
                        send_message_buf[0] = ++time_vector[world_rank];
                        send_message_buf[1] = resource_count - requested_resources;
                        MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                        resource_queue.emplace_back(message_source);
                        break;
                    }
                    case ACK_RES: {
                        used_resources[message_source] -= result.msg_buffer[1];
                        break;
                    }
                    default:
                        break;
                    }
                }

                stop = std::chrono::high_resolution_clock::now();
                duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);
            }

            while (!resource_queue.empty()) {
                send_message_buf[0] = ++time_vector[world_rank];
                send_message_buf[1] = requested_resources;
                MPI_Send(send_message_buf, ACK_RES_LEN, MPI_INT, resource_queue.front(), ACK_RES, MPI_COMM_WORLD);

                resource_queue.pop_front();
            }

            // RESET HARD
            printf("(A%d) [%d] Unpairing with %d\n", world_rank, time_vector[world_rank], process_to_pair);
            send_message_buf[0] = ++time_vector[world_rank];
            MPI_Send(send_message_buf, UNPAIR_LEN, MPI_INT, process_to_pair, UNPAIR, MPI_COMM_WORLD);

            printf("(A%d) [%d] Switching to REST (-%d resources)\n", world_rank, time_vector[world_rank],
                   requested_resources);
            process_to_pair = -1;
            state = REST;
            break;
        }
        default:
            break;
        }
    }
}

RecvResult receive_blocking() {
    auto result = RecvResult{};
    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &result.status);
    MPI_Get_count(&result.status, MPI_INT, &result.msg_length);
    MPI_Recv(result.msg_buffer, result.msg_length, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    return result;
}

RecvResult receive_nonblocking() {
    auto result = RecvResult{};
    int rec;
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &rec, &result.status);
    if (rec == 0)
        return result;

    MPI_Get_count(&result.status, MPI_INT, &result.msg_length);
    MPI_Recv(result.msg_buffer, result.msg_length, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    result.received = true;
    return result;
}

void update_time_vector(const RecvResult& recv_result, std::vector<int>& time_vector, const int world_rank) {
    const int rec_timestamp = recv_result.msg_buffer[0];
    const int own_timestamp = time_vector[world_rank];
    time_vector[recv_result.status.MPI_SOURCE] = rec_timestamp;
    time_vector[world_rank] = std::max(rec_timestamp, own_timestamp) + 1;
}

bool is_pairing_allowed(const std::size_t available_size, const PairQueue& pair_queue, const int world_rank) {
    const int process_position = pair_queue.getPosition(world_rank);
    if (process_position > available_size)
        return false;
    return true;
}

std::unordered_map<int, int> fill_map_with_artists(const std::vector<Role>& process_roles, const int size) {
    std::unordered_map<int, int> result;
    for (int i = 0; i < size; i++) {
        if (process_roles[i] == ART) {
            result[i] = 0;
        }
    }

    return result;
}

int find_least_paired_process(const std::unordered_map<int, int>& pair_count_history,
                              std::unordered_set<int>& available_artists) {
    int min_paired_count = INT_MAX;
    int min_paired_process = -1;

    for (const auto& entry : pair_count_history) {
        if (available_artists.find(entry.first) != available_artists.end() && entry.second < min_paired_count) {
            min_paired_process = entry.first;
            min_paired_count = entry.second;
        }
    }

    return min_paired_process;
}
