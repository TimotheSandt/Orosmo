#!/usr/bin/env python3
"""En-têtes publics autonomes (docs/03 §8.3) : chaque en-tête de include/ doit se compiler seul.

Compile chaque fichier avec `$CXX -std=c++23 -fsyntax-only` (en-tête inclus par un fichier minimal).
Mode avertissement par défaut (exit 0) ; `--strict` sort avec 1 ; `--vcpkg DIR` ajoute -isystem DIR/include
(par défaut vcpkg_installed/x64-linux si présent).
"""
import argparse, concurrent.futures, os, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


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
    vc = a.vcpkg or os.path.join(ROOT, "vcpkg_installed", "x64-linux")
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
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"check_headers: {len(headers)} en-têtes, {len(bad)} non autonome(s)")
    return 1 if a.strict and bad else 0


if __name__ == "__main__":
    sys.exit(main())
