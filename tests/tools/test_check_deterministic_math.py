"""tools/check_deterministic_math.py : fonctions interdites dans le chemin rejoué de la simulation (A79 lot L3)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import check_deterministic_math as cdm  # noqa: E402


class ScanTextTest(unittest.TestCase):
    def labels(self, text):
        return [label for _, label, _ in cdm.scan_text(text)]

    def test_refuses_transcendental_functions(self):
        for call in ["std::sin(x)", "cos(x)", "std::exp(x)", "powf(a, b)", "::log10(v)", "std::atan2(y, x)"]:
            self.assertEqual(self.labels(call), ["transcendental libc function"], call)

    def test_refuses_glm_rand_random_and_clocks(self):
        self.assertEqual(self.labels("glm::angleAxis(a, v)"), ["glm function built on transcendental functions"])
        self.assertEqual(self.labels("int r = rand();"), ["rand()/srand()"])
        self.assertEqual(self.labels("std::mt19937 engine;"), ["<random> engine or distribution"])
        self.assertEqual(self.labels("std::uniform_real_distribution<double> d;"), ["<random> engine or distribution"])
        self.assertEqual(self.labels("auto t = std::chrono::steady_clock::now();"), ["clock read"])

    def test_allows_exact_operations_and_similar_names(self):
        for ok in ["std::sqrt(x)", "a * b + c", "random.nextDouble()", "costly(x)", "obj.sin(x)", "blog(x)", "explain(x)",
                   "ctx.random.range(-3, 5)", "glm::mat3_cast(q)", "glm::dot(a, b)"]:
            self.assertEqual(self.labels(ok), [], ok)

    def test_ignores_comments_and_strings(self):
        self.assertEqual(self.labels('// std::sin(x)\nconst char *s = "cos(x)";\n/* exp(1)\n pow(2, 3) */'), [])
        self.assertEqual(cdm.scan_text("/* a\nb */ std::sin(x)")[0][0], 2)

    def test_repository_is_clean(self):
        self.assertEqual(cdm.main(["check", cdm.ROOT]), 0)


if __name__ == "__main__":
    unittest.main()
