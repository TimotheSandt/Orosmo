import pathlib
import sys
import tempfile
import unittest

TOOLS_DIR = pathlib.Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS_DIR))
import extract_doc_code as edc

PAGE = '''Intro.

```bash
make debug
```

```cpp title="src/state.hpp"
struct State {};
```

```text title="src/View.cui"
view View {}
```

```cpp title="src/main.cpp"
#include "state.hpp"
```

Some text, then an untitled excerpt:

```cpp
not a file
```

```cpp title="src/main.cpp (suite)"
int main() { return 0; }
```
'''


class ExtractDocCodeTests(unittest.TestCase):
    def test_titled_blocks_make_files_and_continued_blocks_append(self):
        files = edc.extract(PAGE)
        self.assertEqual(list(files), ["src/state.hpp", "src/View.cui", "src/main.cpp"])
        self.assertEqual(files["src/main.cpp"], '#include "state.hpp"\nint main() { return 0; }\n')
        self.assertEqual(files["src/View.cui"], "view View {}\n")
        self.assertNotIn("make debug", "".join(files.values()))
        self.assertNotIn("not a file", "".join(files.values()))

    def test_a_marker_followed_by_text_does_not_close_the_block(self):
        page = '```cpp title="a.cpp"\n```suffix\nint x;\n```  \n'
        self.assertEqual(edc.extract(page)["a.cpp"], "```suffix\nint x;\n")

    def test_windows_paths_are_refused(self):
        for path in ("C:/temp/a.cpp", "C:a.cpp", "\\\\server\\share\\a.cpp", "..\\a.cpp", "\\a.cpp"):
            with self.assertRaises(ValueError, msg=path):
                edc.extract(f'```cpp title="{path}"\nx\n```\n')

    def test_errors(self):
        with self.assertRaisesRegex(ValueError, "before the start"):
            edc.extract('```cpp title="a.cpp (suite)"\nx\n```\n')
        with self.assertRaisesRegex(ValueError, "started twice"):
            edc.extract('```cpp title="a.cpp"\nx\n```\n```cpp title="a.cpp"\ny\n```\n')
        with self.assertRaisesRegex(ValueError, "unterminated"):
            edc.extract('```cpp title="a.cpp"\nx\n')
        with self.assertRaisesRegex(ValueError, "relative"):
            edc.extract('```cpp title="../a.cpp"\nx\n```\n')

    def test_main_writes_files_and_removes_the_listed_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            page = root / "page.md"
            page.write_text(PAGE, encoding="utf-8")
            out = root / "project"
            (out / "src").mkdir(parents=True)
            (out / "src" / "Old.cui").write_text("old\n", encoding="utf-8")
            self.assertEqual(edc.main([str(page), "--out", str(out), "--remove", "src/Old.cui"]), 0)
            self.assertFalse((out / "src" / "Old.cui").exists())
            self.assertEqual((out / "src" / "state.hpp").read_text(encoding="utf-8"), "struct State {};\n")
            empty = root / "empty.md"
            empty.write_text("nothing\n", encoding="utf-8")
            self.assertEqual(edc.main([str(empty), "--out", str(out)]), 1)


if __name__ == "__main__":
    unittest.main()
