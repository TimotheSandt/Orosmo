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
        for call in ["::rand()", "::srand(1)", "std::srand(1)"]:
            self.assertEqual(self.labels(call), ["rand()/srand()"], call)
        for call in ["time(nullptr)", "std::time(0)", "std::time(&seconds)", "::time(&t)"]:
            self.assertEqual(self.labels(call), ["clock read"], call)

    def test_detects_calls_split_over_lines(self):
        self.assertEqual(cdm.scan_text("double y = std::sin\n    (x);"), [(1, "transcendental libc function", "std::sin (")])
        self.assertEqual(cdm.scan_text("a;\nb = std::exp(\n x);")[0][0], 2)

    def test_allows_exact_operations_and_similar_names(self):
        for ok in ["std::sqrt(x)", "a * b + c", "random.nextDouble()", "costly(x)", "obj.sin(x)", "blog(x)", "explain(x)",
                   "ctx.random.range(-3, 5)", "glm::mat3_cast(q)", "glm::dot(a, b)", "runtime(x)", "obj.time(x)",
                   "lifetime(x)"]:
            self.assertEqual(self.labels(ok), [], ok)

    def test_detects_a_name_separated_from_its_scope_by_a_comment(self):
        for text in ("auto q = glm::/* note */slerp(a, b, t);", "std::/* note */mt19937 gen;", "float s = std::/* note */sin(x);"):
            self.assertTrue(self.labels(text), text)

    def test_ignores_comments_and_strings(self):
        self.assertEqual(self.labels('// std::sin(x)\nconst char *s = "cos(x)";\n/* exp(1)\n pow(2, 3) */'), [])
        self.assertEqual(cdm.scan_text("/* a\nb */ std::sin(x)")[0][0], 2)

    def test_a_comment_separates_tokens(self):
        self.assertEqual(self.labels("return/* note */rand();"), ["rand()/srand()"])

    def test_ignores_raw_strings(self):
        self.assertEqual(self.labels('auto s = R"(text " rand())";\nauto t = u8R"x( )" sin(1) )x";'), [])
        self.assertEqual(cdm.scan_text('R"(a\nb)";\nrand();')[0][0], 3)

    def test_repository_is_clean(self):
        self.assertEqual(cdm.main(["check", cdm.ROOT]), 0)


if __name__ == "__main__":
    unittest.main()
