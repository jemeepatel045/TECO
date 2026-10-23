# TECO Performance Benchmarking

Phase 5 adds client-observed load testing and process-local request telemetry.

## Metrics

The load tester reports request counts, success/error counts, wall-clock throughput, average latency, and P50/P95/P99 latency.

The `/metrics` endpoint also exposes process-local request counters and a bounded window of recent request latencies.

## Interpretation

TECO's HTTP server is intentionally single-threaded. Client concurrency creates simultaneous client requests, but TECO still processes them through its single-threaded request loop.

The Redis benchmark includes Redis/backend and local container-network costs. The in-memory benchmark does not.

`process_avg_rps` is total observed requests divided by process uptime. For controlled comparisons, use the load tester's `Throughput` result.

Results are machine-dependent. Do not treat one laptop run as a universal performance claim.

## Compare both backends

```sh
./benchmarks/run_comparison.sh
```

Defaults: 1,000 requests, concurrency 8, 25 warmup requests, session-read workload.

Custom:

```sh
./benchmarks/run_comparison.sh 5000 16
```

Stop Docker afterward:

```sh
docker compose down
```

## Direct test

```sh
python3 benchmarks/load_test.py \
  --url http://127.0.0.1:8080 \
  --requests 1000 \
  --concurrency 8 \
  --mode session-read \
  --label Redis
```

The `/metrics` endpoint includes `requests_total`, response-class counters, uptime, process-average RPS, latency sample count, and average/P50/P95/P99 latency. The latency window stores the latest 4,096 completed requests.
