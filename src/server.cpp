// TECO local HTTP demo. Single-threaded, loopback-only, one request per connection.
// Supports either the original in-memory session backend or a Redis-backed shared backend.
// POSIX sockets: builds on macOS and Linux. NOT an internet-facing production server.
#include "redis_store.hpp"
#include "session_store.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
constexpr std::size_t MAX_REQUEST = 16384;

std::string escape_json(const std::string& s) {
    std::string out = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += digits[c >> 4];
                    out += digits[c & 15];
                } else out += static_cast<char>(c);
        }
    }
    return out + '"';
}

class JsonStringObjectParser {
    const std::string& s;
    std::size_t i = 0;
    void whitespace() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool take(char c) { whitespace(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    unsigned codepoint() {
        if (i + 4 > s.size()) throw std::runtime_error("bad unicode escape");
        unsigned n = 0;
        for (int t = 0; t < 4; ++t) {
            int x = hex(s[i++]);
            if (x < 0) throw std::runtime_error("bad unicode escape");
            n = (n << 4) | static_cast<unsigned>(x);
        }
        return n;
    }
    static void append_utf8(std::string& out, unsigned cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (cp & 63)));
        }
    }
    std::string quoted() {
        if (!take('"')) throw std::runtime_error("expected JSON string");
        std::string out;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return out;
            if (c < 0x20) throw std::runtime_error("invalid JSON control character");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (i == s.size()) throw std::runtime_error("unfinished JSON escape");
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = codepoint();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (i + 2 > s.size() || s[i] != '\\' || s[i+1] != 'u') throw std::runtime_error("invalid surrogate pair");
                        i += 2;
                        unsigned lo = codepoint();
                        if (lo < 0xdc00 || lo > 0xdfff) throw std::runtime_error("invalid surrogate pair");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("invalid surrogate pair");
                    append_utf8(out, cp);
                    break;
                }
                default: throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }
public:
    explicit JsonStringObjectParser(const std::string& src) : s(src) {}
    std::map<std::string, std::string> parse() {
        std::map<std::string, std::string> out;
        if (!take('{')) throw std::runtime_error("expected JSON object");
        if (!take('}')) {
            do {
                std::string key = quoted();
                if (!take(':')) throw std::runtime_error("expected colon");
                std::string val = quoted();
                if (!out.emplace(key, val).second) throw std::runtime_error("duplicate JSON key");
                if (take('}')) break;
                if (!take(',')) throw std::runtime_error("expected comma");
            } while (true);
        }
        whitespace();
        if (i != s.size()) throw std::runtime_error("trailing JSON data");
        return out;
    }
};

bool valid_token(const std::string& token) {
    if (token.empty() || token.size() > 64) return false;
    for (unsigned char c : token)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

struct Request { std::string method, path, body; };

bool read_request(int fd, Request& req, int& error_status) {
    std::string data;
    char buf[4096];
    std::size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() >= MAX_REQUEST) { error_status = 413; return false; }
        ssize_t n = recv(fd, buf, std::min(sizeof(buf), MAX_REQUEST - data.size()), 0);
        if (n <= 0) { error_status = 400; return false; }
        data.append(buf, static_cast<std::size_t>(n));
    }
    if (header_end > 8192) { error_status = 431; return false; }
    auto line_end = data.find("\r\n");
    if (line_end == std::string::npos || line_end > 2048) { error_status = 400; return false; }
    std::string line = data.substr(0, line_end);
    auto first = line.find(' ');
    auto second = first == std::string::npos ? first : line.find(' ', first + 1);
    if (first == std::string::npos || second == std::string::npos || line.substr(second + 1) != "HTTP/1.1") {
        error_status = 400; return false;
    }
    req.method = line.substr(0, first);
    req.path = line.substr(first + 1, second - first - 1);
    if (req.path.empty() || req.path[0] != '/') { error_status = 400; return false; }

    std::size_t content_length = 0;
    bool has_length = false;
    std::string content_type;
    for (std::size_t pos = line_end + 2; pos < header_end;) {
        auto next = data.find("\r\n", pos);
        if (next == std::string::npos || next > header_end) { error_status = 400; return false; }
        std::string header = data.substr(pos, next - pos);
        auto colon = header.find(':');
        if (colon == std::string::npos) { error_status = 400; return false; }
        std::string name = header.substr(0, colon);
        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::string value = header.substr(colon + 1);
        auto left = value.find_first_not_of(" \t");
        value = left == std::string::npos ? "" : value.substr(left);
        if (name == "content-length") {
            if (has_length || value.empty()) { error_status = 400; return false; }
            has_length = true;
            try {
                std::size_t consumed = 0;
                auto v = std::stoull(value, &consumed);
                if (consumed != value.size() || v > MAX_REQUEST) { error_status = 413; return false; }
                content_length = static_cast<std::size_t>(v);
            } catch (...) { error_status = 400; return false; }
        } else if (name == "transfer-encoding") {
            error_status = 501; return false;
        } else if (name == "content-type") content_type = value;
        pos = next + 2;
    }

    if (req.method == "POST" && !has_length) { error_status = 411; return false; }
    if (req.method == "POST" && content_type.find("application/json") != 0) { error_status = 415; return false; }

    std::size_t body_start = header_end + 4;
    if (body_start + content_length > MAX_REQUEST) { error_status = 413; return false; }
    while (data.size() - body_start < content_length) {
        ssize_t n = recv(fd, buf, std::min(sizeof(buf), content_length - (data.size() - body_start)), 0);
        if (n <= 0) { error_status = 400; return false; }
        data.append(buf, static_cast<std::size_t>(n));
    }
    req.body = data.substr(body_start, content_length);
    return true;
}

