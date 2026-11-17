"""Tests de tools/compare_png.py (exécution : python3 -m unittest discover -s tests/tools)."""
import importlib.util, os, subprocess, sys, tempfile, unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "compare_png.py")
spec = importlib.util.spec_from_file_location("compare_png", TOOL)
cp = importlib.util.module_from_spec(spec); spec.loader.exec_module(cp)


def make(path, w, h, fn):
    cp.write_png(path, w, h, [bytes(v for x in range(w) for v in fn(x, y)) for y in range(h)])


def run(*args):
    r = subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True)
    return r.returncode, r.stdout


class ComparePng(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.d = self.tmp.name
        self.a = os.path.join(self.d, "a.png"); self.b = os.path.join(self.d, "b.png")
        make(self.a, 8, 6, lambda x, y: (x * 30, y * 40, 7))

    def tearDown(self):
        self.tmp.cleanup()

    def test_identical_images_match(self):
        make(self.b, 8, 6, lambda x, y: (x * 30, y * 40, 7))
        rc, out = run(self.a, self.b)
        self.assertEqual(rc, 0, out); self.assertIn("OK", out)

    def test_one_pixel_difference_fails_by_default(self):
        make(self.b, 8, 6, lambda x, y: (x * 30 + (5 if (x, y) == (3, 2) else 0), y * 40, 7))
        rc, out = run(self.a, self.b)
        self.assertEqual(rc, 1, out); self.assertIn("1 pixel", out)

    def test_tolerance_and_pixel_budget_accept_small_differences(self):
        make(self.b, 8, 6, lambda x, y: (x * 30 + (5 if (x, y) == (3, 2) else 0), y * 40, 7))
        self.assertEqual(run(self.a, self.b, "--tolerance", "5")[0], 0)
        self.assertEqual(run(self.a, self.b, "--max-diff-pixels", "1")[0], 0)
        self.assertEqual(run(self.a, self.b, "--max-diff-percent", "5")[0], 0)

    def test_different_dimensions_fail(self):
        make(self.b, 9, 6, lambda x, y: (0, 0, 0))
        rc, out = run(self.a, self.b)
        self.assertEqual(rc, 1); self.assertIn("dimensions", out)

    def test_diff_image_is_written(self):
        make(self.b, 8, 6, lambda x, y: (255 - x * 30, y * 40, 7))
        out_png = os.path.join(self.d, "diff.png")
        run(self.a, self.b, "--diff-out", out_png)
        w, h, bpp, rows = cp.read_png(out_png)
        self.assertEqual((w, h, bpp), (8, 6, 3))

    def test_png_filters_are_decoded(self):
        # PNG avec filtres 1 à 4 (écrits à la main) : doit se relire à l'identique.
        import struct, zlib
        w, h = 4, 4
        pix = [[(x * 20 + y, y * 30, x * y) for x in range(w)] for y in range(h)]
        raw = b""
        prev = bytes(w * 3)
        for y in range(h):
            cur = bytes(v for p in pix[y] for v in p); f = 1 + (y % 4)
            line = bytearray()
            for i in range(w * 3):
                a = cur[i - 3] if i >= 3 else 0; b = prev[i]; c = prev[i - 3] if i >= 3 else 0
                if f == 1: pr = a
                elif f == 2: pr = b
                elif f == 3: pr = (a + b) >> 1
                else:
                    p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line.append((cur[i] - pr) & 255)
            raw += bytes([f]) + bytes(line); prev = cur
        def chunk(k, body): return struct.pack(">I", len(body)) + k + body + struct.pack(">I", zlib.crc32(k + body) & 0xffffffff)
        path = os.path.join(self.d, "f.png")
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        _, _, _, rows = cp.read_png(path)
        for y in range(h):
            self.assertEqual(bytes(rows[y]), bytes(v for p in pix[y] for v in p))


if __name__ == "__main__":
    unittest.main()
