#include "PairQueue.hpp"

void PairQueue::add(const int id, const int timestamp) {
    this->pair_queue.push_back({id, timestamp});
    this->pair_queue.sort([](const PairQueueEntry& a, const PairQueueEntry& b) {
        if (a.timestamp == b.timestamp)
            return a.processId < b.processId;
        return a.timestamp < b.timestamp;
    });
}

void PairQueue::remove(const int id) {
    this->pair_queue.remove_if([id](const PairQueueEntry& p) { return p.processId == id; });
}

int PairQueue::getPosition(const int id) const {
    int position = 0;

    for (auto it = this->pair_queue.begin(); it != this->pair_queue.end(); ++it, ++position) {
        if (it->processId == id) {
            break;
        }
    }

    return position;
}
