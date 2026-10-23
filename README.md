# TECO: Thread-Less Ephemeral Context Orchestration

C++17 prototype of an in-process, event-driven session state engine with sliding TTL eviction.

## Features
- Token-keyed ephemeral state
- Sliding TTL on updates
- Lazy expiry on lookup and periodic eviction sweep every 64 events
- Session and eviction counters
- CMake build, CTest unit tests, GitHub Actions CI
- Illustrative benchmark against sequential create/join OS threads

## Build
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/teco
```

## Limitations
This is an in-process single-threaded engine, not a distributed mesh, persistent storage, production serverless deployment, or true concurrent thread-pool comparison. Idle sessions are removed on subsequent events or lookup, not by a background timer. Benchmark numbers do not establish serverless superiority.
