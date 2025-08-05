#ifndef PERSISTENT_CACHE_HPP
#define PERSISTENT_CACHE_HPP

#include "radial_circular_list.hpp"
#include "config.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>

struct CheckpointMetadata {
    uint64_t timestamp;
    size_t node_count;
    size_t symbol_count;
    std::string filename;
    bool is_incremental;
    uint64_t base_checkpoint_timestamp;  // For incremental checkpoints
};

class PersistentCache {
private:
    RadialCircularList& cache_;
    CacheConfig config_;
    std::string checkpoint_dir_;
    std::atomic<bool> checkpoint_in_progress_{false};
    std::thread checkpoint_thread_;
    std::atomic<bool> shutdown_{false};
    
    // Checkpoint management
    std::queue<CheckpointMetadata> checkpoint_history_;
    std::mutex checkpoint_mutex_;
    static constexpr size_t MAX_CHECKPOINT_HISTORY = 10;
    
    // Incremental checkpointing
    std::atomic<uint64_t> last_checkpoint_timestamp_{0};
    std::vector<std::pair<uint64_t, std::string>> modified_nodes_;
    std::mutex modification_mutex_;

public:
    PersistentCache(RadialCircularList& cache, const CacheConfig& config, 
                   const std::string& checkpoint_dir = "./checkpoints")
        : cache_(cache), config_(config), checkpoint_dir_(checkpoint_dir) {
        
        std::filesystem::create_directories(checkpoint_dir_);
        start_checkpoint_thread();
    }
    
    ~PersistentCache() {
        shutdown_.store(true);
        if (checkpoint_thread_.joinable()) {
            checkpoint_thread_.join();
        }
    }
    
    // Full checkpoint operations
    bool checkpoint_to_disk(const std::string& filename = "") {
        if (checkpoint_in_progress_.load()) {
            return false;  // Already in progress
        }
        
        std::string checkpoint_file = filename.empty() ? 
            generate_checkpoint_filename() : filename;
        
        return perform_checkpoint(checkpoint_file, false);
    }
    
    bool restore_from_disk(const std::string& filename) {
        if (!std::filesystem::exists(filename)) {
            return false;
        }
        
        return perform_restore(filename);
    }
    
    // Incremental checkpointing
    bool incremental_checkpoint() {
        if (checkpoint_in_progress_.load()) {
            return false;
        }
        
        std::string checkpoint_file = generate_incremental_filename();
        return perform_checkpoint(checkpoint_file, true);
    }
    
    // Point-in-time recovery
    bool point_in_time_recovery(uint64_t timestamp) {
        auto checkpoint_file = find_checkpoint_at_time(timestamp);
        if (checkpoint_file.empty()) {
            return false;
        }
        
        return perform_restore(checkpoint_file);
    }
    
    // Checkpoint management
    std::vector<CheckpointMetadata> list_checkpoints() const {
        std::lock_guard<std::mutex> lock(checkpoint_mutex_);
        std::vector<CheckpointMetadata> checkpoints;
        std::queue<CheckpointMetadata> temp_queue = checkpoint_history_;
        
        while (!temp_queue.empty()) {
            checkpoints.push_back(temp_queue.front());
            temp_queue.pop();
        }
        
        return checkpoints;
    }
    
    bool delete_checkpoint(const std::string& filename) {
        std::filesystem::path filepath = std::filesystem::path(checkpoint_dir_) / filename;
        if (std::filesystem::exists(filepath)) {
            return std::filesystem::remove(filepath);
        }
        return false;
    }
    
    // Automatic checkpointing
    void enable_auto_checkpoint(std::chrono::seconds interval) {
        // Implementation would schedule periodic checkpoints
    }
    
    void disable_auto_checkpoint() {
        // Implementation would stop automatic checkpoints
    }
    
    // Compression support
    void enable_compression(bool enable = true) {
        // Implementation would enable/disable checkpoint compression
    }
    
