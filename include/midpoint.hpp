#ifndef MIDPOINT_HPP
#define MIDPOINT_HPP

#include "lockfree_heap.hpp"

class MidpointNode {
private:
    LockFreeHeap nodes;

public:
    MidpointNode(size_t capacity) : nodes(capacity) {}

    bool add_node(Node* node) { return nodes.push(node); }

    Node* get_highest_priority_node() {
        while (!nodes.empty()) {
            Node* node = nodes.pop();
            if (!node->is_expired()) return node;
            delete node;  // Simple reclamation
        }
        return nullptr;
    }
};

#endif