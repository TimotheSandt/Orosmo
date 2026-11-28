"""M9-L3 : options de lien macOS de tools/engine-sdk/engine.mk (chargeur Vulkan trouvé sans variable d'environnement).

La branche Darwin du Makefile est évaluée sur n'importe quel système en imposant `_ENG_UNAME=Darwin` : seules les variables
sont affichées, rien n'est compilé.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ENGINE_MK = pathlib.Path(__file__).resolve().parents[2] / "tools" / "engine-sdk" / "engine.mk"
MAKE = shutil.which("make") or shutil.which("mingw32-make")


@unittest.skipIf(MAKE is None, "GNU Make introuvable")
class DarwinLinkFlags(unittest.TestCase):
    def ldflags(self, system, *extra, packaged=True):
        with tempfile.TemporaryDirectory() as tmp:
            engine = pathlib.Path(tmp) / "engine"
            (engine / "includes" / "orosmo").mkdir(parents=True)
            if packaged:
                (engine / "deps" / "lib").mkdir(parents=True)
            probe = pathlib.Path(tmp) / "Makefile"
            probe.write_text("include %s\nprint-ldflags:\n\t@echo '$(ENGINE_LDFLAGS)'\n.DEFAULT_GOAL := print-ldflags\n"
                             % ENGINE_MK.as_posix(), encoding="utf-8")
            # `?=` garderait une valeur venue de l'environnement ; la surcharge d'un test passe par `extra`.
            env = {k: v for k, v in os.environ.items() if k != "ENGINE_VULKAN_RPATHS"}
            result = subprocess.run(
                [MAKE, "-s", "-f", str(probe), "OS=", "_ENG_UNAME=" + system, "ENGINE_DIR=" + engine.as_posix(),
                 *extra, "print-ldflags"], cwd=tmp, capture_output=True, text=True, check=True, env=env)
            return result.stdout.split()

    def test_packaged_engine_searches_the_homebrew_and_sdk_loader_directories(self):
        flags = self.ldflags("Darwin")
        self.assertIn("-Wl,-rpath,/opt/homebrew/lib", flags)
        self.assertIn("-Wl,-rpath,/usr/local/lib", flags)

    def test_system_loader_comes_before_the_vcpkg_loader_in_the_repository(self):
        flags = self.ldflags("Darwin", packaged=False)
        rpaths = [f for f in flags if f.startswith("-Wl,-rpath,")]
        self.assertEqual(rpaths[:2], ["-Wl,-rpath,/opt/homebrew/lib", "-Wl,-rpath,/usr/local/lib"])
        self.assertEqual(len(rpaths), 3)
        self.assertTrue(rpaths[2].endswith("/lib"))
        self.assertIn("vcpkg_installed", rpaths[2])

    def test_the_directories_are_overridable(self):
        flags = self.ldflags("Darwin", "ENGINE_VULKAN_RPATHS=/opt/custom/lib")
        self.assertEqual([f for f in flags if f.startswith("-Wl,-rpath,")], ["-Wl,-rpath,/opt/custom/lib"])

    def test_linux_gets_no_rpath(self):
        self.assertEqual([f for f in self.ldflags("Linux") if "rpath" in f], [])


if __name__ == "__main__":
    unittest.main()
