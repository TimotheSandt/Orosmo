"""Tests de tools/check_game_static.py (exécution : python3 -m unittest discover -s tests/tools).

Sorties de `nm` figées pour les trois formats (ELF, PE/COFF de MinGW, Mach-O de macOS), relevées sur la CI de la PR #28 :
l'analyse est testée partout, même là où le format n'est pas celui de la machine.
"""
import importlib.util, os, unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
spec = importlib.util.spec_from_file_location("check_game_static", os.path.join(ROOT, "tools", "check_game_static.py"))
cgs = importlib.util.module_from_spec(spec); spec.loader.exec_module(cgs)


def sysv(rows):
    return "\n".join(f"{name:<20}|0000000000004010|   {cls}  |  OBJECT|0000000000000004|     |{section}" for name, cls, section in rows)


class Sysv(unittest.TestCase):
    def test_elf_state_is_refused_constants_and_runtime_are_not(self):
        text = sysv([("gCounter", "B", ".bss"), ("tFrames", "B", ".tbss"), ("tStart", "D", ".tdata"),
                     ("api::calls", "b", ".bss"), ("counter()::c", "V", ".bss._ZZ7countervE1c"),
                     ("kTable", "r", ".rodata"), ("vtable for Base", "V", ".data.rel.ro"),
                     ("__dso_handle", "d", ".data"), ("completed.0", "b", ".bss"),
                     ("DW.ref.__gxx_personality_v0", "V", ".data")])
        names = [n for n, _ in cgs.parse_sysv(text)]
        self.assertEqual(names, ["gCounter", "tFrames", "tStart", "api::calls", "counter()::c"])

    def test_coff_section_symbols_are_not_game_state(self):
        # Fichier objet MinGW : symboles de section « .bss » et « .data » (CI Windows de la PR #28).
        text = sysv([(".bss", "b", ".bss"), (".data", "d", ".data"), (".data$x", "d", ".data$x"),
                     ("gStart", "D", ".data"), ("__emutls_v.tFrames", "D", ".data"), ("sTls", "D", ".tls$")])
        names = [n for n, _ in cgs.parse_sysv(text)]
        self.assertEqual(names, ["gStart", "__emutls_v.tFrames", "sTls"])


class MachO(unittest.TestCase):
    def test_demangled_names_with_spaces_and_dyld_private(self):
        text = "\n".join([
            "0000000000008000 (__DATA,__data) non-external __dyld_private",
            "0000000000008008 (__DATA,__data) external _gStart",
            "0000000000008010 (__DATA,__bss) non-external api::calls",
            "0000000000008018 (__DATA,__bss) weak external counter()::c",
            "0000000000008020 (__DATA,__thread_vars) external _tFrames",
            "0000000000004000 (__TEXT,__const) external _kTable",
            "0000000000004010 (__DATA_CONST,__const) non-external vtable for Base",
            "0000000000008028 (__DATA,__data) non-external (was a private external) operator()(int) const::n",
        ])
        names = [n for n, _ in cgs.parse_macho(text)]
        self.assertEqual(names, ["_gStart", "api::calls", "counter()::c", "_tFrames", "operator()(int) const::n"])


class Compiler(unittest.TestCase):
    def test_gcc_is_told_apart_from_clang(self):
        self.assertTrue(cgs.is_gcc("g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0\nCopyright (C) 2023 Free Software Foundation, Inc."))
        self.assertTrue(cgs.is_gcc("g++.exe (Rev2, Built by MSYS2 project) 16.1.0"))
        self.assertFalse(cgs.is_gcc("Apple clang version 17.0.0 (clang-1700.0.13.3)\nTarget: arm64-apple-darwin24"))
        self.assertFalse(cgs.is_gcc("Ubuntu clang version 18.1.3\nInstalledDir: /usr/bin"))


if __name__ == "__main__":
    unittest.main()
