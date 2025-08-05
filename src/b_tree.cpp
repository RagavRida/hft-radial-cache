#include "b_tree.hpp"
#include "memory_manager.hpp"
#include "metrics.hpp"
#include "error_handler.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace hft_cache {

// LockFreeBTree Implementation
LockFreeBTree::LockFreeBTree(const CacheConfig& config) 
    : config_(config) {
    
    std::cout << "LockFreeBTree initialized with order " << B_TREE_ORDER << std::endl;
}

LockFreeBTree::~LockFreeBTree() {
    cleanup_tree(root_.load());
}

bool LockFreeBTree::insert(Node* node) {
    if (!node) return false;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        BTreeNode* root = root_.load();
        
        if (!root) {
            // Create new root
            root = allocate_node();
            root->keys[0] = node;
            root->key_count.store(1);
            root_.store(root);
            height_.store(1);
            size_.fetch_add(1);
            
            if (g_metrics) {
                g_metrics->record_insert(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return true;
        }
        
        // Find leaf node for insertion
        BTreeNode* leaf = find_leaf(node->symbol, node->value);
        
        if (insert_into_leaf(leaf, node)) {
            size_.fetch_add(1);
            
            if (g_metrics) {
                g_metrics->record_insert(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start_time).count());
            }
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::INSERTION_FAILED, ErrorSeverity::MEDIUM,
                std::string("B-tree insert failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

Node* LockFreeBTree::find(const std::string& symbol, double value) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        BTreeNode* current = root_.load();
        
        while (current) {
            size_t i = 0;
            size_t key_count = current->key_count.load();
            
            // Find position in current node
            while (i < key_count) {
                if (current->keys[i]) {
                    if (current->keys[i]->symbol == symbol && current->keys[i]->value == value) {
                        if (g_metrics) {
                            g_metrics->record_retrieve(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::high_resolution_clock::now() - start_time).count());
                        }
                        return current->keys[i];
                    }
                    
                    if (current->keys[i]->symbol > symbol || 
                        (current->keys[i]->symbol == symbol && current->keys[i]->value > value)) {
                        break;
                    }
                }
                i++;
            }
            
            // Move to child
            if (current->is_leaf.load()) {
                break;
            }
            current = current->children[i].load();
        }
        
        if (g_metrics) {
            g_metrics->record_miss(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - start_time).count());
        }
        
        return nullptr;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::RETRIEVAL_FAILED, ErrorSeverity::MEDIUM,
                std::string("B-tree find failed: ") + e.what(), __FILE__, __LINE__);
        }
        return nullptr;
    }
}

bool LockFreeBTree::remove(const std::string& symbol, double value) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    try {
        BTreeNode* current = root_.load();
        
        while (current) {
            size_t i = 0;
            size_t key_count = current->key_count.load();
            
            // Find position in current node
            while (i < key_count) {
                if (current->keys[i] && current->keys[i]->symbol == symbol && 
                    current->keys[i]->value == value) {
                    
                    // Found the node to remove
                    Node* node_to_remove = current->keys[i];
                    
                    if (current->is_leaf.load()) {
                        // Remove from leaf
                        for (size_t j = i; j < key_count - 1; ++j) {
                            current->keys[j] = current->keys[j + 1];
                        }
                        current->keys[key_count - 1] = nullptr;
                        current->key_count.fetch_sub(1);
                        
                        delete node_to_remove;
                        size_.fetch_sub(1);
                        
                        if (g_metrics) {
                            g_metrics->record_remove(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::high_resolution_clock::now() - start_time).count());
                        }
                        return true;
                    } else {
                        // Handle internal node removal (simplified)
                        // In a full implementation, this would involve finding successor/predecessor
                        return false;
                    }
                }
                
                if (current->keys[i] && (current->keys[i]->symbol > symbol || 
                    (current->keys[i]->symbol == symbol && current->keys[i]->value > value))) {
                    break;
                }
                i++;
            }
            
            // Move to child
            if (current->is_leaf.load()) {
                break;
            }
            current = current->children[i].load();
        }
        
        return false;
        
    } catch (const std::exception& e) {
        if (g_error_handler) {
            g_error_handler->report_error(ErrorType::REMOVAL_FAILED, ErrorSeverity::MEDIUM,
                std::string("B-tree remove failed: ") + e.what(), __FILE__, __LINE__);
        }
        return false;
    }
}

void LockFreeBTree::clear() {
    cleanup_tree(root_.load());
    root_.store(nullptr);
    size_.store(0);
    height_.store(0);
}

