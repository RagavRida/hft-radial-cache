#include <gtest/gtest.h>
#include "multi_level_cache.hpp"
#include "bloom_filter.hpp"
#include "skip_list.hpp"
#include "b_tree.hpp"
#include "simd_operations.hpp"
#include "advanced_memory_pool.hpp"
#include "config.hpp"
#include <thread>
#include <chrono>
#include <random>

namespace hft_cache {
namespace test {

class EnhancementTest : public ::testing::Test {
protected:
    CacheConfig config_;
    
    void SetUp() override {
        config_.max_nodes = 10000;
        config_.cleanup_interval_ms = 100;
        config_.enable_memory_pool = true;
        config_.enable_metrics = true;
        config_.l1_capacity = 1000;
        config_.l2_capacity = 5000;
        config_.l3_capacity = 10000;
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// Multi-Level Cache Tests
TEST_F(EnhancementTest, MultiLevelCacheBasicOperations) {
    MultiLevelCache cache(config_);
    
    // Test insert
    EXPECT_TRUE(cache.insert(150.75, "AAPL", 1, 60.0));
    EXPECT_TRUE(cache.insert(151.25, "AAPL", 2, 60.0));
    EXPECT_TRUE(cache.insert(152.00, "GOOGL", 1, 60.0));
    
    // Test retrieve
    Node* result = cache.get_highest_priority("AAPL");
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->priority, 2);
    EXPECT_EQ(result->value, 151.25);
    
    // Test remove
    EXPECT_TRUE(cache.remove("AAPL", 150.75));
    result = cache.get_highest_priority("AAPL");
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->value, 151.25);
}

TEST_F(EnhancementTest, MultiLevelCacheLevelPromotion) {
    MultiLevelCache cache(config_);
    
    // Insert many items to trigger level management
    for (int i = 0; i < 2000; ++i) {
        cache.insert(100.0 + i, "TEST", i % 10, 60.0);
    }
    
    // Check statistics
    auto l1_stats = cache.get_l1_stats();
    auto l2_stats = cache.get_l2_stats();
    auto l3_stats = cache.get_l3_stats();
    
    EXPECT_GT(l1_stats.item_count.load(), 0);
    EXPECT_GT(l2_stats.item_count.load(), 0);
    EXPECT_GT(l3_stats.item_count.load(), 0);
}

TEST_F(EnhancementTest, MultiLevelCacheConcurrentAccess) {
    MultiLevelCache cache(config_);
    const int num_threads = 4;
    const int operations_per_thread = 1000;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                std::string symbol = "SYMBOL" + std::to_string(t);
                double value = 100.0 + i;
                int priority = i % 10;
                
                if (cache.insert(value, symbol, priority, 60.0)) {
                    success_count.fetch_add(1);
                }
                
                Node* result = cache.get_highest_priority(symbol);
                if (result) {
                    // Verify data integrity
                    EXPECT_EQ(result->symbol, symbol);
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_GT(success_count.load(), 0);
}

// Bloom Filter Tests
TEST_F(EnhancementTest, BloomFilterBasicOperations) {
    BloomFilter filter(1000, 0.01);
    
    // Test add and might_contain
    filter.add("AAPL");
    filter.add("GOOGL");
    filter.add("MSFT");
    
    EXPECT_TRUE(filter.might_contain("AAPL"));
    EXPECT_TRUE(filter.might_contain("GOOGL"));
    EXPECT_TRUE(filter.might_contain("MSFT"));
    EXPECT_FALSE(filter.might_contain("INVALID"));
    
    // Test statistics
    EXPECT_EQ(filter.get_added_elements(), 3);
    EXPECT_GT(filter.get_bit_array_size(), 0);
    EXPECT_GT(filter.get_hash_function_count(), 0);
}

TEST_F(EnhancementTest, BloomFilterFalsePositiveRate) {
    BloomFilter filter(100, 0.01);
    
    // Add elements
    for (int i = 0; i < 50; ++i) {
        filter.add("KEY" + std::to_string(i));
    }
    
    // Test false positive rate
    int false_positives = 0;
    int total_tests = 1000;
    
    for (int i = 0; i < total_tests; ++i) {
        std::string test_key = "TEST" + std::to_string(i);
        if (filter.might_contain(test_key)) {
            false_positives++;
        }
    }
    
    double actual_fpr = static_cast<double>(false_positives) / total_tests;
    EXPECT_LT(actual_fpr, 0.05); // Should be less than 5%
}

TEST_F(EnhancementTest, ThreadSafeBloomFilter) {
    ThreadSafeBloomFilter filter(1000, 0.01);
    const int num_threads = 4;
    const int operations_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < operations_per_thread; ++i) {
                std::string key = "THREAD" + std::to_string(t) + "_KEY" + std::to_string(i);
                filter.add_thread_safe(key);
                EXPECT_TRUE(filter.might_contain_thread_safe(key));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(filter.get_added_elements(), num_threads * operations_per_thread);
}

TEST_F(EnhancementTest, CountingBloomFilter) {
    CountingBloomFilter filter(1000, 0.01);
    
    // Test add and remove
    filter.add("AAPL");
    filter.add("AAPL"); // Duplicate
    EXPECT_TRUE(filter.might_contain("AAPL"));
    
    EXPECT_TRUE(filter.remove("AAPL"));
    EXPECT_TRUE(filter.might_contain("AAPL")); // Still there after first remove
    
    EXPECT_TRUE(filter.remove("AAPL"));
    EXPECT_FALSE(filter.might_contain("AAPL")); // Gone after second remove
}

// Skip List Tests
TEST_F(EnhancementTest, SkipListBasicOperations) {
    LockFreeSkipList skip_list(config_);
    
    // Create test nodes
    Node* node1 = new Node();
    node1->value = 150.75;
    node1->symbol = "AAPL";
    node1->priority = 1;
    
    Node* node2 = new Node();
    node2->value = 151.25;
    node2->symbol = "AAPL";
    node2->priority = 2;
    
    // Test insert
    EXPECT_TRUE(skip_list.insert(node1));
    EXPECT_TRUE(skip_list.insert(node2));
    
    // Test find
    Node* result = skip_list.find("AAPL", 150.75);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->value, 150.75);
    
    // Test get_highest_priority
    result = skip_list.get_highest_priority("AAPL");
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->priority, 2);
    
