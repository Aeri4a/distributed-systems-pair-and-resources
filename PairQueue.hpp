#ifndef PAIRQUEUE_HPP
#define PAIRQUEUE_HPP

#include <list>

struct PairQueueEntry {
    int processId;
    int timestamp;
};

class PairQueue {
    std::list<PairQueueEntry> pair_queue;

public:
    void add(int id, int timestamp);

    void remove(int id);

    int getPosition(int id) const;
};


#endif // PAIRQUEUE_HPP
