#pragma once

#include "session_store.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace teco_redis_detail {

struct RespValue {
    enum class Type { Simple, Error, Integer, Bulk, Null, Array };
    Type type = Type::Null;
    std::string text;
    long long integer = 0;
    std::vector<RespValue> array;
};

inline void send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) throw std::runtime_error("Redis send failed");
        sent += static_cast<std::size_t>(n);
    }
}

inline std::string read_line(int fd) {
    std::string line;
    char c = 0;
    for (;;) {
        const ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) throw std::runtime_error("Redis connection closed");
        if (c == '\r') {
            char lf = 0;
            if (::recv(fd, &lf, 1, 0) != 1 || lf != '\n')
                throw std::runtime_error("Malformed Redis response");
            return line;
        }
        line.push_back(c);
        if (line.size() > 1024 * 1024)
            throw std::runtime_error("Redis response line too large");
    }
}

inline std::string read_exact(int fd, std::size_t len) {
    std::string out(len, '\0');
    std::size_t have = 0;
    while (have < len) {
        const ssize_t n = ::recv(fd, out.data() + have, len - have, 0);
        if (n <= 0) throw std::runtime_error("Redis connection closed");
        have += static_cast<std::size_t>(n);
    }
    return out;
}

inline RespValue read_resp(int fd) {
    char prefix = 0;
    if (::recv(fd, &prefix, 1, 0) != 1)
        throw std::runtime_error("Redis did not return a response");

    RespValue out;
    if (prefix == '+') {
        out.type = RespValue::Type::Simple;
        out.text = read_line(fd);
        return out;
    }
    if (prefix == '-') {
        out.type = RespValue::Type::Error;
        out.text = read_line(fd);
        return out;
    }
    if (prefix == ':') {
        out.type = RespValue::Type::Integer;
        out.integer = std::stoll(read_line(fd));
        return out;
    }
    if (prefix == '$') {
        const long long len = std::stoll(read_line(fd));
        if (len == -1) {
            out.type = RespValue::Type::Null;
            return out;
        }
        if (len < 0 || len > 4 * 1024 * 1024)
            throw std::runtime_error("Invalid Redis bulk length");
        out.type = RespValue::Type::Bulk;
        out.text = read_exact(fd, static_cast<std::size_t>(len));
        if (read_exact(fd, 2) != "\r\n")
            throw std::runtime_error("Malformed Redis bulk response");
        return out;
    }
    if (prefix == '*') {
        const long long count = std::stoll(read_line(fd));
        if (count == -1) {
            out.type = RespValue::Type::Null;
            return out;
        }
        if (count < 0 || count > 100000)
            throw std::runtime_error("Invalid Redis array length");
        out.type = RespValue::Type::Array;
        out.array.reserve(static_cast<std::size_t>(count));
        for (long long i = 0; i < count; ++i)
            out.array.push_back(read_resp(fd));
        return out;
    }
    throw std::runtime_error("Unsupported Redis response type");
}

inline std::string encode_command(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& arg : args) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}

class RedisConnection {
    std::string host_;
    int port_;

public:
    RedisConnection(std::string host, int port)
        : host_(std::move(host)), port_(port) {}

    RespValue command(const std::vector<std::string>& args) const {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;
        const std::string port_text = std::to_string(port_);
        const int rc = ::getaddrinfo(host_.c_str(), port_text.c_str(), &hints, &result);
        if (rc != 0)
            throw std::runtime_error(std::string("Redis DNS error: ") + gai_strerror(rc));

        int fd = -1;
        for (addrinfo* p = result; p; p = p->ai_next) {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;

            timeval timeout{3, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(result);

        if (fd < 0)
            throw std::runtime_error("Could not connect to Redis at " + host_ + ":" + std::to_string(port_));

        try {
            send_all(fd, encode_command(args));
            RespValue reply = read_resp(fd);
            ::close(fd);
            if (reply.type == RespValue::Type::Error)
                throw std::runtime_error("Redis error: " + reply.text);
            return reply;
        } catch (...) {
            ::close(fd);
            throw;
        }
    }
};

} // namespace teco_redis_detail

class RedisSessionStore final : public SessionStore {
    teco_redis_detail::RedisConnection redis_;
    long long ttl_ms_;
    std::string prefix_;

    std::string key(const std::string& token) const {
        return prefix_ + token;
    }

public:
    RedisSessionStore(std::string host, int port, long long ttl_ms, std::string prefix)
        : redis_(std::move(host), port), ttl_ms_(ttl_ms), prefix_(std::move(prefix)) {
        if (ttl_ms_ <= 0) throw std::invalid_argument("Redis TTL must be positive");
        const auto pong = redis_.command({"PING"});
        if (pong.type != teco_redis_detail::RespValue::Type::Simple || pong.text != "PONG")
            throw std::runtime_error("Redis PING failed");
    }

    bool exists(const std::string& token) override {
        const auto r = redis_.command({"EXISTS", key(token)});
        return r.type == teco_redis_detail::RespValue::Type::Integer && r.integer > 0;
    }

    void put(const std::string& token, const std::string& state) override {
        const auto r = redis_.command({"SET", key(token), state, "PX", std::to_string(ttl_ms_)});
        if (r.type != teco_redis_detail::RespValue::Type::Simple || r.text != "OK")
            throw std::runtime_error("Redis SET failed");
    }

    std::optional<std::string> get(const std::string& token) override {
        const auto r = redis_.command({"GET", key(token)});
        if (r.type == teco_redis_detail::RespValue::Type::Null) return std::nullopt;
        if (r.type != teco_redis_detail::RespValue::Type::Bulk)
            throw std::runtime_error("Unexpected Redis GET response");
        return r.text;
    }

    bool erase(const std::string& token) override {
        const auto r = redis_.command({"DEL", key(token)});
        return r.type == teco_redis_detail::RespValue::Type::Integer && r.integer > 0;
    }

    std::size_t resident_count() override {
        const auto r = redis_.command({"KEYS", prefix_ + "*"});
        if (r.type != teco_redis_detail::RespValue::Type::Array)
            throw std::runtime_error("Unexpected Redis KEYS response");
        return r.array.size();
    }

    std::optional<std::size_t> ttl_evictions() const override {
        // Redis performs TTL expiration internally. This prototype does not enable
        // Redis keyspace notifications, so cross-instance TTL eviction events are
        // not counted by TECO itself.
        return std::nullopt;
    }

    const char* backend_name() const override {
        return "redis";
    }
};
