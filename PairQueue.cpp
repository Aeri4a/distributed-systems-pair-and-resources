#include "PairQueue.hpp"

void PairQueue::add(const int id, const int timestamp) {
    this->pair_queue.push_back({id, timestamp});
    this->pair_queue.sort([](const PairQueueEntry& a, const PairQueueEntry& b) {
        if (a.timestamp == b.timestamp)
            return a.processId < b.processId;
        return a.timestamp < b.timestamp;
    });
    // remove(id); // Ensure no duplicates
    // auto [it, inserted] = queue.insert({id, timestamp});
    // if (inserted) {
    //     id_map[id] = it;
    // }
}

void PairQueue::remove(const int id) {
    this->pair_queue.remove_if([id](const PairQueueEntry& p) { return p.processId == id; });
    // const auto it = id_map.find(id);
    // if (it != id_map.end()) {
    //     queue.erase(it->second);
    //     id_map.erase(it);
    // }
}

int PairQueue::getPosition(const int id) const {
    int position = 0;

    for (auto it = this->pair_queue.begin(); it != this->pair_queue.end(); ++it, ++position) {
        if (it->processId == id) {
            break;
        }
    }

    return position;
    // const auto it = id_map.find(id);
    // if (it == id_map.end()) return -1;
    //
    // int pos = 0;
    // for (auto itr = queue.begin(); itr != it->second; ++itr)
    //     ++pos;
    // return pos;
}
