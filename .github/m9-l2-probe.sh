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
    env $PRE ./bin/debug/main --api vulkan --strict-backend --headless --capture "$OUT/$name" --capture-frames 5 "$@" > "$OUT/$name/demo.log" 2>&1
    echo "exit $?" >> "$OUT/$name/demo.log"
    grep -E "validation|ERROR|error" "$OUT/$name/demo.log" | head -5
}
run default
run ui-off --ui off
run dv-normals --ui off --debug-view normals
run dv-albedo --ui off --debug-view albedo
run dv-wire --ui off --debug-view wireframe
run fplus --ui off --lighting forwardplus
run shadows --ui off --shadows on
run instances --ui off --instances 64
run gpudriven --ui off --instances 64 --gpu-driven on
run post --ui off --post full
run fxaa --ui off --aa fxaa
run anim --ui off --scene anim
run mixed --ui off --scene mixed
run small --ui off --size 640x360
PRE="MVK_CONFIG_FAST_MATH_ENABLED=0"; run nofastmath --ui off; PRE=""
PRE="MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=0"; run noargbuf --ui off; PRE=""
PRE="MVK_CONFIG_LOG_LEVEL=4 MVK_CONFIG_DEBUG=1"; run mvkdebug --ui off; PRE=""
exit 0
