#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mpi.h>
#include <thread>
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
    // other
    UNPAIR, // A->G
};

constexpr int REQ_QUEUE_LEN = 1;
constexpr int ACK_QUEUE_LEN = 1;
constexpr int AVAILABLE_LEN = 1;
constexpr int PAIR_LEN = 1;
constexpr int RELEASE_PAIR_LEN = 2;
constexpr int UNPAIR_LEN = 1;

// enum MessageLength {
//     REQ_QUEUE_LEN, // G->G
//     ACK_QUEUE_LEN, // G->G
//     AVAILABLE_LEN, // A->G
//     PAIR_LEN, // AG->AG
//     RELEASE_PAIR_LEN, // G->AG
//     // other
//     UNPAIR_LEN, // A->G
// };

bool is_pairing_allowed(const std::size_t available_size, const PairQueue& pair_queue,
                        const std::vector<int>& time_vector, int self_timestamp, const std::vector<Role>& process_roles,
                        const int world_rank) {

    const int process_position = pair_queue.getPosition(world_rank);

    if (process_position > available_size)
        return false;

    // const int drop_allow_count = available_size - process_position;
    // int geo_below_with_lower_timestamp = 0;
    //
    // for (int i = 0; i < time_vector.size(); i++) {
    //     if (process_roles[i] != GEO)
    //         continue;
    //
    //     const int pos_in_queue_for_i_process = pair_queue.getPosition(i);
    //
    //     if (pos_in_queue_for_i_process < process_position)
    //         continue;
    //
    //     if (time_vector[i] < self_timestamp) {
    //         if (++geo_below_with_lower_timestamp > drop_allow_count)
    //             return false;
    //     }
    // }

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

int main(int argc, char** argv) {
    int world_size, world_rank, provided;
    State state = REST;
    int role_geo_count = 0;
    int role_art_count = 0;

    MPI_Init_thread(&argc, &argv, 1, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    /*int time_vector[world_size]{};
    Role process_roles[world_size]{};*/
    std::vector<int> time_vector;
    time_vector.resize(world_size, 0);
    std::vector<Role> process_roles;
    process_roles.resize(world_size, GEO);

    printf("(%d) [%d] Process started of %d\n", world_rank, time_vector[world_rank], world_size);

    if (world_rank == 0) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));

        while (role_art_count == 0 || role_geo_count == 0) {
            role_art_count = 0;
            role_geo_count = 0;

            for (int i = 0; i < world_size; i++) {
                if (std::rand() % 2 == 0) {
                    process_roles[i] = GEO;
                    role_geo_count++;
                }
                else {
                    process_roles[i] = ART;
                    role_art_count++;
                }
            }
            printf("GEO COUNT: %d, ART COUNT: %d\n", role_geo_count, role_art_count);
        }

        printf("(%d) [%d] Roles generated, my role: %s\n", world_rank, time_vector[world_rank],
               (process_roles[0] == GEO ? "GEO" : "ART"));
    }

    MPI_Bcast(process_roles.data(), world_size, MPI_INT, 0, MPI_COMM_WORLD);
    const Role role = process_roles[world_rank];
    if (world_rank != 0) {
        printf("(%d) [%d] Received role: %s\n", world_rank, time_vector[world_rank], (role == GEO ? "GEO" : "ART"));
    }

    bool is_done = false;

    switch (role) {
    case GEO: {
        // MPI
        MPI_Status status;
        int message_len = 0;
        int rec_message_buf[2]{}, send_message_buf[2]{};

        // Base structures
        PairQueue pair_queue;
        std::unordered_set<int> available_artists;
        std::unordered_map<int, int> pair_count_history = fill_map_with_artists(process_roles, world_size);
        // edge case where we send to all and all respond with not us, we have to then resend again to all PAIR
        // this below
        std::unordered_set<int> pair_requested; // dunno now if necessary
        int process_to_pair = -1;

        while (!is_done) {
            switch (state) {
            case REST: {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                printf("(G%d) [%d] Changed status to: REST\n", world_rank, time_vector[world_rank]);
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
                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[world_rank] = std::max(rec_timestamp, time_vector[world_rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, status.MPI_SOURCE, ACK_QUEUE,
                                 MPI_COMM_WORLD);
                        break;
                    }
                    case ACK_QUEUE: {
                        ack_counter++;
                        break;
                    }
                    case AVAILABLE: {
                        available_artists.insert(status.MPI_SOURCE);
                        break;
                    }
                    case RELEASE_PAIR: {
                        pair_queue.remove(status.MPI_SOURCE);
                        // with who ART out of queue
                        int busy_artist = rec_message_buf[1];
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
                while (!is_pairing_allowed(available_artists.size(), pair_queue, time_vector, time_vector[world_rank],
                                           process_roles, world_rank)) {

                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[world_rank] = std::max(rec_timestamp, time_vector[world_rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, status.MPI_SOURCE, ACK_QUEUE,
                                 MPI_COMM_WORLD);
                        break;
                    }
                    case AVAILABLE: {
                        available_artists.insert(status.MPI_SOURCE);
                        break;
                    }
                    case RELEASE_PAIR: {
                        pair_queue.remove(status.MPI_SOURCE);
                        // with who ART out of queue
                        int busy_artist = rec_message_buf[1];
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

                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[world_rank] = std::max(rec_timestamp, time_vector[world_rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, ACK_QUEUE_LEN, MPI_INT, status.MPI_SOURCE, ACK_QUEUE,
                                 MPI_COMM_WORLD);
                        break;
                    }
                    case AVAILABLE: {
                        available_artists.insert(status.MPI_SOURCE);
                        if (!is_first_match) {
                            send_message_buf[0] = ++time_vector[world_rank];
                            MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, status.MPI_SOURCE, PAIR, MPI_COMM_WORLD);
                            // pair_requested.insert(status.MPI_SOURCE);
                        }
                        break;
                    }
                    case RELEASE_PAIR: {
                        int artist_pair = rec_message_buf[1];
                        // if someone else then just remove them from queue and available
                        pair_queue.remove(status.MPI_SOURCE);
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
                            printf("(G%d) [%d] Best pair failed, sending to all\n", world_rank,
                                   time_vector[world_rank]);
                        }
                        break;
                    }
                    case PAIR: {
                        process_to_pair = status.MPI_SOURCE;
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
                // is_done = true;

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

        break;
    }
    case ART: {
        // MPI
        MPI_Status status;
        int message_len = 0;
        int rec_message_buf[2]{}, send_message_buf[2]{};

        // (PROCESS_ID/IS_PAIR_SEND)
        std::list<std::pair<int, bool>> pair_wait_queue;
        int process_to_pair = -1;

        while (!is_done) {
            switch (state) {
            case REST: {
                printf("(A%d) [%d] Changed status to: REST\n", world_rank, time_vector[world_rank]);
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
                        MPI_Send(send_message_buf, PAIR_LEN, MPI_INT, pair_wait_queue.front().first, PAIR,
                                 MPI_COMM_WORLD);
                        pair_wait_queue.front().second = true;
                    }

                    // messages...
                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[world_rank] = std::max(rec_timestamp, time_vector[world_rank]) + 1;

                    switch (message_type) {
                    case PAIR: {
                        pair_wait_queue.emplace_back(status.MPI_SOURCE, false);
                        break;
                    }
                    case RELEASE_PAIR: {
                        // it is not from "our" geo
                        if (pair_wait_queue.empty()) {
                            break;
                        }

                        if (status.MPI_SOURCE != pair_wait_queue.front().first) {
                            // check if in our queue and if then delete
                            for (auto it = pair_wait_queue.begin(); it != pair_wait_queue.end(); ++it) {
                                if (it->first == status.MPI_SOURCE) {
                                    pair_wait_queue.erase(it);
                                    break;
                                }
                            }
                        }
                        else {
                            // geo we want
                            // if with us "happy end"
                            int artist_pair = rec_message_buf[1];
                            if (artist_pair == world_rank) {
                                process_to_pair = status.MPI_SOURCE;
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
                    default:
                        break;
                    }
                }

                state = WAIT_FOR_RESOURCE;
                break;
            }
            case WAIT_FOR_RESOURCE: {
                // is_done = true;
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // RESET HARD
                process_to_pair = -1;
                state = REST;
                break;
            }
            case IN_SECTION: {
                break;
            }
            default:
                break;
            }
        }
        break;
    }
    default: {
        printf("NOT AHERE PLZ\n");
        break;
    }
    }

    MPI_Finalize();
}
