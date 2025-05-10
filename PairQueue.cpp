#include "PairQueue.hpp"

void PairQueue::add(int id, int timestamp) {
    remove(id); // Ensure no duplicates
    auto [it, inserted] = queue.insert({id, timestamp});
    if (inserted) {
        id_map[id] = it;
    }
}

void PairQueue::remove(int id) {
    auto it = id_map.find(id);
    if (it != id_map.end()) {
        queue.erase(it->second);
        id_map.erase(it);
    }
}

int PairQueue::getPosition(int id) const {
    auto it = id_map.find(id);
    if (it == id_map.end()) return -1;

    int pos = 0;
    for (auto itr = queue.begin(); itr != it->second; ++itr)
        ++pos;
    return pos;
}