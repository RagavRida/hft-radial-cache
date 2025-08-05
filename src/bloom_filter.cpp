#include "bloom_filter.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace hft_cache {

// BloomFilter Implementation
BloomFilter::BloomFilter(size_t expected_elements, double false_positive_rate) {
    bit_array_size_ = calculate_optimal_size(expected_elements, false_positive_rate);
    hash_function_count_ = calculate_optimal_hash_count(expected_elements, bit_array_size_);
    
    // Initialize bit array with atomic uint64_t for thread safety
    bit_array_.resize((bit_array_size_ + 63) / 64, 0);
    
    std::cout << "BloomFilter initialized with " << bit_array_size_ << " bits, "
              << hash_function_count_ << " hash functions for " << expected_elements 
              << " expected elements" << std::endl;
}

void BloomFilter::add(const std::string& key) {
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    
    for (size_t i = 0; i < hash_function_count_; ++i) {
        uint64_t hash = h1 + i * h2;
        size_t index = hash % bit_array_size_;
        set_bit(index);
    }
    
    added_elements_.fetch_add(1);
}

bool BloomFilter::might_contain(const std::string& key) const {
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    
    for (size_t i = 0; i < hash_function_count_; ++i) {
        uint64_t hash = h1 + i * h2;
        size_t index = hash % bit_array_size_;
        if (!get_bit(index)) {
            return false;
        }
    }
    
    return true;
}

void BloomFilter::clear() {
    for (auto& word : bit_array_) {
        word.store(0);
    }
    added_elements_.store(0);
}

size_t BloomFilter::get_bit_array_size() const {
    return bit_array_size_;
}

size_t BloomFilter::get_hash_function_count() const {
    return hash_function_count_;
}

size_t BloomFilter::get_added_elements() const {
    return added_elements_.load();
}

double BloomFilter::get_current_false_positive_rate() const {
    size_t elements = added_elements_.load();
    if (elements == 0) return 0.0;
    
    // Calculate theoretical false positive rate
    double p = 1.0 - std::exp(-static_cast<double>(hash_function_count_ * elements) / bit_array_size_);
    return std::pow(p, hash_function_count_);
}

void BloomFilter::resize(size_t new_expected_elements, double new_false_positive_rate) {
    size_t new_size = calculate_optimal_size(new_expected_elements, new_false_positive_rate);
    size_t new_hash_count = calculate_optimal_hash_count(new_expected_elements, new_size);
    
    // Create new bit array
    std::vector<std::atomic<uint64_t>> new_bit_array((new_size + 63) / 64, 0);
    
    // Copy existing data (this is a simplified approach)
    // In a real implementation, you'd need to rehash all existing elements
    bit_array_ = std::move(new_bit_array);
    bit_array_size_ = new_size;
    hash_function_count_ = new_hash_count;
    
    std::cout << "BloomFilter resized to " << new_size << " bits, " 
              << new_hash_count << " hash functions" << std::endl;
}

void BloomFilter::optimize_for_workload(size_t actual_elements) {
    if (actual_elements > 0) {
        double current_fpr = get_current_false_positive_rate();
        resize(actual_elements, current_fpr);
    }
}

uint64_t BloomFilter::hash1(const std::string& key) const {
    return murmur_hash3_64(key, 0x1234567890ABCDEF);
}

uint64_t BloomFilter::hash2(const std::string& key) const {
    return murmur_hash3_64(key, 0xFEDCBA0987654321);
}

uint64_t BloomFilter::hash3(const std::string& key) const {
    return murmur_hash3_64(key, 0xDEADBEEFCAFEBABE);
}

uint64_t BloomFilter::hash4(const std::string& key) const {
    return murmur_hash3_64(key, 0xBABECAFEDEADBEEF);
}

