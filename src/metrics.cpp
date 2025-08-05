#include "metrics.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <thread>

MetricsCollector::MetricsCollector(const CacheConfig& config) 
    : config_(config), shutdown_(false) {
    
    if (config_.enable_metrics) {
        start_metrics_thread();
        open_metrics_file();
    }
}

MetricsCollector::~MetricsCollector() {
    shutdown_.store(true);
    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
    }
    
    if (metrics_file_.is_open()) {
        metrics_file_.close();
    }
}

// Metric recording methods
void MetricsCollector::record_insert(uint64_t latency_ns, bool success) {
    if (!config_.enable_metrics) return;
    
    metrics_.total_inserts.fetch_add(1);
    metrics_.total_insert_latency.fetch_add(latency_ns);
    
    if (!success) {
        metrics_.insert_errors.fetch_add(1);
    }
}

void MetricsCollector::record_retrieve(uint64_t latency_ns, bool success, bool hit) {
    if (!config_.enable_metrics) return;
    
    metrics_.total_retrieves.fetch_add(1);
    metrics_.total_retrieve_latency.fetch_add(latency_ns);
    
    if (hit) {
        metrics_.cache_hits.fetch_add(1);
    } else {
        metrics_.cache_misses.fetch_add(1);
    }
    
    if (!success) {
        metrics_.retrieve_errors.fetch_add(1);
    }
}

void MetricsCollector::record_batch_insert(uint64_t latency_ns, size_t batch_size, bool success) {
    if (!config_.enable_metrics) return;
    
    metrics_.total_batch_inserts.fetch_add(1);
    metrics_.total_batch_insert_latency.fetch_add(latency_ns);
    
    if (!success) {
        metrics_.insert_errors.fetch_add(1);
    }
}

void MetricsCollector::record_batch_retrieve(uint64_t latency_ns, size_t batch_size, bool success) {
    if (!config_.enable_metrics) return;
    
    metrics_.total_batch_retrieves.fetch_add(1);
    metrics_.total_batch_retrieve_latency.fetch_add(latency_ns);
    
    if (!success) {
        metrics_.retrieve_errors.fetch_add(1);
    }
}

void MetricsCollector::record_memory_usage(size_t usage_bytes) {
    if (!config_.enable_metrics) return;
    
    metrics_.current_memory_usage.store(usage_bytes);
    
    // Update peak memory usage
    size_t peak_usage = metrics_.peak_memory_usage.load();
    while (usage_bytes > peak_usage) {
        if (metrics_.peak_memory_usage.compare_exchange_weak(peak_usage, usage_bytes)) {
            break;
        }
    }
}

void MetricsCollector::record_error(const std::string& error_type) {
    if (!config_.enable_metrics) return;
    
    if (error_type == "memory") {
        metrics_.memory_errors.fetch_add(1);
    } else if (error_type == "insert") {
        metrics_.insert_errors.fetch_add(1);
    } else if (error_type == "retrieve") {
        metrics_.retrieve_errors.fetch_add(1);
    }
}

void MetricsCollector::record_recovery_attempt() {
    if (!config_.enable_metrics) return;
    
    metrics_.recovery_attempts.fetch_add(1);
}

void MetricsCollector::record_thread_contention(uint64_t wait_time_ns) {
    if (!config_.enable_metrics) return;
    
    metrics_.thread_contention_count.fetch_add(1);
    metrics_.lock_wait_time.fetch_add(wait_time_ns);
}

void MetricsCollector::record_numa_allocation(bool cross_numa) {
    if (!config_.enable_metrics) return;
    
    metrics_.numa_allocations.fetch_add(1);
    if (cross_numa) {
        metrics_.cross_numa_accesses.fetch_add(1);
    }
}

// Metrics retrieval
CacheMetrics MetricsCollector::get_current_metrics() const {
    return metrics_;
}

std::vector<MetricsCollector::MetricsSnapshot> MetricsCollector::get_historical_data() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return historical_data_;
}

// Performance calculations
double MetricsCollector::get_average_insert_latency() const {
    uint64_t total_inserts = metrics_.total_inserts.load();
    if (total_inserts == 0) return 0.0;
    
    uint64_t total_latency = metrics_.total_insert_latency.load();
    return static_cast<double>(total_latency) / static_cast<double>(total_inserts);
}

