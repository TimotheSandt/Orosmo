#!/bin/sh
# TEMPORAIRE (M9-L2) : variantes de la démo sous MoltenVK, une capture par variante. Retiré avant la fin du lot.
OUT="$1"
mkdir -p "$OUT"
cd apps/demo || exit 1
PRE=""
run() {
    name="$1"; shift
    mkdir -p "$OUT/$name"
    echo "=== $name: $*"
    set -- --capture-frames 5 "$@"
    env $PRE ./bin/debug/main --api vulkan --strict-backend --headless --capture "$OUT/$name" "$@" > "$OUT/$name/demo.log" 2>&1
    echo "exit $?" >> "$OUT/$name/demo.log"
    grep -E "validation|ERROR|error" "$OUT/$name/demo.log" | head -5
}
cd ../..
V="VCPKG_TRIPLET=arm64-osx VCPKG_INSTALLED_ROOT=./vcpkg_installed"
make -j3 -f mk/engine.mk BUILD_TYPE=release release $V > "$OUT/build-tests.log" 2>&1 && make -f mk/engine.mk sync-demo BUILD_TYPE=release $V >> "$OUT/build-tests.log" 2>&1 && \
make -j3 -C apps/demo ENGINE_DIR=orosmo RES_DIR=../../res SHADER_DIR=../../shaders TEST_DIR=../../tests ICON_NAME=../../res/public/icon.ico $V bin/tests/tests >> "$OUT/build-tests.log" 2>&1
echo "build tests: $?"
cd apps/demo
ENGINE_TEST_LOG_LEVEL=warning ENGINE_TEST_BACKENDS=vulkan ENGINE_TEST_REQUIRE_GPU=1 ./bin/tests/tests -tc='*M9-L2*' > "$OUT/m9-test.log" 2>&1
echo "m9 test: $?"
grep -E "M9-L2|M9PROBE|test cases" "$OUT/m9-test.log"
exit 0
