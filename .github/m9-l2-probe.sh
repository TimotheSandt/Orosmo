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
run default
PRE="MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog MTL_DEBUG_LAYER_WARNING_MODE=nslog"; run mtlval --ui off --instances 64; PRE=""
PRE="MTL_DEBUG_LAYER=1"; run mtlval-assert --ui off; PRE=""
PRE="MTL_SHADER_VALIDATION=1 MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog"; run mtlshader --ui off; PRE=""
mkdir -p "$OUT/msl"
PRE="MVK_CONFIG_SHADER_DUMP_DIR=$OUT/msl MVK_CONFIG_LOG_LEVEL=4"; run dump --ui off --instances 64 --gpu-driven on; PRE=""
ls "$OUT/msl" | head -50
exit 0