    // Test remove
    EXPECT_TRUE(skip_list.remove("AAPL", 150.75));
    result = skip_list.find("AAPL", 150.75);
    EXPECT_EQ(result, nullptr);
}

TEST_F(EnhancementTest, SkipListRangeQueries) {
    LockFreeSkipList skip_list(config_);
    
    // Insert test data
    for (int i = 0; i < 100; ++i) {
        Node* node = new Node();
        node->value = 100.0 + i;
        node->symbol = "TEST";
        node->priority = i % 10;
        node->timestamp = i;
        skip_list.insert(node);
    }
    
    // Test range queries
    auto range_results = skip_list.get_range("TEST", 120.0, 130.0);
    EXPECT_EQ(range_results.size(), 11); // 120.0 to 130.0 inclusive
    
    auto priority_results = skip_list.get_by_priority_range("TEST", 5, 9);
    EXPECT_GT(priority_results.size(), 0);
    
    auto timestamp_results = skip_list.get_by_timestamp_range("TEST", 50, 70);
    EXPECT_EQ(timestamp_results.size(), 21); // 50 to 70 inclusive
}

TEST_F(EnhancementTest, SkipListStatistics) {
    LockFreeSkipList skip_list(config_);
    
    // Insert data
    for (int i = 0; i < 1000; ++i) {
        Node* node = new Node();
        node->value = 100.0 + i;
        node->symbol = "TEST";
        node->priority = i % 10;
        skip_list.insert(node);
    }
    
    EXPECT_EQ(skip_list.size(), 1000);
    EXPECT_GT(skip_list.get_max_level(), 0);
    EXPECT_GT(skip_list.get_average_level(), 0.0);
}

// B-Tree Tests
TEST_F(EnhancementTest, BTreeBasicOperations) {
    LockFreeBTree b_tree(config_);
    
    // Create test nodes
    Node* node1 = new Node();
    node1->value = 150.75;
    node1->symbol = "AAPL";
    node1->priority = 1;
    
    Node* node2 = new Node();
    node2->value = 151.25;
    node2->symbol = "AAPL";
    node2->priority = 2;
    
    // Test insert
    EXPECT_TRUE(b_tree.insert(node1));
    EXPECT_TRUE(b_tree.insert(node2));
    
    // Test find
    Node* result = b_tree.find("AAPL", 150.75);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->value, 150.75);
    
