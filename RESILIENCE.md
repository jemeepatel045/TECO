# TECO Resilience Validation

Phase 7 adds an automated end-to-end Docker resilience test.

## What it validates

The test starts the full Docker Compose stack and verifies:

1. Both TECO API instances become ready.
2. A session written through instance A is readable through instance B.
3. When Redis is stopped, `/readyz` returns HTTP 503 on both TECO instances.
4. During the Redis outage, `/livez` continues returning HTTP 200.
5. Redis-dependent session traffic fails with HTTP 503 instead of crashing the server.
6. After Redis is restarted, both TECO instances become ready again.
7. Cross-instance session traffic works after recovery.
8. Docker sends SIGTERM to a TECO container and the process exits cleanly with status 0.

The test tears the Compose stack down at the end.

## Run locally

Docker Desktop must be running.

```sh
python3 tests/test_docker_resilience.py
```

The final success line is:

```text
All TECO Docker resilience checks passed.
```

## CI

GitHub Actions runs this test in a separate `docker-resilience` job. The normal CMake/CTest job remains independent so failures in unit/API tests and failures in container resilience are easy to distinguish.

## Scope

This validates observable failure and recovery behavior for the local Docker/Redis architecture. It does not claim automatic failover, Redis replication, Redis Cluster support, or exactly-once request semantics.
