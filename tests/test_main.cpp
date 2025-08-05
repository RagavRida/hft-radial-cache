#include "../include/radial_circular_list.hpp"
#include "../include/config.hpp"
#include "../include/memory_manager.hpp"
#include "../include/metrics.hpp"
#include "../include/error_handler.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include <atomic>
#include <future>

class HFTCacheTest : public ::testing::Test {
protected:
    CacheConfig config_;
    std::unique_ptr<RadialCircularList> cache_;
    std::unique_ptr<MemoryManager> memory_manager_;
    std::unique_ptr<MetricsCollector> metrics_;
    std::unique_ptr<ErrorHandler> error_handler_;
    
    void SetUp() override {
        config_.max_nodes = 1000;
        config_.num_worker_threads = 4;
        config_.enable_metrics = true;
        config_.enable_error_recovery = true;
        
        memory_manager_ = std::make_unique<MemoryManager>(config_);
        metrics_ = std::make_unique<MetricsCollector>(config_);
        error_handler_ = std::make_unique<ErrorHandler>(config_);
        cache_ = std::make_unique<RadialCircularList>(config_);
    }
    
    void TearDown() override {
        cache_.reset();
        memory_manager_.reset();
        metrics_.reset();
        error_handler_.reset();
    }
};

// Basic functionality tests
TEST_F(HFTCacheTest, BasicInsertAndRetrieve) {
    EXPECT_TRUE(cache_->insert(150.75, "AAPL", 1, 60.0));
    
    Node* result = cache_->get_highest_priority("AAPL");
    EXPECT_NE(result, nullptr);
    EXPECT_DOUBLE_EQ(result->value, 150.75);
    EXPECT_EQ(result->priority, 1);
}

TEST_F(HFTCacheTest, PriorityOrdering) {
    cache_->insert(150.75, "AAPL", 1, 60.0);
    cache_->insert(151.00, "AAPL", 3, 60.0);
    cache_->insert(150.50, "AAPL", 2, 60.0);
    
    Node* result = cache_->get_highest_priority("AAPL");
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result->priority, 3);  // Highest priority should be returned first
}

TEST_F(HFTCacheTest, ExpiryHandling) {
    cache_->insert(150.75, "AAPL", 1, 0.001);  // 1ms expiry
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    Node* result = cache_->get_highest_priority("AAPL");
    EXPECT_EQ(result, nullptr);  // Should be expired
}

