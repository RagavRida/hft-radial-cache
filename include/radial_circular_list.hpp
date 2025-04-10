#ifndef RADIAL_CIRCULAR_LIST_HPP
#define RADIAL_CIRCULAR_LIST_HPP

#include "lockfree_map.hpp"
#include <atomic>
#ifdef __linux__
#include <numa.h>
#endif

class RadialCircularList {
private:
    LockFreeHashTable midpoints;
    size_t max_nodes;
    std::atomic<size_t> total_nodes;
    std::vector<Node*> node_pool;
    std::atomic<size_t> pool_index;

public:
    RadialCircularList(size_t max = 1000);
    ~RadialCircularList();

    bool insert(double value, const std::string& midpoint, int priority = 0, double expiry_time = 60.0);
    bool insert_batch(const std::vector<std::tuple<double, std::string, int, double>>& batch);
    Node* get_highest_priority(const std::string& midpoint);
    std::vector<Node*> get_highest_priority_batch(const std::vector<std::string>& midpoints);
};

#endif