    // Encryption support
    void enable_encryption(const std::string& key) {
        // Implementation would enable checkpoint encryption
    }

private:
    void start_checkpoint_thread() {
        checkpoint_thread_ = std::thread([this]() {
            while (!shutdown_.load()) {
                // Background checkpoint processing
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }
    
    std::string generate_checkpoint_filename() const {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        return std::string("checkpoint_") + std::to_string(timestamp) + ".dat";
    }
    
    std::string generate_incremental_filename() const {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        return std::string("incremental_") + std::to_string(timestamp) + ".dat";
    }
    
    bool perform_checkpoint(const std::string& filename, bool incremental) {
        checkpoint_in_progress_.store(true);
        
        try {
            std::filesystem::path filepath = std::filesystem::path(checkpoint_dir_) / filename;
            std::ofstream file(filepath, std::ios::binary);
            
            if (!file.is_open()) {
                checkpoint_in_progress_.store(false);
                return false;
            }
            
            // Write checkpoint header
            write_checkpoint_header(file, incremental);
            
            // Write cache data
            write_cache_data(file, incremental);
            
            // Update metadata
            update_checkpoint_metadata(filename, incremental);
            
            checkpoint_in_progress_.store(false);
            return true;
            
        } catch (const std::exception& e) {
            checkpoint_in_progress_.store(false);
            return false;
        }
    }
    
    bool perform_restore(const std::string& filename) {
        try {
            std::ifstream file(filename, std::ios::binary);
            
            if (!file.is_open()) {
                return false;
            }
            
            // Read checkpoint header
            auto header = read_checkpoint_header(file);
            
            // Clear current cache
            clear_cache();
            
            // Restore cache data
            restore_cache_data(file, header);
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    void write_checkpoint_header(std::ofstream& file, bool incremental) {
        // Write checkpoint format version
        uint32_t version = 1;
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        
        // Write checkpoint type
        uint8_t type = incremental ? 1 : 0;
        file.write(reinterpret_cast<const char*>(&type), sizeof(type));
        
        // Write timestamp
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        file.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
        
        // Write node count placeholder
        uint64_t node_count = 0;  // Will be updated later
        file.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
    }
    
    void write_cache_data(std::ofstream& file, bool incremental) {
        // Implementation would serialize all cache data
        // This is a placeholder for the actual serialization logic
    }
    
    void update_checkpoint_metadata(const std::string& filename, bool incremental) {
        std::lock_guard<std::mutex> lock(checkpoint_mutex_);
        
        CheckpointMetadata metadata;
        metadata.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        metadata.filename = filename;
        metadata.is_incremental = incremental;
        
        checkpoint_history_.push(metadata);
        
        // Maintain history size
        while (checkpoint_history_.size() > MAX_CHECKPOINT_HISTORY) {
            checkpoint_history_.pop();
        }
    }
    
    struct CheckpointHeader {
        uint32_t version;
        uint8_t type;
        uint64_t timestamp;
        uint64_t node_count;
    };
    
    CheckpointHeader read_checkpoint_header(std::ifstream& file) {
        CheckpointHeader header;
        
        file.read(reinterpret_cast<char*>(&header.version), sizeof(header.version));
        file.read(reinterpret_cast<char*>(&header.type), sizeof(header.type));
        file.read(reinterpret_cast<char*>(&header.timestamp), sizeof(header.timestamp));
        file.read(reinterpret_cast<char*>(&header.node_count), sizeof(header.node_count));
        
        return header;
    }
    
    void restore_cache_data(std::ifstream& file, const CheckpointHeader& header) {
        // Implementation would deserialize and restore cache data
        // This is a placeholder for the actual deserialization logic
    }
    
    void clear_cache() {
        // Implementation would clear the current cache
        // This is a placeholder
    }
    
    std::string find_checkpoint_at_time(uint64_t timestamp) const {
        // Implementation would find the closest checkpoint to the given timestamp
        return "";
    }
    
    void record_node_modification(uint64_t timestamp, const std::string& node_id) {
        std::lock_guard<std::mutex> lock(modification_mutex_);
        modified_nodes_.emplace_back(timestamp, node_id);
    }
};

#endif 