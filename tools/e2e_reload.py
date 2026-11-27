#!/usr/bin/env python3
"""GAME-06 (A79 lot L7), partie de `make e2e-init` : modifier un .cpp du jeu, reconstruire et recharger, temps mesuré.

Le projet donné est déjà construit en debug, avec tools/e2e/reload_host.cpp pour src/main.cpp. Le script lance le programme,
modifie src/game/game.cpp, lance `orosmo build --game`, attend que le programme ait rechargé la bibliothèque, vérifie que
l'exécutable n'a pas été refait, écrit les temps dans `--record` et échoue si le total dépasse `--max` secondes.
La source modifiée est remise à son contenu d'origine à la fin.
"""
import argparse, os, queue, subprocess, sys, threading, time

BEFORE = "s.angleDegrees += s.speedDegrees"
AFTER = "s.angleDegrees -= s.speedDegrees"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True)
    ap.add_argument("--tool", required=True, help="outil orosmo")
    ap.add_argument("--exe", default="bin/debug/main", help="programme, relatif au projet")
    ap.add_argument("--max", type=float, default=5.0, help="limite en secondes : modification -> code rechargé")
    ap.add_argument("--record", help="fichier où écrire les mesures")
    a = ap.parse_args()
    project = os.path.abspath(a.project)
    exe = os.path.join(project, *a.exe.split("/"))
    source = os.path.join(project, "src", "game", "game.cpp")
    with open(source, "rb") as f:
        code = f.read()
    if BEFORE.encode() not in code:
        sys.exit(f"e2e_reload : « {BEFORE} » introuvable dans {source}")

    def build_game():
        build = subprocess.run([a.tool, "build", "--game", "-C", project], capture_output=True, text=True, errors="replace")
        if build.returncode != 0:
            sys.exit(f"e2e_reload : `orosmo build --game` a échoué ({build.returncode})\n{build.stdout}{build.stderr}")

    build_game()  # la bibliothèque correspond à la source, même si le script a déjà tourné sur ce projet
    program = subprocess.Popen([exe], cwd=project, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               errors="replace")
    lines = queue.Queue()

    def read():
        for line in program.stdout:
            lines.put(line.rstrip("\r\n"))
        lines.put(None)

    threading.Thread(target=read, daemon=True).start()

    def expect(wanted, timeout):
        seen = []
        end = time.monotonic() + timeout
        while True:
            try:
                line = lines.get(timeout=max(0.0, end - time.monotonic()))
            except queue.Empty:
                line = None
            if line == wanted:
                return
            if line is None or line.startswith("error:"):
                sys.exit(f"e2e_reload : « {wanted} » attendu du programme\n" + "\n".join(seen + ([line] if line else [])))
            seen.append(line)

    try:
        expect("ready", 60)
        exe_time = os.stat(exe).st_mtime_ns
        time.sleep(1.1)  # dates de fichiers à la seconde près sur certains systèmes : make doit voir la modification
        start = time.monotonic()
        with open(source, "wb") as f:
            f.write(code.replace(BEFORE.encode(), AFTER.encode()))
        build_game()
        built = time.monotonic()
        expect("reloaded", 60)
        reloaded = time.monotonic()
        if program.wait(timeout=60) != 0:
            sys.exit(f"e2e_reload : le programme sort avec {program.returncode}")
        if os.stat(exe).st_mtime_ns != exe_time:
            sys.exit("e2e_reload : l'exécutable a été refait (seule la bibliothèque de jeu devait l'être)")
    finally:
        if program.poll() is None:
            program.kill()
        with open(source, "wb") as f:
            f.write(code)

    total = reloaded - start
    report = (f"build --game : {built - start:.2f} s ; modification -> code rechargé : {total:.2f} s "
              f"(limite {a.max:g} s)")
    print("e2e_reload : " + report)
    if a.record:
        with open(a.record, "w", encoding="utf-8") as f:
            f.write(report + "\n")
    if total > a.max:
        sys.exit(f"e2e_reload : {total:.2f} s dépasse la limite de {a.max:g} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
