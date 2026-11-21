# CUI Rust Precompiler

Native implementation of the `.cui` DSL precompiler. It replaces the legacy Python entry point used by the project build and VS Code diagnostics.

## Build

```sh
make cui-tool
```

`make cui-tool` installs the binary as `tools/cui` (`tools/cui.exe` on Windows). A direct Cargo build of the workspace:

```sh
cargo build --release --manifest-path tools/Cargo.toml -p cui
```

writes it to `tools/target/release/cui` instead.

## Usage

```sh
tools/cui run --output Generated apps/demo/src/UI/Views/PerformanceView.cui
tools/cui check apps/demo/src/UI/Views/PerformanceView.cui          # JSON diagnostics (--stdin: source on stdin)
tools/cui spec                                                       # components, modifiers, theme properties (JSON)
```

Names starting with `cui_` are reserved for the generated code (a field or a `let` with such a name is refused).

## Targets and tests

- Every file starts with `import "orosmo/ui"` and is compiled for the engine's `orosmo::ui` module; a file without it is
  refused. The legacy `UI::View` target (component registry scanned from `include/UI`, `scan` / `build` commands, `--headers`
  and `--registry` options) was removed with the legacy interface. Specification (French):
  [docs/UI_LANGUAGE.md](../../docs/UI_LANGUAGE.md).
- Diagnostics: `file:line:column: error: message` (all errors of a file); `#line` directives map C++ errors back to the `.cui`.
- `make cui-test` (repository root): `cargo test -p cui` + the invalid inputs of `tests/ui/cui_errors/` through the real binary.
