#include "teco_engine.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    bool rejected = false;
    try { TECOCloudEngine invalid(0ms); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);

    TECOCloudEngine engine(100ms);
    engine.handle_stateless_event("a", "FIRST");
    assert(engine.has_active_session("a"));
    assert(engine.resident_session_count() == 1);
    std::this_thread::sleep_for(40ms);
    engine.handle_stateless_event("a", "SECOND");
    std::this_thread::sleep_for(40ms);
    assert(engine.has_active_session("a"));
    std::this_thread::sleep_for(80ms);
    assert(!engine.has_active_session("a"));
    assert(engine.resident_session_count() == 0);
    assert(engine.eviction_count() == 1);

    engine.handle_stateless_event("old", "ACTIVE");
    std::this_thread::sleep_for(120ms);
    for (int i = 0; i < 64; ++i)
        engine.handle_stateless_event("fresh", "ACTIVE");
    assert(!engine.has_active_session("old"));
    assert(engine.has_active_session("fresh"));
    std::cout << "All TECO TTL tests passed.\n";
}
