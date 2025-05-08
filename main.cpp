#include <algorithm>
#include <cstdio>
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

std::string msgTypeString(const MessageType msgType) {
    switch (msgType) {
    case REQ_RES:
        return "REQ_RES";
    case ACK_RES:
        return "ACK_RES";
    }
    return "";
}

[[noreturn]] int main(int argc, char** argv) {
    int world_size, world_rank, name_length, provided;
    char processor_name[100]{};
    std::vector<int> time_vector, used_resources;
    std::list<int> resource_queue;

    ProcessState state = REST;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(1, 5);

    constexpr int resource_count = 4;
    const int requested_resources = 2;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Get_processor_name(processor_name, &name_length);

    printf("Hello world: %d of %d na (%s)\n", world_rank, world_size, processor_name);

    const int artist_length = world_size;

    time_vector.resize(world_size, 0);
    used_resources.resize(artist_length, 0);

    printf("[%d:%d] Process enters\n", world_rank, time_vector[world_rank]);

    if (world_rank == 0) {
        int send_message_buf[1]{};
        state = WAIT_FOR_RESOURCE;
        printf("[%d:%d] Switching to WAIT_FOR_RESOURCE\n", world_rank, time_vector[world_rank]);
        for (int i = 0; i < used_resources.size(); i++) {
            if (i == world_rank)
                continue;
            used_resources[i] += resource_count;
            send_message_buf[0] = time_vector[world_rank];
            MPI_Send(send_message_buf, 1, MPI_INT, i, REQ_RES, MPI_COMM_WORLD);
            time_vector[world_rank]++;
        }
    }

    while (true) {
        MPI_Status status;
        int message_len = 0;
        int rec_message_buf[10]{}, send_message_buf[10]{};

        bool flag = false;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, reinterpret_cast<int*>(&flag), &status);
        if (flag == false) continue;
        
        MPI_Get_count(&status, MPI_INT, &message_len);
        MPI_Recv(rec_message_buf, message_len, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        const auto message_type = static_cast<MessageType>(status.MPI_TAG);
        int rec_timestamp = rec_message_buf[0];
        int own_timestamp = time_vector[world_rank];

        time_vector[status.MPI_SOURCE] = std::max(rec_timestamp + 1, own_timestamp);
        time_vector[world_rank] = time_vector[status.MPI_SOURCE] + 1;

        rec_timestamp = time_vector[status.MPI_SOURCE];

        printf("[%d:%d] Process received message %s\n", world_rank, time_vector[world_rank],
               msgTypeString(message_type).c_str());

        switch (state) {
        case REST: {
            state = WAIT_FOR_RESOURCE;
            printf("[%d:%d] Switching to WAIT_FOR_RESOURCE\n", world_rank, time_vector[world_rank]);
            for (int i = 0; i < used_resources.size(); i++) {
                if (i == world_rank)
                    continue;
                used_resources[i] += resource_count;
                send_message_buf[0] = time_vector[world_rank];
                MPI_Send(send_message_buf, 1, MPI_INT, i, REQ_RES, MPI_COMM_WORLD);
                time_vector[world_rank]++;
            }

            switch (message_type) {
            case REQ_RES: {
                send_message_buf[0] = time_vector[world_rank];
                send_message_buf[1] = resource_count;
                MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);
                time_vector[world_rank]++;
                break;
            }
            case ACK_RES: {
                break;
            }
            }

            break;
        }

        case WAIT_FOR_RESOURCE: {
            switch (message_type) {
            case REQ_RES: {
                if (rec_timestamp < own_timestamp ||
                    std::find(resource_queue.begin(), resource_queue.end(), status.MPI_SOURCE) !=
                        resource_queue.end()) {
                    send_message_buf[0] = time_vector[world_rank];
                    send_message_buf[1] = resource_count;
                    MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);
                }
                else {
                    send_message_buf[0] = time_vector[world_rank];
                    send_message_buf[1] = resource_count - requested_resources;
                    MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);
                    resource_queue.emplace_back(status.MPI_SOURCE);
                }

                time_vector[world_rank]++;
                break;
            }

            case ACK_RES: {
                used_resources[status.MPI_SOURCE] -= rec_message_buf[1];

                const int resource_sum = std::accumulate(used_resources.begin(), used_resources.end(), 0);
                if (resource_sum + requested_resources <= resource_count) {
                    state = IN_SECTION;
                    printf("[%d:%d] Switching to IN_SECTION\n", world_rank, time_vector[world_rank]);
                }

                break;
            }
            }

            break;
        }

        case IN_SECTION: {
            const int sleep_seconds = dist(gen);
            std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
            
            switch (message_type) {
            case REQ_RES: {
                if (rec_timestamp < own_timestamp ||
                    std::find(resource_queue.begin(), resource_queue.end(), status.MPI_SOURCE) !=
                        resource_queue.end()) {
                    send_message_buf[0] = time_vector[world_rank];
                    send_message_buf[1] = resource_count;
                    MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);
                }
                else {
                    send_message_buf[0] = time_vector[world_rank];
                    send_message_buf[1] = resource_count - requested_resources;
                    MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);
                    resource_queue.emplace_back(status.MPI_SOURCE);
                }

                time_vector[world_rank]++;
                break;
            }
            case ACK_RES: {
                break;
            }
            }

            state = REST;
            printf("[%d:%d] Switching to REST\n", world_rank, time_vector[world_rank]);
            while (!resource_queue.empty()) {
                send_message_buf[0] = time_vector[world_rank];
                send_message_buf[1] = requested_resources;
                MPI_Send(send_message_buf, 2, MPI_INT, status.MPI_SOURCE, ACK_RES, MPI_COMM_WORLD);

                time_vector[world_rank]++;
                resource_queue.pop_front();
            }

            break;
        }
        }
    }

    MPI_Finalize();
}
