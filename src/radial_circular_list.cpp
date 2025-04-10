#include "radial_circular_list.hpp"

RadialCircularList::RadialCircularList(size_t max)
    : midpoints(max / 10), max_nodes(max), total_nodes(0), pool_index(0) {
#ifdef __linux__
    if (numa_available() >= 0) {
        numa_set_preferred(numa_node_of_cpu(sched_getcpu()));
    }
#endif
    node_pool.resize(max_nodes);
    for (size_t i = 0; i < max_nodes; ++i) {
#ifdef __linux__
        if (numa_available() >= 0) {
            node_pool[i] = static_cast<Node*>(numa_alloc_onnode(sizeof(Node), numa_node_of_cpu(sched_getcpu())));
            new (node_pool[i]) Node(0.0);
        } else {
            node_pool[i] = new Node(0.0);
        }
#else
        node_pool[i] = new Node(0.0);
#endif
    }
}

RadialCircularList::~RadialCircularList() {
    for (auto node : node_pool) {
#ifdef __linux__
        if (numa_available() >= 0) {
            node->~Node();
            numa_free(node, sizeof(Node));
        } else {
            delete node;
        }
#else
        delete node;
#endif
    }
}

bool RadialCircularList::insert(double value, const std::string& midpoint, int priority, double expiry_time) {
    size_t current_nodes = total_nodes.load(std::memory_order_relaxed);
    if (current_nodes >= max_nodes) return false;
    size_t index = pool_index.fetch_add(1, std::memory_order_relaxed);
    if (index >= max_nodes) return false;

    Node* node = node_pool[index];
    node->value = value;
    node->priority = priority;
    node->timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    node->expiry_time_ns = static_cast<uint64_t>(expiry_time * 1'000'000'000);

    if (midpoints.get_or_create(midpoint, max_nodes / 10)->add_node(node)) {
        total_nodes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool RadialCircularList::insert_batch(const std::vector<std::tuple<double, std::string, int, double>>& batch) {
    size_t batch_size = batch.size();
    size_t current_nodes = total_nodes.load(std::memory_order_relaxed);
    if (current_nodes + batch_size > max_nodes) return false;
    size_t start_index = pool_index.fetch_add(batch_size, std::memory_order_relaxed);
    if (start_index + batch_size > max_nodes) return false;

    for (size_t i = 0; i < batch_size; ++i) {
        const auto& [value, midpoint, priority, expiry_time] = batch[i];
        Node* node = node_pool[start_index + i];
        node->value = value;
        node->priority = priority;
        node->timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        node->expiry_time_ns = static_cast<uint64_t>(expiry_time * 1'000'000'000);
        midpoints.get_or_create(midpoint, max_nodes / 10)->add_node(node);
    }
    total_nodes.fetch_add(batch_size, std::memory_order_relaxed);
    return true;
}

Node* RadialCircularList::get_highest_priority(const std::string& midpoint) {
    MidpointNode* mid = midpoints.get(midpoint);
    return mid ? mid->get_highest_priority_node() : nullptr;
}

std::vector<Node*> RadialCircularList::get_highest_priority_batch(const std::vector<std::string>& midpoints_batch) {
    std::vector<Node*> results(midpoints_batch.size(), nullptr);
    for (size_t i = 0; i < midpoints_batch.size(); ++i) {
        results[i] = get_highest_priority(midpoints_batch[i]);
    }
    return results;
}