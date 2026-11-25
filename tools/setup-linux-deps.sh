#!/bin/sh
# Relie les paquets système (apt) au dossier de dépendances lu par les Makefiles : vcpkg_installed/<triplet>/.
#
# Installation complète sous Linux (Ubuntu/Debian), deux commandes :
#   sudo apt install build-essential cmake cargo python3 libglm-dev libstb-dev libshaderc-dev glslc \
#        doctest-dev libfreetype-dev libvulkan-dev vulkan-validationlayers libgl-dev libx11-dev \
#        autoconf autoconf-archive automake libtool pkg-config libwayland-dev libxkbcommon-dev wayland-protocols \
#        libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
#   ~/vcpkg/vcpkg install            # à la racine du dépôt : lit vcpkg.json (sous Linux : glad, msdfgen, GLFW...)
# Le reste est automatique : `make` lance ce script quand le dossier n'est pas encore relié.
#
# vcpkg.json marque glm, stb, freetype, vulkan, shaderc et doctest `"platform": "!linux"` : sous Linux ils viennent
# d'apt, sous Windows/macOS de vcpkg. GLFW vient de vcpkg partout (3.4 et plus, Wayland natif sous Linux : QUESTIONS A67) ;
# les paquets apt de développement Wayland et X11 ci-dessus servent à le compiler. Ce script ajoute (liens symboliques, rien n'est copié) les paquets apt à
# vcpkg_installed/<triplet>/, sans toucher à ce que vcpkg y a installé.
#
# Repli (ancienne installation « classique » dans $VCPKG_ROOT/installed) : si vcpkg_installed/ ne contient pas glad,
# les paquets glad/msdfgen y sont repris depuis $VCPKG_ROOT/installed/<triplet>.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VCPKG_ROOT=${VCPKG_ROOT:-$HOME/vcpkg}
TRIPLET=${TRIPLET:-x64-linux}
DEST=${1:-$ROOT/vcpkg_installed/$TRIPLET}
SYSLIB=/usr/lib/$(gcc -dumpmachine)
link() { ln -sfn "$1" "$2/$(basename "$1")"; }

# 1. glad et msdfgen : installés par `vcpkg install` (mode manifeste) ou, à défaut, repris d'une installation classique.
if [ -d "$DEST/include/glad" ] && [ ! -L "$DEST/include/glad" ] && [ -d "$DEST/include/msdfgen" ]; then
    echo "vcpkg (manifeste) : $DEST"
else
    SRC=$VCPKG_ROOT/installed/$TRIPLET
    [ -d "$SRC/include/glad" ] && [ -d "$SRC/include/msdfgen" ] || {
        echo "glad et msdfgen introuvables : lancer 'vcpkg install' à la racine du dépôt ($ROOT) pour installer vcpkg.json" >&2
        exit 1; }
    echo "vcpkg (installation classique $SRC) : repli"
    rm -rf "$DEST"; mkdir -p "$DEST/include" "$DEST/lib"
    for p in "$SRC"/include/*; do link "$p" "$DEST/include"; done
    for p in "$SRC"/lib/*.a; do link "$p" "$DEST/lib"; done
fi

# 2. Paquets apt.
DOCTEST_INC=${DOCTEST_INCLUDE_DIR:-/usr/include/doctest}
[ -f "$DEST/include/GLFW/glfw3.h" ] && [ ! -L "$DEST/include/GLFW" ] && [ -f "$DEST/lib/libglfw3.a" ] || {
    echo "GLFW (vcpkg) introuvable : lancer 'vcpkg install' à la racine du dépôt ($ROOT)" >&2; exit 1; }
for f in /usr/include/glm/glm.hpp /usr/include/stb/stb_image.h /usr/include/shaderc/shaderc.hpp /usr/include/freetype2/ft2build.h \
         "$DOCTEST_INC/doctest.h" "$SYSLIB/libshaderc.so"; do
    [ -e "$f" ] || { echo "manquant : $f (paquets apt : voir l'en-tête de ce script)" >&2; exit 1; }
done
mkdir -p "$DEST/include" "$DEST/lib"
for p in /usr/include/glm /usr/include/shaderc "$DOCTEST_INC"; do link "$p" "$DEST/include"; done
for p in /usr/include/stb/*.h /usr/include/freetype2/*; do link "$p" "$DEST/include"; done   # stb_image.h, ft2build.h, freetype/
for l in libshaderc.so libglslang.a libSPIRV.a libSPIRV-Tools.a libSPIRV-Tools-opt.a; do
    [ -e "$SYSLIB/$l" ] && link "$SYSLIB/$l" "$DEST/lib"
done
# Dans la liste de liens Linux (tools/engine-sdk/engine.mk) ; inclus dans libshaderc.so avec les paquets apt.
[ -e "$DEST/lib/libshaderc_util.a" ] || ar rcs "$DEST/lib/libshaderc_util.a"
touch "$DEST/.apt-linked"
echo "OK : $DEST"
