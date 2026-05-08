#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
GENERATOR="${GENERATOR:-Ninja}"

echo "==> Configuring OrcDB (type=${BUILD_TYPE})"
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -G "${GENERATOR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DORCDB_BUILD_TESTS=ON

echo "==> Building"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

echo "==> Build complete: ${BUILD_DIR}/orcdb"