double MetricsCollector::get_average_retrieve_latency() const {
    uint64_t total_retrieves = metrics_.total_retrieves.load();
    if (total_retrieves == 0) return 0.0;
    
    uint64_t total_latency = metrics_.total_retrieve_latency.load();
    return static_cast<double>(total_latency) / static_cast<double>(total_retrieves);
}

double MetricsCollector::get_cache_hit_rate() const {
    uint64_t hits = metrics_.cache_hits.load();
    uint64_t misses = metrics_.cache_misses.load();
    uint64_t total = hits + misses;
    
    if (total == 0) return 0.0;
    return static_cast<double>(hits) / static_cast<double>(total);
}

double MetricsCollector::get_error_rate() const {
    uint64_t total_operations = metrics_.total_inserts.load() + metrics_.total_retrieves.load();
    if (total_operations == 0) return 0.0;
    
    uint64_t total_errors = metrics_.insert_errors.load() + metrics_.retrieve_errors.load();
    return static_cast<double>(total_errors) / static_cast<double>(total_operations);
}

double MetricsCollector::get_memory_utilization() const {
    size_t current_usage = metrics_.current_memory_usage.load();
    size_t max_memory = config_.max_memory_mb * 1024 * 1024;
    
    if (max_memory == 0) return 0.0;
    return static_cast<double>(current_usage) / static_cast<double>(max_memory);
}

// Alerts and monitoring
bool MetricsCollector::check_alerts() const {
    // Check latency alerts
    if (get_average_insert_latency() > thresholds_.max_latency_ns) {
        return true;
    }
    
    if (get_average_retrieve_latency() > thresholds_.max_latency_ns) {
        return true;
    }
    
    // Check memory alerts
    if (get_memory_utilization() > 0.9) {  // 90% memory usage
        return true;
    }
    
    // Check error rate alerts
    if (get_error_rate() > static_cast<double>(thresholds_.max_error_rate) / 1000.0) {
        return true;
    }
    
    return false;
}

std::vector<std::string> MetricsCollector::get_active_alerts() const {
    std::vector<std::string> alerts;
    
    // Check latency alerts
    if (get_average_insert_latency() > thresholds_.max_latency_ns) {
        alerts.push_back("High insert latency: " + std::to_string(get_average_insert_latency()) + " ns");
    }
    
    if (get_average_retrieve_latency() > thresholds_.max_latency_ns) {
        alerts.push_back("High retrieve latency: " + std::to_string(get_average_retrieve_latency()) + " ns");
    }
    
    // Check memory alerts
    if (get_memory_utilization() > 0.9) {
        alerts.push_back("High memory usage: " + std::to_string(get_memory_utilization() * 100) + "%");
    }
    
    // Check error rate alerts
    if (get_error_rate() > static_cast<double>(thresholds_.max_error_rate) / 1000.0) {
        alerts.push_back("High error rate: " + std::to_string(get_error_rate() * 100) + "%");
    }
    
    return alerts;
}

