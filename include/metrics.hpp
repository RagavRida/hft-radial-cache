#ifndef METRICS_HPP
#define METRICS_HPP

#include "config.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <map>
#include <functional>

struct CacheMetrics {
    // Performance metrics
    std::atomic<uint64_t> total_inserts{0};
    std::atomic<uint64_t> total_retrieves{0};
    std::atomic<uint64_t> total_batch_inserts{0};
    std::atomic<uint64_t> total_batch_retrieves{0};
    
    // Latency metrics (in nanoseconds)
    std::atomic<uint64_t> total_insert_latency{0};
    std::atomic<uint64_t> total_retrieve_latency{0};
    std::atomic<uint64_t> total_batch_insert_latency{0};
    std::atomic<uint64_t> total_batch_retrieve_latency{0};
    
    // Error metrics
    std::atomic<uint64_t> insert_errors{0};
    std::atomic<uint64_t> retrieve_errors{0};
    std::atomic<uint64_t> memory_errors{0};
    std::atomic<uint64_t> recovery_attempts{0};
    
    // Memory metrics
    std::atomic<size_t> current_memory_usage{0};
    std::atomic<size_t> peak_memory_usage{0};
    std::atomic<size_t> allocated_nodes{0};
    std::atomic<size_t> expired_nodes{0};
    
    // Cache hit/miss metrics
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    
    // Threading metrics
    std::atomic<uint64_t> thread_contention_count{0};
    std::atomic<uint64_t> lock_wait_time{0};
    
    // NUMA metrics
    std::atomic<uint64_t> numa_allocations{0};
    std::atomic<uint64_t> cross_numa_accesses{0};
};

class MetricsCollector {
private:
    CacheConfig config_;
    CacheMetrics metrics_;
    std::atomic<bool> shutdown_{false};
    std::thread metrics_thread_;
    std::mutex file_mutex_;
    std::ofstream metrics_file_;
    
    // Historical data for trend analysis
    struct MetricsSnapshot {
        std::chrono::steady_clock::time_point timestamp;
        CacheMetrics metrics;
    };
    
    std::vector<MetricsSnapshot> historical_data_;
    std::mutex history_mutex_;
    static constexpr size_t MAX_HISTORY_SIZE = 1000;
    
    // Alert thresholds
    struct AlertThresholds {
        uint64_t max_latency_ns = 1000000;  // 1ms
        size_t max_memory_mb = 1024;
        uint64_t max_error_rate = 1000;
        double max_cpu_usage = 80.0;
    } thresholds_;

public:
    explicit MetricsCollector(const CacheConfig& config);
    ~MetricsCollector();
    
    // Metric recording
    void record_insert(uint64_t latency_ns, bool success = true);
    void record_retrieve(uint64_t latency_ns, bool success = true, bool hit = true);
    void record_batch_insert(uint64_t latency_ns, size_t batch_size, bool success = true);
    void record_batch_retrieve(uint64_t latency_ns, size_t batch_size, bool success = true);
    void record_memory_usage(size_t usage_bytes);
    void record_error(const std::string& error_type);
    void record_recovery_attempt();
    void record_thread_contention(uint64_t wait_time_ns);
    void record_numa_allocation(bool cross_numa = false);
    
    // Metrics retrieval
    CacheMetrics get_current_metrics() const;
    std::vector<MetricsSnapshot> get_historical_data() const;
    
    // Performance calculations
    double get_average_insert_latency() const;
    double get_average_retrieve_latency() const;
    double get_cache_hit_rate() const;
    double get_error_rate() const;
    double get_memory_utilization() const;
    
    // Alerts and monitoring
    bool check_alerts() const;
    std::vector<std::string> get_active_alerts() const;
    
    // Reporting
    void generate_report(const std::string& filename = "") const;
    void export_metrics(const std::string& filename) const;
    
    // Configuration
    void set_alert_thresholds(const AlertThresholds& thresholds);
    void enable_metrics_collection(bool enable = true);

private:
    void metrics_worker();
    void write_metrics_to_file();
    void update_historical_data();
    void check_and_trigger_alerts();
    std::string format_metrics() const;
    void rotate_metrics_file();
};

// Global metrics instance
extern MetricsCollector* g_metrics;

// Convenience macros for metrics recording
#define RECORD_METRIC(metric, value) \
    if (g_metrics) g_metrics->record_##metric(value)

#define RECORD_LATENCY(operation, start_time) \
    do { \
        auto end_time = std::chrono::high_resolution_clock::now(); \
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count(); \
        if (g_metrics) g_metrics->record_##operation(latency); \
    } while(0)

#endif 