#pragma once
#include <set>
#include <unordered_map>

#ifndef PAIRQUEUE_HPP
#define PAIRQUEUE_HPP

struct PairQueueEntry {
    int processId;
    int timestamp;

    // Order by timestamp, then processId to ensure uniqueness
    bool operator<(const PairQueueEntry& other) const {
        if (timestamp == other.timestamp)
            return processId < other.processId;
        return timestamp < other.timestamp;
    }
};

class PairQueue {
    std::set<PairQueueEntry> queue;
    std::unordered_map<int, std::set<PairQueueEntry>::iterator> id_map;

public:
    void add(int id, int timestamp);

    void remove(int id);

    int getPosition(int id) const;

    // void printQueue() const {
    //     std::cout << "Queue: ";
    //     for (const auto& p : queue) {
    //         std::cout << "(" << p.id << ", " << p.timestamp << ") ";
    //     }
    //     std::cout << "\n";
    // }

    // std::vector<int> getQueueAsVector() const {
    //     std::vector<int> ids;
    //     for (const auto& p : queue) ids.push_back(p.id);
    //     return ids;
    // }
};



#endif //PAIRQUEUE_HPP
