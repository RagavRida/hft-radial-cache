#pragma once

#include <vector>
#include <atomic>
#include <string>
#include <functional>
#include <cstdint>
#include <cmath>

namespace hft_cache {

/**
 * @brief Bloom filter for efficient membership testing
 * 
 * Reduces cache misses by quickly determining if a key might be in the cache
 * before performing expensive lookups.
 */
class BloomFilter {
public:
    explicit BloomFilter(size_t expected_elements, double false_positive_rate = 0.01);
    ~BloomFilter() = default;

    // Core operations
    void add(const std::string& key);
    bool might_contain(const std::string& key) const;
    void clear();
    
    // Statistics
    size_t get_bit_array_size() const;
    size_t get_hash_function_count() const;
    size_t get_added_elements() const;
    double get_current_false_positive_rate() const;
    
    // Configuration
    void resize(size_t new_expected_elements, double new_false_positive_rate = 0.01);
    void optimize_for_workload(size_t actual_elements);

private:
    std::vector<std::atomic<uint64_t>> bit_array_;
    size_t hash_function_count_;
    size_t bit_array_size_;
    std::atomic<size_t> added_elements_{0};
    
    // Hash functions
    uint64_t hash1(const std::string& key) const;
    uint64_t hash2(const std::string& key) const;
    uint64_t hash3(const std::string& key) const;
    uint64_t hash4(const std::string& key) const;
    
    // Helper methods
    void set_bit(size_t index);
    bool get_bit(size_t index) const;
    size_t calculate_optimal_size(size_t elements, double false_positive_rate) const;
    size_t calculate_optimal_hash_count(size_t elements, size_t size) const;
    
    // MurmurHash3 implementation for better distribution
    uint64_t murmur_hash3_64(const std::string& key, uint64_t seed) const;
};

/**
 * @brief Thread-safe Bloom filter with atomic operations
 */
class ThreadSafeBloomFilter : public BloomFilter {
public:
    explicit ThreadSafeBloomFilter(size_t expected_elements, double false_positive_rate = 0.01);
    
    // Thread-safe operations
    void add_thread_safe(const std::string& key);
    bool might_contain_thread_safe(const std::string& key) const;
    
private:
    mutable std::atomic<size_t> concurrent_readers_{0};
    mutable std::atomic<size_t> concurrent_writers_{0};
};

/**
 * @brief Counting Bloom filter for deletion support
 */
class CountingBloomFilter {
public:
    explicit CountingBloomFilter(size_t expected_elements, double false_positive_rate = 0.01);
    ~CountingBloomFilter() = default;

    // Core operations
    void add(const std::string& key);
    bool might_contain(const std::string& key) const;
    bool remove(const std::string& key);
    void clear();
    
    // Statistics
    size_t get_counter_array_size() const;
    size_t get_max_counter_value() const;
    double get_saturation_rate() const;

private:
    std::vector<std::atomic<uint8_t>> counter_array_;
    size_t hash_function_count_;
    size_t counter_array_size_;
    std::atomic<size_t> added_elements_{0};
    
    // Hash functions (same as BloomFilter)
    uint64_t hash1(const std::string& key) const;
    uint64_t hash2(const std::string& key) const;
    uint64_t hash3(const std::string& key) const;
    uint64_t hash4(const std::string& key) const;
    
    // Helper methods
    void increment_counter(size_t index);
    bool decrement_counter(size_t index);
    uint8_t get_counter(size_t index) const;
};

} // namespace hft_cache 