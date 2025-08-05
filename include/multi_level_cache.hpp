#pragma once

#include "radial_circular_list.hpp"
#include "lockfree_queue.hpp"
#include "persistent_cache.hpp"
#include "config.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace hft_cache {

// Forward declarations
class DiskBackedCache;

/**
 * @brief Multi-level cache architecture for optimal performance
 * 
 * L1: Hot data in lock-free queue (fastest access)
 * L2: Warm data in radial circular list
 * L3: Cold data on disk (persistent)
 */
class MultiLevelCache {
public:
    explicit MultiLevelCache(const CacheConfig& config);
    ~MultiLevelCache();

    // Core operations
    bool insert(double value, const std::string& symbol, int priority, double expiry_seconds = 60.0);
    Node* get_highest_priority(const std::string& symbol);
    bool remove(const std::string& symbol, double value);
    void clear();

    // Level-specific operations
    bool promote_to_l1(Node* node);
    bool demote_to_l2(Node* node);
    bool demote_to_l3(Node* node);
    
    // Statistics and monitoring
    struct LevelStats {
        std::atomic<size_t> item_count{0};
        std::atomic<size_t> hit_count{0};
        std::atomic<size_t> miss_count{0};
        std::atomic<uint64_t> total_access_time_ns{0};
    };

    LevelStats get_l1_stats() const;
    LevelStats get_l2_stats() const;
    LevelStats get_l3_stats() const;

    // Configuration
    void set_l1_capacity(size_t capacity);
    void set_l2_capacity(size_t capacity);
    void set_l3_capacity(size_t capacity);
    
    // Background management
    void start_background_management();
    void stop_background_management();

private:
    CacheConfig config_;
    
    // Cache levels
    LockFreeQueue<Node*> l1_cache_;           // Hot data (fastest)
    std::unique_ptr<RadialCircularList> l2_cache_; // Warm data
    std::unique_ptr<DiskBackedCache> l3_cache_;    // Cold data (persistent)
    
    // Statistics
    mutable LevelStats l1_stats_;
    mutable LevelStats l2_stats_;
    mutable LevelStats l3_stats_;
    
    // Background management
    std::thread management_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    
    // Eviction policies
    void eviction_policy_l1();
    void eviction_policy_l2();
    void eviction_policy_l3();
    
    // Background worker
    void background_management_worker();
    
    // Helper methods
    bool should_promote_to_l1(const Node* node) const;
    bool should_demote_to_l2(const Node* node) const;
    bool should_demote_to_l3(const Node* node) const;
    
    // Access tracking
    void record_access(Node* node, uint64_t access_time_ns);
    void update_access_patterns();
};

/**
 * @brief Disk-backed cache for L3 storage
 */
class DiskBackedCache {
public:
    explicit DiskBackedCache(const CacheConfig& config);
    ~DiskBackedCache();

    bool insert(Node* node);
    Node* retrieve(const std::string& symbol, double value);
    bool remove(const std::string& symbol, double value);
    void clear();

    // Disk operations
    bool flush_to_disk();
    bool load_from_disk();
    size_t get_disk_size() const;

private:
    CacheConfig config_;
    std::string disk_path_;
    std::unordered_map<std::string, Node*> memory_index_;
    std::atomic<size_t> disk_size_{0};
    
    // Serialization helpers
    bool serialize_node(const Node* node, std::ofstream& file);
    Node* deserialize_node(std::ifstream& file);
};

} // namespace hft_cache 