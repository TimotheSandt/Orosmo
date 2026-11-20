#!/usr/bin/env python3
"""Isolation des API (docs/03 §8.2, règle 4 de CLAUDE.md).

Signale tout usage de gl*, GL_*, Vk*/vk*, MTL*, <GLFW/...>, <windows.h>, <vulkan/...>, <glad/...> hors des
dossiers autorisés (tools/layers.json : api_isolation.allowed). Commentaires et chaînes ignorés.
Mode avertissement par défaut (exit 0) ; `--strict` sort avec 1 ; `--baseline FICHIER` n'échoue que sur ce
qui n'y figure pas ; `--write-baseline FICHIER` enregistre l'état actuel.
"""
import argparse, json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PATTERNS = [
    re.compile(r"\bgl[A-Z][A-Za-z0-9_]*\b"),
    re.compile(r"\bGL_[A-Z0-9_]+\b"),
    re.compile(r"\bGL(?:u?int|u?byte|u?short|enum|sizei|boolean|float|double|bitfield|sync|intptr|sizeiptr)\b"),
    re.compile(r"\bVk[A-Z][A-Za-z0-9_]*\b"),
    re.compile(r"\bvk[A-Z][A-Za-z0-9_]*\b"),
    re.compile(r"\bVK_[A-Z0-9_]+\b"),
    re.compile(r"\bMTL[A-Za-z0-9_]+\b"),
    re.compile(r"\bglfw[A-Z][A-Za-z0-9_]*\b"),
    re.compile(r"\bGLFW[A-Za-z0-9_]*\b"),
    re.compile(r"#\s*include\s*<(?:GLFW/|windows\.h|vulkan/|glad/|GL/|d3d)"),
]
# Mode --graphics-only (M2 B6) : seules les API graphiques (OpenGL, Vulkan, Metal, glad) sont surveillées ; GLFW et <windows.h> le seront
# quand `platform::Window` existera (lot C). Mêmes motifs que ci-dessus, sans GLFW ni windows.h.
GRAPHICS_ONLY_PATTERNS = PATTERNS[:7] + [re.compile(r"#\s*include\s*<(?:vulkan/|glad/|GL/|d3d)")]
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', re.S)


def strip_keep_includes(text):
    # supprime commentaires et chaînes en gardant les numéros de ligne ; les #include <...> restent visibles
    def repl(m):
        s = m.group(0)
        return "\n" * s.count("\n") if not s.startswith('"') else '""'
    out = []
    for line in text.split("\n"):
        out.append(line)
    # traiter bloc par bloc pour conserver les lignes
    return STRIP.sub(repl, text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--strict", action="store_true")
    ap.add_argument("--graphics-only", action="store_true", help="ne surveille que gl*, GL_*, Vk*, MTL*, glad, vulkan (pas GLFW ni windows.h)")
    ap.add_argument("--baseline")
    ap.add_argument("--write-baseline")
    a = ap.parse_args()
    patterns = GRAPHICS_ONLY_PATTERNS if a.graphics_only else PATTERNS
    cfg = json.load(open(os.path.join(ROOT, "tools", "layers.json"), encoding="utf-8"))
    allowed = tuple(cfg["api_isolation"]["allowed"])
    found = {}
    files = 0
    for top in cfg["scan"]:
        for dp, _, names in os.walk(os.path.join(ROOT, top)):
            for n in sorted(names):
                if not n.endswith((".h", ".hpp", ".cpp", ".c")):
                    continue
                rel = os.path.relpath(os.path.join(dp, n), ROOT).replace(os.sep, "/")
                if "/Generated/" in rel or rel.startswith("apps/demo/orosmo/") or rel.startswith(allowed):
                    continue
                files += 1
                text = open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace").read()
                for ln, line in enumerate(strip_keep_includes(text).split("\n"), 1):
                    for p in patterns:
                        m = p.search(line)
                        if m:
                            found.setdefault(rel, []).append((ln, m.group(0)))
                            break
    lines = []
    for rel in sorted(found):
        toks = sorted({t for _, t in found[rel]})
        lines.append(f"{rel}: {len(found[rel])} occurrence(s): {', '.join(toks[:6])}{' …' if len(toks) > 6 else ''}")
    for l in lines:
        print("WARNING api-isolation:", l)
    print(f"check_api_isolation: {files} fichiers hors zones autorisées, {len(lines)} fichier(s) en infraction, "
          f"{sum(len(v) for v in found.values())} occurrence(s)")
    if a.write_baseline:
        open(a.write_baseline, "w", encoding="utf-8").write("\n".join(sorted(found)) + "\n")
    new = sorted(found)
    if a.baseline:
        known = set(open(a.baseline, encoding="utf-8").read().split("\n"))
        new = [f for f in new if f not in known]
        print(f"check_api_isolation: {len(new)} nouveau(x) fichier(s) hors baseline")
    return 1 if (a.strict or a.baseline) and new else 0


if __name__ == "__main__":
    sys.exit(main())
