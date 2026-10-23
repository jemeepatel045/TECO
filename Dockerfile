FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make python3 ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/ src/
COPY tests/ tests/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel 2

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 curl ca-certificates && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 teco
COPY --from=builder /source/build/teco_server /usr/local/bin/teco_server
USER teco
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 CMD curl --fail --silent http://127.0.0.1:8080/readyz >/dev/null || exit 1
CMD ["teco_server", "--port", "8080", "--backend", "redis", "--redis-host", "redis", "--redis-port", "6379", "--ttl-ms", "300000"]
