#!/usr/bin/env python3
"""En-têtes publics autonomes (docs/03 §8.3) : chaque en-tête de include/ doit se compiler seul.

Compile chaque fichier avec `$CXX -std=c++23 -fsyntax-only` (en-tête inclus par un fichier minimal).
Mode avertissement par défaut (exit 0) ; `--strict` sort avec 1 ; `--vcpkg DIR` ajoute -isystem DIR/include.
Sans `--vcpkg` : chaque triplet installé (`vcpkg_installed/<triplet>/include`, ou `$VCPKG_INSTALLED_ROOT/<triplet>`) est
ajouté, si bien que Linux, MSYS2/MinGW (x64-mingw-static) et macOS trouvent leurs dépendances sans option.
"""
import argparse, concurrent.futures, os, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

# En-têtes légers (GAME-06, A79 lot L7) : ce que leur inclusion ne doit JAMAIS tirer, même indirectement. Chaque motif est
# cherché dans les chemins (séparés par « / ») de la liste des dépendances donnée par le compilateur (-M). De glm, seuls les
# types sont permis (`glm/ext/…` et ce que `detail/type_quat` tire lui-même, dont `gtc/constants`).
LIGHT_HEADERS = {
    "orosmo/game.hpp": [
        "/orosmo/renderer/", "/orosmo/rhi/", "/orosmo/platform/", "/orosmo/ui/", "/orosmo/assets/", "/orosmo/anim/",
        "/orosmo/sim/simulation.hpp", "/orosmo/scene/scene_file.hpp", "/orosmo/core/serialization.hpp", "/orosmo/core/math.hpp",
        "/nlohmann/json.hpp", "/glm/glm.hpp", "/glm/gtc/quaternion", "/glm/gtx/", "/vulkan/", "/GLFW/",
    ],
}


def forbidden_dependencies(cxx, flags, tmp, header, patterns):
    """Fichiers interdits que `header` inclut ; None si le compilateur échoue (déjà signalé par le contrôle d'autonomie)."""
    src = os.path.join(tmp, "light.cpp")
    with open(src, "w", encoding="utf-8") as f:
        f.write(f'#include <{header}>\n')
    r = subprocess.run([cxx] + [x for x in flags if x != "-fsyntax-only"] + ["-M", src], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    # Format make : « cible: dépendances », lignes continuées par « \ », espaces des chemins échappés ; \ de Windows -> /.
    text = r.stdout.replace("\\\n", " ").replace("\\ ", "\0").replace("\\", "/")
    deps = [d.replace("\0", " ") for d in text.split()]
    return sorted({d for d in deps for p in patterns if p in d})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--vcpkg")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    a = ap.parse_args()
    cxx = os.environ.get("CXX", "g++")
    if shutil.which(cxx) is None:
        print(f"check_headers: compilateur introuvable ({cxx}), contrôle ignoré")
        return 0
    inc = os.path.join(ROOT, "include")
    flags = ["-std=c++23", "-fsyntax-only", "-DGLM_ENABLE_EXPERIMENTAL", "-Wall", "-Wextra", "-I" + inc]
    flags += ["-I" + os.path.join(inc, d) for d in sorted(os.listdir(inc)) if os.path.isdir(os.path.join(inc, d))]
    if a.vcpkg:
        roots = [a.vcpkg]
    else:
        base = os.environ.get("VCPKG_INSTALLED_ROOT") or os.path.join(ROOT, "vcpkg_installed")
        base = base if os.path.isabs(base) else os.path.join(ROOT, base)
        roots = sorted(os.path.join(base, d) for d in os.listdir(base)) if os.path.isdir(base) else []
    for vc in roots:
        if os.path.isdir(os.path.join(vc, "include")):
            flags += ["-isystem", os.path.join(vc, "include")]
    headers = []
    for dp, _, names in os.walk(inc):
        headers += [os.path.relpath(os.path.join(dp, n), inc).replace(os.sep, "/") for n in names if n.endswith((".h", ".hpp"))]
    headers.sort()
    tmp = tempfile.mkdtemp()

    def check(h):
        src = os.path.join(tmp, h.replace("/", "_") + ".cpp")
        with open(src, "w", encoding="utf-8") as f:
            f.write(f'#include "{h}"\n')
        r = subprocess.run([cxx] + flags + [src], capture_output=True, text=True)
        return h, r.returncode, (r.stderr.strip().split("\n") or [""])

    bad = []
    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        for h, rc, err in ex.map(check, headers):
            if rc != 0:
                first = next((l for l in err if "error" in l), err[0])
                bad.append(h)
                print(f"WARNING headers: {h} ne se compile pas seul : {first[:160]}")
    heavy = 0
    for header, patterns in LIGHT_HEADERS.items():
        found = forbidden_dependencies(cxx, flags, tmp, header, patterns) if header in headers else None
        if found is None:
            heavy += 1
            print(f"WARNING headers: en-tête léger {header} absent ou impossible à analyser")
        elif found:
            heavy += 1
            print(f"WARNING headers: en-tête léger {header} tire {len(found)} fichier(s) interdit(s) : {', '.join(found[:4])}")
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"check_headers: {len(headers)} en-têtes, {len(bad)} non autonome(s) ; "
          f"{len(LIGHT_HEADERS)} en-tête(s) léger(s), {heavy} en défaut")
    return 1 if a.strict and (bad or heavy) else 0


if __name__ == "__main__":
    sys.exit(main())