    // Test remove
    EXPECT_TRUE(b_tree.remove("AAPL", 150.75));
    result = b_tree.find("AAPL", 150.75);
    EXPECT_EQ(result, nullptr);
}

TEST_F(EnhancementTest, BTreeRangeQueries) {
    LockFreeBTree b_tree(config_);
    
    // Insert test data
    for (int i = 0; i < 100; ++i) {
        Node* node = new Node();
        node->value = 100.0 + i;
        node->symbol = "TEST";
        node->priority = i % 10;
        node->timestamp = i;
        b_tree.insert(node);
    }
    
    // Test range queries
    auto range_results = b_tree.get_range("TEST", 120.0, 130.0);
    EXPECT_EQ(range_results.size(), 11);
    
    auto priority_results = b_tree.get_by_priority_range("TEST", 5, 9);
    EXPECT_GT(priority_results.size(), 0);
    
    auto timestamp_results = b_tree.get_by_timestamp_range("TEST", 50, 70);
    EXPECT_EQ(timestamp_results.size(), 21);
}

TEST_F(EnhancementTest, BTreeSortedOperations) {
    LockFreeBTree b_tree(config_);
    
    // Insert test data
    for (int i = 0; i < 50; ++i) {
        Node* node = new Node();
        node->value = 200.0 - i; // Decreasing values
        node->symbol = "TEST";
        node->priority = i % 10;
        node->timestamp = i;
        b_tree.insert(node);
    }
    
    // Test sorted operations
    auto sorted_by_value = b_tree.get_sorted_by_value("TEST");
    EXPECT_EQ(sorted_by_value.size(), 50);
    EXPECT_LT(sorted_by_value[0]->value, sorted_by_value[1]->value);
    
    auto sorted_by_priority = b_tree.get_sorted_by_priority("TEST");
    EXPECT_EQ(sorted_by_priority.size(), 50);
    EXPECT_GT(sorted_by_priority[0]->priority, sorted_by_priority[1]->priority);
}

// Advanced Memory Pool Tests
TEST_F(EnhancementTest, AdvancedMemoryPoolBasicOperations) {
    AdvancedMemoryPool pool(config_);
    
    // Test allocation
    Node* node1 = pool.allocate_node();
    EXPECT_NE(node1, nullptr);
    
    Node* node2 = pool.allocate_node();
    EXPECT_NE(node2, nullptr);
    EXPECT_NE(node1, node2);
    
    // Test deallocation
    pool.deallocate_node(node1);
    pool.deallocate_node(node2);
    
    // Test statistics
    EXPECT_GT(pool.get_total_allocated(), 0);
    EXPECT_GT(pool.get_total_deallocated(), 0);
}

TEST_F(EnhancementTest, AdvancedMemoryPoolAlignedAllocation) {
    AdvancedMemoryPool pool(config_);
    
    // Test aligned allocation
    void* aligned_ptr = pool.allocate_aligned(1024, 64);
    EXPECT_NE(aligned_ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned_ptr) % 64, 0);
    
    pool.deallocate_aligned(aligned_ptr);
}

TEST_F(EnhancementTest, AdvancedMemoryPoolDefragmentation) {
    AdvancedMemoryPool pool(config_);
    
    // Allocate many nodes
    std::vector<Node*> nodes;
    for (int i = 0; i < 1000; ++i) {
        nodes.push_back(pool.allocate_node());
    }
    
    // Deallocate some randomly
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(nodes.begin(), nodes.end(), gen);
    
    for (int i = 0; i < 500; ++i) {
        pool.deallocate_node(nodes[i]);
    }
    
    // Test defragmentation
    double fragmentation_before = pool.get_fragmentation_ratio();
    pool.defragment();
    double fragmentation_after = pool.get_fragmentation_ratio();
    
    EXPECT_LE(fragmentation_after, fragmentation_before);
}

