"""Tests de tools/demo_reference_check.py (exécution : python3 -m unittest discover -s tests/tools)."""
import contextlib, importlib.util, io, os, subprocess, sys, tempfile, unittest
from unittest import mock

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "demo_reference_check.py")
spec = importlib.util.spec_from_file_location("demo_reference_check", TOOL)
drc = importlib.util.module_from_spec(spec); spec.loader.exec_module(drc)

# Un faux binaire de démo : écrit scene.png et ui.png (copies d'une référence) dans --capture, et la ligne de validation.
FAKE_DEMO = r'''#!{python}
import shutil, sys
args = sys.argv[1:]
out = args[args.index("--capture") + 1]
shutil.copy({scene!r}, out + "/scene.png")
shutil.copy({ui!r}, out + "/ui.png")
print("Vulkan validation: 0 error/warning message(s) during the run" if {validation} else "no validation")
'''


class DemoReferenceCheck(unittest.TestCase):
    def test_every_case_has_its_reference_image(self):
        names = [name for name, _, _, _ in drc.CASES]
        self.assertEqual(len(names), len(set(names)))
        for name, _, image, reference in drc.CASES:
            self.assertIn(image, ("scene", "ui"), name)
            self.assertTrue(os.path.isfile(os.path.join(drc.REFERENCES, reference)), reference)

    def test_lot_cases_are_covered(self):
        # A90 (L2) : scène, interface, ombres, forward+, GPU-driven, post-traitement.
        names = {name for name, _, _, _ in drc.CASES}
        self.assertTrue({"scene", "ui", "shadows", "forwardplus", "gpudriven", "post"} <= names)

    def run_tool(self, scene, validation):
        with tempfile.TemporaryDirectory() as d:
            binary = os.path.join(d, "apps", "demo", "bin", "debug", "main")
            os.makedirs(os.path.dirname(binary))
            with open(binary, "w") as f:
                f.write(FAKE_DEMO.format(python=sys.executable, scene=scene,
                                         ui=os.path.join(drc.REFERENCES, "ui-opengl-controls-focus-1280x720.png"), validation=validation))
            os.chmod(binary, 0o755)
            r = subprocess.run([sys.executable, TOOL, "--bin", binary, "--out", os.path.join(d, "out"), "--only", "scene", "ui"],
                               capture_output=True, text=True)
            return r.returncode, r.stdout

    def test_matching_captures_and_silent_validation_pass(self):
        rc, out = self.run_tool(os.path.join(drc.REFERENCES, "scene-opengl-demo-1280x720.png"), True)
        self.assertEqual(rc, 0, out)
        self.assertIn("OK (images dans la tolérance)", out)

    def test_a_different_scene_fails(self):
        rc, out = self.run_tool(os.path.join(drc.REFERENCES, "scene-opengl-post-full-1280x720.png"), True)
        self.assertEqual(rc, 1, out)
        self.assertIn("ECHEC", out)

    def test_missing_validation_line_fails(self):
        rc, out = self.run_tool(os.path.join(drc.REFERENCES, "scene-opengl-demo-1280x720.png"), False)
        self.assertEqual(rc, 1, out)
        self.assertIn("validation", out)

    def test_unknown_case_name_is_refused(self):
        r = subprocess.run([sys.executable, TOOL, "--only", "scene", "nope"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2, r.stdout)
        self.assertIn("nope", r.stderr)
        self.assertNotIn("OK", r.stdout)

    def test_timeout_fails_the_case_writes_the_log_and_continues(self):
        calls = []

        def fake_run(binary, api, args, out):
            calls.append(out.name)
            raise subprocess.TimeoutExpired("main", 300, output=b"partial output\n")

        with tempfile.TemporaryDirectory() as d:
            stdout = io.StringIO()
            argv = [TOOL, "--out", d, "--only", "scene", "ui"]
            with mock.patch.object(drc, "run", fake_run), mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(stdout):
                self.assertEqual(drc.main(), 1)
            self.assertEqual(calls, ["scene", "ui"])
            for name in calls:
                self.assertIn("ECHEC " + name, stdout.getvalue())
                with open(os.path.join(d, name, "demo.log")) as f:
                    self.assertIn("partial output", f.read())


if __name__ == "__main__":
    unittest.main()