std::vector<Node*> LockFreeBTree::get_range(const std::string& symbol, double min_value, double max_value) {
    std::vector<Node*> results;
    
    BTreeNode* current = root_.load();
    if (!current) return results;
    
    // Find starting position
    while (current && !current->is_leaf.load()) {
        size_t i = 0;
        size_t key_count = current->key_count.load();
        
        while (i < key_count && current->keys[i] && 
               (current->keys[i]->symbol < symbol || 
                (current->keys[i]->symbol == symbol && current->keys[i]->value < min_value))) {
            i++;
        }
        current = current->children[i].load();
    }
    
    // Collect results from leaves
    while (current) {
        size_t key_count = current->key_count.load();
        
        for (size_t i = 0; i < key_count; ++i) {
            if (current->keys[i] && current->keys[i]->symbol == symbol && 
                current->keys[i]->value >= min_value && current->keys[i]->value <= max_value) {
                results.push_back(current->keys[i]);
            }
        }
        
        // Move to next leaf (simplified - would need proper leaf linking in full implementation)
        break;
    }
    
    return results;
}

std::vector<Node*> LockFreeBTree::get_by_priority_range(const std::string& symbol, int min_priority, int max_priority) {
    std::vector<Node*> results;
    
    // Traverse all nodes (simplified implementation)
    Iterator it = begin();
    Iterator end_it = end();
    
    while (it != end_it) {
        Node* node = *it;
        if (node && node->symbol == symbol && 
            node->priority >= min_priority && node->priority <= max_priority) {
            results.push_back(node);
        }
        ++it;
    }
    
    return results;
}

std::vector<Node*> LockFreeBTree::get_by_timestamp_range(const std::string& symbol, uint64_t start_time, uint64_t end_time) {
    std::vector<Node*> results;
    
    // Traverse all nodes (simplified implementation)
    Iterator it = begin();
    Iterator end_it = end();
    
    while (it != end_it) {
        Node* node = *it;
        if (node && node->symbol == symbol && 
            node->timestamp >= start_time && node->timestamp <= end_time) {
            results.push_back(node);
        }
        ++it;
    }
    
    return results;
}

std::vector<Node*> LockFreeBTree::get_sorted_by_value(const std::string& symbol) {
    std::vector<Node*> results;
    
    Iterator it = begin();
    Iterator end_it = end();
    
    while (it != end_it) {
        Node* node = *it;
        if (node && node->symbol == symbol) {
            results.push_back(node);
        }
        ++it;
    }
    
    // Sort by value
    std::sort(results.begin(), results.end(), 
              [](const Node* a, const Node* b) { return a->value < b->value; });
    
    return results;
}

std::vector<Node*> LockFreeBTree::get_sorted_by_priority(const std::string& symbol) {
    std::vector<Node*> results;
    
    Iterator it = begin();
    Iterator end_it = end();
    
    while (it != end_it) {
        Node* node = *it;
        if (node && node->symbol == symbol) {
            results.push_back(node);
        }
        ++it;
    }
    
    // Sort by priority (descending)
    std::sort(results.begin(), results.end(), 
              [](const Node* a, const Node* b) { return a->priority > b->priority; });
    
    return results;
}

std::vector<Node*> LockFreeBTree::get_sorted_by_timestamp(const std::string& symbol) {
    std::vector<Node*> results;
    
    Iterator it = begin();
    Iterator end_it = end();
    
    while (it != end_it) {
        Node* node = *it;
        if (node && node->symbol == symbol) {
            results.push_back(node);
        }
        ++it;
    }
    
    // Sort by timestamp
    std::sort(results.begin(), results.end(), 
              [](const Node* a, const Node* b) { return a->timestamp < b->timestamp; });
    
    return results;
}

size_t LockFreeBTree::size() const {
    return size_.load();
}

int LockFreeBTree::get_height() const {
    return height_.load();
}

double LockFreeBTree::get_fill_factor() const {
    size_t total_size = size_.load();
    if (total_size == 0) return 0.0;
    
    // Calculate theoretical capacity
    int height = height_.load();
    size_t capacity = 0;
    for (int i = 0; i < height; ++i) {
        capacity += std::pow(B_TREE_ORDER - 1, i) * (B_TREE_ORDER - 1);
    }
    
    return static_cast<double>(total_size) / capacity;
}

