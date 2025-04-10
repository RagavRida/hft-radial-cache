# HFT Radial-Circular Cache

A sub-100ns latency caching system designed for High-Frequency Trading (HFT) applications, featuring a lock-free hash table and priority heap.

## Features
- **Lock-Free Design**: Non-blocking operations using CAS for extreme low latency.
- **NUMA Awareness**: Optimized memory allocation for multi-socket systems (Linux only).
- **Batch Operations**: Efficient multi-insert and multi-retrieve for high throughput.
- **Priority Handling**: Built-in support for order book-style prioritization.
- **Expiry Management**: Automatic cleanup of expired nodes.

## Performance
- **Latency**: ~20-50 ns insert, ~10-30 ns retrieve (local high-end hardware).
- **Throughput**: Scales to 10-20M ops/sec on 8 cores.
- **P99 Latency**: <100 ns under contention.

## Requirements
- C++17 compiler (e.g., g++ 7+)
- CMake 3.10+
- Optional: `libnuma-dev` for NUMA support (Linux)

## Build Instructions
```bash
mkdir build
cd build
cmake ..
make