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
mkdir -p "$OUT/msl-default" "$OUT/msl-inst" "$OUT/pc1" "$OUT/pc2" "$OUT/pc3"
PRE="ENGINE_VULKAN_PIPELINE_CACHE_DIR=$OUT/pc1 MVK_CONFIG_SHADER_DUMP_DIR=$OUT/msl-default"; run default --ui off; PRE=""
PRE="ENGINE_VULKAN_PIPELINE_CACHE_DIR=$OUT/pc2 MVK_CONFIG_SHADER_DUMP_DIR=$OUT/msl-inst"; run inst --ui off --instances 64; PRE=""
PRE="ENGINE_VULKAN_PIPELINE_CACHE_DIR=$OUT/pc3 MVK_CONFIG_SHADER_DUMP_DIR=$OUT/msl-gd"; run gd --ui off --instances 64 --gpu-driven on; PRE=""
ls "$OUT/msl-default" | wc -l
exit 0
