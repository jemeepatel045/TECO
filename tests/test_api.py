"""HTTP integration tests for TECO. Uses the Python standard library only."""
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request


def main(executable):
    process = subprocess.Popen(
        [executable, "--port", "0", "--ttl-ms", "250"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
    )
    try:
        line = process.stdout.readline().strip()
        match = re.search(r"http://127\.0\.0\.1:(\d+)", line)
        assert match, f"Server failed to start: {line!r}"
        base = f"http://127.0.0.1:{match.group(1)}"

        def call(method, route, payload=None, raw=None):
            data = None
            headers = {}
            if payload is not None:
                data = json.dumps(payload).encode("utf-8")
                headers["Content-Type"] = "application/json"
            if raw is not None:
                data = raw.encode("utf-8")
                headers["Content-Type"] = "application/json"
            req = urllib.request.Request(base + route, data=data, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=4) as response:
                    body = response.read()
                    return response.status, json.loads(body) if body else None
            except urllib.error.HTTPError as err:
                body = err.read()
                return err.code, json.loads(body) if body else None

        assert call("GET", "/health") == (200, {"status": "ok", "service": "TECO"})
        status, obj = call("POST", "/sessions", {"token": "alice", "state": "ACTIVE"})
        assert (status, obj) == (201, {"token": "alice", "state": "ACTIVE"})
        assert call("GET", "/sessions/alice") == (200, {"token": "alice", "state": "ACTIVE"})
        state = 'quoted "value" and unicode \u2603'
        assert call("POST", "/sessions", {"token": "alice", "state": state}) == (
            200, {"token": "alice", "state": state})
        assert call("GET", "/sessions/alice") == (200, {"token": "alice", "state": state})
        metrics = call("GET", "/metrics")[1]
        assert metrics["resident_sessions"] == 1
        assert metrics["requests_total"] >= 1
        assert metrics["responses_2xx"] >= 1
        assert metrics["latency_samples"] >= 1
        assert set(metrics["latency_ms"]) == {"avg", "p50", "p95", "p99"}
        assert metrics["latency_ms"]["p95"] >= 0
        assert call("POST", "/sessions", {"token": "invalid/slash", "state": "x"})[0] == 400
        assert call("POST", "/sessions", raw='{bad json}')[0] == 400
        assert call("POST", "/sessions", raw='{"token":"a","token":"b","state":"c"}')[0] == 400
        assert call("GET", "/sessions/missing")[0] == 404
        assert call("DELETE", "/sessions/alice") == (204, None)
        assert call("GET", "/sessions/alice")[0] == 404

        assert call("POST", "/sessions", {"token": "will_expire", "state": "x"})[0] == 201
        time.sleep(0.36)
        assert call("GET", "/sessions/will_expire")[0] == 404
        assert call("GET", "/metrics")[1]["ttl_evictions"] >= 1
        assert call("GET", "/does-not-exist")[0] == 404
        print("All TECO HTTP integration tests passed.")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        if process.stdout:
            process.stdout.close()


if __name__ == "__main__":
    main(sys.argv[1])
