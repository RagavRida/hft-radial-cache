#ifndef ADVANCED_OPERATIONS_HPP
#define ADVANCED_OPERATIONS_HPP

#include "radial_circular_list.hpp"
#include "config.hpp"
#include <functional>
#include <vector>
#include <algorithm>
#include <numeric>
#include <regex>
#include <unordered_map>

// Range query operations
class RangeOperations {
private:
    RadialCircularList& cache_;
    
public:
    explicit RangeOperations(RadialCircularList& cache) : cache_(cache) {}
    
    // Get nodes within value range
    std::vector<Node*> get_range(const std::string& symbol, double min_value, double max_value) {
        std::vector<Node*> results;
        // Implementation would iterate through all nodes for the symbol
        // and filter by value range
        return results;
    }
    
    // Get nodes within priority range
    std::vector<Node*> get_by_priority_range(const std::string& symbol, int min_priority, int max_priority) {
        std::vector<Node*> results;
        // Implementation would filter by priority range
        return results;
    }
    
    // Get nodes within timestamp range
    std::vector<Node*> get_by_timestamp_range(const std::string& symbol, uint64_t start_time, uint64_t end_time) {
        std::vector<Node*> results;
        // Implementation would filter by timestamp range
        return results;
    }
    
    // Get top N nodes by priority
    std::vector<Node*> get_top_n(const std::string& symbol, size_t n) {
        std::vector<Node*> results;
        // Implementation would get top N priority nodes
        return results;
    }
    
    // Get nodes with custom predicate
    std::vector<Node*> get_by_predicate(const std::string& symbol, 
                                       std::function<bool(const Node*)> predicate) {
        std::vector<Node*> results;
        // Implementation would apply custom predicate
        return results;
    }
};

// Aggregation operations
class AggregationOperations {
private:
    RadialCircularList& cache_;
    
public:
    explicit AggregationOperations(RadialCircularList& cache) : cache_(cache) {}
    
    // Calculate average value for a symbol
    double get_average_value(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        if (nodes.empty()) return 0.0;
        
        double sum = std::accumulate(nodes.begin(), nodes.end(), 0.0,
            [](double acc, const Node* node) { return acc + node->value; });
        return sum / nodes.size();
    }
    
    // Calculate median value for a symbol
    double get_median_value(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        if (nodes.empty()) return 0.0;
        
        std::vector<double> values;
        values.reserve(nodes.size());
        for (const auto* node : nodes) {
            values.push_back(node->value);
        }
        
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        if (n % 2 == 0) {
            return (values[n/2 - 1] + values[n/2]) / 2.0;
        } else {
            return values[n/2];
        }
    }
    
    // Calculate standard deviation
    double get_std_deviation(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        if (nodes.size() < 2) return 0.0;
        
        double mean = get_average_value(symbol);
        double variance = 0.0;
        
        for (const auto* node : nodes) {
            double diff = node->value - mean;
            variance += diff * diff;
        }
        variance /= (nodes.size() - 1);
        
        return std::sqrt(variance);
    }
    
    // Get min and max values
    std::pair<double, double> get_min_max(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        if (nodes.empty()) return {0.0, 0.0};
        
        auto [min_it, max_it] = std::minmax_element(nodes.begin(), nodes.end(),
            [](const Node* a, const Node* b) { return a->value < b->value; });
        
        return {(*min_it)->value, (*max_it)->value};
    }
    
    // Get count of nodes for a symbol
    size_t get_count(const std::string& symbol) {
        return get_all_nodes(symbol).size();
    }
    
    // Get sum of values for a symbol
    double get_sum(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        return std::accumulate(nodes.begin(), nodes.end(), 0.0,
            [](double acc, const Node* node) { return acc + node->value; });
    }
    
    // Get weighted average by priority
    double get_weighted_average(const std::string& symbol) {
        auto nodes = get_all_nodes(symbol);
        if (nodes.empty()) return 0.0;
        
        double weighted_sum = 0.0;
        double total_weight = 0.0;
        
        for (const auto* node : nodes) {
            double weight = static_cast<double>(node->priority + 1); // Avoid zero weight
            weighted_sum += node->value * weight;
            total_weight += weight;
        }
        
        return total_weight > 0 ? weighted_sum / total_weight : 0.0;
    }

private:
    std::vector<Node*> get_all_nodes(const std::string& symbol) {
        std::vector<Node*> nodes;
        // Implementation would collect all nodes for the symbol
        // This is a placeholder - actual implementation would depend on cache structure
        return nodes;
    }
};

