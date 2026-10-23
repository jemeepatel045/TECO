"""Redis-backed cross-instance integration test for TECO."""
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid


def redis_available(host, port):
    try:
        with socket.create_connection((host, port), timeout=1):
            return True
    except OSError:
        return False


def start_server(executable, host, redis_port, prefix):
    p = subprocess.Popen(
        [
            executable,
            "--port", "0",
            "--ttl-ms", "300",
            "--backend", "redis",
            "--redis-host", host,
            "--redis-port", str(redis_port),
            "--redis-prefix", prefix,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    line = p.stdout.readline().strip()
    match = re.search(r"http://127\.0\.0\.1:(\d+)", line)
    if not match:
        rest = p.stdout.read() if p.stdout else ""
        p.terminate()
        raise RuntimeError(f"Server failed to start: {line!r} {rest!r}")
    return p, f"http://127.0.0.1:{match.group(1)}"


def call(base, method, route, payload=None):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(base + route, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=4) as response:
            body = response.read()
            return response.status, json.loads(body) if body else None
    except urllib.error.HTTPError as err:
        body = err.read()
        return err.code, json.loads(body) if body else None


def stop(p):
    if p is None:
        return
    p.terminate()
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    if p.stdout:
        p.stdout.close()


def main(executable):
    host = os.environ.get("TECO_REDIS_HOST", "127.0.0.1")
    port = int(os.environ.get("TECO_REDIS_PORT", "6379"))

    if not redis_available(host, port):
        print(f"Redis not available at {host}:{port}; skipping Redis integration test.")
        return 77

    prefix = f"teco:test:{uuid.uuid4().hex}:"
    a = b = None
    try:
        a, base_a = start_server(executable, host, port, prefix)
        b, base_b = start_server(executable, host, port, prefix)

        # Write through A, read through B.
        assert call(base_a, "POST", "/sessions",
                    {"token": "shared_user", "state": "FROM_A"}) == (
                        201, {"token": "shared_user", "state": "FROM_A"})
        assert call(base_b, "GET", "/sessions/shared_user") == (
            200, {"token": "shared_user", "state": "FROM_A"})

        # Update through B, read through A.
        assert call(base_b, "POST", "/sessions",
                    {"token": "shared_user", "state": "UPDATED_BY_B"}) == (
                        200, {"token": "shared_user", "state": "UPDATED_BY_B"})
        assert call(base_a, "GET", "/sessions/shared_user") == (
            200, {"token": "shared_user", "state": "UPDATED_BY_B"})

        metrics = call(base_a, "GET", "/metrics")
        assert metrics[0] == 200
        assert metrics[1]["backend"] == "redis"
        assert metrics[1]["resident_sessions"] >= 1
        assert metrics[1]["ttl_evictions"] is None
        assert metrics[1]["requests_total"] >= 1
        assert metrics[1]["responses_2xx"] >= 1
        assert metrics[1]["latency_samples"] >= 1
        assert set(metrics[1]["latency_ms"]) == {"avg", "p50", "p95", "p99"}

        # Deletion on one process is immediately visible to the other.
        assert call(base_b, "DELETE", "/sessions/shared_user") == (204, None)
        assert call(base_a, "GET", "/sessions/shared_user")[0] == 404

        # Redis owns expiration. GET does not refresh TTL.
        assert call(base_a, "POST", "/sessions",
                    {"token": "expires", "state": "SHORT"})[0] == 201
        time.sleep(0.45)
        assert call(base_b, "GET", "/sessions/expires")[0] == 404

        print("All TECO Redis cross-instance tests passed.")
        return 0
    finally:
        stop(a)
        stop(b)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
