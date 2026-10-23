# TECO Portfolio Demo

This walkthrough demonstrates the main engineering ideas in a few minutes.

## 1. Start the stack

```sh
docker compose up --build -d
docker compose ps
```

Expected topology:

- `teco-a` published on `127.0.0.1:8080`
- `teco-b` published on `127.0.0.1:8081`
- one internal Redis service

## 2. Show liveness and readiness

```sh
curl -s http://127.0.0.1:8080/livez
curl -s http://127.0.0.1:8080/readyz
```

Expected:

```json
{"status":"alive"}
```

```json
{"status":"ready"}
```

Explain that liveness means the HTTP process is responding, while readiness includes the Redis dependency.

## 3. Demonstrate shared state

Write through TECO A:

```sh
curl -i -X POST http://127.0.0.1:8080/sessions \
  -H 'Content-Type: application/json' \
  -d '{"token":"portfolio_demo","state":"CREATED_BY_A"}'
```

Read through TECO B:

```sh
curl -i http://127.0.0.1:8081/sessions/portfolio_demo
```

This shows that state is not stored only inside process A. Both instances use the same Redis backend.

## 4. Show observability

```sh
curl -s http://127.0.0.1:8080/metrics
```

Point out:

- request counters
- response classes
- uptime
- process-average RPS
- latency average/P50/P95/P99
- backend name

## 5. Demonstrate dependency failure

Stop Redis:

```sh
docker compose stop redis
```

Readiness should fail:

```sh
curl -i http://127.0.0.1:8080/readyz
```

Expected HTTP status: `503 Service Unavailable`.

Liveness should still work:

```sh
curl -i http://127.0.0.1:8080/livez
```

Expected HTTP status: `200 OK`.

A Redis-backed session request should also surface backend failure:

```sh
curl -i http://127.0.0.1:8080/sessions/portfolio_demo
```

Expected HTTP status: `503 Service Unavailable`.

## 6. Demonstrate recovery

```sh
docker compose start redis
```

Check readiness until it returns 200:

```sh
curl -i http://127.0.0.1:8080/readyz
```

Create a fresh session:

```sh
curl -i -X POST http://127.0.0.1:8080/sessions \
  -H 'Content-Type: application/json' \
  -d '{"token":"after_recovery","state":"RECOVERED"}'
```

Read it from TECO B:

```sh
curl -i http://127.0.0.1:8081/sessions/after_recovery
```

## 7. Show automated validation

```sh
python3 tests/test_docker_resilience.py
```

The final line should be:

```text
All TECO Docker resilience checks passed.
```

## 8. Show benchmark evidence

```sh
cat benchmarks/latest_results.txt
```

The captured project run records:

- in-memory: 5952.92 req/s, P99 2.469 ms
- Redis: 2535.68 req/s, P99 6.118 ms
- zero client-side errors in both 1,000-request runs

These results are local and machine-specific.

## 9. Clean up

```sh
docker compose down -v
```

## Suggested explanation

A concise way to describe TECO:

> TECO is a C++17 session-state backend prototype. I built the TTL engine first, then added a custom REST server, a storage abstraction, Redis-backed shared state, Docker multi-instance deployment, process metrics, load testing, dependency-aware health checks, graceful shutdown, and automated Redis failure/recovery tests. I kept the limitations explicit: the HTTP loop is single-threaded and the Redis client is intentionally small, so it demonstrates systems concepts without pretending to be a production web server.
