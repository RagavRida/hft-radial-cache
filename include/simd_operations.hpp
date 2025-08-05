#pragma once

#include "node.hpp"
#include <vector>
#include <immintrin.h> // For AVX/AVX2/AVX-512
#include <memory>

namespace hft_cache {

/**
 * @brief SIMD operations for vectorized cache operations
 * 
 * Provides vectorized operations for batch processing, priority updates,
 * expiry checks, and value calculations using AVX/AVX2/AVX-512 instructions.
 */
class SIMDOperations {
public:
    explicit SIMDOperations(const CacheConfig& config);
    ~SIMDOperations() = default;

    // Batch operations
    void vectorized_insert_batch(const std::vector<Node*>& nodes);
    void vectorized_priority_update(const std::vector<std::pair<Node*, int>>& updates);
    void vectorized_expiry_check();
    void vectorized_value_calculation();
    
    // SIMD-enabled search operations
    std::vector<Node*> vectorized_search_by_symbol(const std::string& symbol);
    std::vector<Node*> vectorized_search_by_value_range(double min_value, double max_value);
    std::vector<Node*> vectorized_search_by_priority_range(int min_priority, int max_priority);
    
    // Performance monitoring
    double get_simd_utilization() const;
    size_t get_vectorized_operations_count() const;
    void reset_performance_counters();

private:
    CacheConfig config_;
    std::atomic<size_t> vectorized_ops_count_{0};
    std::atomic<size_t> total_ops_count_{0};
    
    // SIMD helper methods
    bool check_avx_support() const;
    bool check_avx2_support() const;
    bool check_avx512_support() const;
    
    // Vectorized implementations
    void vectorized_insert_avx2(const std::vector<Node*>& nodes);
    void vectorized_insert_avx512(const std::vector<Node*>& nodes);
    void vectorized_priority_update_avx2(const std::vector<std::pair<Node*, int>>& updates);
    void vectorized_expiry_check_avx2();
    
    // Memory alignment helpers
    void* aligned_alloc(size_t size, size_t alignment);
    void aligned_free(void* ptr);
    
    // SIMD data structures
    struct AlignedNode {
        alignas(32) double value;
        alignas(32) int priority;
        alignas(32) uint64_t timestamp;
        alignas(32) uint64_t expiry;
        alignas(32) char symbol[32];
    };
};

/**
 * @brief SIMD-enabled cache with vectorized operations
 */
class SIMDCache {
public:
    explicit SIMDCache(const CacheConfig& config);
    ~SIMDCache();

    // Core operations with SIMD optimization
    bool insert(double value, const std::string& symbol, int priority, double expiry_seconds = 60.0);
    Node* get_highest_priority(const std::string& symbol);
    bool remove(const std::string& symbol, double value);
    
    // Batch operations
    bool insert_batch(const std::vector<std::tuple<double, std::string, int, double>>& items);
    std::vector<Node*> get_batch(const std::vector<std::pair<std::string, double>>& keys);
    
    // SIMD-specific operations
    void optimize_for_simd();
    void vectorize_data_layout();
    double get_simd_performance_improvement() const;

private:
    CacheConfig config_;
    std::unique_ptr<SIMDOperations> simd_ops_;
    std::vector<AlignedNode> aligned_nodes_;
    
    // SIMD optimization
    void align_data_for_simd();
    void reorder_data_for_cache_locality();
};

} // namespace hft_cache 