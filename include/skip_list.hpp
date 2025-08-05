#pragma once

#include "node.hpp"
#include <atomic>
#include <vector>
#include <random>
#include <memory>
#include <cstdint>

namespace hft_cache {

constexpr int MAX_LEVEL = 32;

/**
 * @brief Lock-free skip list node
 */
struct SkipNode {
    Node* data;
    std::atomic<SkipNode*> next[MAX_LEVEL];
    int level;
    
    SkipNode(Node* node_data, int node_level) 
        : data(node_data), level(node_level) {
        for (int i = 0; i < MAX_LEVEL; ++i) {
            next[i].store(nullptr);
        }
    }
    
    ~SkipNode() = default;
};

/**
 * @brief Lock-free skip list for efficient priority-based operations
 * 
 * Provides O(log n) average time complexity for insert, delete, and search operations
 * while maintaining thread safety through lock-free techniques.
 */
class LockFreeSkipList {
public:
    explicit LockFreeSkipList(const CacheConfig& config);
    ~LockFreeSkipList();

    // Core operations
    bool insert(Node* node);
    Node* find(const std::string& symbol, double value);
    bool remove(const std::string& symbol, double value);
    void clear();
    
    // Priority-based operations
    Node* get_highest_priority(const std::string& symbol);
    std::vector<Node*> get_top_n(const std::string& symbol, size_t n);
    std::vector<Node*> get_by_priority_range(const std::string& symbol, int min_priority, int max_priority);
    
    // Range queries
    std::vector<Node*> get_range(const std::string& symbol, double min_value, double max_value);
    std::vector<Node*> get_by_timestamp_range(const std::string& symbol, uint64_t start_time, uint64_t end_time);
    
    // Statistics
    size_t size() const;
    int get_max_level() const;
    double get_average_level() const;
    
    // Iteration
    class Iterator {
    public:
        Iterator(SkipNode* current);
        Iterator& operator++();
        Node* operator*();
        bool operator!=(const Iterator& other) const;
        
    private:
        SkipNode* current_;
    };
    
    Iterator begin();
    Iterator end();

private:
    CacheConfig config_;
    SkipNode* head_;
    SkipNode* tail_;
    std::atomic<size_t> size_{0};
    std::atomic<int> max_level_{1};
    
    // Random number generation for level assignment
    mutable std::mt19937 rng_;
    mutable std::uniform_real_distribution<double> dist_;
    
    // Helper methods
    int random_level() const;
    SkipNode* find_node(const std::string& symbol, double value, SkipNode** update = nullptr);
    bool insert_node(SkipNode* new_node, SkipNode** update);
    bool remove_node(const std::string& symbol, double value, SkipNode** update);
    
    // Memory management
    void cleanup_node(SkipNode* node);
    void cleanup_all_nodes();
    
    // Validation
    bool validate_skip_list() const;
    void print_skip_list() const;
};

/**
 * @brief Thread-safe skip list with additional synchronization
 */
class ThreadSafeSkipList : public LockFreeSkipList {
public:
    explicit ThreadSafeSkipList(const CacheConfig& config);
    
    // Thread-safe operations with additional guarantees
    bool insert_thread_safe(Node* node);
    Node* find_thread_safe(const std::string& symbol, double value);
    bool remove_thread_safe(const std::string& symbol, double value);
    
private:
    mutable std::atomic<size_t> concurrent_readers_{0};
    mutable std::atomic<size_t> concurrent_writers_{0};
};

/**
 * @brief Skip list with memory pool for better performance
 */
class PooledSkipList : public LockFreeSkipList {
public:
    explicit PooledSkipList(const CacheConfig& config);
    ~PooledSkipList();
    
    // Memory pool operations
    SkipNode* allocate_node(Node* data, int level);
    void deallocate_node(SkipNode* node);
    void defragment_pool();
    
private:
    std::vector<std::vector<SkipNode*>> node_pools_; // Pool per level
    std::atomic<size_t> pool_index_{0};
    size_t pool_size_;
    
    void initialize_pools();
    void cleanup_pools();
};

} // namespace hft_cache 