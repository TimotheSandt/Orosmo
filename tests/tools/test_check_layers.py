"""Tests de tools/check_layers.py et de tools/layers.json (M8, A79 lot L1 : modules scene, sim, app et game)."""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest
from unittest import mock

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_layers  # noqa: E402


class LayersConfigTest(unittest.TestCase):
    def setUp(self):
        self.cfg = check_layers.load()

    def test_new_modules_have_rules(self):
        for module in ("scene", "sim", "app"):
            self.assertEqual(check_layers.layer_of(self.cfg, f"include/orosmo/{module}/x.hpp"), module)
            self.assertEqual(check_layers.layer_of(self.cfg, f"src/{module}/x.cpp"), module)
        self.assertEqual(check_layers.layer_of(self.cfg, "game/systems/x.cpp"), "game")

    def test_order_of_docs03_with_app_between_sim_and_game(self):
        lv = self.cfg["levels"]
        order = ["core", "platform", "rhi", "renderer", "assets", "anim", "scene", "ui", "world", "sim", "app", "game", "apps"]
        self.assertEqual([lv[m] for m in order], sorted(lv[m] for m in order))
        self.assertEqual(len({lv[m] for m in order}), len(order))


class LayersViolationTest(unittest.TestCase):
    """Arbre factice : une inclusion vers une couche supérieure est refusée par --strict, l'inverse est accepté."""

    def run_tree(self, files):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        os.makedirs(os.path.join(tmp, "tools"))
        shutil.copy(os.path.join(ROOT, "tools", "layers.json"), os.path.join(tmp, "tools", "layers.json"))
        for rel, text in files.items():
            path = os.path.join(tmp, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        out = io.StringIO()
        with mock.patch.object(check_layers, "ROOT", tmp), mock.patch.object(sys, "argv", ["check_layers.py", "--strict"]):
            with contextlib.redirect_stdout(out):
                rc = check_layers.main()
        return rc, out.getvalue()

    def test_scene_including_app_is_a_violation(self):
        rc, out = self.run_tree({
            "include/orosmo/scene/world.hpp": "#include <orosmo/app/application.hpp>\n",
            "include/orosmo/app/application.hpp": "#pragma once\n",
        })
        self.assertEqual(rc, 1)
        self.assertIn("scene -> app", out)

    def test_app_including_sim_and_game_including_app_are_allowed(self):
        rc, out = self.run_tree({
            "include/orosmo/sim/loop.hpp": "#include <orosmo/scene/world.hpp>\n",
            "include/orosmo/scene/world.hpp": "#pragma once\n",
            "include/orosmo/app/application.hpp": "#include <orosmo/sim/loop.hpp>\n",
            "game/game.cpp": "#include <orosmo/app/application.hpp>\n",
        })
        self.assertEqual(rc, 0, out)
        self.assertIn("0 violation(s), 0 sans couche", out)

    def test_sim_including_game_is_a_violation(self):
        rc, out = self.run_tree({
            "src/sim/loop.cpp": '#include "../../game/game.hpp"\n',
            "game/game.hpp": "#pragma once\n",
        })
        self.assertEqual(rc, 1)
        self.assertIn("sim -> game", out)


if __name__ == "__main__":
    unittest.main()
