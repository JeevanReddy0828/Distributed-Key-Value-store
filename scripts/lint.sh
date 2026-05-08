#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${SCRIPT_DIR}/.."
BUILD_DIR="${ROOT}/build"

echo "==> Running clang-format check"
find "${ROOT}/src" "${ROOT}/tests" \
    -name "*.cpp" -o -name "*.hpp" | \
    xargs clang-format --dry-run --Werror

echo "==> Running clang-tidy"
if [[ -f "${BUILD_DIR}/compile_commands.json" ]]; then
    find "${ROOT}/src" -name "*.cpp" | \
        xargs clang-tidy \
            -p "${BUILD_DIR}" \
            --checks="-*,readability-*,modernize-*,performance-*,bugprone-*" \
            --warnings-as-errors="*"
else
    echo "  Skipping clang-tidy (no compile_commands.json; run build.sh first)"
fi

echo "==> Lint passed"