std::string message_for(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Internal Server Error";
    }
}

void respond(int fd, int status, std::string body = "{}") {
    if (status == 204) body.clear();
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + message_for(status) + "\r\n"
       "Content-Type: application/json; charset=utf-8\r\n"
       "Cache-Control: no-store\r\n"
       "Connection: close\r\n"
       "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    std::size_t written = 0;
    while (written < out.size()) {
        ssize_t n = send(fd, out.data() + written, out.size() - written, 0);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
}

void dispatch(int fd, SessionStore& store, const Request& req) {
    try {
        if (req.path == "/health" && req.method == "GET") {
            respond(fd, 200, R"({"status":"ok","service":"TECO"})");
            return;
        }

        if (req.path == "/metrics" && req.method == "GET") {
            std::string evictions = "null";
            if (auto n = store.ttl_evictions()) evictions = std::to_string(*n);
            respond(fd, 200,
                "{\"backend\":" + escape_json(store.backend_name()) +
                ",\"resident_sessions\":" + std::to_string(store.resident_count()) +
                ",\"ttl_evictions\":" + evictions + "}");
            return;
        }

        if (req.path == "/sessions" && req.method == "POST") {
            std::map<std::string, std::string> fields;
            try {
                fields = JsonStringObjectParser(req.body).parse();
            } catch (const std::exception&) {
                respond(fd, 400, R"({"error":"invalid JSON object"})");
                return;
            }
            if (fields.size() != 2 || !fields.count("token") || !fields.count("state") ||
                !valid_token(fields.at("token")) || fields.at("state").size() > 4096) {
                respond(fd, 400, R"JSON({"error":"expected token (1-64 letters/numbers/_/-) and state (string up to 4096 bytes)"})JSON");
                return;
            }
            const bool existed = store.exists(fields.at("token"));
            store.put(fields.at("token"), fields.at("state"));
            respond(fd, existed ? 200 : 201,
                    "{\"token\":" + escape_json(fields.at("token")) +
                    ",\"state\":" + escape_json(fields.at("state")) + "}");
            return;
        }

        if (req.path.rfind("/sessions/", 0) == 0 &&
            (req.method == "GET" || req.method == "DELETE")) {
            std::string token = req.path.substr(std::string("/sessions/").size());
            if (!valid_token(token)) {
                respond(fd, 400, R"({"error":"invalid token"})");
                return;
            }
            if (req.method == "GET") {
                auto state = store.get(token);
                if (!state) {
                    respond(fd, 404, R"({"error":"session missing or expired"})");
                    return;
                }
                respond(fd, 200,
                        "{\"token\":" + escape_json(token) +
                        ",\"state\":" + escape_json(*state) + "}");
            } else {
                if (store.erase(token)) respond(fd, 204);
                else respond(fd, 404, R"({"error":"session missing or expired"})");
            }
            return;
        }

        if (req.path == "/sessions" || req.path == "/health" || req.path == "/metrics" ||
            req.path.rfind("/sessions/", 0) == 0) {
            respond(fd, 405, R"({"error":"method not allowed"})");
        } else {
            respond(fd, 404, R"({"error":"unknown endpoint"})");
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Backend error: " << e.what() << '\n';
        respond(fd, 503, R"({"error":"session backend unavailable"})");
    } catch (const std::exception&) {
        respond(fd, 400, R"({"error":"invalid JSON object"})");
    }
}

int parse_arg(const char* s, int minval, int maxval) {
    try {
        std::string text(s);
        std::size_t pos = 0;
        long long n = std::stoll(text, &pos);
        if (pos != text.size() || n < minval || n > maxval)
            throw std::invalid_argument("range");
        return static_cast<int>(n);
    } catch (...) {
        throw std::invalid_argument("invalid numeric option");
    }
}
} // namespace

int main(int argc, char** argv) {
    int port = 8080;
    int ttl_ms = 300000;
    int redis_port = 6379;
    std::string backend = "memory";
    std::string redis_host = "127.0.0.1";
    std::string redis_prefix = "teco:session:";

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--port" && i + 1 < argc) port = parse_arg(argv[++i], 0, 65535);
            else if (arg == "--ttl-ms" && i + 1 < argc) ttl_ms = parse_arg(argv[++i], 1, 3600000);
            else if (arg == "--backend" && i + 1 < argc) backend = argv[++i];
            else if (arg == "--redis-host" && i + 1 < argc) redis_host = argv[++i];
            else if (arg == "--redis-port" && i + 1 < argc) redis_port = parse_arg(argv[++i], 1, 65535);
            else if (arg == "--redis-prefix" && i + 1 < argc) redis_prefix = argv[++i];
            else {
                std::cerr
                    << "Usage: teco_server [--port 0..65535] [--ttl-ms 1..3600000] "
                    << "[--backend memory|redis] [--redis-host HOST] [--redis-port PORT] "
                    << "[--redis-prefix PREFIX]\n";
                return 2;
            }
        }

        if (backend != "memory" && backend != "redis") {
            std::cerr << "--backend must be memory or redis\n";
            return 2;
        }

        std::unique_ptr<SessionStore> store;
        if (backend == "redis") {
            store = std::make_unique<RedisSessionStore>(
                redis_host, redis_port, ttl_ms, redis_prefix);
        } else {
            store = std::make_unique<InMemorySessionStore>(
                std::chrono::milliseconds(ttl_ms));
        }

        std::signal(SIGPIPE, SIG_IGN);
        int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0) {
            perror("socket");
            return 1;
        }

        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        // Bind to all container interfaces only when explicitly requested.
        // Docker Compose publishes the ports on the host loopback interface.
        const char* bind_all = std::getenv("TECO_BIND_ALL");
        addr.sin_addr.s_addr = htonl(
            bind_all != nullptr && std::string(bind_all) == "1"
                ? INADDR_ANY : INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<uint16_t>(port));

        if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(server, 16) != 0) {
            perror("bind/listen");
            close(server);
            return 1;
        }

        socklen_t size = sizeof(addr);
        getsockname(server, reinterpret_cast<sockaddr*>(&addr), &size);
        std::cout << "TECO listening on http://127.0.0.1:" << ntohs(addr.sin_port)
                  << " backend=" << store->backend_name() << std::endl;

        for (;;) {
            int client = accept(server, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                break;
            }

            timeval timeout{3, 0};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            Request req;
            int err = 400;
            try {
                if (read_request(client, req, err)) dispatch(client, *store, req);
                else respond(client, err, "{\"error\":" + escape_json(message_for(err)) + "}");
            } catch (const std::exception& e) {
                std::cerr << "Request error: " << e.what() << '\n';
                respond(client, 500, R"({"error":"internal error"})");
            }

            close(client);
        }

        close(server);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "TECO startup failed: " << e.what() << '\n';
        return 1;
    }
}
