#pragma once
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>

struct EphemeralContext {
    std::string token;
    std::string state;
    std::chrono::steady_clock::time_point last_activity;
};

class TECOCloudEngine {
    using Clock = std::chrono::steady_clock;
    std::unordered_map<std::string, EphemeralContext> mesh_;
    std::chrono::milliseconds ttl_;
    std::size_t event_count_ = 0;
    std::size_t evictions_ = 0;
    static constexpr std::size_t sweep_interval_ = 64;

    void sweep(Clock::time_point now) {
        for (auto it = mesh_.begin(); it != mesh_.end();) {
            if (now - it->second.last_activity >= ttl_) {
                it = mesh_.erase(it);
                ++evictions_;
            } else {
                ++it;
            }
        }
    }

public:
    explicit TECOCloudEngine(std::chrono::milliseconds ttl = std::chrono::minutes(5))
        : ttl_(ttl) {
        if (ttl_.count() <= 0) throw std::invalid_argument("TTL must be positive");
    }

    void handle_stateless_event(const std::string& token, const std::string& state) {
        const auto now = Clock::now();
        auto it = mesh_.find(token);
        if (it != mesh_.end() && now - it->second.last_activity >= ttl_) {
            mesh_.erase(it);
            ++evictions_;
            it = mesh_.end();
        }
        if (it == mesh_.end()) {
            mesh_.emplace(token, EphemeralContext{token, state, now});
        } else {
            it->second.state = state;
            it->second.last_activity = now;
        }
        if (++event_count_ >= sweep_interval_) {
            sweep(now);
            event_count_ = 0;
        }
    }

    bool has_active_session(const std::string& token) {
        auto it = mesh_.find(token);
        if (it == mesh_.end()) return false;
        if (Clock::now() - it->second.last_activity >= ttl_) {
            mesh_.erase(it);
            ++evictions_;
            return false;
        }
        return true;
    }

    std::size_t resident_session_count() const { return mesh_.size(); }
    std::size_t eviction_count() const { return evictions_; }
};
