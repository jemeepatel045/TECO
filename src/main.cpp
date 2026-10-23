#include "teco_engine.hpp"
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

int main() {
    constexpr int requests = 10000;
    TECOCloudEngine teco;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < requests; ++i)
        teco.handle_stateless_event("user_" + std::to_string(i % 100), "ACTIVE");
    const auto end = std::chrono::steady_clock::now();

    std::mutex mutex;
    std::unordered_map<std::string, std::string> traditional;
    const auto threaded_start = std::chrono::steady_clock::now();
    for (int i = 0; i < requests; ++i) {
        std::thread worker([&, i] {
            std::lock_guard<std::mutex> guard(mutex);
            traditional["user_" + std::to_string(i % 100)] = "ACTIVE";
        });
        worker.join();
    }
    const auto threaded_end = std::chrono::steady_clock::now();

    const auto teco_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const auto thread_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(threaded_end - threaded_start).count();
    std::cout << "TECO event loop (ns): " << teco_ns << "\n"
              << "Thread creation + join per request (ns): " << thread_ns << "\n"
              << "Resident TECO sessions: " << teco.resident_session_count() << "\n";
    std::cout << "Note: this is a sequential thread-creation baseline, NOT a thread pool comparison.\n";
}
