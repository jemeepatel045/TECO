# TECO: Thread-Less Ephemeral Context Orchestration

TECO is a C++17 **local prototype** of an event-driven, token-keyed, in-memory session state engine. It supports sliding time-to-live (TTL) expiration and exposes the state engine through a small HTTP API.

## Features

- Token-keyed session state with sliding TTL, refreshed on writes
- Lazy eviction when expired tokens are read, and periodic sweeps every 64 state events
- Explicit session deletion and counters for resident entries and TTL evictions
- Loopback-only, single-threaded C++ HTTP server with JSON string fields
- Local CMake + CTest checks, Python HTTP integration tests, and GitHub Actions CI
- Illustrative benchmark against a sequential thread-create/join baseline

## Build on macOS / Linux

Requires C++17 compiler, CMake 3.16+, Python 3.8+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run the API

Terminal 1:

```sh
./build/teco_server --port 8080 --ttl-ms 300000
```

Terminal 2:

```sh
curl -i http://127.0.0.1:8080/health
curl -i -X POST http://127.0.0.1:8080/sessions \
  -H 'Content-Type: application/json' \
  -d '{"token":"demo_1","state":"ACTIVE"}'
curl -i http://127.0.0.1:8080/sessions/demo_1
curl -i http://127.0.0.1:8080/metrics
curl -i -X DELETE http://127.0.0.1:8080/sessions/demo_1
```

The server responds `201` when creating and `200` when updating a session, `200` when retrieving a session, `204` when deleting it, and `404` for a missing or expired session. Use `Ctrl+C` in Terminal 1 to stop.

### Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/health` | Health check |
| `POST` | `/sessions` | Create or update a session using `{"token":"abc","state":"ACTIVE"}` |
| `GET` | `/sessions/{token}` | Get a non-expired session |
| `DELETE` | `/sessions/{token}` | Delete a session |
| `GET` | `/metrics` | Report resident entries and TTL evictions |

Valid tokens are 1–64 ASCII alphanumeric characters, hyphens, or underscores. State values are JSON strings up to 4096 bytes. The API accepts only JSON objects with exactly `token` and `state` string properties. `GET` does **not** extend the session TTL; a write refreshes it. `resident_sessions` counts entries currently present in the map, including expired entries pending a sweep.

## Benchmark

```sh
./build/teco
```

The benchmark compares the single-threaded TECO event loop with repeatedly creating and joining OS threads, **not** with an optimized reusable thread pool. Results do not establish serverless or distributed throughput superiority.

## Scope and security limitations

This is a **local educational prototype**, not an internet-facing service. It binds only to `127.0.0.1`. Its HTTP implementation is intentionally limited to one connection at a time, one small request per connection, `HTTP/1.1`, `Content-Length`, and a restricted subset of JSON, with 3-second connection read/write timeouts. It does not offer authentication, TLS, general-purpose HTTP support, robust UTF-8 validation, rate limiting, shared/distributed state, or persistence. Processes do not share their in-memory maps. Avoid exposing it publicly. HTTP inputs must not be treated as authentication tokens.

Idle sessions are not proactively removed if no requests arrive. They are evicted upon lookup or subsequent write-driven sweeps. The metrics `resident_sessions` value is not a live active-session count.
