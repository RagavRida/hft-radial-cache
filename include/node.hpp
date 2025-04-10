#ifndef NODE_HPP
#define NODE_HPP

#include <chrono>

class Node {
public:
    double value;
    int priority;
    uint64_t timestamp_ns;
    uint64_t expiry_time_ns;

    Node(double val = 0.0, int prio = 0, double expiry = 60.0)
        : value(val), priority(prio),
          timestamp_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::high_resolution_clock::now().time_since_epoch()).count()),
          expiry_time_ns(static_cast<uint64_t>(expiry * 1'000'000'000)) {}

    bool is_expired() const {
        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        return (now_ns - timestamp_ns) > expiry_time_ns;
    }
};

#endif