#!/usr/bin/env python3
"""Vérification des couches (docs/03 §1 et §8.1).

Lit les #include de include/, src/ et apps/ et signale tout fichier qui inclut un fichier d'une couche
SUPÉRIEURE (table tools/layers.json). Mode avertissement par défaut : le code n'est pas encore migré,
le script affiche les violations et sort avec 0. `--strict` sort avec 1 s'il y en a ; `--baseline FICHIER`
n'échoue que sur les violations absentes du fichier (liste de non-régression).
"""
import argparse, fnmatch, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
INC = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def load():
    with open(os.path.join(ROOT, "tools", "layers.json"), encoding="utf-8") as f:
        return json.load(f)


def layer_of(cfg, rel):
    for pat, layer in cfg["rules"]:
        if fnmatch.fnmatch(rel, pat):
            return layer
    return None


def resolve(cfg, src_rel, inc):
    """Retrouve le fichier inclus : dossier du fichier, puis racines d'inclusion."""
    cands = [os.path.normpath(os.path.join(os.path.dirname(src_rel), inc))]
    cands += [os.path.normpath(os.path.join(r, inc)) for r in cfg["include_roots"]]
    for c in cands:
        if os.path.isfile(os.path.join(ROOT, c)):
            return c.replace(os.sep, "/")
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--baseline")
    ap.add_argument("--write-baseline")
    a = ap.parse_args()
    cfg = load(); lv = cfg["levels"]
    viol, unmapped, files = [], set(), 0
    for top in cfg["scan"]:
        for dp, _, names in os.walk(os.path.join(ROOT, top)):
            for n in sorted(names):
                if not n.endswith((".h", ".hpp", ".cpp", ".c")):
                    continue
                rel = os.path.relpath(os.path.join(dp, n), ROOT).replace(os.sep, "/")
                if "/Generated/" in rel or "/engine/" in rel:
                    continue
                src_layer = layer_of(cfg, rel)
                files += 1
                if src_layer is None:
                    unmapped.add(rel); continue
                with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as f:
                    for ln, line in enumerate(f, 1):
                        m = INC.match(line)
                        if not m:
                            continue
                        tgt = resolve(cfg, rel, m.group(1))
                        if tgt is None:
                            continue
                        tl = layer_of(cfg, tgt)
                        if tl is None:
                            unmapped.add(tgt); continue
                        if lv[tl] > lv[src_layer]:
                            viol.append(f"{rel}:{ln}: {src_layer} -> {tl}: {tgt}")
    viol = sorted(set(viol))
    for v in viol:
        print("WARNING layers:", v)
    for u in sorted(unmapped):
        print("WARNING layers: fichier sans couche (ajouter une règle):", u)
    print(f"check_layers: {files} fichiers, {len(viol)} violation(s), {len(unmapped)} sans couche")
    if a.write_baseline:
        with open(a.write_baseline, "w", encoding="utf-8") as f:
            f.write("\n".join(viol) + "\n")
    new = viol
    if a.baseline:
        known = set(open(a.baseline, encoding="utf-8").read().split("\n"))
        new = [v for v in viol if v not in known]
        print(f"check_layers: {len(new)} nouvelle(s) violation(s) hors baseline")
    return 1 if (a.strict or a.baseline) and (new or unmapped) else 0


if __name__ == "__main__":
    sys.exit(main())