void BloomFilter::set_bit(size_t index) {
    size_t word_index = index / 64;
    size_t bit_index = index % 64;
    uint64_t mask = 1ULL << bit_index;
    
    uint64_t old_value = bit_array_[word_index].load();
    uint64_t new_value;
    
    do {
        new_value = old_value | mask;
    } while (!bit_array_[word_index].compare_exchange_weak(old_value, new_value));
}

bool BloomFilter::get_bit(size_t index) const {
    size_t word_index = index / 64;
    size_t bit_index = index % 64;
    uint64_t mask = 1ULL << bit_index;
    
    return (bit_array_[word_index].load() & mask) != 0;
}

size_t BloomFilter::calculate_optimal_size(size_t elements, double false_positive_rate) const {
    // m = -n * ln(p) / (ln(2)^2)
    // where m is the number of bits, n is the number of elements, p is the false positive rate
    double ln2_squared = std::log(2.0) * std::log(2.0);
    return static_cast<size_t>(-static_cast<double>(elements) * std::log(false_positive_rate) / ln2_squared);
}

size_t BloomFilter::calculate_optimal_hash_count(size_t elements, size_t size) const {
    // k = m/n * ln(2)
    // where k is the number of hash functions, m is the number of bits, n is the number of elements
    return static_cast<size_t>(static_cast<double>(size) / elements * std::log(2.0));
}

uint64_t BloomFilter::murmur_hash3_64(const std::string& key, uint64_t seed) const {
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t* data = reinterpret_cast<const uint64_t*>(key.data());
    const size_t nblocks = key.length() / 16;
    
    // Body
    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k1 = data[i * 2];
        uint64_t k2 = data[i * 2 + 1];
        
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> 33);
        k1 *= c2;
        h1 ^= k1;
        
        h1 = (h1 << 27) | (h1 >> 37);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> 31);
        k2 *= c1;
        h2 ^= k2;
        
        h2 = (h2 << 31) | (h2 >> 33);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }
    
    // Tail
    const uint8_t* tail = reinterpret_cast<const uint8_t*>(data + nblocks * 2);
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch (key.length() & 15) {
        case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48; [[fallthrough]];
        case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40; [[fallthrough]];
        case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32; [[fallthrough]];
        case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24; [[fallthrough]];
        case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16; [[fallthrough]];
        case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8; [[fallthrough]];
        case 9:  k2 ^= static_cast<uint64_t>(tail[8]) << 0;
                 k2 *= c2;
                 k2 = (k2 << 33) | (k2 >> 31);
                 k2 *= c1;
                 h2 ^= k2;
                 [[fallthrough]];
        case 8:  k1 ^= static_cast<uint64_t>(tail[7]) << 56; [[fallthrough]];
        case 7:  k1 ^= static_cast<uint64_t>(tail[6]) << 48; [[fallthrough]];
        case 6:  k1 ^= static_cast<uint64_t>(tail[5]) << 40; [[fallthrough]];
        case 5:  k1 ^= static_cast<uint64_t>(tail[4]) << 32; [[fallthrough]];
        case 4:  k1 ^= static_cast<uint64_t>(tail[3]) << 24; [[fallthrough]];
        case 3:  k1 ^= static_cast<uint64_t>(tail[2]) << 16; [[fallthrough]];
        case 2:  k1 ^= static_cast<uint64_t>(tail[1]) << 8; [[fallthrough]];
        case 1:  k1 ^= static_cast<uint64_t>(tail[0]) << 0;
                 k1 *= c1;
                 k1 = (k1 << 31) | (k1 >> 33);
                 k1 *= c2;
                 h1 ^= k1;
    }
    
    // Finalization
    h1 ^= key.length();
    h2 ^= key.length();
    
    h1 += h2;
    h2 += h1;
    
    h1 = fmix64(h1);
    h2 = fmix64(h2);
    
    h1 += h2;
    h2 += h1;
    
    return h1;
}

