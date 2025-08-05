#ifndef MEMORY_MANAGER_HPP
#define MEMORY_MANAGER_HPP

#include "config.hpp"
#include "node.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <functional>

#ifdef __linux__
#include <numa.h>
#include <sched.h>
#endif

class MemoryManager {
private:
    struct MemoryBlock {
        void* ptr;
        size_t size;
        int numa_node;
        std::chrono::steady_clock::time_point allocation_time;
        
        MemoryBlock(void* p, size_t s, int node) 
            : ptr(p), size(s), numa_node(node), 
              allocation_time(std::chrono::steady_clock::now()) {}
    };

    CacheConfig config_;
    std::atomic<bool> shutdown_{false};
    std::thread cleanup_thread_;
    
    // Memory pool
    std::vector<Node*> node_pool_;
    std::atomic<size_t> pool_index_{0};
    std::atomic<size_t> allocated_nodes_{0};
    
    // Background cleanup
    std::queue<Node*> expired_nodes_;
    std::mutex expired_mutex_;
    std::condition_variable cleanup_cv_;
    
    // Memory tracking
    std::atomic<size_t> total_memory_usage_{0};
    std::vector<MemoryBlock> memory_blocks_;
    std::mutex memory_mutex_;
    
    // NUMA awareness
    int current_numa_node_;
    
    // Metrics
    std::atomic<uint64_t> allocations_{0};
    std::atomic<uint64_t> deallocations_{0};
    std::atomic<uint64_t> cleanup_cycles_{0};

public:
    explicit MemoryManager(const CacheConfig& config);
    ~MemoryManager();
    
    // Memory allocation
    Node* allocate_node();
    void deallocate_node(Node* node);
    void mark_for_cleanup(Node* node);
    
    // Background cleanup
    void start_cleanup_thread();
    void stop_cleanup_thread();
    void cleanup_expired_nodes();
    
    // Memory monitoring
    size_t get_memory_usage() const { return total_memory_usage_.load(); }
    size_t get_allocated_nodes() const { return allocated_nodes_.load(); }
    bool is_memory_available() const;
    
    // NUMA operations
    int get_current_numa_node() const;
    void* numa_allocate(size_t size);
    void numa_deallocate(void* ptr, size_t size);
    
    // Metrics
    struct MemoryMetrics {
        uint64_t allocations;
        uint64_t deallocations;
        uint64_t cleanup_cycles;
        size_t memory_usage;
        size_t allocated_nodes;
        size_t expired_nodes_queue_size;
    };
    
    MemoryMetrics get_metrics() const;
    
    // Error handling
    bool validate_memory_integrity() const;
    void emergency_cleanup();

private:
    void cleanup_worker();
    void initialize_numa();
    void* allocate_on_numa(size_t size, int numa_node);
    void deallocate_from_numa(void* ptr, size_t size, int numa_node);
    bool should_cleanup() const;
};

#endif 