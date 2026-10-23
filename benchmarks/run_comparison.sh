#!/bin/sh
set -eu

REQUESTS="${1:-1000}"
CONCURRENCY="${2:-8}"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Building TECO Release binary"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "==> Starting Docker Redis deployment"
docker compose up --build -d

echo "==> Starting local in-memory TECO on port 8090"
./build/teco_server --port 8090 --backend memory --ttl-ms 300000 \
  > /tmp/teco-memory-benchmark.log 2>&1 &
MEMORY_PID=$!

cleanup() {
  kill "$MEMORY_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

wait_for_health() {
  url="$1"
  count=0
  until curl -fsS "$url/health" >/dev/null 2>&1; do
    count=$((count + 1))
    if [ "$count" -ge 30 ]; then
      echo "Server did not become healthy: $url" >&2
      exit 1
    fi
    sleep 1
  done
}

wait_for_health "http://127.0.0.1:8090"
wait_for_health "http://127.0.0.1:8080"

echo
echo "===== IN-MEMORY BACKEND ====="
python3 benchmarks/load_test.py \
  --url http://127.0.0.1:8090 \
  --requests "$REQUESTS" \
  --concurrency "$CONCURRENCY" \
  --mode session-read \
  --label "In-memory"

echo
echo "===== REDIS BACKEND ====="
python3 benchmarks/load_test.py \
  --url http://127.0.0.1:8080 \
  --requests "$REQUESTS" \
  --concurrency "$CONCURRENCY" \
  --mode session-read \
  --label "Redis"

echo
echo "Metrics snapshots:"
echo "In-memory:"
curl -fsS http://127.0.0.1:8090/metrics
echo
echo "Redis:"
curl -fsS http://127.0.0.1:8080/metrics
echo
echo
echo "Docker services remain running. Stop them with: docker compose down"
