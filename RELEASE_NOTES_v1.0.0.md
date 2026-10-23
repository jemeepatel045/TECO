# TECO v1.0.0

TECO v1.0.0 is the first complete portfolio release of **Thread-Less Ephemeral Context Orchestration**, a C++17 backend-systems prototype for short-lived session state.

## Highlights

- Custom C++17 HTTP/1.1 server using POSIX sockets
- Token-keyed TTL session engine
- Interchangeable in-memory and Redis session stores
- Shared session state across multiple TECO processes
- Docker Compose deployment with two API instances and Redis
- Process-local request/latency metrics
- Repeatable local benchmark tooling
- `/livez` and Redis-aware `/readyz` health semantics
- Graceful `SIGINT` and `SIGTERM` shutdown
- Automated Redis outage/recovery testing
- GitHub Actions build/test and Docker resilience jobs

## Captured local benchmark

Session-read workload: 1,000 measured requests, concurrency 8, 25 warmup requests.

| Backend | Throughput | Avg | P50 | P95 | P99 | Errors |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| In-memory | 5952.92 req/s | 1.312 ms | 1.258 ms | 1.816 ms | 2.469 ms | 0 |
| Redis | 2535.68 req/s | 3.114 ms | 2.988 ms | 3.958 ms | 6.118 ms | 0 |

These are local, machine-specific results. The comparison is a project snapshot, not a universal performance claim.

## Validation

Before this release, run:

```sh
./scripts/release_check.sh
```

The script performs:

- Release CMake configuration
- C++ build
- complete CTest suite
- Docker Redis outage/recovery scenario
- graceful container shutdown validation

## Scope

TECO v1.0.0 remains an educational portfolio prototype. It is intentionally single-threaded and does not implement production TLS, authentication, Redis connection pooling, Redis replication/Cluster/Sentinel, or a production-grade HTTP parser.
