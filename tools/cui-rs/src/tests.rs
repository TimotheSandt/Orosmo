// UI-11 : tests du compilateur (cargo test --manifest-path tools/Cargo.toml -p cui). Entrées valides : formes du code généré ;
// entrées invalides : message exact et position `fichier:ligne:colonne`. Les cas de bout en bout (binaire cui, compilateur C++)
// sont dans tests/ui/cui_errors/ (make cui-test) et tests/ui/cui/ (compilés avec les tests C++).

use crate::compile_source;
use crate::lexer::{Lexer, TokenType};

fn engine(src: &str) -> Result<(String, String), Vec<String>> {
    compile_source(&format!("import \"orosmo/ui\"\n{src}"), "v.cui", "v")
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
    assert!(header.contains("inline std::unique_ptr<orosmo::ui::Widget> buildPanel(orosmo::ui::Ui &cui_ui, State &state, const Stats &stats)"));
    assert!(source.contains("#include \"v.gen.h\""));
    assert!(source.contains("orosmo::ui::Slider>(orosmo::ui::numberBinding(state->scale), static_cast<double>(0.0), static_cast<double>(10.0), static_cast<double>(1.0))"));
    assert!(source.contains("decltype(state->changed(std::forward<decltype(cui_a)>(cui_a)...))"));
    assert!(source.contains("bindText([=] { return orosmo::ui::text(\"FPS \", stats->fps); });"));
    assert!(source.contains("onClick(cui_ui.actions().bind(\"regenerate\"));"));
    assert!(source.contains("setWidth(orosmo::ui::SizeSpec::fixed(static_cast<float>(w)));"));
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
    assert!(source.contains("#line 4 \"v.cui\"\n    auto cui_w1 = std::make_unique<orosmo::ui::Label>();"));
    assert!(source.contains("#line 5 \"v.cui\"\n    auto cui_w2 = std::make_unique<orosmo::ui::Label>();"));
}

