#!/usr/bin/env python3
"""UI-11 : tests de compilation du langage d'interface sur des entrées INVALIDES (tests/ui/cui_errors/*.cui).

Chaque fichier annonce ce qu'on attend, en commentaire :
  // expect: LIGNE:COLONNE: error: MESSAGE      cui doit échouer et écrire exactement « <fichier>:LIGNE:COLONNE: error: MESSAGE »
  // expect-cxx: LIGNE                           cui réussit, mais le C++ généré ne compile pas, et l'erreur du compilateur C++
                                                désigne la ligne LIGNE du .cui (directives #line)
Un fichier refusé par cui ne doit laisser aucun code généré.

Usage : check_cui_errors.py --cui <binaire cui> --headers <include/> --cases <dossier> --out <dossier temporaire>
                            [--cxx auto | "<commande du compilateur C++ avec ses -I>"]
--cxx auto : $CXX (ou c++) avec les en-têtes du dépôt et ceux de chaque triplet vcpkg installé (comme check_headers.py).
"""
from __future__ import annotations

import argparse
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path


def run_case(path: Path, args: argparse.Namespace) -> list[str]:
    text = path.read_text(encoding="utf-8")
    expected = [line.split("expect:", 1)[1].strip() for line in text.splitlines() if line.lstrip().startswith("// expect:")]
    expected_cxx = [int(line.split("expect-cxx:", 1)[1]) for line in text.splitlines() if line.lstrip().startswith("// expect-cxx:")]
    out = Path(args.out) / path.stem
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*"):
        old.unlink()
    cmd = [args.cui, "run", "--headers", args.headers, "--output", str(out), str(path)]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    failures: list[str] = []
    display = str(path)

    if expected:
        if result.returncode == 0:
            failures.append(f"{path.name}: cui accepted an invalid file")
        for e in expected:
            want = f"{display}:{e}"
            if want not in result.stderr.splitlines():
                failures.append(f"{path.name}: missing diagnostic\n  expected: {want}\n  stderr:\n{result.stderr}")
        left = sorted(p.name for p in out.iterdir())  # en-tête, source ET redirection au nom du .cui
        if left:
            failures.append(f"{path.name}: generated files left behind for an invalid file: {', '.join(left)}")
        return failures

    if expected_cxx:
        if result.returncode != 0:
            return [f"{path.name}: cui refused the file:\n{result.stderr}"]
        if not args.cxx:
            print(f"  {path.name}: C++ check skipped (no --cxx)")
            return []
        source = out / f"{path.stem}.gen.cpp"
        compile_cmd = shlex.split(args.cxx) + ["-fsyntax-only", f"-I{out}", str(source)]
        compiled = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
        if compiled.returncode == 0:
            return [f"{path.name}: the generated C++ compiled although an error was expected"]
        # Les directives #line écrivent le chemin avec des « / » (aussi sous Windows) ; le diagnostic doit être une ERREUR à
        # cette ligne (pas une note, et pas seulement un chemin qui contient le mot « error »).
        cxx_display = display.replace("\\", "/")
        for line in expected_cxx:
            pattern = re.compile(re.escape(cxx_display) + rf":{line}:(\d+:)? (fatal )?error:")
            if not any(pattern.search(l.replace("\\", "/")) for l in compiled.stderr.splitlines()):
                failures.append(f"{path.name}: no C++ error at {cxx_display}:{line}\n{compiled.stderr[:3000]}")
        return failures

    return [f"{path.name}: no '// expect:' or '// expect-cxx:' line"]


def auto_compiler(root: Path) -> str:
    flags = [os.environ.get("CXX", "c++"), "-std=c++23", "-DGLM_ENABLE_EXPERIMENTAL"]
    for d in ("include", "include/UI", "tests"):
        flags.append(f"-I{root / d}")
    base = Path(os.environ.get("VCPKG_INSTALLED_ROOT") or root / "vcpkg_installed")
    for inc in sorted(base.glob("*/include")):
        flags += ["-isystem", str(inc)]
    return shlex.join(flags)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cui", required=True)
    parser.add_argument("--headers", required=True)
    parser.add_argument("--cases", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--cxx", default="")
    args = parser.parse_args()
    if args.cxx == "auto":
        args.cxx = auto_compiler(Path(__file__).resolve().parent.parent)

    cases = sorted(Path(args.cases).glob("*.cui"))
    if not cases:
        print(f"no case in {args.cases}", file=sys.stderr)
        return 1
    failures: list[str] = []
    for case in cases:
        failures.extend(run_case(case, args))
    for f in failures:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"check_cui_errors: {len(cases)} case(s), {len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