TEST_F(EnhancementTest, LockFreeMemoryPool) {
    LockFreeMemoryPool pool(config_);
    
    // Test lock-free allocation
    Node* node1 = pool.allocate_node_lock_free();
    EXPECT_NE(node1, nullptr);
    
    Node* node2 = pool.allocate_node_lock_free();
    EXPECT_NE(node2, nullptr);
    
    // Test deallocation
    pool.deallocate_node_lock_free(node1);
    pool.deallocate_node_lock_free(node2);
    
    // Test statistics
    EXPECT_GT(pool.get_allocated_nodes(), 0);
    EXPECT_FALSE(pool.is_empty());
}

TEST_F(EnhancementTest, HierarchicalMemoryPool) {
    HierarchicalMemoryPool pool(config_);
    
    // Test hierarchical allocation
    Node* fast_node = pool.allocate_node_fast();
    EXPECT_NE(fast_node, nullptr);
    
    Node* standard_node = pool.allocate_node_standard();
    EXPECT_NE(standard_node, nullptr);
    
    Node* slow_node = pool.allocate_node_slow();
    EXPECT_NE(slow_node, nullptr);
    
    // Test deallocation
    pool.deallocate_node(fast_node);
    pool.deallocate_node(standard_node);
    pool.deallocate_node(slow_node);
    
    // Test statistics
    auto stats = pool.get_pool_statistics();
    EXPECT_GT(stats.l1_allocations, 0);
    EXPECT_GT(stats.l2_allocations, 0);
    EXPECT_GT(stats.l3_allocations, 0);
}

// Performance Tests
TEST_F(EnhancementTest, MultiLevelCachePerformance) {
    MultiLevelCache cache(config_);
    const int num_operations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        cache.insert(100.0 + i, "PERF", i % 10, 60.0);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = static_cast<double>(num_operations) / duration.count() * 1000000;
    EXPECT_GT(ops_per_second, 100000); // Should handle at least 100k ops/sec
}

TEST_F(EnhancementTest, BloomFilterPerformance) {
    BloomFilter filter(10000, 0.01);
    const int num_operations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        filter.add("PERF" + std::to_string(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = static_cast<double>(num_operations) / duration.count() * 1000000;
    EXPECT_GT(ops_per_second, 1000000); // Should handle at least 1M ops/sec
}

TEST_F(EnhancementTest, SkipListPerformance) {
    LockFreeSkipList skip_list(config_);
    const int num_operations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        Node* node = new Node();
        node->value = 100.0 + i;
        node->symbol = "PERF";
        node->priority = i % 10;
        skip_list.insert(node);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = static_cast<double>(num_operations) / duration.count() * 1000000;
    EXPECT_GT(ops_per_second, 50000); // Should handle at least 50k ops/sec
}

TEST_F(EnhancementTest, BTreePerformance) {
    LockFreeBTree b_tree(config_);
    const int num_operations = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_operations; ++i) {
        Node* node = new Node();
        node->value = 100.0 + i;
        node->symbol = "PERF";
        node->priority = i % 10;
        b_tree.insert(node);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = static_cast<double>(num_operations) / duration.count() * 1000000;
    EXPECT_GT(ops_per_second, 50000); // Should handle at least 50k ops/sec
}

TEST_F(EnhancementTest, MemoryPoolPerformance) {
    AdvancedMemoryPool pool(config_);
    const int num_operations = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<Node*> nodes;
    for (int i = 0; i < num_operations; ++i) {
        nodes.push_back(pool.allocate_node());
    }
    
    for (Node* node : nodes) {
        pool.deallocate_node(node);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double ops_per_second = static_cast<double>(num_operations * 2) / duration.count() * 1000000;
    EXPECT_GT(ops_per_second, 500000); // Should handle at least 500k ops/sec
}

} // namespace test
} // namespace hft_cache 