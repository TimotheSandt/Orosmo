#!/usr/bin/env python3
"""Fichiers sources d'une page du site, extraits de ses blocs de code (A79 lot L14 : le tutoriel 3D compilé par `make e2e-init`).

Un bloc ```cpp ou ```text dont le titre nomme un fichier (title="src/main.cpp") crée ce fichier ; un titre qui finit par
« (suite) » (title="src/main.cpp (suite)") ajoute le bloc au fichier de ce nom, déjà commencé plus haut. Les blocs sans titre et
les autres langages (bash...) sont ignorés : ce sont des commandes ou des extraits. Les fichiers sont écrits sous --out
(chemins relatifs, sans « .. »). `--remove` efface d'abord des fichiers du dossier (ceux que la page fait supprimer).

    python3 tools/extract_doc_code.py site/src/content/docs/demarrer/tutoriel-jeu-3d.md --out build/e2e/tutorial \
        --remove src/HudView.cui --remove src/hud_state.hpp

Sort avec 1 (message) si un bloc « (suite) » n'a pas de début, si un fichier est commencé deux fois, ou si aucun fichier n'est
extrait.
"""
import argparse
import pathlib
import re
import sys

FENCE = re.compile(r'^```(\w+)(.*)$')
TITLE = re.compile(r'title="([^"]+)"')
CONTINUED = " (suite)"
LANGUAGES = ("cpp", "text")


def extract(markdown):
    """Rend {chemin: texte} dans l'ordre de la page ; ValueError pour un bloc mal titré."""
    files = {}
    current = None
    lines = []
    for number, line in enumerate(markdown.splitlines(), start=1):
        if current is None:
            fence = FENCE.match(line)
            if not fence:
                continue
            title = TITLE.search(fence.group(2))
            if fence.group(1) not in LANGUAGES or not title:
                current = ""  # bloc ignoré, lu jusqu'à sa fin
            else:
                current = title.group(1)
                if current.endswith(CONTINUED):
                    current = current[: -len(CONTINUED)]
                    if current not in files:
                        raise ValueError(f"line {number}: « {current}{CONTINUED} » before the start of {current}")
                elif current in files:
                    raise ValueError(f"line {number}: {current} started twice")
                else:
                    files[current] = ""
            lines = []
        elif line.startswith("```"):
            if current:
                files[current] += "".join(l + "\n" for l in lines)
            current = None
        else:
            lines.append(line)
    if current is not None:
        raise ValueError("unterminated code block")
    for path in files:
        p = pathlib.PurePosixPath(path)
        if p.is_absolute() or ".." in p.parts:
            raise ValueError(f"{path}: the path must be relative, without ..")
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("page")
    parser.add_argument("--out", required=True)
    parser.add_argument("--remove", action="append", default=[])
    a = parser.parse_args(argv)
    try:
        files = extract(pathlib.Path(a.page).read_text(encoding="utf-8"))
    except ValueError as e:
        print(f"extract_doc_code : {a.page}: {e}", file=sys.stderr)
        return 1
    if not files:
        print(f"extract_doc_code : {a.page}: no titled cpp/text block", file=sys.stderr)
        return 1
    out = pathlib.Path(a.out)
    for path in a.remove:
        (out / path).unlink(missing_ok=True)
    for path, text in files.items():
        target = out / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
        print(f"extract_doc_code : {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
