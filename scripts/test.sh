#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build"

if [[ ! -d "${BUILD_DIR}" ]]; then
    echo "Build directory not found. Run scripts/build.sh first."
    exit 1
fi

echo "==> Running unit tests"
ctest --test-dir "${BUILD_DIR}" \
    --output-on-failure \
    --tests-regex "orcdb_unit" \
    --parallel "$(nproc)"

echo "==> Running integration tests"
ctest --test-dir "${BUILD_DIR}" \
    --output-on-failure \
    --tests-regex "orcdb_integration" \
    --parallel 1 \
    --timeout 60

echo "==> All tests passed"