BTreeNode* LockFreeBTree::find_leaf(const std::string& symbol, double value) {
    BTreeNode* current = root_.load();
    
    while (current && !current->is_leaf.load()) {
        size_t i = 0;
        size_t key_count = current->key_count.load();
        
        while (i < key_count && current->keys[i] && 
               (current->keys[i]->symbol < symbol || 
                (current->keys[i]->symbol == symbol && current->keys[i]->value < value))) {
            i++;
        }
        current = current->children[i].load();
    }
    
    return current;
}

bool LockFreeBTree::insert_into_leaf(BTreeNode* leaf, Node* node) {
    if (!leaf) return false;
    
    size_t key_count = leaf->key_count.load();
    
    if (key_count < B_TREE_ORDER - 1) {
        // Simple insertion
        size_t insert_pos = 0;
        while (insert_pos < key_count && leaf->keys[insert_pos] && 
               (leaf->keys[insert_pos]->symbol < node->symbol || 
                (leaf->keys[insert_pos]->symbol == node->symbol && leaf->keys[insert_pos]->value < node->value))) {
            insert_pos++;
        }
        
        // Shift elements
        for (size_t i = key_count; i > insert_pos; --i) {
            leaf->keys[i] = leaf->keys[i - 1];
        }
        
        leaf->keys[insert_pos] = node;
        leaf->key_count.fetch_add(1);
        return true;
    } else {
        // Node is full - would need to split (simplified)
        return false;
    }
}

void LockFreeBTree::split_node(BTreeNode* parent, size_t child_index) {
    // Simplified split implementation
    // In a full implementation, this would split the child node and update parent
}

void LockFreeBTree::merge_nodes(BTreeNode* parent, size_t child_index) {
    // Simplified merge implementation
    // In a full implementation, this would merge child nodes
}

void LockFreeBTree::rebalance_tree(BTreeNode* node) {
    // Simplified rebalancing
    // In a full implementation, this would rebalance the tree after operations
}

BTreeNode* LockFreeBTree::allocate_node() {
    return new BTreeNode();
}

void LockFreeBTree::deallocate_node(BTreeNode* node) {
    delete node;
}

void LockFreeBTree::cleanup_tree(BTreeNode* node) {
    if (!node) return;
    
    if (!node->is_leaf.load()) {
        for (size_t i = 0; i < B_TREE_ORDER; ++i) {
            BTreeNode* child = node->children[i].load();
            if (child) {
                cleanup_tree(child);
            }
        }
    }
    
    // Clean up keys
    for (size_t i = 0; i < B_TREE_ORDER - 1; ++i) {
        if (node->keys[i]) {
            delete node->keys[i];
        }
    }
    
    delete node;
}

bool LockFreeBTree::validate_tree() const {
    // Basic validation
    BTreeNode* root = root_.load();
    if (!root) return true;
    
    return calculate_height(root) == height_.load() && 
           calculate_size(root) == size_.load();
}

void LockFreeBTree::print_tree() const {
    std::cout << "B-Tree (size: " << size_.load() << ", height: " << height_.load() << "):" << std::endl;
    // Simplified printing - would need recursive traversal for full tree display
}

int LockFreeBTree::calculate_height(BTreeNode* node) const {
    if (!node) return 0;
    if (node->is_leaf.load()) return 1;
    
    int max_child_height = 0;
    for (size_t i = 0; i < B_TREE_ORDER; ++i) {
        BTreeNode* child = node->children[i].load();
        if (child) {
            max_child_height = std::max(max_child_height, calculate_height(child));
        }
    }
    
    return 1 + max_child_height;
}

size_t LockFreeBTree::calculate_size(BTreeNode* node) const {
    if (!node) return 0;
    
    size_t count = node->key_count.load();
    
    if (!node->is_leaf.load()) {
        for (size_t i = 0; i < B_TREE_ORDER; ++i) {
            BTreeNode* child = node->children[i].load();
            if (child) {
                count += calculate_size(child);
            }
        }
    }
    
    return count;
}

// Iterator Implementation
LockFreeBTree::Iterator::Iterator(BTreeNode* root) {
    if (root) {
        push_left(root);
    }
}

LockFreeBTree::Iterator& LockFreeBTree::Iterator::operator++() {
    if (stack_.empty()) return *this;
    
    BTreeNode* current = stack_.back();
    size_t current_index = indices_.back();
    
    // Move to next key in current node
    current_index++;
    if (current_index < current->key_count.load()) {
        indices_.back() = current_index;
        return *this;
    }
    
    // Move to next child
    if (!current->is_leaf.load()) {
        BTreeNode* child = current->children[current_index].load();
        if (child) {
            push_left(child);
            return *this;
        }
    }
    
    // Move to next node in stack
    stack_.pop_back();
    indices_.pop_back();
    
    return *this;
}

