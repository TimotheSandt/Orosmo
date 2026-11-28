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
run cf1 --ui off --capture-frames 1
run cf2 --ui off --capture-frames 2
run fif1 --ui off --frames-in-flight 1
run gcoff --ui off --graph-cache off
run direct --ui off --direct-present on
run nodirect --ui off --direct-present off
PRE="MVK_CONFIG_USE_MTLHEAP=1"; run heap --ui off; PRE=""
PRE="MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1"; run syncsubmit --ui off; PRE=""
PRE="MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1"; run prefill --ui off; PRE=""
PRE="MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION=0 MVK_CONFIG_SPECIALIZED_QUEUE_FAMILIES=0"; run misc --ui off; PRE=""
exit 0
