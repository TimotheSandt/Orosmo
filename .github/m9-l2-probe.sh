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
run default --ui off
PRE="OROSMO_M9_PROBE=indirect"; run indirect --ui off; PRE=""
PRE="OROSMO_M9_PROBE=indirect"; run indirect-dvwire --ui off --debug-view wireframe; PRE=""
exit 0