Node* LockFreeBTree::Iterator::operator*() {
    if (stack_.empty()) return nullptr;
    
    BTreeNode* current = stack_.back();
    size_t current_index = indices_.back();
    
    if (current_index < current->key_count.load()) {
        return current->keys[current_index];
    }
    
    return nullptr;
}

bool LockFreeBTree::Iterator::operator!=(const Iterator& other) const {
    return stack_ != other.stack_ || indices_ != other.indices_;
}

void LockFreeBTree::Iterator::push_left(BTreeNode* node) {
    while (node) {
        stack_.push_back(node);
        indices_.push_back(0);
        
        if (node->is_leaf.load()) break;
        
        node = node->children[0].load();
    }
}

LockFreeBTree::Iterator LockFreeBTree::begin() {
    return Iterator(root_.load());
}

LockFreeBTree::Iterator LockFreeBTree::end() {
    return Iterator(nullptr);
}

// ThreadSafeBTree Implementation
ThreadSafeBTree::ThreadSafeBTree(const CacheConfig& config)
    : LockFreeBTree(config) {
}

bool ThreadSafeBTree::insert_thread_safe(Node* node) {
    concurrent_writers_.fetch_add(1);
    bool result = insert(node);
    concurrent_writers_.fetch_sub(1);
    return result;
}

Node* ThreadSafeBTree::find_thread_safe(const std::string& symbol, double value) {
    concurrent_readers_.fetch_add(1);
    Node* result = find(symbol, value);
    concurrent_readers_.fetch_sub(1);
    return result;
}

bool ThreadSafeBTree::remove_thread_safe(const std::string& symbol, double value) {
    concurrent_writers_.fetch_add(1);
    bool result = remove(symbol, value);
    concurrent_writers_.fetch_sub(1);
    return result;
}

// PooledBTree Implementation
PooledBTree::PooledBTree(const CacheConfig& config)
    : LockFreeBTree(config), pool_size_(config.max_nodes / 10) {
    initialize_pool();
}

PooledBTree::~PooledBTree() {
    cleanup_pool();
}

BTreeNode* PooledBTree::allocate_node_from_pool() {
    if (!node_pool_.empty()) {
        BTreeNode* node = node_pool_.back();
        node_pool_.pop_back();
        return node;
    }
    
    return new BTreeNode();
}

void PooledBTree::deallocate_node_to_pool(BTreeNode* node) {
    if (node_pool_.size() < pool_size_) {
        // Reset node state
        node->key_count.store(0);
        node->is_leaf.store(true);
        for (size_t i = 0; i < B_TREE_ORDER - 1; ++i) {
            node->keys[i] = nullptr;
        }
        for (size_t i = 0; i < B_TREE_ORDER; ++i) {
            node->children[i].store(nullptr);
        }
        node_pool_.push_back(node);
    } else {
        delete node;
    }
}

void PooledBTree::defragment_pool() {
    if (node_pool_.size() > pool_size_ / 2) {
        node_pool_.resize(pool_size_ / 2);
    }
}

void PooledBTree::initialize_pool() {
    node_pool_.reserve(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        node_pool_.push_back(new BTreeNode());
    }
}

void PooledBTree::cleanup_pool() {
    for (BTreeNode* node : node_pool_) {
        delete node;
    }
    node_pool_.clear();
}

// CompressedBTree Implementation
CompressedBTree::CompressedBTree(const CacheConfig& config)
    : LockFreeBTree(config) {
}

void CompressedBTree::compress_node(BTreeNode* node) {
    if (should_compress_node(node)) {
        apply_compression(node);
        compressed_nodes_.fetch_add(1);
    }
    total_nodes_.fetch_add(1);
}

void CompressedBTree::decompress_node(BTreeNode* node) {
    // Simplified decompression
    // In a full implementation, this would decompress the node data
}

double CompressedBTree::get_compression_ratio() const {
    size_t compressed = compressed_nodes_.load();
    size_t total = total_nodes_.load();
    
    return total > 0 ? static_cast<double>(compressed) / total : 0.0;
}

bool CompressedBTree::should_compress_node(BTreeNode* node) const {
    if (!node) return false;
    
    size_t key_count = node->key_count.load();
    return key_count < (B_TREE_ORDER - 1) / 2; // Compress if less than half full
}

void CompressedBTree::apply_compression(BTreeNode* node) {
    // Simplified compression
    // In a full implementation, this would compress the node data
}

} // namespace hft_cache 