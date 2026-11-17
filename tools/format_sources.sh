#!/bin/sh
# Formate include/, src/ et tests/ avec clang-format (équivalent POSIX de format_sources.ps1).
# Usage : tools/format_sources.sh [clang-format]
set -eu
CF=${1:-clang-format}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
for d in include src tests; do
    [ -d "$ROOT/$d" ] || continue
    find "$ROOT/$d" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cpp' \) -exec "$CF" -i {} +
done