#[test]
fn tabs_pages_sections_and_param_panels() {
    let (_, source) = engine(
        "view V {\n    @Mutable var s: S\n    Tabs {\n        Page(\"A\") { ParamPanel($s.params) }\n        Page(\"B\") { Label(\"1\") Label(\"2\") }\n    }\n}\n",
    )
    .unwrap();
    assert!(source.contains("orosmo::ui::reflectedPanel(s->params)"));
    assert!(source.contains("addPage(std::string(\"A\"), std::move("));
    assert!(source.contains("addPage(std::string(\"B\"), std::move("));
    assert!(source.contains("std::make_unique<orosmo::ui::VBox>()")); // page à plusieurs enfants
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
fn a_file_without_the_engine_ui_import_is_refused() {
    // La cible héritée UI::View a été supprimée : sans `import "orosmo/ui"`, rien n'est généré.
    let err = compile_source("view F {\n    VBox {}\n}\n", "f.cui", "f").unwrap_err();
    assert_eq!(err.len(), 1, "{err:?}");
    assert!(err[0].starts_with("f.cui:1:1: error: missing `import \"orosmo/ui\"`"), "{err:?}");
}

#[test]
fn a_slider_default_is_set_after_construction() {
    let (_, source) = engine("view V {\n    @Mutable var s: S\n    Slider($s.v, min: 0, max: 10, default: 3)\n}\n").unwrap();
    assert!(source.contains("->setDefault(static_cast<double>(3.0));"), "{source}");
}

#[test]
fn the_spec_lists_every_component_as_json() {
    let spec = crate::engine::spec_json();
    assert!(spec.starts_with("{\"import\":\"orosmo/ui\""));
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
    assert!(header.contains("#include \"orosmo/ui/menus.hpp\""));
    assert!(source.contains("->items().add(std::move("));
    assert!(source.contains("orosmo::ui::MenuItem>(std::string(\"Grid\"), std::string(), orosmo::ui::Binding<bool>::ref(s->grid))"));
    assert!(source.contains("->body().add(std::move("));
    assert!(source.contains("(&cui_w4->panel())->setWidth(orosmo::ui::SizeSpec::fixed(static_cast<float>(200.0)));"));
    assert!(source.contains("orosmo::ui::Binding<orosmo::ui::Gradient>::ref(s->gradient)"));
    assert!(source.contains("[=] { return orosmo::ui::imageOf(s->map); }"));
    assert!(source.contains("setFit(orosmo::ui::ImageFit::kCover);"));
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

#[test]
fn review_m3_names_numbers_arguments_and_descendants() {
    // Variable du helper préfixée (cui_view) ; préfixe cui_ réservé aux noms générés.
    let (header, _) = engine("view V {\n    @Mutable var model: S\n    Label(\"x\")\n}\n").unwrap();
    assert!(header.contains("V cui_view;"), "{header}");
    assert!(header.contains("cui_view.model = &model;"), "{header}");
    let err = errors("view V {\n    @Mutable var cui_w0: S\n    Label(\"x\")\n}\n");
    assert!(err.iter().any(|e| e.contains("v.cui:3:5: error: 'cui_w0': names starting with 'cui_' are reserved")), "{err:?}");
    // Le type d'un champ est rapporté à sa ligne du .cui dans l'en-tête.
    let (header, _) = engine("view V {\n    @Mutable var s: MissingType\n    Label(\"x\")\n}\n").unwrap();
    assert!(header.contains("#line 3 \"v.cui\"\n    MissingType *s = nullptr;"), "{header}");
    // Grands nombres : littéral flottant (exposant), pas un entier trop grand.
    let (_, source) = engine("view V {\n    Label(\"x\").width(100000000000000000000.0)\n}\n").unwrap();
    assert!(source.contains("static_cast<float>(1e20)"), "{source}");
    // Grid : nombre de colonnes borné ; un littéral hors plage est refusé.
    let (_, source) = engine("view V {\n    Grid(2) {\n        Label(\"x\")\n    }\n}\n").unwrap();
    assert!(source.contains("std::clamp(static_cast<double>(static_cast<double>(2.0)), 1.0, 256.0)"), "{source}");
    let err = errors("view V {\n    Grid(2147483648) {\n        Label(\"x\")\n    }\n}\n");
    assert!(err.iter().any(|e| e.contains("'Grid' columns must be an integer from 1 to 256")), "{err:?}");
    // Deuxième argument positionnel ou argument nommé inconnu : refusés au lieu d'être ignorés.
    let err = errors("view V {\n    Label(\"x\").align(.start, .end)\n}\n");
    assert!(err.iter().any(|e| e.contains(".align(x: .start, y: .end)")), "{err:?}");
    let err = errors("view V {\n    Label(\"x\").margin(4, 16)\n}\n");
    assert!(err.iter().any(|e| e.contains(".margin(horizontal: h, vertical: v)")), "{err:?}");
    let err = errors("view V {\n    Plot(values).range(0, 100, typo: 1)\n}\n");
    assert!(err.iter().any(|e| e.contains("'.range' takes two numbers")), "{err:?}");
    // Composant inconnu : ses descendants sont vérifiés aussi (deux erreurs en une passe).
    let err = errors("view V {\n    Unknown {\n        Label(\"x\").widht(30)\n    }\n}\n");
    assert!(err.iter().any(|e| e.contains("unknown component 'Unknown'")), "{err:?}");
    assert!(err.iter().any(|e| e.contains("'Label' has no modifier 'widht'")), "{err:?}");
}

#[test]
fn cache_hash_is_invalidated_when_cpp_write_fails() {
    use std::fs;
    let unique = format!(
        "cui_cache_test_{}_{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    let root = std::env::temp_dir().join(unique);
    let out_dir = root.join("out");
    fs::create_dir_all(&out_dir).unwrap();
    let cui_path = root.join("panel.cui");
    let header_path = out_dir.join("panel.gen.h");
    let cpp_path = out_dir.join("panel.gen.cpp");

    let src_a = "import \"orosmo/ui\"\nview Panel {\n    Label(\"version A\")\n}\n";
    let src_b = "import \"orosmo/ui\"\nview Panel {\n    Label(\"version B\")\n}\n";

    fs::write(&cui_path, src_a).unwrap();
    crate::compile_file(&cui_path, &out_dir).unwrap();
    let old_cpp = fs::read_to_string(&cpp_path).unwrap();
    assert!(old_cpp.contains("version A"));

    // Simule un échec d'écriture sur .gen.cpp (répertoire à sa place : échoue même sous root).
    fs::remove_file(&cpp_path).unwrap();
    fs::create_dir(&cpp_path).unwrap();
    fs::write(&cui_path, src_b).unwrap();
    assert!(crate::compile_file(&cui_path, &out_dir).is_err());
    assert_ne!(
        crate::read_stored_hash(&header_path).as_deref(),
        Some(crate::source_hash(src_b).as_str()),
        "le hash de B ne doit pas être validé après un échec d'écriture de .gen.cpp"
    );

    // Remet l'ancien .gen.cpp (version A) : la relance doit régénérer B au lieu d'ignorer le fichier.
    fs::remove_dir(&cpp_path).unwrap();
    fs::write(&cpp_path, &old_cpp).unwrap();
    crate::compile_file(&cui_path, &out_dir).unwrap();
    let new_cpp = fs::read_to_string(&cpp_path).unwrap();
    assert!(new_cpp.contains("version B"), "le .gen.cpp périmé a été conservé : {new_cpp}");
    assert_eq!(
        crate::read_stored_hash(&header_path).as_deref(),
        Some(crate::source_hash(src_b).as_str())
    );

    let _ = fs::remove_dir_all(&root);
}

#[test]
fn lex_number_restores_column_when_suffix_is_rejected() {
    let tokens = Lexer::new("1.foo bar").tokenize().unwrap();
    assert_eq!(tokens[0].ty, TokenType::IntLit);
    assert_eq!((tokens[0].col, tokens[0].end_col), (1, 2));
    assert_eq!(tokens[1].ty, TokenType::Dot);
    assert_eq!((tokens[1].col, tokens[1].end_col), (2, 3));
    assert_eq!(tokens[2].ty, TokenType::Ident);
    assert_eq!((tokens[2].col, tokens[2].end_col), (3, 6));
    assert_eq!(tokens[3].ty, TokenType::Ident);
    assert_eq!((tokens[3].col, tokens[3].end_col), (7, 10));

    // Les suffixes de durée reconnus (comme 1.ms) restent acceptés.
    let dur = Lexer::new("1.ms x").tokenize().unwrap();
    assert_eq!(dur[0].ty, TokenType::FloatLit);
    assert_eq!(dur[0].text(), "0.001");
    assert_eq!((dur[0].col, dur[0].end_col), (1, 5));
    assert_eq!(dur[1].ty, TokenType::Ident);
    assert_eq!((dur[1].col, dur[1].end_col), (6, 7));
}

#[test]
fn a_named_argument_given_twice_is_refused_at_its_label() {
    let err = errors("view V {\n    @Mutable var s: S\n    Slider($s.v, min: 0, max: 10, min: 1)\n}\n");
    assert_eq!(err, vec!["v.cui:4:35: error: argument 'min' is given twice".to_string()]);
}

#[test]
fn a_cui_cpp_block_is_copied_exactly() {
    let (header, _) = engine(
        "view V {\n    @cui-cpp {\n        std::string greeting = \"a  b\";\n        int twice(int v) { return v * 2; }\n    }\n    Label(\"x\")\n}\n",
    )
    .unwrap();
    assert!(header.contains("std::string greeting = \"a  b\";\n        int twice(int v) { return v * 2; }"), "{header}");
}

#[test]
fn source_hash_changes_with_compiler_fingerprint_or_version() {
    let src = "import \"orosmo/ui\"\nview V { Label(\"ok\") }\n";
    assert_eq!(crate::COMPILER_FINGERPRINT.len(), 16);
    assert!(crate::COMPILER_FINGERPRINT.chars().all(|c| c.is_ascii_hexdigit()));

    let h_base = crate::source_hash_with_meta("10", "0.3.1", "0123456789abcdef", src);
    let h_diff_fp = crate::source_hash_with_meta("10", "0.3.1", "fedcba9876543210", src);
    let h_diff_ver = crate::source_hash_with_meta("10", "0.3.2", "0123456789abcdef", src);
    let h_diff_fmt = crate::source_hash_with_meta("11", "0.3.1", "0123456789abcdef", src);
    assert_ne!(h_base, h_diff_fp);
    assert_ne!(h_base, h_diff_ver);
    assert_ne!(h_base, h_diff_fmt);
    assert_eq!(
        crate::source_hash(src),
        crate::source_hash_with_meta(
            crate::OUTPUT_FORMAT_VERSION,
            crate::COMPILER_VERSION,
            crate::COMPILER_FINGERPRINT,
            src
        )
    );
}
