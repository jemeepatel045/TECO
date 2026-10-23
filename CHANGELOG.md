# Changelog

All notable TECO portfolio milestones are summarized here.

## [1.0.0] - 2026-10-23

First portfolio release.

### Core engine

- Token-keyed ephemeral session state
- TTL expiration
- Lazy expiration on lookup
- Periodic cleanup support
- Unit tests for core behavior

### HTTP API

- C++17 POSIX-socket HTTP server
- Create/update, read, and delete session endpoints
- Health and metrics endpoints
- Request validation and bounded payload/header handling
- HTTP integration tests

### Distributed state

- `SessionStore` backend abstraction
- Redis-backed session storage
- Redis `SET ... PX` TTL behavior
- Cross-instance session sharing tests

### Containers

- Multi-stage Docker image
- Docker Compose topology with two TECO API instances and Redis
- Host-loopback-only published API ports
- Dependency-aware Docker health checks

### Observability

- Request and response-class counters
- Process uptime and process-average RPS
- Bounded latency sample window
- Average, P50, P95, and P99 latency
- Standard-library load-testing utility
- Captured in-memory and Redis benchmark comparison

### Reliability

- `/livez` process liveness endpoint
- `/readyz` backend-aware readiness endpoint
- Redis `PING` readiness validation
- Graceful `SIGINT` / `SIGTERM` handling
- Environment-based configuration
- Optional structured JSON request logs

### Resilience

- Automated Docker outage/recovery test
- Redis failure mapped to HTTP 503
- Liveness/readiness split verified during dependency loss
- Redis restart recovery verified
- Graceful container stop exit code verified
- Dedicated GitHub Actions Docker resilience job

### Documentation

- Recruiter-oriented README
- Architecture documentation
- Portfolio demo walkthrough
- Benchmark methodology
- Resilience validation notes
- Release verification script
