#!/usr/bin/env python3
"""Chemin rejoué de la simulation sans calcul dépendant de la plateforme (SIM-01, A79 lot L3, règle 9).

Les fonctions transcendantes de la bibliothèque C (sin, cos, exp, log, pow...) n'ont pas le même arrondi sous glibc, macOS et
MinGW : une simulation qui s'en sert ne donne pas la même empreinte partout. Ce contrôle refuse, dans les fichiers du chemin
rejoué (DETERMINISTIC_PATHS), ces fonctions, les fonctions de glm qui les appellent, rand()/srand(), les générateurs et
distributions de <random> (algorithmes propres à chaque bibliothèque standard) et la lecture d'une horloge. sqrt, les
opérations + - * / et les comparaisons sont exactes (IEEE 754) et restent permises. Commentaires et chaînes ignorés.
Sort avec 1 s'il trouve quelque chose (`make check`).
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
# Dossiers et fichiers du chemin rejoué : boucle de simulation, monde et sauvegarde, générateur, scénario de la démo.
DETERMINISTIC_PATHS = [
    "src/sim",
    "include/orosmo/sim",
    "src/scene",
    "include/orosmo/scene",
    "include/orosmo/core/random.hpp",
    "include/orosmo/core/hash.hpp",
    "apps/demo/src/Sim",
]
TRANSCENDENTAL = (r"sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|exp|exp2|expm1|log|log2|log10|log1p|"
                  r"pow|cbrt|hypot|erf|erfc|tgamma|lgamma")
PATTERNS = [
    (re.compile(r"(?<![\w.:>])(?:std::|::)?(?:" + TRANSCENDENTAL + r")[fl]?\s*\("), "transcendental libc function"),
    (re.compile(r"\bglm::(?:" + TRANSCENDENTAL + r"|angleAxis|slerp|eulerAngles|eulerAngleXYZ|yaw|pitch|roll|angle|axis|rotate|"
                r"perspective|lookAt|sphericalRand|linearRand|gaussRand)\s*\("), "glm function built on transcendental functions"),
    (re.compile(r"(?<![\w.:>])(?:std::|::)?s?rand\s*\("), "rand()/srand()"),
    (re.compile(r"\bstd::(?:random_device|mt19937(?:_64)?|minstd_rand0?|default_random_engine|ranlux\w*|knuth_b|\w+_distribution)\b"),
     "<random> engine or distribution"),
    (re.compile(r"\b(?:steady_clock|system_clock|high_resolution_clock)::now\b|(?<![\w.:>])(?:std::|::)?time\s*\("), "clock read"),
]
# Les chaînes brutes (R"x(...)x") passent avant les chaînes ordinaires : un guillemet intérieur y est permis.
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|(?<!\w)(?:u8|[uUL])?R"(?P<delim>[^ ()\\\s"]{0,16})\(.*?\)(?P=delim)"'
                   r'|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
SOURCE_EXT = (".hpp", ".h", ".cpp", ".cc", ".inl", ".inc")


def strip(text):
    # Un commentaire devient un espace (séparateur, comme en C++), une chaîne "" ; les sauts de ligne sont gardés.
    return STRIP.sub(lambda m: (" " if m.group(0)[0] == "/" else '""') + "\n" * m.group(0).count("\n"), text)


def scan_text(text):
    """Liste de (ligne, motif, extrait) pour un texte source."""
    # Motifs appliqués au texte entier (un appel peut couper la ligne entre le nom et la parenthèse) ; le numéro de ligne
    # vient de la position du résultat. Un même motif n'est compté qu'une fois par ligne.
    cleaned = strip(text)
    found = []
    for pattern, label in PATTERNS:
        seen = set()
        for m in pattern.finditer(cleaned):
            number = cleaned.count("\n", 0, m.start()) + 1
            if number not in seen:
                seen.add(number)
                found.append((number, label, " ".join(m.group(0).split())))
    found.sort(key=lambda item: item[0])
    return found


def files(root):
    for rel in DETERMINISTIC_PATHS:
        path = os.path.join(root, rel)
        if os.path.isfile(path):
            yield rel
        elif os.path.isdir(path):
            for directory, _, names in os.walk(path):
                for name in sorted(names):
                    if name.endswith(SOURCE_EXT):
                        yield os.path.relpath(os.path.join(directory, name), root).replace(os.sep, "/")


def main(argv):
    root = argv[1] if len(argv) > 1 else ROOT
    problems = 0
    for rel in files(root):
        with open(os.path.join(root, rel), encoding="utf-8") as f:
            for number, label, excerpt in scan_text(f.read()):
                print(f"{rel}:{number}: {label}: {excerpt}")
                problems += 1
    if problems:
        print(f"check_deterministic_math: {problems} problem(s) in the replayed simulation path (rule 9, A79 L3)")
        return 1
    print("check_deterministic_math: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
