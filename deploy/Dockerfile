# ── Stage 1: Builder ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    clang-14 \
    libssl-dev \
    libstdc++-12-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    libfmt-dev \
    libyaml-cpp-dev \
    curl \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++-14 \
    -DCMAKE_C_COMPILER=clang-14 \
    -DORCDB_BUILD_TESTS=OFF \
    && ninja -C build -j$(nproc)

# ── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    libstdc++6 \
    libspdlog1 \
    libfmt8 \
    libyaml-cpp0.7 \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -r -s /bin/false orcdb && \
    mkdir -p /var/lib/orcdb /etc/orcdb && \
    chown orcdb:orcdb /var/lib/orcdb

COPY --from=builder /build/build/orcdb /usr/local/bin/orcdb
COPY config/           /etc/orcdb/

USER orcdb

EXPOSE 2379 2380

HEALTHCHECK --interval=10s --timeout=5s --start-period=15s --retries=3 \
    CMD curl -sf http://localhost:2379/healthz || exit 1

ENTRYPOINT ["/usr/local/bin/orcdb"]
CMD []
