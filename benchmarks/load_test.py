#!/usr/bin/env python3
import argparse
import concurrent.futures
import json
import math
import statistics
import time
import urllib.error
import urllib.request


def percentile(values, p):
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(1, math.ceil((p / 100.0) * len(ordered)))
    return ordered[min(rank - 1, len(ordered) - 1)]


def request_once(url):
    started = time.perf_counter()
    status = None
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=10) as response:
            response.read()
            status = response.status
    except urllib.error.HTTPError as err:
        err.read()
        status = err.code
    except Exception:
        return False, (time.perf_counter() - started) * 1000.0, None

    return 200 <= status < 300, (time.perf_counter() - started) * 1000.0, status


def create_session(base_url, token):
    payload = json.dumps({"token": token, "state": "BENCHMARK"}).encode("utf-8")
    req = urllib.request.Request(
        base_url.rstrip("/") + "/sessions",
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            response.read()
            if response.status not in (200, 201):
                raise RuntimeError(f"unexpected session create status {response.status}")
    except urllib.error.HTTPError as err:
        body = err.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"session create failed: HTTP {err.code}: {body}") from err


def main():
    parser = argparse.ArgumentParser(description="Benchmark a running TECO server.")
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=8)
    parser.add_argument("--warmup", type=int, default=25)
    parser.add_argument("--mode", choices=("health", "session-read"), default="session-read")
    parser.add_argument("--token", default="teco_benchmark")
    parser.add_argument("--label", default="TECO")
    args = parser.parse_args()

    if args.requests < 1:
        parser.error("--requests must be at least 1")
    if args.concurrency < 1:
        parser.error("--concurrency must be at least 1")
    if args.warmup < 0:
        parser.error("--warmup cannot be negative")

    base = args.url.rstrip("/")
    if args.mode == "session-read":
        create_session(base, args.token)
        target = f"{base}/sessions/{args.token}"
    else:
        target = f"{base}/health"

    for _ in range(args.warmup):
        request_once(target)

    started = time.perf_counter()
    latencies = []
    statuses = {}
    successes = errors = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [executor.submit(request_once, target) for _ in range(args.requests)]
        for future in concurrent.futures.as_completed(futures):
            ok, latency_ms, status = future.result()
            latencies.append(latency_ms)
            successes += int(ok)
            errors += int(not ok)
            key = str(status) if status is not None else "connection_error"
            statuses[key] = statuses.get(key, 0) + 1

    duration = time.perf_counter() - started
    throughput = args.requests / duration if duration > 0 else 0.0
    average = statistics.fmean(latencies) if latencies else 0.0

    print(f"TECO Load Test: {args.label}")
    print(f"Target: {target}")
    print(f"Mode: {args.mode}")
    print(f"Requests: {args.requests}")
    print(f"Concurrency: {args.concurrency}")
    print(f"Warmup requests: {args.warmup}")
    print(f"Successes: {successes}")
    print(f"Errors: {errors}")
    print(f"Statuses: {json.dumps(statuses, sort_keys=True)}")
    print(f"Duration: {duration:.3f} s")
    print(f"Throughput: {throughput:.2f} req/s")
    print(f"Latency avg: {average:.3f} ms")
    print(f"Latency p50: {percentile(latencies, 50):.3f} ms")
    print(f"Latency p95: {percentile(latencies, 95):.3f} ms")
    print(f"Latency p99: {percentile(latencies, 99):.3f} ms")

    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