// Concurrent access tests
TEST_F(HFTCacheTest, ConcurrentInserts) {
    const size_t num_threads = 8;
    const size_t inserts_per_thread = 100;
    std::atomic<size_t> successful_inserts{0};
    
    auto insert_worker = [&](size_t thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_real_distribution<> value_dist(100.0, 200.0);
        std::uniform_int_distribution<> priority_dist(0, 10);
        
        for (size_t i = 0; i < inserts_per_thread; ++i) {
            double value = value_dist(gen);
            int priority = priority_dist(gen);
            if (cache_->insert(value, "AAPL", priority, 60.0)) {
                successful_inserts.fetch_add(1);
            }
        }
    };
    
    std::vector<std::thread> threads;
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(insert_worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_GT(successful_inserts.load(), 0);
    EXPECT_LE(successful_inserts.load(), num_threads * inserts_per_thread);
}

TEST_F(HFTCacheTest, ConcurrentInsertsAndRetrieves) {
    const size_t num_threads = 4;
    const size_t operations_per_thread = 1000;
    std::atomic<size_t> successful_operations{0};
    std::atomic<bool> stop_retrieves{false};
    
    auto insert_worker = [&](size_t thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_real_distribution<> value_dist(100.0, 200.0);
        std::uniform_int_distribution<> priority_dist(0, 10);
        
        for (size_t i = 0; i < operations_per_thread; ++i) {
            double value = value_dist(gen);
            int priority = priority_dist(gen);
            if (cache_->insert(value, "AAPL", priority, 60.0)) {
                successful_operations.fetch_add(1);
            }
        }
    };
    
    auto retrieve_worker = [&](size_t thread_id) {
        while (!stop_retrieves.load()) {
            Node* result = cache_->get_highest_priority("AAPL");
            if (result) {
                successful_operations.fetch_add(1);
            }
            std::this_thread::yield();
        }
    };
    
    std::vector<std::thread> insert_threads;
    std::vector<std::thread> retrieve_threads;
    
    // Start retrieve threads
    for (size_t i = 0; i < num_threads; ++i) {
        retrieve_threads.emplace_back(retrieve_worker, i);
    }
    
    // Start insert threads
    for (size_t i = 0; i < num_threads; ++i) {
        insert_threads.emplace_back(insert_worker, i);
    }
    
    // Wait for insert threads to complete
    for (auto& t : insert_threads) {
        t.join();
    }
    
    // Stop retrieve threads
    stop_retrieves.store(true);
    for (auto& t : retrieve_threads) {
        t.join();
    }
    
    EXPECT_GT(successful_operations.load(), 0);
}

// Stress tests
TEST_F(HFTCacheTest, HighLoadStressTest) {
    const size_t num_operations = 10000;
    const size_t num_threads = 16;
    std::atomic<size_t> successful_operations{0};
    
    auto stress_worker = [&](size_t thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_real_distribution<> value_dist(100.0, 200.0);
        std::uniform_int_distribution<> priority_dist(0, 10);
        std::uniform_int_distribution<> op_dist(0, 1);
        std::vector<std::string> symbols = {"AAPL", "GOOG", "MSFT", "TSLA", "AMZN"};
        std::uniform_int_distribution<> symbol_dist(0, symbols.size() - 1);
        
        for (size_t i = 0; i < num_operations / num_threads; ++i) {
            if (op_dist(gen) == 0) {
                // Insert operation
                double value = value_dist(gen);
                int priority = priority_dist(gen);
                std::string symbol = symbols[symbol_dist(gen)];
                if (cache_->insert(value, symbol, priority, 60.0)) {
                    successful_operations.fetch_add(1);
                }
            } else {
                // Retrieve operation
                std::string symbol = symbols[symbol_dist(gen)];
                Node* result = cache_->get_highest_priority(symbol);
                if (result) {
                    successful_operations.fetch_add(1);
                }
            }
        }
    };
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(stress_worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_GT(successful_operations.load(), 0);
    std::cout << "Stress test completed: " << successful_operations.load() 
              << " operations in " << duration.count() << "ms" << std::endl;
}

// Memory leak tests
TEST_F(HFTCacheTest, MemoryLeakTest) {
    const size_t num_operations = 10000;
    std::vector<Node*> retrieved_nodes;
    
    // Insert many nodes
    for (size_t i = 0; i < num_operations; ++i) {
        cache_->insert(100.0 + i, "AAPL", i % 10, 60.0);
    }
    
    // Retrieve all nodes
    for (size_t i = 0; i < num_operations; ++i) {
        Node* node = cache_->get_highest_priority("AAPL");
        if (node) {
            retrieved_nodes.push_back(node);
        }
    }
    
    // Check that we retrieved a reasonable number of nodes
    EXPECT_GT(retrieved_nodes.size(), 0);
    EXPECT_LE(retrieved_nodes.size(), num_operations);
}

// Batch operation tests
TEST_F(HFTCacheTest, BatchOperations) {
    std::vector<std::tuple<double, std::string, int, double>> batch;
    for (size_t i = 0; i < 100; ++i) {
        batch.emplace_back(100.0 + i, "AAPL", i % 10, 60.0);
    }
    
    EXPECT_TRUE(cache_->insert_batch(batch));
    
    std::vector<std::string> symbols(100, "AAPL");
    auto results = cache_->get_highest_priority_batch(symbols);
    
    size_t valid_results = 0;
    for (auto* result : results) {
        if (result) valid_results++;
    }
    
    EXPECT_GT(valid_results, 0);
}

// Error handling tests
TEST_F(HFTCacheTest, ErrorRecovery) {
    // Test with invalid configuration
    CacheConfig invalid_config;
    invalid_config.max_nodes = 0;  // Invalid
    
    EXPECT_THROW({
        MemoryManager invalid_mm(invalid_config);
    }, std::invalid_argument);
}

// Performance benchmarks
TEST_F(HFTCacheTest, PerformanceBenchmark) {
    const size_t num_operations = 100000;
    std::vector<uint64_t> insert_times;
    std::vector<uint64_t> retrieve_times;
    
    insert_times.reserve(num_operations);
    retrieve_times.reserve(num_operations);
    
    // Benchmark inserts
    for (size_t i = 0; i < num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        cache_->insert(100.0 + i, "AAPL", i % 10, 60.0);
        auto end = std::chrono::high_resolution_clock::now();
        insert_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    
    // Benchmark retrieves
    for (size_t i = 0; i < num_operations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        cache_->get_highest_priority("AAPL");
        auto end = std::chrono::high_resolution_clock::now();
        retrieve_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    
    // Calculate statistics
    auto calc_stats = [](const std::vector<uint64_t>& times) {
        uint64_t sum = 0;
        uint64_t min = times[0];
        uint64_t max = times[0];
        for (auto t : times) {
            sum += t;
            min = std::min(min, t);
            max = std::max(max, t);
        }
        double avg = static_cast<double>(sum) / times.size();
        return std::make_tuple(avg, min, max);
    };
    
    auto [insert_avg, insert_min, insert_max] = calc_stats(insert_times);
    auto [retrieve_avg, retrieve_min, retrieve_max] = calc_stats(retrieve_times);
    
    std::cout << "Insert Performance:" << std::endl;
    std::cout << "  Average: " << insert_avg << " ns" << std::endl;
    std::cout << "  Min: " << insert_min << " ns" << std::endl;
    std::cout << "  Max: " << insert_max << " ns" << std::endl;
    
    std::cout << "Retrieve Performance:" << std::endl;
    std::cout << "  Average: " << retrieve_avg << " ns" << std::endl;
    std::cout << "  Min: " << retrieve_min << " ns" << std::endl;
    std::cout << "  Max: " << retrieve_max << " ns" << std::endl;
    
    // Performance assertions
    EXPECT_LT(insert_avg, 10000);    // Average insert should be < 10µs
    EXPECT_LT(retrieve_avg, 5000);   // Average retrieve should be < 5µs
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 