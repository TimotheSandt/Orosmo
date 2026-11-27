#!/usr/bin/env python3
"""Règle 13 vérifiée sur le binaire (GAME-02, A79 lot L5) : la bibliothèque de jeu ne garde aucun état.

Refuse tout symbole DÉFINI dans une section modifiable ou locale au thread :
  ELF     .data, .data.<x> (sauf .data.rel.ro*), .bss*, .tdata*, .tbss*
  PE      .data*, .bss*, .tls*
  Mach-O  (__DATA,__data|__bss|__common|__thread_vars|__thread_data|__thread_bss)
Les constantes en lecture seule (.rodata/.rdata, __DATA_CONST, `constexpr`) et les tables relogées puis protégées
(.data.rel.ro : vtables, typeinfo, `constexpr OrosmoGameApi`) restent permises : elles ne portent aucun état.

Usage :
  check_game_static.py BINAIRE...    contrôle des bibliothèques (ou fichiers objets) donnés (Makefile du modèle et de la démo)
  check_game_static.py               `make check` : auto-test (un jeu fautif doit être refusé, un jeu propre accepté), puis
                                     compilation et contrôle de la bibliothèque de jeu de test (tests/app/game_module/game.cpp)
Lit `nm` (GNU : --format=sysv ; macOS : nm -m). Sortie 1 si un symbole est refusé.
"""
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Liste blanche courte : symboles du démarrage C/C++ ajoutés par l'éditeur de liens, pas par le jeu.
WHITELIST = {
    "completed.0",      # crtstuff.c (GCC) : drapeau de __do_global_dtors_aux
    "completed.1",
    "__dso_handle",     # identifiant du module pour __cxa_atexit
    "__TMC_END__",      # fin de la table de mémoire transactionnelle (crtend)
    "__bss_start", "_edata", "_end", "__data_start", "data_start",  # bornes de sections (éditeur de liens)
    "DW.ref.__gxx_personality_v0",  # pointeur vers la routine de déroulement des exceptions (GCC, ELF)
    "__dyld_private",   # Mach-O : mot réservé à dyld par l'éditeur de liens (ld64), dans (__DATA,__data)
    "std::__ioinit",    # objet d'initialisation de <iostream> (un par unité qui l'inclut ; état de la bibliothèque standard)
    "_ZStL8__ioinit",
}

ELF_PE_STATE = re.compile(r"^(\.data(?!\.rel\.ro)(\..*|\$.*)?|\.bss(\..*|\$.*)?|\.tdata(\..*)?|\.tbss(\..*)?|\.tls(\$.*)?)$")
MACHO_STATE = re.compile(r"\(__DATA,(__data|__bss|__common|__thread_vars|__thread_data|__thread_bss)\)")
# Ligne de `nm -m` : adresse, (segment,section), portée, nom (démanglé par -C, peut contenir des espaces).
MACHO_LINE = re.compile(r"^\S+\s+(\(__DATA,\w+\))\s+(?:weak\s+)?(?:private\s+)?(?:non-external|external)\s+"
                        r"(?:\(was a private external\)\s+)?(?:\[[^\]]*\]\s+)?(.*?)(?:\s+\(was a private external\))?$")


def whitelisted(name):
    # Symbole de section COFF (nom = « .bss », « .data$x »…) ou étiquette locale : pas une variable du jeu.
    return name in WHITELIST or name.lstrip("_") in WHITELIST or name.startswith(".")


def parse_sysv(text):
    """Sortie de `nm --format=sysv` (GNU, ELF et PE) -> [(nom, section)] des symboles d'état."""
    found = []
    for line in text.splitlines():
        cols = [c.strip() for c in line.split("|")]
        if len(cols) < 7:
            continue
        name, section = cols[0], cols[6]
        if ELF_PE_STATE.match(section) and not whitelisted(name):
            found.append((name, section))
    return found


def parse_macho(text):
    """Sortie de `nm -m -C` (macOS) -> [(nom, section)] des symboles d'état."""
    found = []
    for line in text.splitlines():
        if not MACHO_STATE.search(line):
            continue
        m = MACHO_LINE.match(line.strip())
        name = m.group(2) if m else line.split()[-1]
        if not whitelisted(name):
            found.append((name, MACHO_STATE.search(line).group(0)))
    return found


def is_gcc(version_text):
    """Vrai pour GCC (et non Clang, même appelé « c++ » ou « g++ » comme sous macOS)."""
    lowered = version_text.lower()
    return "clang" not in lowered and any(k in lowered for k in ("gcc", "g++", "free software foundation"))


def nm_tool():
    return os.environ.get("NM", "nm")


def state_symbols(binary):
    """Liste (symbole, section) des symboles définis dans une section modifiable ou TLS."""
    env = dict(os.environ, LC_ALL="C")
    darwin = platform.system() == "Darwin"
    args = ["-m", "-C", "--defined-only"] if darwin else ["--format=sysv", "-C", "--defined-only"]
    out = subprocess.run([nm_tool()] + args + [binary], capture_output=True, text=True, env=env)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.strip())
    return parse_macho(out.stdout) if darwin else parse_sysv(out.stdout)


