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
tools/cui scan --headers include --output Generated/registry.json
tools/cui run --headers include --output Generated apps/demo/src/UI/Views/PerformanceView.cui
tools/cui check --registry Generated/registry.json apps/demo/src/UI/Views/PerformanceView.cui
```

Names starting with `cui_` are reserved for the generated code (a field or a `let` with such a name is refused).

## Targets and tests

- A file that starts with `import "engine/ui"` is compiled for the engine's `engine::ui` module (no `--headers` needed);
  the other files keep the legacy `UI::View` target. Specification (French): [docs/UI_LANGUAGE.md](../../docs/UI_LANGUAGE.md).
- Diagnostics: `file:line:column: error: message` (all errors of a file); `#line` directives map C++ errors back to the `.cui`.
- `make cui-test` (repository root): `cargo test -p cui` + the invalid inputs of `tests/ui/cui_errors/` through the real binary.
