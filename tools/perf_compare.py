#!/usr/bin/env python3
"""Mesure comparée OpenGL / Vulkan de la démo (M4, RHI-14).

    perf_compare.py [--bin apps/demo/bin/release/Orosmo] [--runs 7] [--frames 600] [--ui off|hud|full] [--modes headless,windowed]
                    [--api opengl,vulkan] [--out FICHIER.json]

Lance la démo `runs` fois par backend (en alternance OpenGL / Vulkan pour répartir la dérive thermique) avec `--capture-frames N` :
- `headless` : appareil hors écran, pas de présentation, pas de fenêtre ;
- `windowed` : fenêtre réelle, chaîne d'échange, sans synchronisation verticale (il faut un affichage).
Chaque exécution journalise (apps/demo/src/main.cpp) la durée de la boucle et les moyennes du Profiler : `Render` (GPU, requêtes
de temps), les passes du graphe de l'image `RenderGraph/scene`, `RenderGraph/upscale`, `RenderGraph/ui`, `RenderGraph/present`
(GPU, une mesure par passe depuis M5 ; avant : `RenderScene`, `Upscale`, `UIUpscale`) et `SwapBuffers` (CPU). Le script en tire, par exécution, le temps de
frame CPU = durée de la boucle / images (`frameCpuMs`, premier affichage compris), le temps CPU médian par image, celui
de la première image et le maximum (`frameMedian`, `firstFrame`, `frameMax` : journal `Perf: frameMedianMs=`...), puis la médiane, le minimum, le maximum et l'écart interquartile sur les exécutions.
La première exécution de chaque couple est écartée (caches de pipelines et de shaders froids). Utiliser un binaire RELEASE
(couches de validation coupées) : le binaire de debug mesure surtout la validation.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PERF = re.compile(r"Perf: (?:([\w/.-]+) avgMs|(loop|frameMedian|firstFrame|frameMax)Ms)=([0-9.eE+-]+)")
DEVICE = re.compile(r"Graphics device: (.*?)(?:\x1b\[[0-9;]*m)*$", re.M)
METRICS = ["frameCpuMs", "frameMedian", "firstFrame", "frameMax", "Render", "RenderGraph/scene", "RenderGraph/upscale", "RenderGraph/ui", "RenderGraph/present", "SwapBuffers"]


def run_once(binary, api, mode, frames, ui, scratch):
    cmd = [str(binary), "--api", api, "--strict-backend", "--ui", ui, "--gpu-timings", "on", "--capture", str(scratch), "--capture-frames", str(frames)]
    if mode == "headless":
        cmd.append("--headless")
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=600)
    log = proc.stdout + proc.stderr
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} : code {proc.returncode}\n{log[-1500:]}")
    values = {}
    for name, loop, value in PERF.findall(log):
        values[name or loop] = float(value)
    values["frameCpuMs"] = values.pop("loop") / frames if "loop" in values else float("nan")
    device = DEVICE.search(log)
    return values, (device.group(1).strip() if device else "?")


def quartiles(xs):
    s = sorted(xs)
    if len(s) < 4:
        return s[0], s[-1]
    q = statistics.quantiles(s, n=4)
    return q[0], q[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=str(ROOT / "apps/demo/bin/release/Orosmo"))
    ap.add_argument("--runs", type=int, default=7)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--ui", default="off", choices=["off", "hud", "full"])
    ap.add_argument("--modes", default="headless,windowed")
    ap.add_argument("--api", default="opengl,vulkan")
    ap.add_argument("--out", default="")
    opts = ap.parse_args()
    apis = opts.api.split(",")
    result = {}
    devices = {}
    with tempfile.TemporaryDirectory() as scratch:
        for mode in opts.modes.split(","):
            samples = {api: [] for api in apis}
            for i in range(opts.runs + 1):  # +1 : exécution de chauffe écartée
                for api in apis:
                    values, device = run_once(opts.bin, api, mode, opts.frames, opts.ui, scratch)
                    devices[api] = device
                    if i > 0:
                        samples[api].append(values)
            for api in apis:
                stats = {}
                for metric in METRICS:
                    xs = [s[metric] for s in samples[api] if metric in s]
                    if xs:
                        q1, q3 = quartiles(xs)
                        stats[metric] = {"median": statistics.median(xs), "min": min(xs), "max": max(xs), "q1": q1, "q3": q3, "n": len(xs)}
                result[f"{mode}/{api}"] = stats
    print(f"frames={opts.frames} runs={opts.runs} ui={opts.ui}")
    for api, device in devices.items():
        print(f"{api}: {device}")
    print(f"{'mode/api':<18}{'métrique':<13}{'médiane':>9}{'min':>9}{'max':>9}{'IQR':>9}")
    for key, stats in result.items():
        for metric, st in stats.items():
            print(f"{key:<18}{metric:<13}{st['median']:9.3f}{st['min']:9.3f}{st['max']:9.3f}{st['q3'] - st['q1']:9.3f}")
    if opts.out:
        Path(opts.out).write_text(json.dumps({"devices": devices, "frames": opts.frames, "runs": opts.runs, "ui": opts.ui, "results": result}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
