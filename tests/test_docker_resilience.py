#!/usr/bin/env python3
"""End-to-end Docker resilience test for TECO.

Validates:
- both TECO instances become ready
- cross-instance Redis session sharing works
- Redis outage makes readiness fail with 503
- liveness remains 200 while Redis is unavailable
- session operations surface backend failure as 503
- Redis restart restores readiness and cross-instance traffic
- SIGTERM-driven Docker stop exits TECO cleanly with code 0
"""

import json
import subprocess
import time
import urllib.error
import urllib.request


A = "http://127.0.0.1:8080"
B = "http://127.0.0.1:8081"


def run(*args, check=True, capture=False):
    cmd = list(args)
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(
        cmd,
        check=check,
        text=True,
        capture_output=capture,
    )


def http(method, url, payload=None, timeout=6):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            body = response.read()
            parsed = json.loads(body) if body else None
            return response.status, parsed
    except urllib.error.HTTPError as err:
        body = err.read()
        parsed = json.loads(body) if body else None
        return err.code, parsed
    except (urllib.error.URLError, TimeoutError):
        return None, None


def wait_for(method, url, expected_status, payload=None, timeout=45):
    deadline = time.monotonic() + timeout
    last = (None, None)
    while time.monotonic() < deadline:
        last = http(method, url, payload)
        if last[0] == expected_status:
            return last
        time.sleep(1)
    raise AssertionError(
        f"{method} {url} did not reach HTTP {expected_status}; last result={last!r}"
    )


def main():
    try:
        run("docker", "compose", "down", "-v", check=False)
        run("docker", "compose", "up", "--build", "-d")

        print("\n== Initial readiness ==", flush=True)
        assert wait_for("GET", A + "/readyz", 200)[1] == {"status": "ready"}
        assert wait_for("GET", B + "/readyz", 200)[1] == {"status": "ready"}

        print("\n== Cross-instance session sharing ==", flush=True)
        status, body = http(
            "POST", A + "/sessions",
            {"token": "resilience_before", "state": "FROM_A"},
        )
        assert status == 201, (status, body)
        assert http("GET", B + "/sessions/resilience_before") == (
            200, {"token": "resilience_before", "state": "FROM_A"}
        )

        print("\n== Redis outage ==", flush=True)
        run("docker", "compose", "stop", "redis")

        status_a, body_a = wait_for("GET", A + "/readyz", 503)
        status_b, body_b = wait_for("GET", B + "/readyz", 503)
        assert body_a == {"status": "not_ready", "backend": "redis"}
        assert body_b == {"status": "not_ready", "backend": "redis"}

        assert wait_for("GET", A + "/livez", 200)[1] == {"status": "alive"}
        assert wait_for("GET", B + "/livez", 200)[1] == {"status": "alive"}

        failed_status, failed_body = http("GET", A + "/sessions/resilience_before")
        assert failed_status == 503, (failed_status, failed_body)

        print("\n== Redis recovery ==", flush=True)
        run("docker", "compose", "start", "redis")
        assert wait_for("GET", A + "/readyz", 200)[1] == {"status": "ready"}
        assert wait_for("GET", B + "/readyz", 200)[1] == {"status": "ready"}

        status, body = http(
            "POST", A + "/sessions",
            {"token": "resilience_after", "state": "RECOVERED"},
        )
        assert status == 201, (status, body)
        assert http("GET", B + "/sessions/resilience_after") == (
            200, {"token": "resilience_after", "state": "RECOVERED"}
        )

        print("\n== Graceful container shutdown ==", flush=True)
        container_id = run(
            "docker", "compose", "ps", "-q", "teco-a", capture=True
        ).stdout.strip()
        assert container_id, "Could not resolve teco-a container ID"

        run("docker", "compose", "stop", "teco-a")
        exit_code = run(
            "docker", "inspect", "--format", "{{.State.ExitCode}}",
            container_id, capture=True
        ).stdout.strip()
        assert exit_code == "0", f"teco-a exit code after SIGTERM was {exit_code}"

        run("docker", "compose", "start", "teco-a")
        assert wait_for("GET", A + "/readyz", 200)[1] == {"status": "ready"}

        print("\nAll TECO Docker resilience checks passed.", flush=True)
        return 0
    finally:
        run("docker", "compose", "down", "-v", check=False)


if __name__ == "__main__":
    raise SystemExit(main())
