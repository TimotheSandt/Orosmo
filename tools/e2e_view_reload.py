#!/usr/bin/env python3
"""UI-08 (A79 lot L8), partie de `make e2e-init` : un .cui enregistré pendant que le programme tourne change l'arbre, sans
redémarrage ; un .cui invalide garde l'ancienne vue et affiche l'erreur.

Le projet donné est déjà construit en debug, avec tools/e2e/view_reload_host.cpp pour src/main.cpp et la vue src/HudView.cui du
gabarit. Le script lance le programme (qui lance lui-même la reconstruction, `--build`), écrit un HudView.cui invalide (le
programme doit signaler l'échec et garder le même nombre de widgets), puis un HudView.cui valide avec un widget de plus (le
programme doit recréer l'arbre : un widget de plus, focus gardé sur « reset »). Il vérifie que l'exécutable n'a pas été refait,
écrit les temps dans `--record` et échoue si « .cui enregistré -> vue recréée » dépasse `--max` secondes. La vue est remise à son
contenu d'origine à la fin.
"""
import argparse, os, queue, subprocess, sys, threading, time

ANCHOR = 'Button("Reset counter")'
BROKEN = '        Lable("typo in the component name")\n'
ADDED = '        Label("Added while running").id("added")\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True)
    ap.add_argument("--build", required=True, help="commande de reconstruction passée au programme (ex. « orosmo build --game »)")
    ap.add_argument("--exe", default="bin/debug/main", help="programme, relatif au projet")
    ap.add_argument("--max", type=float, default=5.0, help="limite en secondes : .cui enregistré -> vue recréée")
    ap.add_argument("--record", help="fichier où écrire les mesures")
    a = ap.parse_args()
    project = os.path.abspath(a.project)
    exe = os.path.join(project, *a.exe.split("/"))
    view = os.path.join(project, "src", "HudView.cui")
    with open(view, "r", encoding="utf-8", newline="") as f:
        original = f.read()
    lines = original.splitlines(keepends=True)
    at = next((i for i, line in enumerate(lines) if ANCHOR in line), None)
    if at is None:
        sys.exit(f"e2e_view_reload : « {ANCHOR} » introuvable dans {view}")

    def with_line(extra):
        return "".join(lines[:at] + [extra] + lines[at:])

    program = subprocess.Popen([exe, "--build", a.build], cwd=project, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace")
    out = queue.Queue()

    def read():
        for line in program.stdout:
            out.put(line.rstrip("\r\n"))
        out.put(None)

    threading.Thread(target=read, daemon=True).start()

    def expect(prefix, timeout):
        seen = []
        end = time.monotonic() + timeout
        while True:
            try:
                line = out.get(timeout=max(0.0, end - time.monotonic()))
            except queue.Empty:
                line = None
            if line is not None and line.startswith(prefix):
                return line
            if line is None or line.startswith("error:"):
                sys.exit(f"e2e_view_reload : « {prefix} » attendu du programme\n" + "\n".join(seen + ([line] if line else [])))
            seen.append(line)

    def save(text):
        time.sleep(1.1)  # dates de fichiers à la seconde près sur certains systèmes : make doit voir la modification
        with open(view, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        return time.monotonic()

    try:
        initial = int(expect("widgets ", 60).split()[1])
        expect("ready", 10)
        exe_time = os.stat(exe).st_mtime_ns

        save(with_line(BROKEN))
        failed = expect("build-failed", 120)
        if int(failed.split()[2]) != initial:
            sys.exit(f"e2e_view_reload : l'ancienne vue n'est pas gardée après l'erreur ({failed})")

        start = save(with_line(ADDED))
        rebuilt = expect("rebuilt", 120)
        done = time.monotonic()
        parts = rebuilt.split()
        if int(parts[2]) != initial + 1:
            sys.exit(f"e2e_view_reload : un widget de plus attendu ({initial} -> {initial + 1}) : {rebuilt}")
        if parts[4] != "reset":
            sys.exit(f"e2e_view_reload : le focus n'est pas gardé ({rebuilt})")
        if program.wait(timeout=60) != 0:
            sys.exit(f"e2e_view_reload : le programme sort avec {program.returncode}")
        if os.stat(exe).st_mtime_ns != exe_time:
            sys.exit("e2e_view_reload : l'exécutable a été refait (seule la bibliothèque de jeu devait l'être)")
    finally:
        if program.poll() is None:
            program.kill()
        with open(view, "w", encoding="utf-8", newline="") as f:
            f.write(original)

    total = done - start
    report = (f".cui invalide : ancienne vue gardée ({initial} widgets), erreur signalée ; .cui enregistré -> vue recréée "
              f"({initial + 1} widgets, focus gardé) : {total:.2f} s (limite {a.max:g} s)")
    print("e2e_view_reload : " + report)
    if a.record:
        with open(a.record, "w", encoding="utf-8") as f:
            f.write(report + "\n")
    if total > a.max:
        sys.exit(f"e2e_view_reload : {total:.2f} s dépasse la limite de {a.max:g} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