// Reporting
void MetricsCollector::generate_report(const std::string& filename) const {
    std::ofstream report_file;
    if (filename.empty()) {
        report_file.open("cache_performance_report.html");
    } else {
        report_file.open(filename);
    }
    
    if (!report_file.is_open()) {
        std::cerr << "Failed to open report file" << std::endl;
        return;
    }
    
    // Generate HTML report
    report_file << "<!DOCTYPE html>\n<html>\n<head>\n";
    report_file << "<title>HFT Cache Performance Report</title>\n";
    report_file << "<style>\n";
    report_file << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    report_file << ".metric { margin: 10px 0; padding: 10px; border: 1px solid #ccc; }\n";
    report_file << ".alert { background-color: #ffebee; border-color: #f44336; }\n";
    report_file << ".good { background-color: #e8f5e8; border-color: #4caf50; }\n";
    report_file << "</style>\n</head>\n<body>\n";
    
    report_file << "<h1>HFT Cache Performance Report</h1>\n";
    report_file << "<p>Generated: " << std::chrono::system_clock::now().time_since_epoch().count() << "</p>\n";
    
    // Performance metrics
    report_file << "<h2>Performance Metrics</h2>\n";
    report_file << "<div class='metric " << (get_average_insert_latency() < 1000 ? "good" : "alert") << "'>\n";
    report_file << "<strong>Average Insert Latency:</strong> " << get_average_insert_latency() << " ns\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric " << (get_average_retrieve_latency() < 500 ? "good" : "alert") << "'>\n";
    report_file << "<strong>Average Retrieve Latency:</strong> " << get_average_retrieve_latency() << " ns\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric " << (get_cache_hit_rate() > 0.8 ? "good" : "alert") << "'>\n";
    report_file << "<strong>Cache Hit Rate:</strong> " << std::fixed << std::setprecision(2) 
                << (get_cache_hit_rate() * 100) << "%\n";
    report_file << "</div>\n";
    
    // Memory metrics
    report_file << "<h2>Memory Metrics</h2>\n";
    report_file << "<div class='metric " << (get_memory_utilization() < 0.8 ? "good" : "alert") << "'>\n";
    report_file << "<strong>Memory Utilization:</strong> " << std::fixed << std::setprecision(2) 
                << (get_memory_utilization() * 100) << "%\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Current Memory Usage:</strong> " << metrics_.current_memory_usage.load() << " bytes\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Peak Memory Usage:</strong> " << metrics_.peak_memory_usage.load() << " bytes\n";
    report_file << "</div>\n";
    
    // Error metrics
    report_file << "<h2>Error Metrics</h2>\n";
    report_file << "<div class='metric " << (get_error_rate() < 0.01 ? "good" : "alert") << "'>\n";
    report_file << "<strong>Error Rate:</strong> " << std::fixed << std::setprecision(4) 
                << (get_error_rate() * 100) << "%\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Total Errors:</strong> " << (metrics_.insert_errors.load() + metrics_.retrieve_errors.load()) << "\n";
    report_file << "</div>\n";
    
    // Operation counts
    report_file << "<h2>Operation Counts</h2>\n";
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Total Inserts:</strong> " << metrics_.total_inserts.load() << "\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Total Retrieves:</strong> " << metrics_.total_retrieves.load() << "\n";
    report_file << "</div>\n";
    
    report_file << "<div class='metric'>\n";
    report_file << "<strong>Total Batch Operations:</strong> " 
                << (metrics_.total_batch_inserts.load() + metrics_.total_batch_retrieves.load()) << "\n";
    report_file << "</div>\n";
    
    // Active alerts
    auto alerts = get_active_alerts();
    if (!alerts.empty()) {
        report_file << "<h2>Active Alerts</h2>\n";
        for (const auto& alert : alerts) {
            report_file << "<div class='metric alert'>\n";
            report_file << "<strong>Alert:</strong> " << alert << "\n";
            report_file << "</div>\n";
        }
    }
    
    report_file << "</body>\n</html>\n";
    report_file.close();
}

void MetricsCollector::export_metrics(const std::string& filename) const {
    std::ofstream export_file;
    if (filename.empty()) {
        export_file.open("cache_metrics.json");
    } else {
        export_file.open(filename);
    }
    
    if (!export_file.is_open()) {
        std::cerr << "Failed to open export file" << std::endl;
        return;
    }
    
    // Export as JSON
    export_file << "{\n";
    export_file << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << ",\n";
    export_file << "  \"performance\": {\n";
    export_file << "    \"average_insert_latency_ns\": " << get_average_insert_latency() << ",\n";
    export_file << "    \"average_retrieve_latency_ns\": " << get_average_retrieve_latency() << ",\n";
    export_file << "    \"cache_hit_rate\": " << get_cache_hit_rate() << ",\n";
    export_file << "    \"error_rate\": " << get_error_rate() << "\n";
    export_file << "  },\n";
    export_file << "  \"memory\": {\n";
    export_file << "    \"current_usage_bytes\": " << metrics_.current_memory_usage.load() << ",\n";
    export_file << "    \"peak_usage_bytes\": " << metrics_.peak_memory_usage.load() << ",\n";
    export_file << "    \"utilization\": " << get_memory_utilization() << "\n";
    export_file << "  },\n";
    export_file << "  \"operations\": {\n";
    export_file << "    \"total_inserts\": " << metrics_.total_inserts.load() << ",\n";
    export_file << "    \"total_retrieves\": " << metrics_.total_retrieves.load() << ",\n";
    export_file << "    \"total_batch_inserts\": " << metrics_.total_batch_inserts.load() << ",\n";
    export_file << "    \"total_batch_retrieves\": " << metrics_.total_batch_retrieves.load() << "\n";
    export_file << "  },\n";
    export_file << "  \"errors\": {\n";
    export_file << "    \"insert_errors\": " << metrics_.insert_errors.load() << ",\n";
    export_file << "    \"retrieve_errors\": " << metrics_.retrieve_errors.load() << ",\n";
    export_file << "    \"memory_errors\": " << metrics_.memory_errors.load() << ",\n";
    export_file << "    \"recovery_attempts\": " << metrics_.recovery_attempts.load() << "\n";
    export_file << "  }\n";
    export_file << "}\n";
    
    export_file.close();
}

