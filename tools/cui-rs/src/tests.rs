// UI-11 : tests du compilateur (cargo test --manifest-path tools/Cargo.toml -p cui). Entrées valides : formes du code généré ;
// entrées invalides : message exact et position `fichier:ligne:colonne`. Les cas de bout en bout (binaire cui, compilateur C++)
// sont dans tests/ui/cui_errors/ (make cui-test) et tests/ui/cui/ (compilés avec les tests C++).

use crate::compile_source;
use crate::lexer::{Lexer, TokenType};
use crate::registry::Registry;

fn engine(src: &str) -> Result<(String, String), Vec<String>> {
    compile_source(&format!("import \"engine/ui\"\n{src}"), "v.cui", "v", None).map(|(h, c, _)| (h, c))
}

fn errors(src: &str) -> Vec<String> {
    engine(src).expect_err("the source should be refused")
}

#[test]
fn lexer_reads_dollar_and_token_ends() {
    let tokens = Lexer::new("$state.seed \"ab\"").tokenize().unwrap();
    assert_eq!(tokens[0].ty, TokenType::Dollar);
    assert_eq!((tokens[1].line, tokens[1].col, tokens[1].end_col), (1, 2, 7));
    assert_eq!(tokens[4].ty, TokenType::String);
    assert_eq!((tokens[4].col, tokens[4].end_col), (13, 17));
}

#[test]
fn a_missing_symbol_is_reported_after_the_previous_token() {
    let err = errors("view V {\n    Label(\"hi\"\n}\n");
    assert_eq!(err, vec!["v.cui:3:15: error: expected ')' (found '}')".to_string()]);
}

#[test]
fn a_view_compiles_to_a_struct_and_a_build_function() {
    let (header, source) = engine(
        "import \"state.hpp\"\nview Panel {\n    @Mutable var state: State\n    @Observed var stats: Stats\n    let w = 320\n    VBox {\n        Slider($state.scale, min: 0, max: 10, step: 1).onChange(state.changed)\n        Label(\"FPS \\(stats.fps)\")\n        Button(\"Go\").onClick(regenerate)\n    }\n    .width(w)\n}\n",
    )
    .unwrap();
    assert!(header.contains("#include \"state.hpp\""));
    assert!(header.contains("namespace cui"));
    assert!(header.contains("struct Panel"));
    assert!(header.contains("    State *state = nullptr;"));
    assert!(header.contains("    const Stats *stats = nullptr;"));
    assert!(header.contains("    static constexpr auto w = 320.0;"));
    assert!(header.contains("inline std::unique_ptr<engine::ui::Widget> buildPanel(engine::ui::Ui &cui_ui, State &state, const Stats &stats)"));
    assert!(source.contains("#include \"v.gen.h\""));
    assert!(source.contains("engine::ui::Slider>(engine::ui::numberBinding(state->scale), static_cast<double>(0.0), static_cast<double>(10.0), static_cast<double>(1.0))"));
    assert!(source.contains("decltype(state->changed(std::forward<decltype(cui_a)>(cui_a)...))"));
    assert!(source.contains("bindText([=] { return engine::ui::text(\"FPS \", stats->fps); });"));
    assert!(source.contains("onClick(cui_ui.actions().bind(\"regenerate\"));"));
    assert!(source.contains("setWidth(engine::ui::SizeSpec::fixed(static_cast<float>(w)));"));
}

#[test]
fn every_statement_is_mapped_to_its_line_in_the_cui_file() {
    let (_, source) = engine("view V {\n    VBox {\n        Label(\"a\")\n        Label(\"b\")\n    }\n}\n").unwrap();
    let lines: Vec<&str> = source.lines().collect();
    for (i, line) in lines.iter().enumerate() {
        if line.starts_with("    ") && !line.trim_start().starts_with('#') && line.trim() != "{" && line.trim() != "}" && !line.contains("(void)cui_ui") && !line.contains("return cui_w0") {
            assert!(lines[i - 1].starts_with("#line "), "statement without #line: {line}");
        }
    }
    assert!(source.contains("#line 4 \"v.cui\"\n    auto cui_w1 = std::make_unique<engine::ui::Label>();"));
    assert!(source.contains("#line 5 \"v.cui\"\n    auto cui_w2 = std::make_unique<engine::ui::Label>();"));
}

