"""Tests de tools/build_progress.py (exécution : python3 -m unittest discover -s tests/tools)."""
import importlib.util, io, os, shutil, subprocess, sys, tempfile, textwrap, unittest
from contextlib import redirect_stdout

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "build_progress.py")
spec = importlib.util.spec_from_file_location("build_progress", TOOL)
bp = importlib.util.module_from_spec(spec); spec.loader.exec_module(bp)

# Même définition que mk/engine.mk et tools/engine-sdk/engine.mk.
MAKEFILE = textwrap.dedent("""\
    ORS_Q := $(if $(ORS_PROGRESS),$(if $(filter 1,$(V)),,@))
    ors_compile = @printf '%s\\n' '$(if $(ORS_PROGRESS),@@ors begin $@ $< $(1),  $(1) $<)'
    ors_end = $(if $(ORS_PROGRESS),@printf '%s\\n' '@@ors end $@')
    all: a.o b.o
    %.o: %.c
    \t$(call ors_compile,CC)
    \t$(ORS_Q)cp $< $@
    \t$(ors_end)
    """)


class Markers(unittest.TestCase):
    def test_begin_and_end_are_parsed_in_a_run_and_in_a_dry_run(self):
        m = bp.MARK.search("@@ors begin obj/debug/a.o src/a.cpp CXX [debug]")
        self.assertEqual(m.groups(), ("begin", "obj/debug/a.o", "src/a.cpp", "CXX [debug]"))
        m = bp.MARK.search("printf '%s\\n' '@@ors begin obj/a.o src/a.cpp CXX'")  # ligne affichée par make -n
        self.assertEqual(m.group(2, 3, 4), ("obj/a.o", "src/a.cpp", "CXX"))
        self.assertEqual(bp.MARK.search("@@ors end obj/a.o").group(1, 2), ("end", "obj/a.o"))

    def test_make_noise_and_failures(self):
        self.assertTrue(bp.MAKE_NOISE.match("make[1] : on entre dans le répertoire « /x »"))
        self.assertTrue(bp.MAKE_NOISE.match("make -f mk/engine.mk sync-demo BUILD_TYPE=debug"))
        self.assertFalse(bp.MAKE_NOISE.match("make: *** [Makefile:7 : bad.o] Erreur 1"))  # les erreurs restent
        self.assertEqual(bp.MAKE_FAILED.search("make: *** [Makefile:7 : bad.o] Erreur 1").group(1), "bad.o")
        self.assertEqual(bp.MAKE_FAILED.search("make[1]: *** [mk/engine.mk:302: obj/x.o] Error 1").group(1), "obj/x.o")


class Counting(unittest.TestCase):
    def test_compiled_running_skipped_and_remaining(self):
        out = io.StringIO()
        with redirect_stdout(out):
            d = bp.Display(stale={"a.o", "b.o"}, total={"a.o", "b.o", "c.o", "d.o"})
            self.assertEqual(d.skipped, 2)
            d.begin("a.o", "a.cpp", "CXX")
            self.assertEqual(list(d.running.values()), ["a.cpp"])
            d.end("a.o")
            d.begin("e.o", "e.cpp", "CXX")  # découvert en cours de route : ajouté au total
            self.assertEqual(len(d.todo), 3)
            d.fail("e.o")
            self.assertEqual(d.failed, ["e.cpp"])
            d.summary(2)
        self.assertIn("en échec : e.cpp", out.getvalue())


@unittest.skipUnless(shutil.which("make"), "GNU make absent")
class Plan(unittest.TestCase):
    def test_plan_separates_stale_and_up_to_date_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "Makefile"), "w") as f:
                f.write(MAKEFILE)
            for name in ("a.c", "b.c"):
                with open(os.path.join(tmp, name), "w") as f:
                    f.write(name)
            subprocess.run(["make", "-s", "-C", tmp, "a.o"], check=True, capture_output=True)
            stale, total = bp.plan(["make", "-C", tmp, "all"], dict(os.environ, ORS_PROGRESS="1"))
            self.assertEqual(stale, {"b.o"})
            self.assertEqual(total, {"a.o", "b.o"})

    def test_without_a_terminal_make_runs_as_usual(self):
        with tempfile.TemporaryDirectory() as tmp:
            with open(os.path.join(tmp, "Makefile"), "w") as f:
                f.write(MAKEFILE)
            with open(os.path.join(tmp, "a.c"), "w") as f:
                f.write("a")
            r = subprocess.run([sys.executable, TOOL, "make", "-C", tmp, "a.o"], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0)
            self.assertIn("  CC a.c", r.stdout)       # étiquette habituelle
            self.assertIn("cp a.c a.o", r.stdout)     # commande complète, comme avant
            self.assertNotIn("@@ors", r.stdout)


if __name__ == "__main__":
    unittest.main()
