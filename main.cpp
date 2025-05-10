#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits.h>
#include <mpi.h>
#include <unordered_set>
#include <vector>

#include "PairQueue.hpp"

enum Role {
    GEO,
    ART,
};

enum State {
    REST,
    WAIT_FOR_QUEUE,
    WAIT_FOR_PAIR,
    LOOK_FOR_PAIR,
    PAIRED,
    WAIT_FOR_RESOURCE,
    IN_SECTION,
};

enum MessageType {
    REQ_QUEUE,
    ACK_QUEUE,
    AVAILABLE,
    PAIR,
    RELEASE_PAIR,
    // other
    UNPAIR,
};

bool is_pairing_allowed(int available_size, const PairQueue& pair_queue, const int time_vector[], int size,
                        int self_timestamp) {
    for (int i = 0; i < size; i++) {
        if (pair_queue.getPosition(i) + 1 > available_size && time_vector[i] < self_timestamp) {
            return false;
        }
    }

    return true;
}

std::unordered_map<int, int> fill_map_with_artists(const Role process_roles[], int size) {
    std::unordered_map<int, int> result;

    for (int i = 0; i < size; i++) {
        if (process_roles[i] == ART) {
            result[i] = 0;
        }
    }

    return result;
}

int find_least_paired_process(const std::unordered_map<int, int>& pair_count_history, std::unordered_set<int> available_artists) {
    int min_paired_count = INT_MAX;
    int min_paired_process = -1;

    for (const auto& entry : pair_count_history) {
        if (available_artists.find(entry.first) != available_artists.end() && entry.second < min_paired_count) {
            min_paired_count = entry.second;
            min_paired_process = entry.first;
        }
    }

    return min_paired_process;
}

int main(int argc, char** argv) {
    int size, rank;
    State state = REST;
    int role_geo_count = 0;
    int role_art_count = 0;
    int time_vector[size]{};

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    Role process_roles[size];

    printf("[%d][%d] Process started of %d\n", rank, time_vector[rank], size);

    if (rank == 0) {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));

        while (role_art_count != 0 && role_geo_count != 0) {
            role_art_count = 0;
            role_geo_count = 0;

            for (int i = 0; i < size; i++) {
                if (std::rand() % 2 == 0) {
                    process_roles[i] = GEO;
                    role_geo_count++;
                }
                else {
                    process_roles[i] = ART;
                    role_art_count++;
                }
            }
        }

        printf("[%d][%d] Roles generated, my role: %s\n", rank, time_vector[rank],
               (process_roles[0] == GEO ? "GEO" : "ART"));
    }

    MPI_Bcast(process_roles, size, MPI_INT, 0, MPI_COMM_WORLD);
    const Role role = process_roles[rank];
    if (rank != 0) {
        printf("[%d][%d] Received role: %s\n", rank, time_vector[rank], (role == GEO ? "GEO" : "ART"));
    }

    bool is_done = false;

    switch (role) {
    case GEO: {
        // MPI
        MPI_Status status;
        int message_len = 0;
        int rec_message_buf[10]{}, send_message_buf[10]{};

        // Base structures
        PairQueue pair_queue;
        std::unordered_set<int> available_artists;
        std::unordered_map<int, int> pair_count_history;
        std::unordered_set<int> pair_requested;
        int process_to_pair = -1;

        while (!is_done) {
            switch (state) {
            case REST:
                state = WAIT_FOR_QUEUE;
                break;
            case WAIT_FOR_QUEUE: {
                int ack_counter = 0;

                pair_queue.add(rank, time_vector[rank]);
                for (int i = 0; i < size; i++) {
                    if (process_roles[i] != rank && process_roles[i] == GEO) {
                        send_message_buf[0] = ++time_vector[rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, i, REQ_QUEUE, MPI_COMM_WORLD);
                    }
                }

                while (ack_counter != role_geo_count - 1) {
                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[rank] = std::max(rec_timestamp, time_vector[rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, status.MPI_SOURCE, ACK_QUEUE, MPI_COMM_WORLD);
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
                        if (process_roles[status.MPI_SOURCE] == GEO) {
                            pair_queue.remove(status.MPI_SOURCE);
                            // with who ART out of queue
                            int busy_artist = rec_message_buf[1];
                            available_artists.erase(busy_artist);
                        }
                        // ART source not possible here
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
                bool is_first_match = true;

                // wait for available artist
                while (!is_pairing_allowed(
                    available_artists.size(), pair_queue, time_vector, size, time_vector[rank])) {

                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[rank] = std::max(rec_timestamp, time_vector[rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, status.MPI_SOURCE, ACK_QUEUE, MPI_COMM_WORLD);
                        break;
                    }
                    case AVAILABLE: {
                        available_artists.insert(status.MPI_SOURCE);
                        break;
                    }
                    case RELEASE_PAIR: {
                        if (process_roles[status.MPI_SOURCE] == GEO) {
                            pair_queue.remove(status.MPI_SOURCE);
                            // with who ART out of queue
                            int busy_artist = rec_message_buf[1];
                            available_artists.erase(busy_artist);
                        }
                        // ART source not possible here
                        break;
                    }
                    default: break;
                    }
                }

                // allowed for available artist
                // if process is in cut then try to send request for pairing
                while (process_to_pair == -1) {
                    if (is_first_match) {
                        int best_candidate = find_least_paired_process(pair_count_history, available_artists);
                        send_message_buf[0] = ++time_vector[rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, best_candidate, PAIR, MPI_COMM_WORLD);
                        pair_requested.insert(best_candidate);
                        is_first_match = false;
                    }

                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                    MPI_Get_count(&status, MPI_INT, &message_len);
                    MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,
                             MPI_STATUS_IGNORE);

                    const auto message_type = static_cast<MessageType>(status.MPI_TAG);
                    int rec_timestamp = rec_message_buf[0];
                    time_vector[status.MPI_SOURCE] = rec_timestamp;
                    time_vector[rank] = std::max(rec_timestamp, time_vector[rank]) + 1;

                    switch (message_type) {
                    case REQ_QUEUE: {
                        pair_queue.add(status.MPI_SOURCE, rec_timestamp);

                        send_message_buf[0] = ++time_vector[rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, status.MPI_SOURCE, ACK_QUEUE, MPI_COMM_WORLD);
                        break;
                    }
                    case AVAILABLE: {
                        // add or not in that case?
                        break;
                    }
                    default: break;
                    }
                }

                state = PAIRED;
                break;
            }
            case PAIRED: {
                is_done = true;
                break;
            }
            }
        }

        break;
    }
    case ART:
        while (!is_done) {
        }

        break;
    }

    MPI_Finalize();
}
