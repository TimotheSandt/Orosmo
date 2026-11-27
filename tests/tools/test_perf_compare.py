import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from perf_compare import reserved_options  # noqa: E402


class ReservedOptions(unittest.TestCase):
    def test_refuses_options_set_by_the_tool(self):
        self.assertEqual(reserved_options(["--capture-frames", "60"]), ["--capture-frames"])
        self.assertEqual(reserved_options(["--capture-frames=60", "--ui", "full"]), ["--capture-frames=60", "--ui"])

    def test_accepts_measured_options(self):
        self.assertEqual(reserved_options(["--vsync", "off", "--graph-cache", "off", "--frames-in-flight", "3", "--scene", "mixed"]), [])


if __name__ == "__main__":
    unittest.main()
