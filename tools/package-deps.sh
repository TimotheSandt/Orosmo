#!/bin/sh
# Remplit deps/{include,lib} du paquet de release (contrat A76) : les bibliothèques tierces du moteur, précompilées, pour que le
# développeur n'ait plus besoin de vcpkg. Appelé par `make -f mk/engine.mk dist` ; lancer avec `sh` (disque sans bit exécutable).
#
#   sh tools/package-deps.sh <vcpkg_installed/<triplet>> <dossier deps/ de sortie>
#
# Source : le dossier de dépendances que le build du moteur vient d'utiliser (mêmes en-têtes, mêmes versions : les archives du
# moteur et deps/ restent cohérentes).
#   - Windows, macOS : vcpkg_installed/<triplet> (triplets statiques : x64-mingw-static, *-osx).
#   - Linux : vcpkg (glad, msdfgen, GLFW, volk, ktx…) + paquets apt reliés par tools/setup-linux-deps.sh (glm, stb, freetype,
#     shaderc, glslang, SPIRV-Tools). Le dépôt lie shaderc en bibliothèque partagée (libshaderc.so) ; le paquet prend les archives
#     statiques que Debian/Ubuntu livrent dans les mêmes paquets -dev (libshaderc_combined.a = shaderc + shaderc_util, renommée
#     libshaderc.a pour garder la liste de liens de engine.mk ; libfreetype.a, libpng16.a, libbrotli*.a). Aucun paquet en plus,
#     aucun build vcpkg en plus en CI, et le build de développement ne change pas.
# Jamais embarqués : chargeur Vulkan (libvulkan*, vulkan-1), OpenGL, X11/Wayland, libc ; aucune bibliothèque partagée
# (.so, .dylib, .dll, .dll.a) : le jeu lié reste autonome.
# Licences : share/<port>/copyright (vcpkg) et /usr/share/doc/<paquet>/copyright (apt) copiés dans deps/licenses/.
set -eu

[ $# -eq 2 ] || { echo "usage: sh tools/package-deps.sh <vcpkg_installed/<triplet>> <deps/>" >&2; exit 2; }
SRC=$1
DEST=$2
[ -d "$SRC/include" ] && [ -d "$SRC/lib" ] || {
    echo "package-deps : $SRC/include ou $SRC/lib introuvable (lancer 'vcpkg install' ; sous Linux aussi tools/setup-linux-deps.sh)" >&2
    exit 1; }

rm -rf "$DEST"
mkdir -p "$DEST/include" "$DEST/lib" "$DEST/licenses"

# 1. En-têtes (liens symboliques vers /usr/include suivis : -L). doctest ne sert qu'aux tests du dépôt.
for p in "$SRC"/include/*; do
    case $(basename "$p") in doctest) continue ;; esac
    cp -RL "$p" "$DEST/include/"
done

# 2. Archives statiques (version release de vcpkg, comme le build du moteur, pour debug et release).
for p in "$SRC"/lib/*.a; do
    [ -e "$p" ] || continue
    case $(basename "$p") in libvulkan*|*vulkan-1*|*.dll.a) continue ;; esac
    cp -L "$p" "$DEST/lib/"
done

# 3. Linux : archives statiques des paquets apt (le dépôt les lie en .so ou par lien symbolique).
if [ "$(uname -s)" = Linux ]; then
    SYSLIB=/usr/lib/$(${CC:-gcc} -dumpmachine)
    for l in libshaderc_combined.a libglslang.a libSPIRV.a libSPIRV-Tools-opt.a libSPIRV-Tools.a \
             libfreetype.a libpng16.a libbrotlidec.a libbrotlicommon.a; do
        [ -f "$SYSLIB/$l" ] || { echo "package-deps : $SYSLIB/$l manquant (paquets libshaderc-dev, libfreetype-dev)" >&2; exit 1; }
    done
    cp -L "$SYSLIB/libshaderc_combined.a" "$DEST/lib/libshaderc.a"
    for l in libglslang.a libSPIRV.a libSPIRV-Tools-opt.a libSPIRV-Tools.a libfreetype.a libpng16.a libbrotlidec.a libbrotlicommon.a; do
        cp -L "$SYSLIB/$l" "$DEST/lib/"
    done
    # Contenu de libshaderc_combined.a (déjà dans libshaderc.a) : archive vide pour la liste de liens -lshaderc_util.
    rm -f "$DEST/lib/libshaderc_util.a"
    ar rcs "$DEST/lib/libshaderc_util.a"
    for pkg in libshaderc-dev glslang-dev spirv-tools libfreetype-dev libpng-dev libbrotli-dev libglm-dev libstb-dev; do
        [ -f "/usr/share/doc/$pkg/copyright" ] && cp -L "/usr/share/doc/$pkg/copyright" "$DEST/licenses/$pkg.txt"
    done
fi

# 4. Licences des ports vcpkg.
for c in "$SRC"/share/*/copyright; do
    [ -f "$c" ] || continue
    cp -L "$c" "$DEST/licenses/$(basename "$(dirname "$c")").txt"
done

# 5. Contrôle : aucune bibliothèque partagée ni chargeur Vulkan, aucun lien symbolique restant.
bad=$(find "$DEST" \( -name '*.so' -o -name '*.so.*' -o -name '*.dylib' -o -name '*.dll' -o -name '*.dll.a' -o -name 'libvulkan*' \
          -o -type l \) -print)
[ -z "$bad" ] || { echo "package-deps : fichiers interdits dans deps/ :" >&2; echo "$bad" >&2; exit 1; }

printf 'deps/ : %s en-têtes de premier niveau, %s archives, %s\n' \
    "$(ls "$DEST/include" | wc -l | tr -d ' ')" "$(ls "$DEST/lib" | wc -l | tr -d ' ')" "$(du -sh "$DEST" | cut -f1)"
