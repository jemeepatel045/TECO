# TECO: Thread-Less Ephemeral Context Orchestration

TECO is a C++17 backend-systems portfolio project exploring event-driven, token-keyed ephemeral session state. It supports a local in-memory TTL engine and an optional Redis-backed mode that allows multiple TECO server processes to share session state.

## Current architecture

```text
Client
  |
  v
TECO REST API
  |
  +-------------------+
  |                   |
In-memory          Redis
backend            backend
  |                   |
per-process        shared session state
sliding TTL        Redis PX TTL
```

## Features

- Token-keyed session state
- Sliding TTL refreshed on writes
- In-memory backend with lazy lookup eviction and periodic sweeps
- Redis backend using Redis TTL (`SET ... PX`)
- Cross-process session sharing through Redis
- REST endpoints for create/update, read, delete, health, and metrics
- CMake build and CTest suite
- Python HTTP integration tests
- Redis cross-instance integration test
- GitHub Actions CI with a Redis service container
- Illustrative local benchmark against sequential OS thread create/join

## Build

Requires C++17, CMake 3.16+, and Python 3.8+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

When Redis is not available, the Redis integration test is skipped. On GitHub Actions, CI launches Redis automatically.

## In-memory server

```sh
./build/teco_server --port 8080 --backend memory --ttl-ms 300000
```

## Redis-backed server

Start a local Redis service first:

```sh
brew services start redis
redis-cli ping
```

Then:

```sh
./build/teco_server \
  --port 8080 \
  --backend redis \
  --redis-host 127.0.0.1 \
  --redis-port 6379 \
  --ttl-ms 300000
```

Start a second instance in another terminal:

```sh
./build/teco_server \
  --port 8081 \
  --backend redis \
  --redis-host 127.0.0.1 \
  --redis-port 6379 \
  --ttl-ms 300000
```

A session written to port 8080 can then be read from port 8081 because both processes use the same Redis backend.

## API

```sh
curl -i http://127.0.0.1:8080/health

curl -i -X POST http://127.0.0.1:8080/sessions \
  -H 'Content-Type: application/json' \
  -d '{"token":"demo_1","state":"ACTIVE"}'

curl -i http://127.0.0.1:8081/sessions/demo_1

curl -i http://127.0.0.1:8080/metrics

curl -i -X DELETE http://127.0.0.1:8081/sessions/demo_1
```

## Metrics behavior

The in-memory backend reports TECO-observed TTL evictions. Redis performs expiration internally, so `ttl_evictions` is `null` in Redis mode until a future phase adds Redis keyspace-notification telemetry.

The Redis prototype uses `KEYS` to count TECO-prefixed sessions for a small local demonstration. A production implementation should use a scalable metrics strategy instead of `KEYS`.

## Scope and limitations

TECO is currently an educational local prototype, not a production internet-facing service.

The HTTP server is intentionally single-threaded and loopback-only. The included Redis client is a small RESP client created for this project and opens a Redis TCP connection per command. It does not implement authentication, TLS, connection pooling, Redis Cluster, Sentinel, retries, or production-grade observability.

The original benchmark compares the TECO event loop against sequential OS thread creation and joining. It is not a reusable thread-pool comparison and does not prove serverless or distributed performance superiority.
