#!/usr/bin/env python3
"""Parité d'image OpenGL / Vulkan de la démo et silence des couches de validation (M4, RHI-14).

    demo_parity.py [--bin apps/demo/bin/debug/main] [--out DIR] [--tolerance 8] [--max-diff-percent 2] [--full]

Pour chaque cas, lance la démo `--headless` avec `--api opengl` puis `--api vulkan`, compare `scene.png` et `ui.png` avec
`tools/compare_png.py` et exige que le journal Vulkan contienne « Vulkan validation: 0 error/warning message(s) » (le binaire de
debug active les couches, synchronisation comprise). Sort avec 1 au premier écart. `--full` ajoute tous les onglets et fenêtres de
`make demo-capture`.

Tolérance (8 par canal, 2 % de pixels) : celle de llvmpipe contre un GPU matériel. Sur Intel Arc (Mesa), la scène 3D est identique
bit à bit ; seule `ui.png` diffère (~0,6 % des pixels) parce que l'interface affiche le nom du pilote (« Vulkan 1.4... » contre
« 4.6 Core Profile... »), pas à cause du rendu.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OK_LINE = re.compile(r"Vulkan validation: 0 error/warning message\(s\)")

QUICK = [
    ("off", ["--ui", "off"]),
    ("hud", ["--ui", "hud"]),
    ("dark-widgets", ["--ui", "full", "--theme", "dark", "--tab", "widgets"]),
    ("dark-scene", ["--ui", "full", "--theme", "dark", "--tab", "scene"]),
    ("light-widgets", ["--ui", "full", "--theme", "light", "--tab", "widgets"]),
    ("post-full", ["--ui", "off", "--post", "full"]),  # REN-17/18 : HDR, exposition auto (compute), bloom, LUT, vignettage
]
FULL = [
    (f"dark-{t}", ["--ui", "full", "--theme", "dark", "--tab", t])
    for t in ("controls", "color", "windows", "layout", "themes")
] + [
    ("dropdown", ["--ui", "full", "--theme", "dark", "--tab", "widgets", "--click", "fruit"]),
    ("framed", ["--ui", "full", "--theme", "framed", "--tab", "themes"]),
    ("dialog", ["--ui", "full", "--theme", "dark", "--tab", "windows", "--click", "openDialog"]),
    ("menu", ["--ui", "full", "--theme", "dark", "--tab", "windows", "--click", "menuFile"]),
]


def run(binary, api, args, out):
    cmd = [str(binary), "--api", api, "--headless", *args, "--capture", str(out), "--capture-frames", "5"]
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=300)
    return proc.returncode, proc.stdout + proc.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "apps/demo/bin/debug/main"))
    ap.add_argument("--out", default=str(ROOT / "build/demo-parity"))
    ap.add_argument("--tolerance", type=int, default=8)
    ap.add_argument("--max-diff-percent", type=float, default=2.0)
    ap.add_argument("--full", action="store_true")
    opts = ap.parse_args()
    out = Path(opts.out)
    failed = 0
    for name, args in QUICK + (FULL if opts.full else []):
        dirs = {}
        for api in ("opengl", "vulkan"):
            dirs[api] = out / api / name
            code, log = run(opts.bin, api, args, dirs[api])
            if code != 0:
                print(f"ECHEC {name} [{api}] : code de sortie {code}")
                failed += 1
            if api == "vulkan" and not OK_LINE.search(log):
                print(f"ECHEC {name} [vulkan] : la validation Vulkan n'est pas silencieuse (ou pas active) :")
                print("\n".join(l for l in log.splitlines() if "validation" in l.lower())[:2000])
                failed += 1
        for image in ("scene", "ui"):
            cmp = subprocess.run(
                [sys.executable, str(ROOT / "tools/compare_png.py"), str(dirs["opengl"] / f"{image}.png"),
                 str(dirs["vulkan"] / f"{image}.png"), "--tolerance", str(opts.tolerance),
                 "--max-diff-percent", str(opts.max_diff_percent), "--diff-out", str(out / f"diff-{name}-{image}.png")],
                capture_output=True, text=True)
            print(f"{name}/{image}: {cmp.stdout.strip() or cmp.stderr.strip()}")
            if cmp.returncode != 0:
                failed += 1
    print("demo-vulkan-check : " + ("ECHEC" if failed else "OK (images dans la tolérance, validation Vulkan silencieuse)"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
