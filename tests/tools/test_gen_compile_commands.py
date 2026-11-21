import json
import pathlib
import sys
import tempfile
import unittest

TOOLS_DIR = pathlib.Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS_DIR))
import gen_compile_commands as gccdb


class GenCompileCommandsTests(unittest.TestCase):
    def test_project_mode_generates_entries_for_src_and_generated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "orosmo" / "includes" / "orosmo").mkdir(parents=True)
            (root / "orosmo" / "Generated").mkdir(parents=True)
            (root / "src").mkdir(parents=True)
            (root / "src" / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
            (root / "orosmo" / "Generated" / "HudView.gen.cpp").write_text("// gen\n", encoding="utf-8")

            out = root / "compile_commands.json"
            rc = gccdb.main([
                "--root", str(root),
                "--mode", "auto",
                "--build-type", "debug",
                "--triplet", "x64-linux",
                "--out", str(out),
            ])
            self.assertEqual(rc, 0)
            data = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(len(data), 2)
            files = {pathlib.Path(e["file"]).name for e in data}
            self.assertEqual(files, {"main.cpp", "HudView.gen.cpp"})
            for entry in data:
                args = entry["arguments"]
                self.assertIn("-std=c++23", args)
                self.assertIn("-DDEBUG", args)
                self.assertIn(f"-I{(root / 'orosmo' / 'includes').resolve()}", args)
                self.assertIn(f"-I{(root / 'orosmo' / 'Generated').resolve()}", args)

    def test_repo_mode_generates_entries_for_engine_demo_and_tests(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "mk").mkdir(parents=True)
            (root / "mk" / "engine.mk").write_text("# mk\n", encoding="utf-8")
            (root / "include" / "orosmo").mkdir(parents=True)
            (root / "src" / "core").mkdir(parents=True)
            (root / "src" / "core" / "log.cpp").write_text("// log\n", encoding="utf-8")
            (root / "src" / "ThirdParty").mkdir(parents=True)
            (root / "src" / "ThirdParty" / "glad.c").write_text("// c\n", encoding="utf-8")
            (root / "apps" / "demo" / "src").mkdir(parents=True)
            (root / "apps" / "demo" / "src" / "main.cpp").write_text("int main(){}\n", encoding="utf-8")
            (root / "tests" / "core").mkdir(parents=True)
            (root / "tests" / "core" / "log_tests.cpp").write_text("// t\n", encoding="utf-8")

            out = root / "compile_commands.json"
            rc = gccdb.main([
                "--root", str(root),
                "--build-type", "release",
                "--triplet", "x64-linux",
                "--out", str(out),
            ])
            self.assertEqual(rc, 0)
            data = json.loads(out.read_text(encoding="utf-8"))
            self.assertEqual(len(data), 4)

            by_name = {pathlib.Path(e["file"]).name: e for e in data}
            self.assertIn("-std=c++23", by_name["log.cpp"]["arguments"])
            self.assertIn("-DNDEBUG", by_name["log.cpp"]["arguments"])
            self.assertNotIn("-std=c++23", by_name["glad.c"]["arguments"])
            self.assertIn(f"-I{(root / 'tests').resolve()}", by_name["log_tests.cpp"]["arguments"])


if __name__ == "__main__":
    unittest.main()
