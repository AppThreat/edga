#!/usr/bin/env bash
# Builds edga against musl in an Alpine container (static, as on glibc) and runs the golden
# tests on the binary: tools/build-alpine.sh [build directory, default build-musl]
set -euo pipefail

cd "$(dirname "$0")/.."
build=${1:-build-musl}
docker run --rm -v "$PWD:/src" -w /src alpine:3.22 sh -c "
set -e
apk add -q build-base cmake samurai python3 py3-jsonschema git
git config --global --add safe.directory '*'
cmake -S . -B '$build' -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build '$build' --target edga
'$build/bin/edga' --version
python3 tests/run.py --edga '$build/bin/edga'
"
