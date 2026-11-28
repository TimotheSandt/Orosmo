#!/usr/bin/env python3
"""Captures de la démo comparées aux images de référence de `docs/BASELINE/reference/` (M9-L2, RHI-14).

    demo_reference_check.py [--bin apps/demo/bin/debug/main] [--api vulkan] [--out DIR] [--tolerance 8] [--max-diff-percent 2]
                            [--only NOM ...]

Pour chaque cas de CASES, lance la démo `--headless --strict-backend` sur l'API demandée (sans repli silencieux sur une autre),
compare l'image capturée (`scene.png` ou `ui.png`) à sa référence avec `tools/compare_png.py` et, en Vulkan, exige la ligne
« Vulkan validation: 0 error/warning message(s) » (le binaire de debug active les couches). Sort avec 1 si un cas échoue ; tous
les cas sont joués (une image de différence `diff-<cas>.png` par cas).

Les références sont des captures **OpenGL** faites sous Linux (voir `docs/BASELINE/reference/README.md`) : il n'existe pas de
référence propre à Vulkan, la règle de RHI-14 étant « même image pour un même shader, quel que soit le backend ». C'est ce que
vérifie la CI macOS (Vulkan sur MoltenVK), où OpenGL 4.3 n'existe pas. Tolérance (8 par canal, 2 % de pixels au plus) : celle de
`make demo-capture` et de `tools/demo_parity.py`.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REFERENCES = ROOT / "docs/BASELINE/reference"
OK_LINE = re.compile(r"Vulkan validation: 0 error/warning message\(s\)")

# (nom, arguments de la démo, image capturée, référence)
CASES = [
    ("scene", ["--ui", "off"], "scene", "scene-opengl-demo-1280x720.png"),
    ("ui", ["--ui", "full", "--theme", "dark", "--tab", "controls", "--focus", "detailMedium"], "ui",
     "ui-opengl-controls-focus-1280x720.png"),
    ("shadows", ["--ui", "off", "--shadows", "on"], "scene", "scene-opengl-demo-shadows-1280x720.png"),
    # Forward+ (REN-04) : même éclairage que le forward, par construction (mesh.frag) : même référence que la scène.
    ("forwardplus", ["--ui", "off", "--lighting", "forwardplus"], "scene", "scene-opengl-demo-1280x720.png"),
    ("gpudriven", ["--ui", "off", "--instances", "2000", "--gpu-driven", "on"], "scene", "scene-opengl-gpudriven-1280x720.png"),
    ("post", ["--ui", "off", "--post", "full"], "scene", "scene-opengl-post-full-1280x720.png"),
]


def run(binary, api, args, out):
    cmd = [str(binary), "--api", api, "--strict-backend", "--headless", *args, "--capture", str(out), "--capture-frames", "5"]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=300, cwd=Path(binary).resolve().parent.parent.parent)
    return proc.returncode, proc.stdout + proc.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "apps/demo/bin/debug/main"))
    ap.add_argument("--api", default="vulkan", choices=["vulkan", "opengl"])
    ap.add_argument("--out", default=str(ROOT / "build/demo-reference"))
    ap.add_argument("--tolerance", type=int, default=8)
    ap.add_argument("--max-diff-percent", type=float, default=2.0)
    ap.add_argument("--only", nargs="*", default=None, help="noms des cas à jouer (défaut : tous)")
    opts = ap.parse_args()
    unknown = sorted(set(opts.only or []) - {name for name, _, _, _ in CASES})
    if unknown:
        ap.error(f"--only : cas inconnu(s) : {', '.join(unknown)} (connus : {', '.join(name for name, _, _, _ in CASES)})")
    out = Path(opts.out).resolve()
    binary = Path(opts.bin).resolve()
    failed = 0
    for name, args, image, reference in CASES:
        if opts.only and name not in opts.only:
            continue
        directory = out / name
        directory.mkdir(parents=True, exist_ok=True)
        try:
            code, log = run(binary, opts.api, args, directory)
        except subprocess.TimeoutExpired as e:
            partial = "".join(s.decode(errors="replace") if isinstance(s, bytes) else s or "" for s in (e.stdout, e.stderr))
            (directory / "demo.log").write_text(f"{partial}\ndelai depasse : {e.timeout} s\n")
            print(f"ECHEC {name} : délai de {e.timeout} s dépassé (journal : {directory / 'demo.log'})")
            failed += 1
            continue
        (directory / "demo.log").write_text(log)
        if code != 0:
            print(f"ECHEC {name} : code de sortie {code} (journal : {directory / 'demo.log'})")
            failed += 1
            continue
        if opts.api == "vulkan" and not OK_LINE.search(log):
            print(f"ECHEC {name} : la validation Vulkan n'est pas silencieuse (ou pas active) :")
            print("\n".join(l for l in log.splitlines() if "validation" in l.lower())[:2000])
            failed += 1
        cmp = subprocess.run(
            [sys.executable, str(ROOT / "tools/compare_png.py"), str(REFERENCES / reference), str(directory / f"{image}.png"),
             "--tolerance", str(opts.tolerance), "--max-diff-percent", str(opts.max_diff_percent),
             "--diff-out", str(out / f"diff-{name}.png")],
            capture_output=True, text=True)
        print(f"{name} ({image}.png / {reference}) : {cmp.stdout.strip() or cmp.stderr.strip()}")
        if cmp.returncode != 0:
            failed += 1
    print(f"demo-reference-check [{opts.api}] : " + ("ECHEC" if failed else "OK (images dans la tolérance)"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