uint64_t BloomFilter::fmix64(uint64_t k) const {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

// ThreadSafeBloomFilter Implementation
ThreadSafeBloomFilter::ThreadSafeBloomFilter(size_t expected_elements, double false_positive_rate)
    : BloomFilter(expected_elements, false_positive_rate) {
}

void ThreadSafeBloomFilter::add_thread_safe(const std::string& key) {
    concurrent_writers_.fetch_add(1);
    add(key);
    concurrent_writers_.fetch_sub(1);
}

bool ThreadSafeBloomFilter::might_contain_thread_safe(const std::string& key) const {
    concurrent_readers_.fetch_add(1);
    bool result = might_contain(key);
    concurrent_readers_.fetch_sub(1);
    return result;
}

// CountingBloomFilter Implementation
CountingBloomFilter::CountingBloomFilter(size_t expected_elements, double false_positive_rate) {
    counter_array_size_ = calculate_optimal_size(expected_elements, false_positive_rate);
    hash_function_count_ = calculate_optimal_hash_count(expected_elements, counter_array_size_);
    
    // Initialize counter array
    counter_array_.resize(counter_array_size_, 0);
    
    std::cout << "CountingBloomFilter initialized with " << counter_array_size_ << " counters, "
              << hash_function_count_ << " hash functions for " << expected_elements 
              << " expected elements" << std::endl;
}

void CountingBloomFilter::add(const std::string& key) {
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    
    for (size_t i = 0; i < hash_function_count_; ++i) {
        uint64_t hash = h1 + i * h2;
        size_t index = hash % counter_array_size_;
        increment_counter(index);
    }
    
    added_elements_.fetch_add(1);
}

bool CountingBloomFilter::might_contain(const std::string& key) const {
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    
    for (size_t i = 0; i < hash_function_count_; ++i) {
        uint64_t hash = h1 + i * h2;
        size_t index = hash % counter_array_size_;
        if (get_counter(index) == 0) {
            return false;
        }
    }
    
    return true;
}

bool CountingBloomFilter::remove(const std::string& key) {
    uint64_t h1 = hash1(key);
    uint64_t h2 = hash2(key);
    
    for (size_t i = 0; i < hash_function_count_; ++i) {
        uint64_t hash = h1 + i * h2;
        size_t index = hash % counter_array_size_;
        if (!decrement_counter(index)) {
            return false;
        }
    }
    
    added_elements_.fetch_sub(1);
    return true;
}

void CountingBloomFilter::clear() {
    for (auto& counter : counter_array_) {
        counter.store(0);
    }
    added_elements_.store(0);
}

size_t CountingBloomFilter::get_counter_array_size() const {
    return counter_array_size_;
}

size_t CountingBloomFilter::get_max_counter_value() const {
    uint8_t max_value = 0;
    for (const auto& counter : counter_array_) {
        uint8_t value = counter.load();
        if (value > max_value) {
            max_value = value;
        }
    }
    return max_value;
}

double CountingBloomFilter::get_saturation_rate() const {
    size_t saturated_counters = 0;
    for (const auto& counter : counter_array_) {
        if (counter.load() >= 255) { // Assuming 8-bit counters
            saturated_counters++;
        }
    }
    return static_cast<double>(saturated_counters) / counter_array_size_;
}

uint64_t CountingBloomFilter::hash1(const std::string& key) const {
    return murmur_hash3_64(key, 0x1234567890ABCDEF);
}

uint64_t CountingBloomFilter::hash2(const std::string& key) const {
    return murmur_hash3_64(key, 0xFEDCBA0987654321);
}

uint64_t CountingBloomFilter::hash3(const std::string& key) const {
    return murmur_hash3_64(key, 0xDEADBEEFCAFEBABE);
}

uint64_t CountingBloomFilter::hash4(const std::string& key) const {
    return murmur_hash3_64(key, 0xBABECAFEDEADBEEF);
}

void CountingBloomFilter::increment_counter(size_t index) {
    uint8_t old_value = counter_array_[index].load();
    uint8_t new_value;
    
    do {
        new_value = (old_value < 255) ? old_value + 1 : 255;
    } while (!counter_array_[index].compare_exchange_weak(old_value, new_value));
}

bool CountingBloomFilter::decrement_counter(size_t index) {
    uint8_t old_value = counter_array_[index].load();
    uint8_t new_value;
    
    do {
        if (old_value == 0) {
            return false;
        }
        new_value = old_value - 1;
    } while (!counter_array_[index].compare_exchange_weak(old_value, new_value));
    
    return true;
}

uint8_t CountingBloomFilter::get_counter(size_t index) const {
    return counter_array_[index].load();
}

uint64_t CountingBloomFilter::murmur_hash3_64(const std::string& key, uint64_t seed) const {
    // Same implementation as BloomFilter
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t* data = reinterpret_cast<const uint64_t*>(key.data());
    const size_t nblocks = key.length() / 16;
    
    // Body
    for (size_t i = 0; i < nblocks; i++) {
        uint64_t k1 = data[i * 2];
        uint64_t k2 = data[i * 2 + 1];
        
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> 33);
        k1 *= c2;
        h1 ^= k1;
        
        h1 = (h1 << 27) | (h1 >> 37);
        h1 += h2;
        h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> 31);
        k2 *= c1;
        h2 ^= k2;
        
        h2 = (h2 << 31) | (h2 >> 33);
        h2 += h1;
        h2 = h2 * 5 + 0x38495ab5;
    }
    
    // Tail
    const uint8_t* tail = reinterpret_cast<const uint8_t*>(data + nblocks * 2);
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch (key.length() & 15) {
        case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48; [[fallthrough]];
        case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40; [[fallthrough]];
        case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32; [[fallthrough]];
        case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24; [[fallthrough]];
        case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16; [[fallthrough]];
        case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8; [[fallthrough]];
        case 9:  k2 ^= static_cast<uint64_t>(tail[8]) << 0;
                 k2 *= c2;
                 k2 = (k2 << 33) | (k2 >> 31);
                 k2 *= c1;
                 h2 ^= k2;
                 [[fallthrough]];
        case 8:  k1 ^= static_cast<uint64_t>(tail[7]) << 56; [[fallthrough]];
        case 7:  k1 ^= static_cast<uint64_t>(tail[6]) << 48; [[fallthrough]];
        case 6:  k1 ^= static_cast<uint64_t>(tail[5]) << 40; [[fallthrough]];
        case 5:  k1 ^= static_cast<uint64_t>(tail[4]) << 32; [[fallthrough]];
        case 4:  k1 ^= static_cast<uint64_t>(tail[3]) << 24; [[fallthrough]];
        case 3:  k1 ^= static_cast<uint64_t>(tail[2]) << 16; [[fallthrough]];
        case 2:  k1 ^= static_cast<uint64_t>(tail[1]) << 8; [[fallthrough]];
        case 1:  k1 ^= static_cast<uint64_t>(tail[0]) << 0;
                 k1 *= c1;
                 k1 = (k1 << 31) | (k1 >> 33);
                 k1 *= c2;
                 h1 ^= k1;
    }
    
    // Finalization
    h1 ^= key.length();
    h2 ^= key.length();
    
    h1 += h2;
    h2 += h1;
    
    h1 = fmix64(h1);
    h2 = fmix64(h2);
    
    h1 += h2;
    h2 += h1;
    
    return h1;
}

uint64_t CountingBloomFilter::fmix64(uint64_t k) const {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

size_t CountingBloomFilter::calculate_optimal_size(size_t elements, double false_positive_rate) const {
    double ln2_squared = std::log(2.0) * std::log(2.0);
    return static_cast<size_t>(-static_cast<double>(elements) * std::log(false_positive_rate) / ln2_squared);
}

size_t CountingBloomFilter::calculate_optimal_hash_count(size_t elements, size_t size) const {
    return static_cast<size_t>(static_cast<double>(size) / elements * std::log(2.0));
}

} // namespace hft_cache 