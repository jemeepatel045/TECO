#pragma once

#include "teco_engine.hpp"
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

class SessionStore {
public:
    virtual ~SessionStore() = default;
    virtual bool exists(const std::string& token) = 0;
    virtual void put(const std::string& token, const std::string& state) = 0;
    virtual std::optional<std::string> get(const std::string& token) = 0;
    virtual bool erase(const std::string& token) = 0;
    virtual std::size_t resident_count() = 0;
    virtual std::optional<std::size_t> ttl_evictions() const = 0;
    virtual const char* backend_name() const = 0;
};

class InMemorySessionStore final : public SessionStore {
    TECOCloudEngine engine_;

public:
    explicit InMemorySessionStore(std::chrono::milliseconds ttl) : engine_(ttl) {}

    bool exists(const std::string& token) override {
        return engine_.has_active_session(token);
    }

    void put(const std::string& token, const std::string& state) override {
        engine_.handle_stateless_event(token, state);
    }

    std::optional<std::string> get(const std::string& token) override {
        return engine_.get_session(token);
    }

    bool erase(const std::string& token) override {
        return engine_.delete_session(token);
    }

    std::size_t resident_count() override {
        return engine_.resident_session_count();
    }

    std::optional<std::size_t> ttl_evictions() const override {
        return engine_.eviction_count();
    }

    const char* backend_name() const override {
        return "memory";
    }
};
