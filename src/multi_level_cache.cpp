#include "multi_level_cache.hpp"
#include "memory_manager.hpp"
#include "metrics.hpp"
#include "error_handler.hpp"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hft_cache {

// MultiLevelCache Implementation
MultiLevelCache::MultiLevelCache(const CacheConfig& config) 
    : config_(config) {
    
    if (!config_.validate_config()) {
        throw std::invalid_argument("Invalid cache configuration for multi-level cache");
    }
    
    // Initialize L2 cache
    l2_cache_ = std::make_unique<RadialCircularList>(config_);
    
    // Initialize L3 cache
    l3_cache_ = std::make_unique<DiskBackedCache>(config_);
    
    // Start background management
    start_background_management();
    
    if (g_metrics) {
        g_metrics->record_cache_creation("MultiLevelCache");
    }
}

MultiLevelCache::~MultiLevelCache() {
    stop_background_management();
    if (management_thread_.joinable()) {
        management_thread_.join();
    }
}

bool MultiLevelCache::insert(double value, const std::string& symbol, int priority, double expiry_seconds) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Create new node
        Node* node = new Node();
        node->value = value;
        node->symbol = symbol;
        node->priority = priority;
        node->timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        node->expiry = std::chrono::high_resolution_clock::now().time_since_epoch().count() + 
                      static_cast<uint64_t>(expiry_seconds * 1e9);
        
        // Try to insert into L1 first (hottest data)
        if (l1_stats_.item_count.load() < config_.l1_capacity && priority >= config_.l1_min_priority) {
            if (l1_cache_.enqueue(node)) {
                l1_stats_.item_count.fetch_add(1);
                record_access(node, 0);
                
                if (g_metrics) {
                    g_metrics->record_insert(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now() - start_time).count());
                }
                return true;
            }
        }
        
        // Fall back to L2 cache
        if (l2_cache_->insert(value, symbol, priority, expiry_seconds)) {
            l2_stats_.item_count.fetch_add(1);
            
            if (g_metrics) {
                g_metrics->record_insert(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return true;
        }
        
        // Finally, try L3 cache
        if (l3_cache_->insert(node)) {
            l3_stats_.item_count.fetch_add(1);
            
            if (g_metrics) {
                g_metrics->record_insert(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return true;
        }
        
        // If all levels are full, delete the node
        delete node;
        
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::CACHE_FULL, ErrorSeverity::MEDIUM, 
                "All cache levels are full", __FILE__, __LINE__);
        }
        
        return false;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::INSERTION_FAILED, ErrorSeverity::HIGH,
                std::string("Insert failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

Node* MultiLevelCache::get_highest_priority(const std::string& symbol) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        // Search L1 first (fastest)
        Node* result = nullptr;
        std::vector<Node*> temp_nodes;
        
        // Dequeue all nodes from L1 to search
        while (l1_cache_.dequeue(result)) {
            if (result->symbol == symbol) {
                // Found in L1, record hit and re-queue remaining nodes
                l1_stats_.hit_count.fetch_add(1);
                record_access(result, std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
                
                // Re-queue other nodes
                for (Node* node : temp_nodes) {
                    l1_cache_.enqueue(node);
                }
                
                if (g_metrics) {
                    g_metrics->record_retrieve(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now() - start_time).count());
                }
                return result;
            }
            temp_nodes.push_back(result);
        }
        
        // Re-queue all L1 nodes
        for (Node* node : temp_nodes) {
            l1_cache_.enqueue(node);
        }
        
        // Search L2 cache
        result = l2_cache_->get_highest_priority(symbol);
        if (result) {
            l2_stats_.hit_count.fetch_add(1);
            record_access(result, std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count());
            
            // Consider promoting to L1
            if (should_promote_to_l1(result)) {
                promote_to_l1(result);
            }
            
            if (g_metrics) {
                g_metrics->record_retrieve(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return result;
        }
        
        // Search L3 cache
        result = l3_cache_->retrieve(symbol, 0.0); // 0.0 means any value
        if (result) {
            l3_stats_.hit_count.fetch_add(1);
            record_access(result, std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count());
            
            // Promote to L2
            if (l2_cache_->insert(result->value, result->symbol, result->priority, 60.0)) {
                l2_stats_.item_count.fetch_add(1);
            }
            
            if (g_metrics) {
                g_metrics->record_retrieve(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return result;
        }
        
        // Record miss
        l1_stats_.miss_count.fetch_add(1);
        l2_stats_.miss_count.fetch_add(1);
        l3_stats_.miss_count.fetch_add(1);
        
        if (g_metrics) {
            g_metrics->record_miss(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count());
        }
        
        return nullptr;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::RETRIEVAL_FAILED, ErrorSeverity::HIGH,
                std::string("Retrieve failed: ") + e.what(), __FILE__, __LINE__);
        }
        return nullptr;
    }
}

bool MultiLevelCache::remove(const std::string& symbol, double value) {
    try {
        // Try to remove from L1
        Node* result = nullptr;
        std::vector<Node*> temp_nodes;
        
        while (l1_cache_.dequeue(result)) {
            if (result->symbol == symbol && result->value == value) {
                l1_stats_.item_count.fetch_sub(1);
                delete result;
                
                // Re-queue remaining nodes
                for (Node* node : temp_nodes) {
                    l1_cache_.enqueue(node);
                }
                return true;
            }
            temp_nodes.push_back(result);
        }
        
        // Re-queue all L1 nodes
        for (Node* node : temp_nodes) {
            l1_cache_.enqueue(node);
        }
        
        // Try L2 cache
        if (l2_cache_->remove(symbol, value)) {
            l2_stats_.item_count.fetch_sub(1);
            return true;
        }
        
        // Try L3 cache
        if (l3_cache_->remove(symbol, value)) {
            l3_stats_.item_count.fetch_sub(1);
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::REMOVAL_FAILED, ErrorSeverity::MEDIUM,
                std::string("Remove failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

void MultiLevelCache::clear() {
    // Clear L1 cache
    Node* node = nullptr;
    while (l1_cache_.dequeue(node)) {
        delete node;
    }
    l1_stats_.item_count.store(0);
    
    // Clear L2 cache
    l2_cache_->clear();
    l2_stats_.item_count.store(0);
    
    // Clear L3 cache
    l3_cache_->clear();
    l3_stats_.item_count.store(0);
}

bool MultiLevelCache::promote_to_l1(Node* node) {
    if (l1_stats_.item_count.load() >= config_.l1_capacity) {
        // Need to evict from L1 first
        eviction_policy_l1();
    }
    
    if (l1_cache_.enqueue(node)) {
        l1_stats_.item_count.fetch_add(1);
        return true;
    }
    return false;
}

bool MultiLevelCache::demote_to_l2(Node* node) {
    if (l2_cache_->insert(node->value, node->symbol, node->priority, 60.0)) {
        l2_stats_.item_count.fetch_add(1);
        return true;
    }
    return false;
}

bool MultiLevelCache::demote_to_l3(Node* node) {
    if (l3_cache_->insert(node)) {
        l3_stats_.item_count.fetch_add(1);
        return true;
    }
    return false;
}

MultiLevelCache::LevelStats MultiLevelCache::get_l1_stats() const {
    return l1_stats_;
}

MultiLevelCache::LevelStats MultiLevelCache::get_l2_stats() const {
    return l2_stats_;
}

MultiLevelCache::LevelStats MultiLevelCache::get_l3_stats() const {
    return l3_stats_;
}

void MultiLevelCache::set_l1_capacity(size_t capacity) {
    config_.l1_capacity = capacity;
}

void MultiLevelCache::set_l2_capacity(size_t capacity) {
    config_.l2_capacity = capacity;
}

void MultiLevelCache::set_l3_capacity(size_t capacity) {
    config_.l3_capacity = capacity;
}

void MultiLevelCache::start_background_management() {
    if (!running_.load()) {
        running_.store(true);
        should_stop_.store(false);
        management_thread_ = std::thread(&MultiLevelCache::background_management_worker, this);
    }
}

void MultiLevelCache::stop_background_management() {
    should_stop_.store(true);
    running_.store(false);
}

void MultiLevelCache::eviction_policy_l1() {
    // Simple LRU-like eviction: remove oldest/lowest priority items
    Node* node = nullptr;
    if (l1_cache_.dequeue(node)) {
        l1_stats_.item_count.fetch_sub(1);
        
        // Try to demote to L2
        if (!demote_to_l2(node)) {
            // If L2 is full, demote to L3
            if (!demote_to_l3(node)) {
                // If all levels are full, delete
                delete node;
            }
        }
    }
}

void MultiLevelCache::eviction_policy_l2() {
    // L2 uses the existing eviction policy from RadialCircularList
    // This is handled internally by the L2 cache
}

void MultiLevelCache::eviction_policy_l3() {
    // L3 eviction is handled by disk space management
    // This is handled internally by the DiskBackedCache
}

void MultiLevelCache::background_management_worker() {
    while (running_.load() && !should_stop_.load()) {
        try {
            // Update access patterns
            update_access_patterns();
            
            // Run eviction policies periodically
            if (l1_stats_.item_count.load() > config_.l1_capacity * 0.9) {
                eviction_policy_l1();
            }
            
            // Sleep for management interval
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.management_interval_ms));
            
        } catch (const std::exception& e) {
            if (g_error_handler) {
                g_error_handler->report_error(ErrorType::BACKGROUND_WORKER_FAILED, ErrorSeverity::MEDIUM,
                    std::string("Background worker failed: ") + e.what(), __FILE__, __LINE__);
            }
        }
    }
}

bool MultiLevelCache::should_promote_to_l1(const Node* node) const {
    // Promote based on priority and access frequency
    return node->priority >= config_.l1_min_priority;
}

bool MultiLevelCache::should_demote_to_l2(const Node* node) const {
    // Demote based on low priority or age
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return node->priority < config_.l1_min_priority || 
           (now - node->timestamp) > config_.l1_max_age_ns;
}

bool MultiLevelCache::should_demote_to_l3(const Node* node) const {
    // Demote very old or low priority items
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (now - node->timestamp) > config_.l2_max_age_ns;
}

void MultiLevelCache::record_access(Node* node, uint64_t access_time_ns) {
    node->last_access = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    node->access_count++;
}

void MultiLevelCache::update_access_patterns() {
    // Update access patterns for promotion/demotion decisions
    // This could be enhanced with more sophisticated algorithms
}

// DiskBackedCache Implementation
DiskBackedCache::DiskBackedCache(const CacheConfig& config) 
    : config_(config) {
    
    disk_path_ = config_.disk_cache_path;
    if (disk_path_.empty()) {
        disk_path_ = "./cache_data";
    }
    
    // Create directory if it doesn't exist
    std::filesystem::create_directories(disk_path_);
    
    // Load existing data from disk
    load_from_disk();
}

DiskBackedCache::~DiskBackedCache() {
    flush_to_disk();
    
    // Clean up memory index
    for (auto& pair : memory_index_) {
        delete pair.second;
    }
    memory_index_.clear();
}

bool DiskBackedCache::insert(Node* node) {
    try {
        std::string key = node->symbol + "_" + std::to_string(node->value);
        
        // Check if already exists
        auto it = memory_index_.find(key);
        if (it != memory_index_.end()) {
            // Update existing node
            delete it->second;
            it->second = node;
        } else {
            // Insert new node
            memory_index_[key] = node;
            disk_size_.fetch_add(sizeof(Node));
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::DISK_INSERT_FAILED, ErrorSeverity::MEDIUM,
                std::string("Disk insert failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

Node* DiskBackedCache::retrieve(const std::string& symbol, double value) {
    try {
        std::string key = symbol + "_" + std::to_string(value);
        
        auto it = memory_index_.find(key);
        if (it != memory_index_.end()) {
            return it->second;
        }
        
        return nullptr;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::DISK_RETRIEVE_FAILED, ErrorSeverity::MEDIUM,
                std::string("Disk retrieve failed: ") + e.what(), __FILE__, __LINE__);
        }
        return nullptr;
    }
}

bool DiskBackedCache::remove(const std::string& symbol, double value) {
    try {
        std::string key = symbol + "_" + std::to_string(value);
        
        auto it = memory_index_.find(key);
        if (it != memory_index_.end()) {
            delete it->second;
            memory_index_.erase(it);
            disk_size_.fetch_sub(sizeof(Node));
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::DISK_REMOVE_FAILED, ErrorSeverity::MEDIUM,
                std::string("Disk remove failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

void DiskBackedCache::clear() {
    for (auto& pair : memory_index_) {
        delete pair.second;
    }
    memory_index_.clear();
    disk_size_.store(0);
}

bool DiskBackedCache::flush_to_disk() {
    try {
        std::string filename = disk_path_ + "/cache_data.bin";
        std::ofstream file(filename, std::ios::binary);
        
        if (!file.is_open()) {
            return false;
        }
        
        // Write header
        uint64_t magic = 0xCAFECACHE;
        uint64_t version = 1;
        uint64_t count = memory_index_.size();
        
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        // Write nodes
        for (const auto& pair : memory_index_) {
            if (!serialize_node(pair.second, file)) {
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::DISK_FLUSH_FAILED, ErrorSeverity::HIGH,
                std::string("Disk flush failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

bool DiskBackedCache::load_from_disk() {
    try {
        std::string filename = disk_path_ + "/cache_data.bin";
        std::ifstream file(filename, std::ios::binary);
        
        if (!file.is_open()) {
            return false;
        }
        
        // Read header
        uint64_t magic, version, count;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        if (magic != 0xCAFECACHE) {
            return false;
        }
        
        // Read nodes
        for (uint64_t i = 0; i < count; ++i) {
            Node* node = deserialize_node(file);
            if (node) {
                std::string key = node->symbol + "_" + std::to_string(node->value);
                memory_index_[key] = node;
                disk_size_.fetch_add(sizeof(Node));
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::DISK_LOAD_FAILED, ErrorSeverity::HIGH,
                std::string("Disk load failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

size_t DiskBackedCache::get_disk_size() const {
    return disk_size_.load();
}

bool DiskBackedCache::serialize_node(const Node* node, std::ofstream& file) {
    try {
        // Write node data
        file.write(reinterpret_cast<const char*>(&node->value), sizeof(node->value));
        file.write(reinterpret_cast<const char*>(&node->priority), sizeof(node->priority));
        file.write(reinterpret_cast<const char*>(&node->timestamp), sizeof(node->timestamp));
        file.write(reinterpret_cast<const char*>(&node->expiry), sizeof(node->expiry));
        file.write(reinterpret_cast<const char*>(&node->last_access), sizeof(node->last_access));
        file.write(reinterpret_cast<const char*>(&node->access_count), sizeof(node->access_count));
        
        // Write symbol string
        uint32_t symbol_length = static_cast<uint32_t>(node->symbol.length());
        file.write(reinterpret_cast<const char*>(&symbol_length), sizeof(symbol_length));
        file.write(node->symbol.c_str(), symbol_length);
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

Node* DiskBackedCache::deserialize_node(std::ifstream& file) {
    try {
        Node* node = new Node();
        
        // Read node data
        file.read(reinterpret_cast<char*>(&node->value), sizeof(node->value));
        file.read(reinterpret_cast<char*>(&node->priority), sizeof(node->priority));
        file.read(reinterpret_cast<char*>(&node->timestamp), sizeof(node->timestamp));
        file.read(reinterpret_cast<char*>(&node->expiry), sizeof(node->expiry));
        file.read(reinterpret_cast<char*>(&node->last_access), sizeof(node->last_access));
        file.read(reinterpret_cast<char*>(&node->access_count), sizeof(node->access_count));
        
        // Read symbol string
        uint32_t symbol_length;
        file.read(reinterpret_cast<char*>(&symbol_length), sizeof(symbol_length));
        
        std::vector<char> symbol_buffer(symbol_length);
        file.read(symbol_buffer.data(), symbol_length);
        node->symbol = std::string(symbol_buffer.data(), symbol_length);
        
        return node;
        
    } catch (const std::exception& e) {
        return nullptr;
    }
}

} // namespace hft_cache 