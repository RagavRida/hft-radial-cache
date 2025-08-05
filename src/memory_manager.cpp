#include "memory_manager.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

MemoryManager::MemoryManager(const CacheConfig& config) 
    : config_(config), current_numa_node_(-1) {
    
    if (!config_.validate_config()) {
        throw std::invalid_argument("Invalid cache configuration");
    }
    
    initialize_numa();
    
    // Pre-allocate node pool
    if (config_.enable_memory_pool) {
        node_pool_.reserve(config_.max_nodes);
        for (size_t i = 0; i < config_.max_nodes; ++i) {
            Node* node = static_cast<Node*>(numa_allocate(sizeof(Node)));
            new (node) Node();
            node_pool_.push_back(node);
            total_memory_usage_.fetch_add(sizeof(Node));
        }
    }
    
    start_cleanup_thread();
}

MemoryManager::~MemoryManager() {
    stop_cleanup_thread();
    emergency_cleanup();
    
    // Clean up node pool
    for (auto node : node_pool_) {
        if (node) {
            node->~Node();
            numa_deallocate(node, sizeof(Node));
        }
    }
    
    // Clean up any remaining memory blocks
    std::lock_guard<std::mutex> lock(memory_mutex_);
    for (const auto& block : memory_blocks_) {
        numa_deallocate(block.ptr, block.size);
    }
}

Node* MemoryManager::allocate_node() {
    if (!is_memory_available()) {
        return nullptr;
    }
    
    if (config_.enable_memory_pool && pool_index_.load() < node_pool_.size()) {
        size_t index = pool_index_.fetch_add(1);
        if (index < node_pool_.size()) {
            allocated_nodes_.fetch_add(1);
            allocations_.fetch_add(1);
            return node_pool_[index];
        }
    }
    
    // Fallback to dynamic allocation
    Node* node = static_cast<Node*>(numa_allocate(sizeof(Node)));
    if (node) {
        new (node) Node();
        allocated_nodes_.fetch_add(1);
        allocations_.fetch_add(1);
        
        std::lock_guard<std::mutex> lock(memory_mutex_);
        memory_blocks_.emplace_back(node, sizeof(Node), current_numa_node_);
    }
    
    return node;
}

void MemoryManager::deallocate_node(Node* node) {
    if (!node) return;
    
    deallocations_.fetch_add(1);
    allocated_nodes_.fetch_sub(1);
    
    // Mark for cleanup instead of immediate deletion
    mark_for_cleanup(node);
}

void MemoryManager::mark_for_cleanup(Node* node) {
    if (!node) return;
    
    std::lock_guard<std::mutex> lock(expired_mutex_);
    expired_nodes_.push(node);
    cleanup_cv_.notify_one();
}

void MemoryManager::start_cleanup_thread() {
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
    
    shutdown_.store(false);
    cleanup_thread_ = std::thread(&MemoryManager::cleanup_worker, this);
}

