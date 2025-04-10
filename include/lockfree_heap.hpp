#ifndef LOCKFREE_HEAP_HPP
#define LOCKFREE_HEAP_HPP

#include "node.hpp"
#include <atomic>
#include <vector>

class LockFreeHeap {
private:
    static constexpr size_t INITIAL_SIZE = 1024;
    struct HeapNode {
        std::atomic<Node*> node;
        HeapNode() : node(nullptr) {}
    };
    std::vector<HeapNode> heap;
    std::atomic<size_t> size_;
    std::atomic<size_t> capacity;

    void sift_up(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            Node* child = heap[index].node.load(std::memory_order_relaxed);
            Node* parent_node = heap[parent].node.load(std::memory_order_relaxed);
            if (!child || !parent_node || parent_node->priority >= child->priority) break;
            if (heap[parent].node.compare_exchange_strong(parent_node, child, std::memory_order_acq_rel) &&
                heap[index].node.compare_exchange_strong(child, parent_node, std::memory_order_acq_rel)) {
                index = parent;
            } else {
                break;  // Retry on contention
            }
        }
    }

    void sift_down(size_t index) {
        size_t current_size = size_.load(std::memory_order_relaxed);
        while (true) {
            size_t max_index = index;
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;
            Node* current = heap[index].node.load(std::memory_order_relaxed);
            if (left < current_size) {
                Node* left_node = heap[left].node.load(std::memory_order_relaxed);
                if (left_node && left_node->priority > current->priority) max_index = left;
            }
            if (right < current_size) {
                Node* right_node = heap[right].node.load(std::memory_order_relaxed);
                if (right_node && right_node->priority > heap[max_index].node.load(std::memory_order_relaxed)->priority) max_index = right;
            }
            if (max_index == index) break;
            Node* max_node = heap[max_index].node.load(std::memory_order_relaxed);
            if (heap[index].node.compare_exchange_strong(current, max_node, std::memory_order_acq_rel) &&
                heap[max_index].node.compare_exchange_strong(max_node, current, std::memory_order_acq_rel)) {
                index = max_index;
            } else {
                break;
            }
        }
    }

public:
    LockFreeHeap(size_t initial_capacity = INITIAL_SIZE) : size_(0), capacity(initial_capacity) {
        heap.resize(initial_capacity);
    }

    bool push(Node* node) {
        while (true) {
            size_t current_size = size_.load(std::memory_order_acquire);
            if (current_size >= capacity) return false;  // Fixed cap for predictability
            if (size_.compare_exchange_weak(current_size, current_size + 1, std::memory_order_acq_rel)) {
                heap[current_size].node.store(node, std::memory_order_relaxed);
                sift_up(current_size);
                return true;
            }
            std::this_thread::yield();
        }
    }

    Node* pop() {
        while (true) {
            size_t current_size = size_.load(std::memory_order_acquire);
            if (current_size == 0) return nullptr;
            Node* top = heap[0].node.load(std::memory_order_relaxed);
            if (!top) continue;
            if (size_.compare_exchange_weak(current_size, current_size - 1, std::memory_order_acq_rel)) {
                Node* last = heap[current_size - 1].node.exchange(nullptr, std::memory_order_acq_rel);
                if (current_size > 1) {
                    heap[0].node.store(last, std::memory_order_relaxed);
                    sift_down(0);
                }
                return top;
            }
            std::this_thread::yield();
        }
    }

    size_t size() const { return size_.load(std::memory_order_relaxed); }
    bool empty() const { return size() == 0; }
};

#endif