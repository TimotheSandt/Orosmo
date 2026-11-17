#!/bin/sh
# Prépare vcpkg_installed/x64-linux à partir des paquets système (apt) et de vcpkg.
#
# Paquets apt (Ubuntu/Debian) : tout ce qui existe dans la distribution.
#   sudo apt install build-essential libglfw3-dev libglm-dev libstb-dev \
#        libshaderc-dev glslc doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers \
#        libgl-dev libx11-dev autoconf autoconf-archive automake libtool
# Repli vcpkg : ce qui n'existe pas dans apt (glad v1, msdfgen).
#   ~/vcpkg/vcpkg install glad msdfgen --triplet x64-linux
#
# Le Makefile cherche tout dans vcpkg_installed/<triplet>/{include,lib} : ce script y
# relie (liens symboliques, rien n'est copié) les deux sources. Dossier ignoré par Git.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VCPKG_ROOT=${VCPKG_ROOT:-$HOME/vcpkg}
TRIPLET=${TRIPLET:-x64-linux}
SRC=$VCPKG_ROOT/installed/$TRIPLET
DEST=${1:-$ROOT/vcpkg_installed/$TRIPLET}
SYSLIB=/usr/lib/$(gcc -dumpmachine)

[ -d "$SRC/include/glad" ] && [ -d "$SRC/include/msdfgen" ] || {
    echo "glad et msdfgen introuvables dans $SRC : vcpkg install glad msdfgen --triplet $TRIPLET" >&2; exit 1; }
for f in /usr/include/GLFW/glfw3.h /usr/include/glm/glm.hpp /usr/include/stb/stb_image.h \
         /usr/include/shaderc/shaderc.hpp "$SYSLIB/libglfw.so" "$SYSLIB/libshaderc.so"; do
    [ -e "$f" ] || { echo "manquant : $f (paquets apt, voir l'en-tête du script)" >&2; exit 1; }
done

rm -rf "$DEST"; mkdir -p "$DEST/include" "$DEST/lib"
link() { ln -sf "$1" "$2/$(basename "$1")"; }

for p in "$SRC"/include/*; do link "$p" "$DEST/include"; done
DOCTEST_INC=${DOCTEST_INCLUDE_DIR:-/usr/include/doctest}
[ -f "$DOCTEST_INC/doctest.h" ] || { echo "manquant : $DOCTEST_INC/doctest.h (sudo apt install doctest-dev, ou DOCTEST_INCLUDE_DIR=...)" >&2; exit 1; }
for p in /usr/include/GLFW /usr/include/glm /usr/include/shaderc "$DOCTEST_INC"; do link "$p" "$DEST/include"; done
for p in /usr/include/stb/*.h; do link "$p" "$DEST/include"; done

for p in "$SRC"/lib/*.a; do link "$p" "$DEST/lib"; done
for l in libglfw.so libshaderc.so libglslang.a libSPIRV.a libSPIRV-Tools.a libSPIRV-Tools-opt.a; do
    [ -e "$SYSLIB/$l" ] && link "$SYSLIB/$l" "$DEST/lib"
done
# Dans la liste de liens Linux (engine.mk) ; inclus dans libshaderc.so avec les paquets apt.
[ -e "$DEST/lib/libshaderc_util.a" ] || ar rcs "$DEST/lib/libshaderc_util.a"
echo "OK : $DEST"
