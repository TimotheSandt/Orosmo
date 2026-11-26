#!/usr/bin/env python3
"""Generate a Clang JSON Compilation Database (compile_commands.json) for Orosmo.

Supports two layouts automatically (or via --mode):
  - repo:    Orosmo engine repository (src/, apps/demo/src/, tests/, apps/demo/Generated/)
  - project: Standalone game project created by `orosmo init` (src/, Generated/ ; engine headers and deps/ from --engine-dir)

No external tools (such as `bear`) are required.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import sys
from pathlib import Path


def detect_platform_and_triplet() -> tuple[str, str, list[str], list[str]]:
    sys_name = platform.system()
    machine = platform.machine().lower()
    if sys_name == "Windows":
        return (
            "Windows",
            "x64-mingw-static",
            ["-m64"],
            ["-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN"],
        )
    if sys_name == "Darwin":
        if machine in ("arm64", "aarch64"):
            return ("Darwin", "arm64-osx", [], [])
        return ("Darwin", "x64-osx", ["-m64"], [])
    if machine in ("arm64", "aarch64"):
        return ("Linux", "arm64-linux", [], [])
    return ("Linux", "x64-linux", ["-m64"], [])


def build_flags(build_type: str, arch_flags: list[str], platform_defs: list[str], is_cpp: bool) -> list[str]:
    base = [
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Werror",
        "-ffp-contract=off",
        *arch_flags,
    ]
    if build_type == "debug":
        base += ["-O1", "-g", "-DDEBUG"]
    elif build_type == "dev":
        base += ["-g3", "-O0", "-DDEBUG"]
    else:
        base += ["-O3", "-DNDEBUG"]
    base += [*platform_defs, "-DGLM_ENABLE_EXPERIMENTAL"]
    return (["-std=c++23"] + base) if is_cpp else base


def iter_sources(directory: Path, exts: tuple[str, ...]) -> list[Path]:
    if not directory.is_dir():
        return []
    found: list[Path] = []
    for root, dirs, files in os.walk(directory):
        dirs.sort()
        for name in sorted(files):
            if name.endswith(exts):
                found.append(Path(root) / name)
    return sorted(found)


def detect_mode(root: Path) -> str:
    if (root / "mk" / "engine.mk").is_file() and (root / "include" / "orosmo").is_dir():
        return "repo"
    return "project"


def make_entry(
    directory: Path,
    src_file: Path,
    compiler: str,
    flags: list[str],
    includes: list[str],
    out_obj: str,
) -> dict[str, object]:
    return {
        "directory": str(directory.resolve()),
        "file": str(src_file.resolve()),
        "arguments": [compiler, *flags, *includes, "-c", str(src_file.resolve()), "-o", out_obj],
    }


def generate_repo_entries(
    root: Path,
    build_type: str,
    cxx: str,
    cc: str,
    triplet: str,
    arch_flags: list[str],
    platform_defs: list[str],
) -> list[dict[str, object]]:
    vcpkg_inc = root / "vcpkg_installed" / triplet / "include"
    inc_base = root / "include"
    engine_includes = [f"-I{inc_base.resolve()}"]
    if inc_base.is_dir():
        for child in sorted(inc_base.iterdir()):
            if child.is_dir():
                engine_includes.append(f"-I{child.resolve()}")
    engine_includes += ["-isystem", str(vcpkg_inc.resolve())]

    entries: list[dict[str, object]] = []

    # 1. Engine sources (src/)
    for src in iter_sources(root / "src", (".cpp", ".c")):
        is_cpp = src.suffix == ".cpp"
        rel = src.relative_to(root / "src")
        out_obj = str((root / "obj" / build_type / "Libraries" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                root,
                src,
                cxx if is_cpp else cc,
                build_flags(build_type, arch_flags, platform_defs, is_cpp),
                engine_includes,
                out_obj,
            )
        )

    # 2. Demo & generated CUI sources (apps/demo/src/, apps/demo/Generated/)
    demo_dir = root / "apps" / "demo"
    demo_src = demo_dir / "src"
    demo_gen = demo_dir / "Generated"
    demo_includes = [
        *engine_includes,
        f"-I{demo_gen.resolve()}",
        f"-I{demo_src.resolve()}",
    ]
    for src in iter_sources(demo_src, (".cpp", ".c")):
        is_cpp = src.suffix == ".cpp"
        rel = src.relative_to(demo_src)
        out_obj = str((demo_dir / "obj" / build_type / "src" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                demo_dir,
                src,
                cxx if is_cpp else cc,
                build_flags(build_type, arch_flags, platform_defs, is_cpp),
                demo_includes,
                out_obj,
            )
        )
    for src in iter_sources(demo_gen, (".gen.cpp",)):
        rel = src.relative_to(demo_gen)
        out_obj = str((demo_dir / "obj" / build_type / "gen" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                demo_dir,
                src,
                cxx,
                build_flags(build_type, arch_flags, platform_defs, True),
                demo_includes,
                out_obj,
            )
        )

    # 3. Tests (tests/)
    tests_dir = root / "tests"
    test_includes = [
        *demo_includes,
        f"-I{tests_dir.resolve()}",
    ]
    for src in iter_sources(tests_dir, (".cpp",)):
        rel = src.relative_to(tests_dir)
        out_obj = str((demo_dir / "obj" / build_type / "tests" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                demo_dir,
                src,
                cxx,
                build_flags(build_type, arch_flags, platform_defs, True),
                test_includes,
                out_obj,
            )
        )

    return entries


def generate_project_entries(
    root: Path,
    engine_dir: Path,
    gen_dir: Path,
    build_type: str,
    cxx: str,
    cc: str,
    triplet: str,
    arch_flags: list[str],
    platform_defs: list[str],
) -> list[dict[str, object]]:
    inc_base = engine_dir / "includes"
    if not inc_base.is_dir():
        inc_base = engine_dir / "include"
    # Third-party headers: deps/ of a release package, else vcpkg_installed/<triplet> (engine repository, older packages).
    vcpkg_inc = engine_dir / "deps" / "include"
    if not vcpkg_inc.is_dir():
        vcpkg_inc = engine_dir / "vcpkg_installed" / triplet / "include"
    src_dir = root / "src"

    includes = [f"-I{inc_base.resolve()}"]
    if inc_base.is_dir():
        for child in sorted(inc_base.iterdir()):
            if child.is_dir():
                includes.append(f"-I{child.resolve()}")
    includes += [
        "-isystem",
        str(vcpkg_inc.resolve()),
        f"-I{gen_dir.resolve()}",
        f"-I{src_dir.resolve()}",
    ]

    entries: list[dict[str, object]] = []
    for src in iter_sources(src_dir, (".cpp", ".c")):
        is_cpp = src.suffix == ".cpp"
        rel = src.relative_to(src_dir)
        out_obj = str((root / "obj" / build_type / "src" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                root,
                src,
                cxx if is_cpp else cc,
                build_flags(build_type, arch_flags, platform_defs, is_cpp),
                includes,
                out_obj,
            )
        )
    for src in iter_sources(gen_dir, (".gen.cpp",)):
        rel = src.relative_to(gen_dir)
        out_obj = str((root / "obj" / build_type / "gen" / rel).with_suffix(".o").resolve())
        entries.append(
            make_entry(
                root,
                src,
                cxx,
                build_flags(build_type, arch_flags, platform_defs, True),
                includes,
                out_obj,
            )
        )
    return entries


def main(argv: list[str] | None = None) -> int:
    _, default_triplet, arch_flags, platform_defs = detect_platform_and_triplet()
    parser = argparse.ArgumentParser(description="Generate compile_commands.json for Orosmo.")
    parser.add_argument("--root", default=".", help="Project or repository root directory")
    parser.add_argument("--mode", choices=("auto", "repo", "project"), default="auto")
    parser.add_argument("--engine-dir", default="orosmo", help="Engine SDK directory in project mode")
    parser.add_argument("--gen-dir", default="Generated", help="CUI output directory of the project (project mode)")
    parser.add_argument("--build-type", choices=("debug", "dev", "release"), default="debug")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    parser.add_argument("--triplet", default=default_triplet)
    parser.add_argument("--out", default="", help="Output path (default: <root>/compile_commands.json)")
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    cxx = args.cxx or "g++"
    cc = args.cc or "gcc"
    mode = detect_mode(root) if args.mode == "auto" else args.mode
    if mode == "repo":
        entries = generate_repo_entries(root, args.build_type, cxx, cc, args.triplet, arch_flags, platform_defs)
    else:
        engine_dir = Path(args.engine_dir)
        if not engine_dir.is_absolute():
            engine_dir = (root / engine_dir).resolve()
        gen_dir = Path(args.gen_dir)
        if not gen_dir.is_absolute():
            gen_dir = (root / gen_dir).resolve()
        entries = generate_project_entries(
            root, engine_dir, gen_dir, args.build_type, cxx, cc, args.triplet, arch_flags, platform_defs
        )

    out_path = Path(args.out) if args.out else (root / "compile_commands.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8") as f:
        json.dump(entries, f, indent=2)
        f.write("\n")

    print(f"compile_commands: wrote {len(entries)} entries ({mode}, {args.build_type}) -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
