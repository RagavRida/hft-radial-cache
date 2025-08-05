#pragma once

#include "node.hpp"
#include <vector>
#include <atomic>
#include <memory>
#include <algorithm>
#include <cstdint>

namespace hft_cache {

constexpr int B_TREE_ORDER = 64; // High order for better cache performance

/**
 * @brief B-tree node for efficient range queries
 */
struct BTreeNode {
    std::vector<Node*> keys;
    std::vector<std::atomic<BTreeNode*>> children;
    std::atomic<bool> is_leaf{true};
    std::atomic<size_t> key_count{0};
    
    BTreeNode() {
        keys.resize(B_TREE_ORDER - 1, nullptr);
        children.resize(B_TREE_ORDER, nullptr);
    }
    
    ~BTreeNode() = default;
};

/**
 * @brief Lock-free B-tree for efficient range queries and ordered traversal
 * 
 * Provides O(log n) time complexity for insert, delete, and search operations
 * with excellent cache locality and efficient range queries.
 */
class LockFreeBTree {
public:
    explicit LockFreeBTree(const CacheConfig& config);
    ~LockFreeBTree();

    // Core operations
    bool insert(Node* node);
    Node* find(const std::string& symbol, double value);
    bool remove(const std::string& symbol, double value);
    void clear();
    
    // Range queries
    std::vector<Node*> get_range(const std::string& symbol, double min_value, double max_value);
    std::vector<Node*> get_by_priority_range(const std::string& symbol, int min_priority, int max_priority);
    std::vector<Node*> get_by_timestamp_range(const std::string& symbol, uint64_t start_time, uint64_t end_time);
    
    // Ordered operations
    std::vector<Node*> get_sorted_by_value(const std::string& symbol);
    std::vector<Node*> get_sorted_by_priority(const std::string& symbol);
    std::vector<Node*> get_sorted_by_timestamp(const std::string& symbol);
    
    // Statistics
    size_t size() const;
    int get_height() const;
    double get_fill_factor() const;
    
    // Iteration
    class Iterator {
    public:
        Iterator(BTreeNode* root);
        Iterator& operator++();
        Node* operator*();
        bool operator!=(const Iterator& other) const;
        
    private:
        std::vector<BTreeNode*> stack_;
        std::vector<size_t> indices_;
        void push_left(BTreeNode* node);
    };
    
    Iterator begin();
    Iterator end();

private:
    CacheConfig config_;
    std::atomic<BTreeNode*> root_{nullptr};
    std::atomic<size_t> size_{0};
    std::atomic<int> height_{0};
    
    // Helper methods
    BTreeNode* find_leaf(const std::string& symbol, double value);
    bool insert_into_leaf(BTreeNode* leaf, Node* node);
    void split_node(BTreeNode* parent, size_t child_index);
    void merge_nodes(BTreeNode* parent, size_t child_index);
    void rebalance_tree(BTreeNode* node);
    
    // Memory management
    BTreeNode* allocate_node();
    void deallocate_node(BTreeNode* node);
    void cleanup_tree(BTreeNode* node);
    
    // Validation and debugging
    bool validate_tree() const;
    void print_tree() const;
    int calculate_height(BTreeNode* node) const;
    size_t calculate_size(BTreeNode* node) const;
};

/**
 * @brief Thread-safe B-tree with additional synchronization
 */
class ThreadSafeBTree : public LockFreeBTree {
public:
    explicit ThreadSafeBTree(const CacheConfig& config);
    
    // Thread-safe operations
    bool insert_thread_safe(Node* node);
    Node* find_thread_safe(const std::string& symbol, double value);
    bool remove_thread_safe(const std::string& symbol, double value);
    
private:
    mutable std::atomic<size_t> concurrent_readers_{0};
    mutable std::atomic<size_t> concurrent_writers_{0};
};

/**
 * @brief B-tree with memory pool for better performance
 */
class PooledBTree : public LockFreeBTree {
public:
    explicit PooledBTree(const CacheConfig& config);
    ~PooledBTree();
    
    // Memory pool operations
    BTreeNode* allocate_node_from_pool();
    void deallocate_node_to_pool(BTreeNode* node);
    void defragment_pool();
    
private:
    std::vector<BTreeNode*> node_pool_;
    std::atomic<size_t> pool_index_{0};
    size_t pool_size_;
    
    void initialize_pool();
    void cleanup_pool();
};

/**
 * @brief B-tree with compression for memory efficiency
 */
class CompressedBTree : public LockFreeBTree {
public:
    explicit CompressedBTree(const CacheConfig& config);
    
    // Compression operations
    void compress_node(BTreeNode* node);
    void decompress_node(BTreeNode* node);
    double get_compression_ratio() const;
    
private:
    std::atomic<size_t> compressed_nodes_{0};
    std::atomic<size_t> total_nodes_{0};
    
    bool should_compress_node(BTreeNode* node) const;
    void apply_compression(BTreeNode* node);
};

} // namespace hft_cache 