def check(binaries, quiet=False):
    problems = 0
    for binary in binaries:
        try:
            found = state_symbols(binary)
        except (OSError, RuntimeError) as e:
            print(f"check_game_static: {binary}: nm failed: {e}")
            return 1
        for name, section in found:
            print(f"{binary}: état du jeu dans {section} : {name} (règle 13 : l'état va dans le moteur, GameContext)")
        problems += len(found)
        if not found and not quiet:
            print(f"check_game_static: {binary}: aucun état (OK)")
    return 1 if problems else 0


# ── make check ───────────────────────────────────────────────────────────────

BAD_GAME = r"""
int gCounter;                                   // .bss
int gStart = 3;                                 // .data
thread_local int tFrames;                       // .tbss
thread_local int tStart = 5;                    // .tdata
inline int &counter() { static int c = 0; return c; }   // .bss (variable static d'une fonction inline)
extern "C" int api(int i) { static int calls = 0; ++calls; gStart += i; return gCounter + tFrames + tStart + counter()
    + calls; }
"""
BAD_EXPECTED = ["gCounter", "gStart", "tFrames", "tStart", "counter()::c", "api::calls"]

GOOD_GAME = r"""
struct Base { virtual ~Base(); virtual int f() const { return 1; } };
Base::~Base() = default;
constexpr int kTable[4] = {1, 2, 3, 4};
struct Api { int version; int (*fn)(int); };
static int twice(int i) { return 2 * i; }
constexpr Api kApi{1, &twice};
extern "C" const Api *api(int i) { Base b; return kTable[i & 3] + b.f() > 0 ? &kApi : nullptr; }
"""


def compiler_flags(cxx):
    flags = ["-std=c++23", "-fPIC", "-O1", "-DGLM_ENABLE_EXPERIMENTAL"]
    version = subprocess.run([cxx, "--version"], capture_output=True, text=True)
    if version.returncode == 0 and is_gcc(version.stdout):
        flags.append("-fno-gnu-unique")  # comme ENGINE_MODULE_CXXFLAGS (tools/engine-sdk/engine.mk) : GCC seulement
    inc = os.path.join(ROOT, "include")
    flags += ["-I" + inc, "-I" + os.path.join(ROOT, "tests")]
    base = os.environ.get("VCPKG_INSTALLED_ROOT") or os.path.join(ROOT, "vcpkg_installed")
    if os.path.isdir(base):
        for triplet in sorted(os.listdir(base)):
            d = os.path.join(base, triplet, "include")
            if os.path.isdir(d):
                flags += ["-isystem", d]
    return flags


def build(cxx, source, out_dir, name, extra=()):
    """Bibliothèque partagée (Linux, macOS : symboles du moteur laissés indéfinis) ; objet sous Windows (pas d'hôte ici)."""
    obj = os.path.join(out_dir, name + ".o")
    r = subprocess.run([cxx] + compiler_flags(cxx) + list(extra) + ["-c", source, "-o", obj], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"compilation de {source} :\n{r.stderr}")
    system = platform.system()
    if system == "Windows" or system.startswith(("MINGW", "MSYS", "CYGWIN")):
        return obj
    lib = os.path.join(out_dir, name + (".dylib" if system == "Darwin" else ".so"))
    link = ["-dynamiclib", "-undefined", "dynamic_lookup"] if system == "Darwin" else ["-shared"]
    r = subprocess.run([cxx, obj] + link + ["-o", lib], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"édition de liens de {source} :\n{r.stderr}")
    return lib


def self_check():
    cxx = os.environ.get("CXX", "g++")
    if shutil.which(cxx) is None or shutil.which(nm_tool()) is None:
        print(f"check_game_static: {cxx} ou {nm_tool()} introuvable, contrôle ignoré")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        try:
            for name, text in (("bad", BAD_GAME), ("good", GOOD_GAME)):
                with open(os.path.join(tmp, name + ".cpp"), "w") as f:
                    f.write(text)
            bad = build(cxx, os.path.join(tmp, "bad.cpp"), tmp, "bad")
            good = build(cxx, os.path.join(tmp, "good.cpp"), tmp, "good")
            found = [n for n, _ in state_symbols(bad)]
            missing = [n for n in BAD_EXPECTED if not any(n in f for f in found)]
            if missing:
                print(f"check_game_static: auto-test : état non détecté : {', '.join(missing)} (trouvé : {found})")
                return 1
            if state_symbols(good):
                print(f"check_game_static: auto-test : constantes refusées à tort : {state_symbols(good)}")
                return 1
            game = os.path.join(ROOT, "tests", "app", "game_module", "game.cpp")
            libs = [build(cxx, game, tmp, "test_game_" + v, ["-DOROSMO_TEST_GAME_VARIANT_" + v]) for v in ("v1", "v2")]
        except (OSError, RuntimeError) as e:
            print(f"check_game_static: {e}")
            return 1
        status = check(libs, quiet=True)
        if status == 0:
            print("check_game_static: auto-test et bibliothèque de jeu de test sans état (OK)")
        return status


def main(argv):
    quiet = "--quiet" in argv
    binaries = [a for a in argv if not a.startswith("--")]
    if binaries:
        return check(binaries, quiet)
    return self_check()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
