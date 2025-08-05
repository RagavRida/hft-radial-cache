#ifndef LOCKFREE_QUEUE_HPP
#define LOCKFREE_QUEUE_HPP

#include <atomic>
#include <memory>
#include <thread>

template<typename T>
class LockFreeQueue {
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        
        Node() : next(nullptr) {}
        explicit Node(const T& value) : data(value), next(nullptr) {}
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    
    // Memory management
    std::atomic<Node*> free_list_;
    std::atomic<size_t> free_count_;
    static constexpr size_t MAX_FREE_NODES = 1000;
    
public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy);
        tail_.store(dummy);
        free_list_.store(nullptr);
        free_count_.store(0);
    }
    
    ~LockFreeQueue() {
        // Clean up all nodes
        Node* current = head_.load();
        while (current) {
            Node* next = current->next.load();
            delete current;
            current = next;
        }
        
        // Clean up free list
        current = free_list_.load();
        while (current) {
            Node* next = current->next.load();
            delete current;
            current = next;
        }
    }
    
    // Non-copyable
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    
    void enqueue(const T& value) {
        Node* new_node = allocate_node(value);
        
        while (true) {
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = tail->next.load(std::memory_order_acquire);
            
            if (tail == tail_.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, new_node, 
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed)) {
                        tail_.compare_exchange_strong(tail, new_node,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed);
                        return;
                    }
                } else {
                    tail_.compare_exchange_strong(tail, next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed);
                }
            }
            std::this_thread::yield();
        }
    }
    
    bool dequeue(T& value) {
        while (true) {
            Node* head = head_.load(std::memory_order_acquire);
            Node* tail = tail_.load(std::memory_order_acquire);
            Node* next = head->next.load(std::memory_order_acquire);
            
            if (head == head_.load(std::memory_order_acquire)) {
                if (head == tail) {
                    if (next == nullptr) {
                        return false;  // Queue is empty
                    }
                    tail_.compare_exchange_strong(tail, next,
                                                std::memory_order_release,
                                                std::memory_order_relaxed);
                } else {
                    if (next == nullptr) {
                        continue;  // Another thread is updating the queue
                    }
                    value = next->data;
                    if (head_.compare_exchange_weak(head, next,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
                        deallocate_node(head);
                        return true;
                    }
                }
            }
            std::this_thread::yield();
        }
    }
    
    bool empty() const {
        Node* head = head_.load(std::memory_order_acquire);
        Node* tail = tail_.load(std::memory_order_acquire);
        return head == tail && head->next.load(std::memory_order_acquire) == nullptr;
    }
    
    size_t size() const {
        size_t count = 0;
        Node* current = head_.load(std::memory_order_acquire)->next.load(std::memory_order_acquire);
        while (current) {
            count++;
            current = current->next.load(std::memory_order_acquire);
        }
        return count;
    }

private:
    Node* allocate_node(const T& value) {
        // Try to reuse from free list first
        Node* free_node = free_list_.load(std::memory_order_acquire);
        while (free_node) {
            Node* next = free_node->next.load(std::memory_order_acquire);
            if (free_list_.compare_exchange_weak(free_node, next,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
                free_count_.fetch_sub(1, std::memory_order_relaxed);
                free_node->data = value;
                free_node->next.store(nullptr, std::memory_order_relaxed);
                return free_node;
            }
        }
        
        // Allocate new node
        return new Node(value);
    }
    
    void deallocate_node(Node* node) {
        if (!node) return;
        
        size_t current_free = free_count_.load(std::memory_order_relaxed);
        if (current_free < MAX_FREE_NODES) {
            Node* old_head = free_list_.load(std::memory_order_acquire);
            node->next.store(old_head, std::memory_order_relaxed);
            if (free_list_.compare_exchange_weak(old_head, node,
                                               std::memory_order_release,
                                               std::memory_order_relaxed)) {
                free_count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        
        // Free list is full or CAS failed, delete the node
        delete node;
    }
};

#endif 