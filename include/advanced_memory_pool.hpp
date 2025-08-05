#pragma once

#include "node.hpp"
#include "config.hpp"
#include <vector>
#include <stack>
#include <atomic>
#include <thread>
#include <memory>
#include <mutex>

namespace hft_cache {

/**
 * @brief Advanced memory pool with object recycling and per-thread optimization
 * 
 * Provides high-performance memory allocation with reduced fragmentation,
 * per-thread free lists, and automatic defragmentation.
 */
class AdvancedMemoryPool {
public:
    explicit AdvancedMemoryPool(const CacheConfig& config);
    ~AdvancedMemoryPool();

    // Core allocation operations
    Node* allocate_node();
    void deallocate_node(Node* node);
    void* allocate_aligned(size_t size, size_t alignment);
    void deallocate_aligned(void* ptr);
    
    // Memory management
    void defragment();
    void compact();
    void resize_pool(size_t new_size);
    void clear();
    
    // Statistics and monitoring
    size_t get_total_allocated() const;
    size_t get_total_deallocated() const;
    size_t get_free_list_size() const;
    double get_fragmentation_ratio() const;
    size_t get_pool_size() const;
    
    // Performance optimization
    void optimize_for_thread(size_t thread_id);
    void preallocate_for_thread(size_t thread_id, size_t count);
    void cleanup_thread_resources(size_t thread_id);

private:
    CacheConfig config_;
    
    // Main pool
    std::vector<Node*> main_pool_;
    std::atomic<size_t> pool_index_{0};
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_deallocated_{0};
    
    // Per-thread free lists
    struct ThreadFreeList {
        std::stack<Node*> free_nodes;
        std::mutex mutex;
        std::atomic<size_t> allocation_count{0};
        std::atomic<size_t> deallocation_count{0};
    };
    
    std::vector<std::unique_ptr<ThreadFreeList>> thread_free_lists_;
    size_t thread_count_;
    
    // Aligned allocation pool
    struct AlignedBlock {
        void* ptr;
        size_t size;
        size_t alignment;
        bool in_use;
    };
    
    std::vector<AlignedBlock> aligned_blocks_;
    std::mutex aligned_mutex_;
    
    // Helper methods
    size_t get_thread_id() const;
    ThreadFreeList* get_thread_free_list(size_t thread_id);
    void initialize_thread_free_lists();
    void cleanup_thread_free_lists();
    
    // Memory management helpers
    void* allocate_aligned_internal(size_t size, size_t alignment);
    void deallocate_aligned_internal(void* ptr);
    void defragment_aligned_blocks();
    
    // Statistics
    void update_statistics(size_t thread_id, bool is_allocation);
    double calculate_fragmentation() const;
};

/**
 * @brief NUMA-aware memory pool
 */
class NUMAMemoryPool : public AdvancedMemoryPool {
public:
    explicit NUMAMemoryPool(const CacheConfig& config);
    
    // NUMA-specific operations
    Node* allocate_node_on_numa(int numa_node);
    void deallocate_node_to_numa(Node* node, int numa_node);
    int get_current_numa_node() const;
    void set_preferred_numa_node(int numa_node);
    
    // NUMA statistics
    size_t get_numa_allocation_count(int numa_node) const;
    double get_numa_utilization(int numa_node) const;
    
private:
    std::vector<std::unique_ptr<AdvancedMemoryPool>> numa_pools_;
    std::atomic<int> preferred_numa_node_{-1};
    std::vector<std::atomic<size_t>> numa_allocation_counts_;
    
    void initialize_numa_pools();
    int get_numa_node_for_thread() const;
};

/**
 * @brief Lock-free memory pool for high-concurrency scenarios
 */
class LockFreeMemoryPool {
public:
    explicit LockFreeMemoryPool(const CacheConfig& config);
    ~LockFreeMemoryPool();

    // Lock-free operations
    Node* allocate_node_lock_free();
    void deallocate_node_lock_free(Node* node);
    
    // Statistics
    size_t get_available_nodes() const;
    size_t get_allocated_nodes() const;
    bool is_empty() const;
    bool is_full() const;

private:
    CacheConfig config_;
    
    // Lock-free pool using atomic operations
    struct PoolNode {
        std::atomic<PoolNode*> next;
        Node data;
    };
    
    std::atomic<PoolNode*> free_list_head_{nullptr};
    std::atomic<size_t> allocated_count_{0};
    std::atomic<size_t> total_count_{0};
    
    std::vector<PoolNode> pool_nodes_;
    
    // Helper methods
    PoolNode* pop_free_node();
    void push_free_node(PoolNode* node);
    void initialize_pool();
};

/**
 * @brief Hierarchical memory pool with multiple allocation strategies
 */
class HierarchicalMemoryPool {
public:
    explicit HierarchicalMemoryPool(const CacheConfig& config);
    ~HierarchicalMemoryPool();

    // Hierarchical allocation
    Node* allocate_node_fast();      // From L1 (fastest)
    Node* allocate_node_standard();  // From L2 (standard)
    Node* allocate_node_slow();      // From L3 (slowest, but guaranteed)
    
    void deallocate_node(Node* node);
    
    // Pool management
    void rebalance_pools();
    void optimize_allocation_strategy();
    
    // Statistics
    struct PoolStats {
        size_t l1_allocations;
        size_t l2_allocations;
        size_t l3_allocations;
        double l1_hit_rate;
        double l2_hit_rate;
        double l3_hit_rate;
    };
    
    PoolStats get_pool_statistics() const;

private:
    CacheConfig config_;
    
    // Three-level hierarchy
    std::unique_ptr<LockFreeMemoryPool> l1_pool_;      // Fastest, smallest
    std::unique_ptr<AdvancedMemoryPool> l2_pool_;      // Standard, medium
    std::unique_ptr<NUMAMemoryPool> l3_pool_;          // Slowest, largest
    
    // Statistics
    mutable std::atomic<size_t> l1_allocations_{0};
    mutable std::atomic<size_t> l2_allocations_{0};
    mutable std::atomic<size_t> l3_allocations_{0};
    
    // Strategy selection
    Node* select_allocation_strategy();
    void update_allocation_statistics(int level);
};

} // namespace hft_cache 