#[test]
fn tabs_pages_sections_and_param_panels() {
    let (_, source) = engine(
        "view V {\n    @Mutable var s: S\n    Tabs {\n        Page(\"A\") { ParamPanel($s.params) }\n        Page(\"B\") { Label(\"1\") Label(\"2\") }\n    }\n}\n",
    )
    .unwrap();
    assert!(source.contains("engine::ui::reflectedPanel(s->params)"));
    assert!(source.contains("addPage(std::string(\"A\"), std::move("));
    assert!(source.contains("addPage(std::string(\"B\"), std::move("));
    assert!(source.contains("std::make_unique<engine::ui::VBox>()")); // page à plusieurs enfants
}

#[test]
fn invalid_sources_give_exact_messages() {
    let cases: &[(&str, &str)] = &[
        ("view V {\n    Buton(\"x\")\n}\n", "v.cui:3:5: error: unknown component 'Buton'"),
        ("view V {\n    Label(\"x\").colour(1)\n}\n", "v.cui:3:15: error: 'Label' has no modifier 'colour'"),
        ("view V {\n    @Mutable var s: S\n    Button(\"x\").onClick(s.go(1))\n}\n", "v.cui:4:16: error: '.onClick' names a C++ function"),
        ("view V {\n    @Observed var s: S\n    Checkbox(\"x\", checked: $s.on)\n}\n", "v.cui:4:5: error: '$s.on': 's' is @Observed (read only)"),
        ("view V {\n    Slider($x, min: 0, max: 1)\n}\n", "v.cui:3:5: error: '$x': a two-way binding must start with a @Mutable field"),
        ("view V {\n    Slider(min: 0, max: 1)\n}\n", "v.cui:3:5: error: 'Slider' requires the argument 'value'"),
        ("view V {\n    Label(\"x\", y: 1)\n}\n", "v.cui:3:5: error: 'Label' has no argument 'y' (arguments: text)"),
        ("view V {\n    Label(\"x\").style(colour: \"a\")\n}\n", "v.cui:3:15: error: unknown theme property 'colour'"),
        ("view V {\n    Label(\"x\").onClick(go)\n}\n", "v.cui:3:15: error: 'Label' has no event 'onClick' (events: onHover, onFocus)"),
        ("view V {\n    Label(\"x\").width(.wide)\n}\n", "v.cui:3:15: error: '.width(.wide)': expected a number, .fit or .fill"),
        ("view V {\n    Label(\"x\").align(.middle)\n}\n", "v.cui:3:15: error: unknown alignment '.middle'"),
        ("view V {\n    Label(\"x\") { Label(\"y\") }\n}\n", "v.cui:3:5: error: 'Label' cannot have children"),
        ("view V {\n    Tabs { Label(\"x\") }\n}\n", "v.cui:3:12: error: 'Tabs' only contains 'Page' elements, not 'Label'"),
        ("view V {\n    Label(\"\\(.center)\")\n}\n", "v.cui:3:5: error: '.center' is only allowed as a modifier argument"),
        ("view V {\n    @Mutable var s: S\n    Label($s.name)\n}\n", "v.cui:4:5: error: argument 'text' of 'Label' is a text, not a binding"),
        ("view V {\n    Label(\"x\")\n    Label(\"y\")\n}\n", "v.cui:4:5: error: A view may only have one root element"),
    ];
    for (src, want) in cases {
        let err = errors(src);
        assert!(err.iter().any(|e| e.starts_with(want)), "for {src:?}\n  expected a message starting with: {want}\n  got: {err:?}");
    }
}