void MemoryManager::stop_cleanup_thread() {
    shutdown_.store(true);
    cleanup_cv_.notify_all();
    
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

void MemoryManager::cleanup_worker() {
    while (!shutdown_.load()) {
        cleanup_expired_nodes();
        
        std::unique_lock<std::mutex> lock(expired_mutex_);
        cleanup_cv_.wait_for(lock, 
            std::chrono::milliseconds(config_.cleanup_interval_ms),
            [this] { return shutdown_.load() || !expired_nodes_.empty(); });
    }
}

void MemoryManager::cleanup_expired_nodes() {
    std::queue<Node*> nodes_to_cleanup;
    
    {
        std::lock_guard<std::mutex> lock(expired_mutex_);
        size_t cleanup_count = std::min(expired_nodes_.size(), 
                                       config_.max_expired_nodes_per_cleanup);
        
        for (size_t i = 0; i < cleanup_count && !expired_nodes_.empty(); ++i) {
            nodes_to_cleanup.push(expired_nodes_.front());
            expired_nodes_.pop();
        }
    }
    
    while (!nodes_to_cleanup.empty()) {
        Node* node = nodes_to_cleanup.front();
        nodes_to_cleanup.pop();
        
        if (node) {
            node->~Node();
            numa_deallocate(node, sizeof(Node));
            total_memory_usage_.fetch_sub(sizeof(Node));
        }
    }
    
    cleanup_cycles_.fetch_add(1);
}

bool MemoryManager::is_memory_available() const {
    return allocated_nodes_.load() < config_.max_nodes &&
           total_memory_usage_.load() < (config_.max_memory_mb * 1024 * 1024);
}

void MemoryManager::initialize_numa() {
#ifdef __linux__
    if (config_.enable_numa && numa_available() >= 0) {
        if (config_.numa_node >= 0) {
            current_numa_node_ = config_.numa_node;
        } else {
            current_numa_node_ = numa_node_of_cpu(sched_getcpu());
        }
        numa_set_preferred(current_numa_node_);
    } else {
        current_numa_node_ = -1;
    }
#else
    current_numa_node_ = -1;
#endif
}

int MemoryManager::get_current_numa_node() const {
    return current_numa_node_;
}

void* MemoryManager::numa_allocate(size_t size) {
    return allocate_on_numa(size, current_numa_node_);
}

void MemoryManager::numa_deallocate(void* ptr, size_t size) {
    deallocate_from_numa(ptr, size, current_numa_node_);
}

void* MemoryManager::allocate_on_numa(size_t size, int numa_node) {
#ifdef __linux__
    if (config_.enable_numa && numa_available() >= 0 && numa_node >= 0) {
        void* ptr = numa_alloc_onnode(size, numa_node);
        if (ptr) {
            total_memory_usage_.fetch_add(size);
            return ptr;
        }
    }
#endif
    
    // Fallback to standard allocation
    void* ptr = std::aligned_alloc(64, size);  // 64-byte alignment for cache lines
    if (ptr) {
        total_memory_usage_.fetch_add(size);
    }
    return ptr;
}

void MemoryManager::deallocate_from_numa(void* ptr, size_t size, int numa_node) {
    if (!ptr) return;
    
#ifdef __linux__
    if (config_.enable_numa && numa_available() >= 0 && numa_node >= 0) {
        numa_free(ptr, size);
    } else {
        std::free(ptr);
    }
#else
    std::free(ptr);
#endif
    
    total_memory_usage_.fetch_sub(size);
}

MemoryManager::MemoryMetrics MemoryManager::get_metrics() const {
    MemoryMetrics metrics;
    metrics.allocations = allocations_.load();
    metrics.deallocations = deallocations_.load();
    metrics.cleanup_cycles = cleanup_cycles_.load();
    metrics.memory_usage = total_memory_usage_.load();
    metrics.allocated_nodes = allocated_nodes_.load();
    
    std::lock_guard<std::mutex> lock(expired_mutex_);
    metrics.expired_nodes_queue_size = expired_nodes_.size();
    
    return metrics;
}

bool MemoryManager::validate_memory_integrity() const {
    // Check if allocated nodes count is reasonable
    if (allocated_nodes_.load() > config_.max_nodes) {
        return false;
    }
    
    // Check if memory usage is reasonable
    if (total_memory_usage_.load() > config_.max_memory_mb * 1024 * 1024) {
        return false;
    }
    
    // Check if pool index is within bounds
    if (pool_index_.load() > node_pool_.size()) {
        return false;
    }
    
    return true;
}

void MemoryManager::emergency_cleanup() {
    // Force cleanup of all expired nodes
    std::queue<Node*> emergency_queue;
    
    {
        std::lock_guard<std::mutex> lock(expired_mutex_);
        emergency_queue.swap(expired_nodes_);
    }
    
    while (!emergency_queue.empty()) {
        Node* node = emergency_queue.front();
        emergency_queue.pop();
        
        if (node) {
            node->~Node();
            numa_deallocate(node, sizeof(Node));
            total_memory_usage_.fetch_sub(sizeof(Node));
        }
    }
} 