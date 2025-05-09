#include <algorithm>
#include <cstdio>
#include <iostream>
#include <list>
#include <vector>

#include <mpi.h>
#include <random>
#include <thread>

enum ProcessState { REST, WAIT_FOR_RESOURCE, IN_SECTION };

enum MessageType {
    REQ_RES,
    ACK_RES,
};

struct RecvResult {
    MPI_Status status;
    int msg_length;
    int msg_buffer[2];
    bool received;
};

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
    time_vector[recv_result.status.MPI_SOURCE] = std::max(rec_timestamp, own_timestamp);
    time_vector[world_rank] = time_vector[recv_result.status.MPI_SOURCE] + 1;
}

const char* msg_type_string(const MessageType msgType) {
    switch (msgType) {
    case REQ_RES:
        return "REQ_RES";
    case ACK_RES:
        return "ACK_RES";
    }
    return "";
}

int main(int argc, char** argv) {
    int resource_count = 10;
    if (argc > 1) {
        try {
            resource_count = std::stoi(argv[1]);
        }
        catch (std::invalid_argument const& ex) {
            fprintf(stderr, "%s\n", ex.what());
            return EXIT_FAILURE;
        }
    }

    int world_size, world_rank, name_length, provided;
    char processor_name[100]{};
    std::vector<int> time_vector, used_resources;
    std::list<int> resource_queue;

    ProcessState state = REST;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> wait_dist(1, 5), resource_dist(1, resource_count);

    int requested_resources = resource_dist(gen);

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Get_processor_name(processor_name, &name_length);

    const int artist_length = world_size;

    time_vector.resize(world_size, 0);
    used_resources.resize(artist_length, 0);

    printf("[%d:%d] Process enters (%s)\n", world_rank, time_vector[world_rank], processor_name);

    auto start = std::chrono::high_resolution_clock::now();
    auto rest_wait_seconds = wait_dist(gen);
    auto section_wait_seconds = wait_dist(gen);

    while (true) {
        int send_message_buf[2]{};

        switch (state) {
        case REST: {
            const auto result = receive_nonblocking();

            if (result.received == false) {
                auto stop = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);

                if (duration.count() >= rest_wait_seconds) {
                    for (int i = 0; i < used_resources.size(); i++) {
                        if (i == world_rank)
                            continue;
                        used_resources[i] += resource_count;
                        send_message_buf[0] = ++time_vector[world_rank];
                        MPI_Send(send_message_buf, 1, MPI_INT, i, REQ_RES, MPI_COMM_WORLD);
                    }
                    state = WAIT_FOR_RESOURCE;
                    printf("[%d:%d] Switching to WAIT_FOR_RESOURCE (%d resources)\n", world_rank,
                           time_vector[world_rank], requested_resources);
                }
                continue;
            }

            const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
            const auto message_source = result.status.MPI_SOURCE;

            update_time_vector(result, time_vector, world_rank);

            switch (message_type) {
            case REQ_RES: {
                send_message_buf[0] = ++time_vector[world_rank];
                send_message_buf[1] = resource_count;
                MPI_Send(send_message_buf, 2, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                break;
            }
            case ACK_RES: {
                used_resources[message_source] -= result.msg_buffer[1];
                break;
            }
            }

            break;
        }

        case WAIT_FOR_RESOURCE: {
            const auto result = receive_blocking();

            const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
            const auto message_source = result.status.MPI_SOURCE;

            int rec_timestamp = result.msg_buffer[0];
            int own_timestamp = time_vector[world_rank];
            update_time_vector(result, time_vector, world_rank);

            switch (message_type) {
            case REQ_RES: {
                bool other_has_priority =
                    rec_timestamp < own_timestamp || (rec_timestamp == own_timestamp && message_source < world_rank);
                if (other_has_priority ||
                    std::find(resource_queue.begin(), resource_queue.end(), message_source) != resource_queue.end()) {
                    send_message_buf[0] = ++time_vector[world_rank];
                    send_message_buf[1] = resource_count;
                    MPI_Send(send_message_buf, 2, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                }
                else {
                    send_message_buf[0] = ++time_vector[world_rank];
                    send_message_buf[1] = resource_count - requested_resources;
                    MPI_Send(send_message_buf, 2, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                    resource_queue.emplace_back(message_source);
                }
                break;
            }

            case ACK_RES: {
                used_resources[message_source] -= result.msg_buffer[1];

                const int resource_sum = std::accumulate(used_resources.begin(), used_resources.end(), 0);
                /*printf("[%d:%d] %d taken, %d requested, %d total\n", world_rank, time_vector[world_rank],
                   resource_sum, requested_resources, resource_count);*/

                if (resource_sum + requested_resources <= resource_count) {
                    state = IN_SECTION;
                    start = std::chrono::high_resolution_clock::now();
                    section_wait_seconds = wait_dist(gen);
                    printf("[%d:%d] Switching to IN_SECTION (%d resources)\n", world_rank, time_vector[world_rank],
                           requested_resources);
                }

                break;
            }
            }

            break;
        }

        case IN_SECTION: {
            const auto result = receive_nonblocking();

            if (result.received == false) {
                auto stop = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(stop - start);

                if (duration.count() >= section_wait_seconds) {
                    while (!resource_queue.empty()) {
                        send_message_buf[0] = ++time_vector[world_rank];
                        send_message_buf[1] = requested_resources;
                        MPI_Send(send_message_buf, 2, MPI_INT, resource_queue.front(), ACK_RES, MPI_COMM_WORLD);

                        resource_queue.pop_front();
                    }
                    state = REST;
                    start = std::chrono::high_resolution_clock::now();
                    rest_wait_seconds = wait_dist(gen);
                    requested_resources = resource_dist(gen);
                    printf("[%d:%d] Switching to REST\n", world_rank, time_vector[world_rank]);
                }
                continue;
            }

            const auto message_type = static_cast<MessageType>(result.status.MPI_TAG);
            const auto message_source = result.status.MPI_SOURCE;

            update_time_vector(result, time_vector, world_rank);

            switch (message_type) {
            case REQ_RES: {
                send_message_buf[0] = ++time_vector[world_rank];
                send_message_buf[1] = resource_count - requested_resources;
                MPI_Send(send_message_buf, 2, MPI_INT, message_source, ACK_RES, MPI_COMM_WORLD);
                resource_queue.emplace_back(message_source);
                break;
            }
            case ACK_RES: {
                used_resources[message_source] -= result.msg_buffer[1];
                break;
            }
            }

            break;
        }
        }
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