// Configuration
void MetricsCollector::set_alert_thresholds(const AlertThresholds& thresholds) {
    thresholds_ = thresholds;
}

void MetricsCollector::enable_metrics_collection(bool enable) {
    config_.enable_metrics = enable;
    
    if (enable && !metrics_thread_.joinable()) {
        start_metrics_thread();
        open_metrics_file();
    } else if (!enable && metrics_thread_.joinable()) {
        shutdown_.store(true);
        metrics_thread_.join();
        if (metrics_file_.is_open()) {
            metrics_file_.close();
        }
    }
}

// Private methods
void MetricsCollector::start_metrics_thread() {
    shutdown_.store(false);
    metrics_thread_ = std::thread(&MetricsCollector::metrics_worker, this);
}

void MetricsCollector::metrics_worker() {
    while (!shutdown_.load()) {
        write_metrics_to_file();
        update_historical_data();
        check_and_trigger_alerts();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.metrics_interval_ms));
    }
}

void MetricsCollector::write_metrics_to_file() {
    if (!metrics_file_.is_open()) return;
    
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    metrics_file_ << timestamp << ","
                  << get_average_insert_latency() << ","
                  << get_average_retrieve_latency() << ","
                  << get_cache_hit_rate() << ","
                  << get_error_rate() << ","
                  << get_memory_utilization() << ","
                  << metrics_.total_inserts.load() << ","
                  << metrics_.total_retrieves.load() << "\n";
    
    metrics_file_.flush();
}

void MetricsCollector::update_historical_data() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    
    MetricsSnapshot snapshot;
    snapshot.timestamp = std::chrono::steady_clock::now();
    snapshot.metrics = metrics_;
    
    historical_data_.push_back(snapshot);
    
    // Maintain history size
    while (historical_data_.size() > MAX_HISTORY_SIZE) {
        historical_data_.erase(historical_data_.begin());
    }
}

void MetricsCollector::check_and_trigger_alerts() {
    if (check_alerts()) {
        auto alerts = get_active_alerts();
        for (const auto& alert : alerts) {
            std::cerr << "ALERT: " << alert << std::endl;
        }
    }
}

std::string MetricsCollector::format_metrics() const {
    std::stringstream ss;
    ss << "Metrics Summary:\n";
    ss << "  Insert Latency: " << get_average_insert_latency() << " ns\n";
    ss << "  Retrieve Latency: " << get_average_retrieve_latency() << " ns\n";
    ss << "  Cache Hit Rate: " << std::fixed << std::setprecision(2) 
       << (get_cache_hit_rate() * 100) << "%\n";
    ss << "  Error Rate: " << std::fixed << std::setprecision(4) 
       << (get_error_rate() * 100) << "%\n";
    ss << "  Memory Usage: " << std::fixed << std::setprecision(2) 
       << (get_memory_utilization() * 100) << "%\n";
    return ss.str();
}

void MetricsCollector::open_metrics_file() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (metrics_file_.is_open()) {
        metrics_file_.close();
    }
    
    metrics_file_.open(config_.metrics_file, std::ios::app);
    if (metrics_file_.is_open()) {
        // Write header if file is empty
        metrics_file_.seekp(0, std::ios::end);
        if (metrics_file_.tellp() == 0) {
            metrics_file_ << "timestamp,insert_latency_ns,retrieve_latency_ns,hit_rate,error_rate,memory_utilization,total_inserts,total_retrieves\n";
        }
    }
}

void MetricsCollector::rotate_metrics_file() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    if (metrics_file_.is_open()) {
        metrics_file_.close();
    }
    
    // Create new filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    std::string new_filename = config_.metrics_file + "." + std::to_string(timestamp);
    std::rename(config_.metrics_file.c_str(), new_filename.c_str());
    
    // Open new file
    metrics_file_.open(config_.metrics_file, std::ios::app);
    if (metrics_file_.is_open()) {
        metrics_file_ << "timestamp,insert_latency_ns,retrieve_latency_ns,hit_rate,error_rate,memory_utilization,total_inserts,total_retrieves\n";
    }
}

// Global metrics instance
MetricsCollector* g_metrics = nullptr; 