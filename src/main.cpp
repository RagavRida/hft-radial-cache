#include "radial_circular_list.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <thread>
#include <algorithm>

void benchmark(RadialCircularList& cache, size_t num_operations = 1000) {
    std::vector<std::string> midpoints = {"AAPL", "GOOG", "MSFT"};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> value_dist(100.0, 200.0);
    std::uniform_int_distribution<> priority_dist(0, 10);
    std::uniform_int_distribution<> midpoint_dist(0, 2);

    std::vector<uint64_t> insert_times(num_operations);
    std::vector<uint64_t> retrieve_times(num_operations);
    std::vector<uint64_t> batch_insert_times(num_operations / 10);
    std::vector<uint64_t> batch_retrieve_times(num_operations / 10);

    auto insert_task = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            double value = value_dist(gen);
            std::string midpoint = midpoints[midpoint_dist(gen)];
            int priority = priority_dist(gen);
            auto start_time = std::chrono::high_resolution_clock::now();
            cache.insert(value, midpoint, priority, 1.0);
            auto end_time = std::chrono::high_resolution_clock::now();
            insert_times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        }
    };

    auto retrieve_task = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            std::string midpoint = midpoints[midpoint_dist(gen)];
            auto start_time = std::chrono::high_resolution_clock::now();
            Node* node = cache.get_highest_priority(midpoint);
            auto end_time = std::chrono::high_resolution_clock::now();
            retrieve_times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
            if (node && i % 100 == 0) {
                std::cout << "Retrieved: midpoint=" << midpoint << ", value=" << node->value << ", priority=" << node->priority << "\n";
            }
        }
    };

    auto batch_insert_task = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            std::vector<std::tuple<double, std::string, int, double>> batch(10);
            for (auto& item : batch) {
                item = {value_dist(gen), midpoints[midpoint_dist(gen)], priority_dist(gen), 1.0};
            }
            auto start_time = std::chrono::high_resolution_clock::now();
            cache.insert_batch(batch);
            auto end_time = std::chrono::high_resolution_clock::now();
            batch_insert_times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        }
    };

    auto batch_retrieve_task = [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            std::vector<std::string> batch(10, midpoints[midpoint_dist(gen)]);
            auto start_time = std::chrono::high_resolution_clock::now();
            cache.get_highest_priority_batch(batch);
            auto end_time = std::chrono::high_resolution_clock::now();
            batch_retrieve_times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
        }
    };

    size_t num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    size_t ops_per_thread = num_operations / num_threads;
    size_t batch_ops_per_thread = (num_operations / 10) / num_threads;

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * ops_per_thread;
        size_t end = (t == num_threads - 1) ? num_operations : start + ops_per_thread;
        threads.emplace_back(insert_task, start, end);
    }
    for (auto& t : threads) t.join();
    threads.clear();

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * ops_per_thread;
        size_t end = (t == num_threads - 1) ? num_operations : start + ops_per_thread;
        threads.emplace_back(retrieve_task, start, end);
    }
    for (auto& t : threads) t.join();
    threads.clear();

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * batch_ops_per_thread;
        size_t end = (t == num_threads - 1) ? (num_operations / 10) : start + batch_ops_per_thread;
        threads.emplace_back(batch_insert_task, start, end);
    }
    for (auto& t : threads) t.join();
    threads.clear();

    for (size_t t = 0; t < num_threads; ++t) {
        size_t start = t * batch_ops_per_thread;
        size_t end = (t == num_threads - 1) ? (num_operations / 10) : start + batch_ops_per_thread;
        threads.emplace_back(batch_retrieve_task, start, end);
    }
    for (auto& t : threads) t.join();

    auto calc_stats = [](const std::vector<uint64_t>& times) {
        uint64_t sum = 0;
        uint64_t min = times[0];
        uint64_t max = times[0];
        std::vector<uint64_t> sorted(times);
        std::sort(sorted.begin(), sorted.end());
        for (auto t : times) {
            sum += t;
            min = std::min(min, t);
            max = std::max(max, t);
        }
        double avg = static_cast<double>(sum) / times.size();
        double p99 = sorted[static_cast<size_t>(times.size() * 0.99)];
        return std::make_tuple(avg, min, max, p99);
    };

    auto [insert_avg, insert_min, insert_max, insert_p99] = calc_stats(insert_times);
    auto [retrieve_avg, retrieve_min, retrieve_max, retrieve_p99] = calc_stats(retrieve_times);
    auto [batch_insert_avg, batch_insert_min, batch_insert_max, batch_insert_p99] = calc_stats(batch_insert_times);
    auto [batch_retrieve_avg, batch_retrieve_min, batch_retrieve_max, batch_retrieve_p99] = calc_stats(batch_retrieve_times);

    std::cout << "\nBenchmark Results (" << num_operations << " operations, " << num_threads << " threads):\n";
    std::cout << "Single Insertions:\n";
    std::cout << "  Average: " << insert_avg << " ns (" << insert_avg / 1000.0 << " µs)\n";
    std::cout << "  Min: " << insert_min << " ns (" << insert_min / 1000.0 << " µs)\n";
    std::cout << "  Max: " << insert_max << " ns (" << insert_max / 1000.0 << " µs)\n";
    std::cout << "  P99: " << insert_p99 << " ns (" << insert_p99 / 1000.0 << " µs)\n";
    std::cout << "Single Retrievals:\n";
    std::cout << "  Average: " << retrieve_avg << " ns (" << retrieve_avg / 1000.0 << " µs)\n";
    std::cout << "  Min: " << retrieve_min << " ns (" << retrieve_min / 1000.0 << " µs)\n";
    std::cout << "  Max: " << retrieve_max << " ns (" << retrieve_max / 1000.0 << " µs)\n";
    std::cout << "  P99: " << retrieve_p99 << " ns (" << retrieve_p99 / 1000.0 << " µs)\n";
    std::cout << "Batch Insertions (10 ops/batch):\n";
    std::cout << "  Average: " << batch_insert_avg << " ns (" << batch_insert_avg / 1000.0 << " µs)\n";
    std::cout << "  Min: " << batch_insert_min << " ns (" << batch_insert_min / 1000.0 << " µs)\n";
    std::cout << "  Max: " << batch_insert_max << " ns (" << batch_insert_max / 1000.0 << " µs)\n";
    std::cout << "  P99: " << batch_insert_p99 << " ns (" << batch_insert_p99 / 1000.0 << " µs)\n";
    std::cout << "Batch Retrievals (10 ops/batch):\n";
    std::cout << "  Average: " << batch_retrieve_avg << " ns (" << batch_retrieve_avg / 1000.0 << " µs)\n";
    std::cout << "  Min: " << batch_retrieve_min << " ns (" << batch_retrieve_min / 1000.0 << " µs)\n";
    std::cout << "  Max: " << batch_retrieve_max << " ns (" << batch_retrieve_max / 1000.0 << " µs)\n";
    std::cout << "  P99: " << batch_retrieve_p99 << " ns (" << batch_retrieve_p99 / 1000.0 << " µs)\n";
}

int main() {
    RadialCircularList cache(1000);
    benchmark(cache, 1000);
    return 0;
}