// Pattern matching and search operations
class SearchOperations {
private:
    RadialCircularList& cache_;
    
public:
    explicit SearchOperations(RadialCircularList& cache) : cache_(cache) {}
    
    // Search by regex pattern in symbol names
    std::vector<Node*> search_by_pattern(const std::string& pattern) {
        std::vector<Node*> results;
        std::regex regex_pattern(pattern);
        
        // Implementation would iterate through all symbols and match against pattern
        // This is a placeholder
        return results;
    }
    
    // Fuzzy search for symbols
    std::vector<Node*> fuzzy_search(const std::string& query, double threshold = 0.8) {
        std::vector<Node*> results;
        
        // Implementation would use string similarity algorithms
        // like Levenshtein distance or Jaro-Winkler
        return results;
    }
    
    // Search by custom predicate
    std::vector<Node*> search_by_predicate(std::function<bool(const Node*)> predicate) {
        std::vector<Node*> results;
        
        // Implementation would apply predicate to all nodes
        return results;
    }
    
    // Search for nodes with similar values
    std::vector<Node*> search_similar_values(double target_value, double tolerance) {
        return search_by_predicate([target_value, tolerance](const Node* node) {
            return std::abs(node->value - target_value) <= tolerance;
        });
    }
    
    // Search for nodes with high priority
    std::vector<Node*> search_high_priority(int min_priority) {
        return search_by_predicate([min_priority](const Node* node) {
            return node->priority >= min_priority;
        });
    }
    
    // Search for recent nodes
    std::vector<Node*> search_recent(uint64_t max_age_ns) {
        uint64_t current_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        return search_by_predicate([current_time, max_age_ns](const Node* node) {
            return (current_time - node->timestamp_ns) <= max_age_ns;
        });
    }
};

// Advanced cache operations combining multiple features
class AdvancedCacheOperations {
private:
    RadialCircularList& cache_;
    RangeOperations range_ops_;
    AggregationOperations agg_ops_;
    SearchOperations search_ops_;
    
public:
    explicit AdvancedCacheOperations(RadialCircularList& cache) 
        : cache_(cache), range_ops_(cache), agg_ops_(cache), search_ops_(cache) {}
    
    // Get statistical summary for a symbol
    struct SymbolSummary {
        size_t count;
        double average;
        double median;
        double std_deviation;
        std::pair<double, double> min_max;
        double weighted_average;
    };
    
    SymbolSummary get_symbol_summary(const std::string& symbol) {
        return {
            agg_ops_.get_count(symbol),
            agg_ops_.get_average_value(symbol),
            agg_ops_.get_median_value(symbol),
            agg_ops_.get_std_deviation(symbol),
            agg_ops_.get_min_max(symbol),
            agg_ops_.get_weighted_average(symbol)
        };
    }
    
    // Get top symbols by activity
    std::vector<std::pair<std::string, size_t>> get_top_symbols_by_activity(size_t limit = 10) {
        std::vector<std::pair<std::string, size_t>> results;
        // Implementation would track symbol activity and return top ones
        return results;
    }
    
    // Get symbols with high volatility
    std::vector<std::pair<std::string, double>> get_volatile_symbols(double threshold = 0.1) {
        std::vector<std::pair<std::string, double>> results;
        // Implementation would calculate volatility and return symbols above threshold
        return results;
    }
    
    // Get correlation between symbols
    double get_correlation(const std::string& symbol1, const std::string& symbol2) {
        // Implementation would calculate correlation coefficient
        return 0.0;
    }
    
    // Get market depth for a symbol
    struct MarketDepth {
        std::vector<std::pair<double, size_t>> bids;  // price, quantity
        std::vector<std::pair<double, size_t>> asks;  // price, quantity
    };
    
    MarketDepth get_market_depth(const std::string& symbol, size_t levels = 10) {
        MarketDepth depth;
        // Implementation would aggregate orders by price level
        return depth;
    }
    
    // Get time-weighted average price (TWAP)
    double get_twap(const std::string& symbol, uint64_t window_ns) {
        // Implementation would calculate TWAP over the specified window
        return 0.0;
    }
    
    // Get volume-weighted average price (VWAP)
    double get_vwap(const std::string& symbol, uint64_t window_ns) {
        // Implementation would calculate VWAP over the specified window
        return 0.0;
    }
    
    // Get access to individual operation classes
    RangeOperations& range_operations() { return range_ops_; }
    AggregationOperations& aggregation_operations() { return agg_ops_; }
    SearchOperations& search_operations() { return search_ops_; }
};

#endif 