#[test]
fn the_legacy_target_includes_the_header_named_after_the_file() {
    // Registre réel des composants hérités (doxygen des en-têtes include/UI).
    let include = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../include");
    let registry = crate::scanner::Scanner::new().scan_directory(&include).unwrap();
    let src = "view F {\n    VBox {\n    }\n}\n";
    let (_, cpp, _) = compile_source(src, "st.cui", "st", Some(&registry)).unwrap();
    assert!(cpp.contains("#include \"st.gen.h\""), "{cpp}");
    assert!(!cpp.contains("#include \"F.gen.h\""));
}

#[test]
fn the_legacy_target_refuses_two_way_bindings() {
    let registry = Registry::default();
    let err = compile_source("view F {\n    VBox($x)\n}\n", "f.cui", "f", Some(&registry)).unwrap_err();
    assert_eq!(err, vec!["f.cui:2:5: error: two-way bindings ($...) need `import \"engine/ui\"`".to_string()]);
}

#[test]
fn a_slider_default_is_set_after_construction() {
    let (_, source) = engine("view V {\n    @Mutable var s: S\n    Slider($s.v, min: 0, max: 10, default: 3)\n}\n").unwrap();
    assert!(source.contains("->setDefault(static_cast<double>(3.0));"), "{source}");
}

#[test]
fn the_spec_lists_every_component_as_json() {
    let spec = crate::engine::spec_json();
    assert!(spec.starts_with("{\"import\":\"engine/ui\""));
    for name in ["\"VBox\"", "\"Slider\"", "\"ParamPanel\"", "\"onClick\"", "\"imageTint\""] {
        assert!(spec.contains(name), "{name} missing");
    }
    assert_eq!(spec.matches('{').count(), spec.matches('}').count());
}

#[test]
fn ui03_components_generate_their_widgets_and_check_their_children() {
    let (header, source) = engine(
        "view V {\n    @Mutable var s: S\n    VBox {\n        MenuBar {\n            Menu(\"File\") {\n                MenuItem(\"Grid\", checked: $s.grid)\n            }\n        }\n        Dialog(\"D\", open: $s.open) {\n            Label(\"in\")\n        }\n        .width(200)\n        GradientEditor($s.gradient)\n        ImageView(s.map).fit(.cover)\n        Button(\"Go\").shortcut(\"Ctrl+G\")\n        TextArea($s.notes, wrap: false)\n    }\n}\n",
    )
    .unwrap();
    assert!(header.contains("#include \"engine/ui/menus.hpp\""));
    assert!(source.contains("->items().add(std::move("));
    assert!(source.contains("engine::ui::MenuItem>(std::string(\"Grid\"), std::string(), engine::ui::Binding<bool>::ref(s->grid))"));
    assert!(source.contains("->body().add(std::move("));
    assert!(source.contains("(&cui_w4->panel())->setWidth(engine::ui::SizeSpec::fixed(static_cast<float>(200.0)));"));
    assert!(source.contains("engine::ui::Binding<engine::ui::Gradient>::ref(s->gradient)"));
    assert!(source.contains("[=] { return engine::ui::imageOf(s->map); }"));
    assert!(source.contains("setFit(engine::ui::ImageFit::kCover);"));
    assert!(source.contains("->setShortcut(std::string(\"Ctrl+G\"));"));
    assert!(source.contains("->setLineNumbers(false).setReadOnly(false).setWrap(static_cast<bool>(false));"));
    assert_eq!(
        errors("view V {\n    MenuBar {\n        Label(\"x\")\n    }\n}\n"),
        vec!["v.cui:4:9: error: 'MenuBar' only contains 'Menu' elements, not 'Label'".to_string()]
    );
    assert_eq!(
        errors("view V {\n    VBox {\n        MenuItem(\"x\")\n    }\n}\n"),
        vec!["v.cui:4:9: error: 'MenuItem' is only allowed inside 'Menu' or 'ContextMenu'".to_string()]
    );
}
