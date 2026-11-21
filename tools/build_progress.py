#!/usr/bin/env python3
"""Affichage de la compilation : barre de progression au lieu des commandes.

Lancé par le `Makefile` racine quand la sortie est un terminal (`make all-debug`, `make demo-test`…) :

    python3 tools/build_progress.py <make> [buts et variables…]

1. Plan : `make -n` (étapes à refaire) et `make -n -B` (toutes les étapes) avec ORS_PROGRESS=1 ; les règles de compilation
   annoncent `@@ors begin <cible> <affiché> <étiquette>`. Différence : les étapes à jour (sautées).
2. Exécution : `make` avec ORS_PROGRESS=1 ; les lignes `@@ors begin/end` mettent à jour la barre, le reste (avertissements,
   erreurs, sortie des tests) s'affiche au-dessus.

Désactivé par `V=1` (commandes complètes), `ORS_NO_PROGRESS=1`, ou une sortie qui n'est pas un terminal (CI) : le Makefile appelle
alors make directement. Sans dépendance hors de la bibliothèque standard.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import threading
import time

MARK = re.compile(r"@@ors (begin|end) (\S+)(?: (\S+) (.*?))?'?$")
# Bruit de make : entrée / sortie de répertoire, « rien à faire », et les commandes des sous-make (`make -f mk/engine.mk …`).
# Les erreurs (`make: *** …`) restent affichées.
MAKE_NOISE = re.compile(r"^(?:\S*make(?:\.exe)?|mingw32-make)(?:\[\d+\])?\s*:\s*(?:on entre|on quitte|Entering|Leaving|Nothing|Rien)"
                        r"|^(?:\S*/)?(?:g?make|mingw32-make)(?:\.exe)? -")
# Buts dont le plan (make -n) exécuterait quand même des commandes : une ligne de recette qui contient $(MAKE) s'exécute sous
# -n (`run` lance le programme). Pour eux, pas de plan : la barre compte seulement ce qui est compilé.
MAKE_FAILED = re.compile(r"\*\*\* \[(?:[^\]]*?:\s*)?(\S+)\] (?:Erreur|Error)")
NO_PLAN = re.compile(r"run|capture|e2e|install|clean|check|format|dist|engine-tool|benchmark|deps")

USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
GREEN, DIM, BOLD, RED, RESET = ("\033[32m", "\033[2m", "\033[1m", "\033[31m", "\033[0m") if USE_COLOR else ("",) * 5


def plan(make_cmd: list[str], env: dict[str, str]) -> tuple[set[str], set[str]]:
    """(étapes à refaire, toutes les étapes) d'après make -n et make -n -B. Vide si le plan échoue."""

    def dry(extra: list[str]) -> set[str]:
        try:
            out = subprocess.run(make_cmd[:1] + ["-n", "--no-print-directory"] + extra + make_cmd[1:], env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors="replace",
                                 timeout=120).stdout
        except (OSError, subprocess.SubprocessError):
            return set()
        return {m.group(2) for m in map(MARK.search, out.splitlines()) if m and m.group(1) == "begin"}

    result: dict[str, set[str]] = {}
    threads = [threading.Thread(target=lambda k=k, e=e: result.__setitem__(k, dry(e))) for k, e in (("stale", []), ("all", ["-B"]))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    stale, total = result.get("stale", set()), result.get("all", set())
    return stale, total | stale


class Display:
    def __init__(self, stale: set[str], total: set[str]) -> None:
        self.todo = set(stale)          # à faire (connu par le plan, ou découvert en cours de route)
        self.skipped = len(total - stale)
        self.running: dict[str, str] = {}  # cible -> texte affiché
        self.done: set[str] = set()
        self.failed: list[str] = []
        self.lines_drawn = 0
        self.start = time.monotonic()
        self.last_draw = 0.0
        self.lock = threading.Lock()

    def width(self) -> int:
        return max(40, shutil.get_terminal_size((100, 20)).columns)

    def clear(self) -> None:
        if self.lines_drawn:
            sys.stdout.write("\r\033[K" + "\033[1A\033[K" * (self.lines_drawn - 1))
            self.lines_drawn = 0

    def draw(self, force: bool = False) -> None:
        now = time.monotonic()
        if not force and now - self.last_draw < 0.05:
            return
        self.last_draw = now
        self.clear()
        if not self.running and not (self.todo - self.done):
            sys.stdout.flush()
            return  # rien en cours : pas de barre (sortie des tests, programme lancé…)
        total = len(self.todo)
        done = len(self.done)
        width = self.width()
        bar_w = max(10, min(40, width - 60))
        filled = int(bar_w * done / total) if total else bar_w
        pct = int(100 * done / total) if total else 100
        remaining = max(0, total - done - len(self.running) - len(self.failed))
        bar = f"{GREEN}{'█' * filled}{DIM}{'░' * (bar_w - filled)}{RESET}"
        head = (f"[{bar}] {BOLD}{done}/{total}{RESET} {pct:3d} %  compilés {done} · en cours {len(self.running)} · "
                f"restants {remaining} · à jour {self.skipped}  {DIM}{now - self.start:5.1f} s{RESET}")
        current = ", ".join(self.running.values())
        current_line = f"{DIM}en cours :{RESET} {current}" if current else ""
        if len(current_line) > width + len(DIM) + len(RESET):
            current_line = current_line[: width + len(DIM) + len(RESET) - 1] + "…"
        sys.stdout.write(head + ("\n" + current_line if current_line else ""))
        self.lines_drawn = 2 if current_line else 1
        sys.stdout.flush()

    def message(self, text: str) -> None:
        self.clear()
        sys.stdout.write(text + "\n")
        self.draw(force=True)

    def begin(self, target: str, shown: str, label: str) -> None:
        self.todo.add(target)
        self.running[target] = shown if label.startswith(("CXX", "CC")) else f"{label.split()[0]} {shown}"
        self.draw()

    def end(self, target: str) -> None:
        self.running.pop(target, None)
        self.done.add(target)
        self.draw()

    def fail(self, target: str) -> None:
        shown = self.running.pop(target, None)
        if shown is not None:
            self.failed.append(shown)

    def summary(self, code: int) -> None:
        self.clear()
        elapsed = time.monotonic() - self.start
        if code == 0:
            sys.stdout.write(f"{GREEN}✔{RESET} {len(self.done)} compilé(s), {self.skipped} à jour, {elapsed:.1f} s\n")
        else:
            failed = ", ".join(self.failed or list(self.running.values()))
            sys.stdout.write(f"{RED}✘ échec{RESET} (code {code}) après {len(self.done)} compilé(s)"
                             + (f" ; en échec : {failed}" if failed else "") + f", {elapsed:.1f} s\n")
        sys.stdout.flush()


def main(argv: list[str]) -> int:
    if not argv:
        print("usage: build_progress.py <make> [buts et variables…]", file=sys.stderr)
        return 2
    make_cmd = argv
    if not sys.stdout.isatty():  # sortie redirigée : make habituel, sans marqueurs
        os.execvpe(make_cmd[0], make_cmd, dict(os.environ, ORS_NO_PROGRESS="1"))
    env = dict(os.environ, ORS_PROGRESS="1")
    goals = [a for a in make_cmd[1:] if not a.startswith("-") and "=" not in a]
    stale: set[str] = set()
    total: set[str] = set()
    if not any(NO_PLAN.search(g) for g in goals):
        plan_env = dict(env)
        plan_env.pop("MAKEFLAGS", None)  # pas de jobserver ni de -j pour le plan
        plan_env["MAKEFLAGS"] = " ".join(f for f in os.environ.get("MAKEFLAGS", "").split()
                                         if "=" in f and not f.startswith("-"))  # garde les variables (SANITIZE=…)
        stale, total = plan(make_cmd, plan_env)
    display = Display(stale, total)
    display.draw(force=True)

    proc = subprocess.Popen(make_cmd[:1] + ["--no-print-directory"] + make_cmd[1:], env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, close_fds=False, bufsize=1, text=True, errors="replace")
    assert proc.stdout is not None
    try:
        for raw in proc.stdout:
            line = raw.rstrip("\n")
            m = MARK.search(line)
            if m and line.lstrip("'").startswith("@@ors"):
                if m.group(1) == "begin":
                    display.begin(m.group(2), m.group(3) or m.group(2), m.group(4) or "")
                else:
                    display.end(m.group(2))
                continue
            if MAKE_NOISE.match(line):
                continue
            failed = MAKE_FAILED.search(line)
            if failed:
                display.fail(failed.group(1))
            display.message(line)
        code = proc.wait()
    except KeyboardInterrupt:
        proc.wait()
        code = 130
    display.summary(code)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
