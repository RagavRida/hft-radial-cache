#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>
#include <chrono>

struct CacheConfig {
    // Memory management
    size_t max_nodes = 10000;
    size_t cleanup_interval_ms = 1000;  // Background cleanup interval
    size_t max_memory_mb = 1024;        // Maximum memory usage
    bool enable_memory_pool = true;
    
    // Performance tuning
    size_t num_worker_threads = 4;
    size_t batch_size = 100;
    size_t hash_table_buckets = 256;
    size_t heap_initial_capacity = 1024;
    
    // NUMA settings
    bool enable_numa = true;
    int numa_node = -1;  // -1 for auto-detect
    
    // Monitoring
    bool enable_metrics = true;
    size_t metrics_interval_ms = 5000;
    std::string metrics_file = "cache_metrics.log";
    
    // Error handling
    bool enable_error_recovery = true;
    size_t max_retry_attempts = 3;
    std::chrono::milliseconds retry_delay{10};
    
    // Expiry and cleanup
    double default_expiry_seconds = 60.0;
    bool enable_lazy_cleanup = true;
    size_t max_expired_nodes_per_cleanup = 1000;
    
    // Threading
    bool enable_lock_free_operations = true;
    size_t spin_count_before_yield = 1000;
    
    // Validation
    bool validate_config() const {
        return max_nodes > 0 && 
               cleanup_interval_ms > 0 && 
               max_memory_mb > 0 &&
               num_worker_threads > 0 &&
               batch_size > 0 &&
               hash_table_buckets > 0;
    }
};

#endif 