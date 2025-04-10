#ifndef LOCKFREE_MAP_HPP
#define LOCKFREE_MAP_HPP

#include "midpoint.hpp"
#include <atomic>
#include <array>
#include <string>

class LockFreeHashTable {
private:
    struct BucketNode {
        std::string key;
        MidpointNode* value;
        std::atomic<BucketNode*> next;
        BucketNode(const std::string& k, size_t cap) : key(k), value(new MidpointNode(cap)), next(nullptr) {}
    };
    static constexpr size_t BUCKETS = 64;
    std::array<std::atomic<BucketNode*>, BUCKETS> buckets;

    size_t hash(const std::string& key) const {
        constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;
        constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
        uint64_t h = FNV_OFFSET;
        for (char c : key) { h ^= static_cast<uint64_t>(c); h *= FNV_PRIME; }
        return h % BUCKETS;
    }

public:
    LockFreeHashTable(size_t capacity_per_bucket) {
        for (auto& bucket : buckets) bucket.store(nullptr, std::memory_order_relaxed);
    }

    ~LockFreeHashTable() {
        for (auto& bucket : buckets) {
            BucketNode* node = bucket.load(std::memory_order_relaxed);
            while (node) {
                BucketNode* next = node->next.load(std::memory_order_relaxed);
                delete node->value;
                delete node;
                node = next;
            }
        }
    }

    MidpointNode* get_or_create(const std::string& key, size_t capacity) {
        size_t index = hash(key);
        BucketNode* head = buckets[index].load(std::memory_order_acquire);
        while (head) {
            if (head->key == key) return head->value;
            head = head->next.load(std::memory_order_acquire);
        }
        BucketNode* new_node = new BucketNode(key, capacity);
        while (true) {
            head = buckets[index].load(std::memory_order_acquire);
            new_node->next.store(head, std::memory_order_relaxed);
            if (buckets[index].compare_exchange_weak(head, new_node, std::memory_order_acq_rel)) {
                return new_node->value;
            }
            std::this_thread::yield();
        }
    }

    MidpointNode* get(const std::string& key) const {
        size_t index = hash(key);
        BucketNode* head = buckets[index].load(std::memory_order_acquire);
        while (head) {
            if (head->key == key) return head->value;
            head = head->next.load(std::memory_order_acquire);
        }
        return nullptr;
    }
};

#endif