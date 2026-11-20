// Cible `engine/ui` (UI-05) : un fichier qui commence par `import "engine/ui"` est compilé vers le module `engine::ui` du moteur
// (docs/03 §12) au lieu de l'interface héritée `UI::`. Le langage DÉCRIT (règle 12) : l'arbre de widgets, les liaisons (`$chemin` :
// dans les deux sens) et les événements, qui NOMMENT une fonction C++ (`.onClick(state.regenerate)` : appel direct ;
// `.onClick(regenerate)` : action retrouvée par nom dans `Ui::actions()` au moment de l'événement). L'apparence vient du thème
// (`.style(...)` nomme des jetons). Chaque instruction C++ générée est précédée d'une directive `#line` vers le `.cui` : une erreur
// du compilateur C++ montre le fichier, la ligne du langage d'interface.

use crate::ast::{Element, Expr, FileAst, FuncDecl, LetDecl, Modifier, StringPart, ViewDecl, ViewField, WrappedField};
use crate::validator::ValidationError;
use std::collections::BTreeMap;

pub const IMPORT: &str = "engine/ui";

pub fn is_engine_target(ast: &FileAst) -> bool {
    ast.imports.iter().any(|import| import.path == IMPORT)
}

// ── Composants ───────────────────────────────────────────────────────────────────────────────────────────────────────

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Kind {
    Text,       // texte : chaîne (interpolée ou non) ou expression
    BindBool,   // valeur d'un widget : `$chemin` (deux sens) ou expression (lecture seule)
    BindNumber,
    BindInt,
    BindString,
    BindColor,
    BindGradient, // engine::ui::Gradient
    BindCurve,    // engine::ui::Curve
    Image,      // expression -> DynamicImage ou std::shared_ptr<const AtlasImage>, relue à chaque image
    Number,     // nombre évalué à la construction
    Bool,       // booléen évalué à la construction
    Options,    // expression -> std::vector<std::string>, relue à chaque image
    Values,     // expression -> suite de nombres, relue à chaque image
    Reflected,  // `$chemin` vers une structure réfléchie
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Default {
    Required,
    Value(&'static str),
    Omit,
}

struct Param {
    name: &'static str,
    kind: Kind,
    default: Default,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Children {
    None,
    Many,
    One,   // ScrollView
    Body,  // Section : enfants dans body()
    Pages, // Tabs : seulement des Page
    Page,  // Page : contenu d'un onglet
    Items, // Menu, ContextMenu : enfants dans items()
    Menus, // MenuBar : seulement des Menu
}

struct Component {
    name: &'static str,
    params: &'static [Param],
    children: Children,
    events: &'static [&'static str],
}

const EV_BASIC: &[&str] = &["onHover", "onFocus"];
const EV_CLICK: &[&str] = &["onClick", "onHover", "onFocus"];
const EV_CHANGE: &[&str] = &["onChange", "onHover", "onFocus"];
const EV_ALL: &[&str] = &["onClick", "onChange", "onHover", "onFocus"];

const fn p(name: &'static str, kind: Kind, default: Default) -> Param {
    Param { name, kind, default }
}

static COMPONENTS: &[Component] = &[
    Component { name: "VBox", params: &[], children: Children::Many, events: EV_BASIC },
    Component { name: "HBox", params: &[], children: Children::Many, events: EV_BASIC },
    Component { name: "Panel", params: &[], children: Children::Many, events: EV_BASIC },
    Component { name: "Overlay", params: &[], children: Children::Many, events: EV_BASIC },
    Component { name: "Grid", params: &[p("columns", Kind::Number, Default::Required)], children: Children::Many, events: EV_BASIC },
    Component { name: "ScrollView", params: &[], children: Children::One, events: EV_BASIC },
    Component { name: "Tabs", params: &[p("selected", Kind::BindInt, Default::Omit)], children: Children::Pages, events: EV_CHANGE },
    Component { name: "Page", params: &[p("title", Kind::Text, Default::Required)], children: Children::Page, events: &[] },
    Component {
        name: "Section",
        params: &[p("title", Kind::Text, Default::Required), p("expanded", Kind::Bool, Default::Value("true"))],
        children: Children::Body,
        events: EV_BASIC,
    },
    Component { name: "Label", params: &[p("text", Kind::Text, Default::Value("\"\""))], children: Children::None, events: EV_BASIC },
    Component { name: "Button", params: &[p("text", Kind::Text, Default::Required)], children: Children::None, events: EV_CLICK },
    Component {
        name: "Checkbox",
        params: &[p("text", Kind::Text, Default::Required), p("checked", Kind::BindBool, Default::Required)],
        children: Children::None,
        events: EV_ALL,
    },
    Component {
        name: "Slider",
        params: &[
            p("value", Kind::BindNumber, Default::Required),
            p("min", Kind::Number, Default::Required),
            p("max", Kind::Number, Default::Required),
            p("step", Kind::Number, Default::Value("0.0")),
            p("default", Kind::Number, Default::Omit),
        ],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component {
        name: "Dropdown",
        params: &[p("options", Kind::Options, Default::Required), p("selected", Kind::BindInt, Default::Required)],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component { name: "TextField", params: &[p("text", Kind::BindString, Default::Required)], children: Children::None, events: EV_CHANGE },
    Component {
        name: "NumberField",
        params: &[
            p("value", Kind::BindNumber, Default::Required),
            p("integer", Kind::Bool, Default::Value("false")),
            p("min", Kind::Number, Default::Value("-engine::ui::NumberField::kUnboundedNumber")),
            p("max", Kind::Number, Default::Value("engine::ui::NumberField::kUnboundedNumber")),
        ],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component {
        name: "ColorPicker",
        params: &[p("color", Kind::BindColor, Default::Required), p("alpha", Kind::Bool, Default::Value("false"))],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component { name: "Plot", params: &[p("values", Kind::Values, Default::Required)], children: Children::None, events: EV_BASIC },
    Component { name: "Separator", params: &[], children: Children::None, events: EV_BASIC },
    Component { name: "ParamPanel", params: &[p("target", Kind::Reflected, Default::Required)], children: Children::None, events: EV_CHANGE },
    // UI-03 : contrôles supplémentaires (controls.hpp), couleur (color.hpp), fenêtres et menus (menus.hpp).
    Component {
        name: "RadioButton",
        params: &[p("text", Kind::Text, Default::Required), p("value", Kind::Number, Default::Required), p("selected", Kind::BindInt, Default::Required)],
        children: Children::None,
        events: EV_ALL,
    },
    Component {
        name: "RangeSlider",
        params: &[
            p("low", Kind::BindNumber, Default::Required),
            p("high", Kind::BindNumber, Default::Required),
            p("min", Kind::Number, Default::Required),
            p("max", Kind::Number, Default::Required),
            p("step", Kind::Number, Default::Value("0.0")),
        ],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component {
        name: "SpinBox",
        params: &[
            p("value", Kind::BindNumber, Default::Required),
            p("min", Kind::Number, Default::Value("-engine::ui::NumberField::kUnboundedNumber")),
            p("max", Kind::Number, Default::Value("engine::ui::NumberField::kUnboundedNumber")),
            p("step", Kind::Number, Default::Value("1.0")),
            p("integer", Kind::Bool, Default::Value("true")),
        ],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component {
        name: "TextArea",
        params: &[
            p("text", Kind::BindString, Default::Required),
            p("lineNumbers", Kind::Bool, Default::Value("false")),
            p("readOnly", Kind::Bool, Default::Value("false")),
            p("wrap", Kind::Bool, Default::Value("true")),
        ],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component { name: "ColorWheel", params: &[p("color", Kind::BindColor, Default::Required)], children: Children::None, events: EV_CHANGE },
    Component {
        name: "ColorEditor",
        params: &[p("color", Kind::BindColor, Default::Required), p("alpha", Kind::Bool, Default::Value("false"))],
        children: Children::None,
        events: EV_CHANGE,
    },
    Component { name: "GradientEditor", params: &[p("gradient", Kind::BindGradient, Default::Required)], children: Children::None, events: EV_CHANGE },
    Component { name: "CurveEditor", params: &[p("curve", Kind::BindCurve, Default::Required)], children: Children::None, events: EV_CHANGE },
    Component {
        name: "Dialog",
        params: &[p("title", Kind::Text, Default::Required), p("open", Kind::BindBool, Default::Required), p("dismiss", Kind::Bool, Default::Value("true"))],
        children: Children::Body,
        events: EV_CHANGE,
    },
    Component { name: "MenuBar", params: &[], children: Children::Menus, events: EV_BASIC },
    Component { name: "Menu", params: &[p("title", Kind::Text, Default::Required)], children: Children::Items, events: EV_BASIC },
    Component {
        name: "MenuItem",
        params: &[p("text", Kind::Text, Default::Required), p("shortcut", Kind::Text, Default::Value("std::string()")), p("checked", Kind::BindBool, Default::Omit)],
        children: Children::None,
        events: EV_CLICK,
    },
    Component { name: "ContextMenu", params: &[], children: Children::Items, events: &[] },
    Component {
        name: "Notifications",
        params: &[p("max", Kind::Number, Default::Value("5.0")), p("maxHeight", Kind::Number, Default::Value("0.0")), p("width", Kind::Number, Default::Value("0.0"))],
        children: Children::None,
        events: &[],
    },
    Component { name: "ImageView", params: &[p("image", Kind::Image, Default::Required)], children: Children::None, events: EV_BASIC },
];

const COMMON_MODIFIERS: &[&str] =
    &["width", "height", "margin", "align", "span", "id", "tooltip", "visible", "enabled", "style", "variant", "overflow"];

// Propriétés du thème (engine/ui/theme.hpp) acceptées par `.style(...)`.
const STYLE_PROPERTIES: &[(&str, &str)] = &[
    ("background", "kBackground"),
    ("foreground", "kForeground"),
    ("border", "kBorder"),
    ("accent", "kAccent"),
    ("muted", "kMuted"),
    ("shadowColor", "kShadowColor"),
    ("borderWidth", "kBorderWidth"),
    ("radius", "kRadius"),
    ("padding", "kPadding"),
    ("spacing", "kSpacing"),
    ("fontSize", "kFontSize"),
    ("shadowSize", "kShadowSize"),
    ("shadowOffset", "kShadowOffset"),
    ("width", "kWidth"),
    ("image", "kImage"),
    ("imageTint", "kImageTint"),
    ("font", "kFont"),
];

fn component(name: &str) -> Option<&'static Component> {
    COMPONENTS.iter().find(|c| c.name == name)
}

fn extra_modifiers(component: &str) -> &'static [&'static str] {
    match component {
        "Label" => &["textAlign"],
        "Plot" => &["range"],
        "ImageView" => &["fit"],
        "Button" => &["shortcut"],
        _ => &[],
    }
}

fn known_list(items: impl IntoIterator<Item = impl AsRef<str>>) -> String {
    items.into_iter().map(|s| s.as_ref().to_string()).collect::<Vec<_>>().join(", ")
}

// ── Contexte d'une vue ───────────────────────────────────────────────────────────────────────────────────────────────

struct Scope<'a> {
    fields: BTreeMap<String, &'a WrappedField>,
    lets: BTreeMap<String, &'a LetDecl>,
    funcs: BTreeMap<String, &'a FuncDecl>,
    params: Vec<String>, // paramètres de la fonction en cours (corps d'un `func`)
}

impl<'a> Scope<'a> {
    fn new(view: &'a ViewDecl) -> Self {
        let mut scope = Scope { fields: BTreeMap::new(), lets: BTreeMap::new(), funcs: BTreeMap::new(), params: Vec::new() };
        for field in &view.fields {
            match field {
                ViewField::Wrapped(w) => {
                    scope.fields.insert(w.name.clone(), w);
                }
                ViewField::Let(l) => {
                    scope.lets.insert(l.name.clone(), l);
                }
                ViewField::Func(f) => {
                    scope.funcs.insert(f.name.clone(), f);
                }
                ViewField::Cpp(_) => {}
            }
        }
        scope
    }

    fn is_pointer_field(&self, name: &str) -> bool {
        self.fields.get(name).is_some_and(|f| f.wrapper == "@Mutable" || f.wrapper == "@Observed")
    }
}

type Diag = (usize, usize, String);

// ── Expressions ──────────────────────────────────────────────────────────────────────────────────────────────────────

// Toujours un littéral flottant C++ : un point décimal ou un exposant (1e20 écrit en entier serait un entier trop grand).
fn cpp_number(value: f64) -> String {
    if value.abs() >= 1e15 {
        format!("{value:e}")
    } else if value.fract() == 0.0 {
        format!("{value:.1}")
    } else {
        format!("{value}")
    }
}

fn cpp_string(text: &str) -> String {
    let mut out = String::from("\"");
    for ch in text.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\t' => out.push_str("\\t"),
            '\r' => out.push_str("\\r"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn path_cpp(path: &str, scope: &Scope) -> String {
    let mut parts = path.split('.');
    let first = parts.next().unwrap_or("");
    let rest: Vec<&str> = parts.collect();
    if scope.is_pointer_field(first) && !scope.params.iter().any(|p| p == first) {
        if rest.is_empty() {
            return format!("(*{first})");
        }
        return format!("{first}->{}", rest.join("."));
    }
    path.to_string()
}

fn expr_cpp(expr: &Expr, scope: &Scope) -> Result<String, String> {
    match expr {
        Expr::Number(v) => Ok(cpp_number(*v)),
        Expr::Bool(b) => Ok(if *b { "true".into() } else { "false".into() }),
        Expr::Identifier(name) => Ok(path_cpp(name, scope)),
        Expr::PropertyAccess { obj, prop } => {
            if obj == "?" {
                return Err("a property can only follow a name (for example state.seed)".into());
            }
            Ok(path_cpp(&format!("{obj}.{prop}"), scope))
        }
        Expr::Call { callee, args, named_args } => {
            if !named_args.is_empty() {
                return Err(format!("named arguments are not allowed in a call to the C++ function '{callee}'"));
            }
            if callee.starts_with("?.") {
                return Err("a call can only follow a name (for example state.fps())".into());
            }
            let args = args.iter().map(|a| expr_cpp(a, scope)).collect::<Result<Vec<_>, _>>()?;
            Ok(format!("{}({})", path_cpp(callee, scope), args.join(", ")))
        }
        Expr::Binary { op, left, right } => Ok(format!("({} {op} {})", expr_cpp(left, scope)?, expr_cpp(right, scope)?)),
        Expr::Capture { expr, .. } => expr_cpp(expr, scope),
        Expr::StringLit(parts) => text_call(parts, scope),
        Expr::EnumCase(name) => Err(format!("'.{name}' is only allowed as a modifier argument (for example .align(.center))")),
        Expr::Binding(path) => Err(format!("'${path}': a two-way binding is only allowed as the value of a widget")),
    }
}

fn text_call(parts: &[StringPart], scope: &Scope) -> Result<String, String> {
    if parts.iter().all(|p| matches!(p, StringPart::Text(_))) {
        let text: String = parts.iter().map(|p| if let StringPart::Text(t) = p { t.as_str() } else { "" }).collect();
        return Ok(format!("std::string({})", cpp_string(&text)));
    }
    let mut args = Vec::new();
    for part in parts {
        match part {
            StringPart::Text(t) => args.push(cpp_string(t)),
            StringPart::Interp { expr, .. } => args.push(expr_cpp(expr, scope)?),
        }
    }
    Ok(format!("engine::ui::text({})", args.join(", ")))
}

fn is_constant_text(expr: &Expr) -> bool {
    match expr {
        Expr::StringLit(parts) => parts.iter().all(|p| matches!(p, StringPart::Text(_))),
        _ => false,
    }
}

// ── Validation et génération (une seule passe : la génération échoue avec les diagnostics) ──────────────────────────

struct Gen<'a> {
    scope: Scope<'a>,
    file: String,
    lines: Vec<String>,
    diags: Vec<Diag>,
    counter: usize,
    page_titles: BTreeMap<String, String>, // contenu d'une Page -> son titre, consommé par le Tabs parent
    current_line: usize,
}

impl<'a> Gen<'a> {
    fn err(&mut self, line: usize, col: usize, message: impl Into<String>) {
        self.diags.push((line, col, message.into()));
    }

    // Ligne du `.cui` des instructions suivantes.
    fn line_directive(&mut self, line: usize) {
        self.current_line = line.max(1);
    }

    // Chaque instruction est précédée de `#line` : une erreur du compilateur C++ désigne la ligne du `.cui` qui l'a produite.
    fn emit(&mut self, text: impl Into<String>) {
        self.lines.push(format!("#line {} \"{}\"", self.current_line, self.file));
        self.lines.push(format!("    {}", text.into()));
    }

    fn fresh(&mut self) -> String {
        let name = format!("cui_w{}", self.counter);
        self.counter += 1;
        name
    }

    // Arguments d'un composant : positionnels dans l'ordre, puis nommés.
    fn bind_args(&mut self, element: &'a Element, comp: &Component) -> Option<Vec<Option<&'a Expr>>> {
        let mut values: Vec<Option<&Expr>> = vec![None; comp.params.len()];
        let mut ok = true;
        if element.args.len() > comp.params.len() {
            self.err(
                element.line,
                element.col,
                format!("'{}' takes at most {} argument(s) ({}), {} given", comp.name, comp.params.len(),
                        known_list(comp.params.iter().map(|p| p.name)), element.args.len()),
            );
            return None;
        }
        for (i, arg) in element.args.iter().enumerate() {
            values[i] = Some(arg);
        }
        for (name, arg) in &element.named_args {
            match comp.params.iter().position(|p| p.name == name) {
                Some(i) if values[i].is_some() => {
                    self.err(element.line, element.col, format!("argument '{name}' of '{}' is given twice", comp.name));
                    ok = false;
                }
                Some(i) => values[i] = Some(arg),
                None => {
                    self.err(
                        element.line,
                        element.col,
                        format!("'{}' has no argument '{name}' (arguments: {})", comp.name,
                                if comp.params.is_empty() { "none".to_string() } else { known_list(comp.params.iter().map(|p| p.name)) }),
                    );
                    ok = false;
                }
            }
        }
        for (i, param) in comp.params.iter().enumerate() {
            if values[i].is_none() && param.default == Default::Required {
                self.err(element.line, element.col, format!("'{}' requires the argument '{}'", comp.name, param.name));
                ok = false;
            }
        }
        ok.then_some(values)
    }

    // Valeur C++ d'un argument selon son genre (None : erreur déjà signalée).
    fn arg_cpp(&mut self, line: usize, col: usize, comp: &str, param: &Param, expr: &Expr) -> Option<String> {
        let what = format!("argument '{}' of '{comp}'", param.name);
        let scope = &self.scope;
        let result: Result<String, String> = match (param.kind, expr) {
            (Kind::Text, Expr::Binding(path)) => Err(format!("{what} is a text, not a binding: write \"\\({path})\"")),
            (Kind::Text, e) => expr_cpp(e, scope).map(|v| if matches!(e, Expr::StringLit(_)) { v } else { format!("engine::ui::text({v})") }),
            (Kind::Number, Expr::StringLit(_)) | (Kind::Bool, Expr::StringLit(_)) => Err(format!("{what} expects a {}", if param.kind == Kind::Number { "number" } else { "boolean" })),
            (Kind::Number, e) => expr_cpp(e, scope).map(|v| format!("static_cast<double>({v})")),
            (Kind::Bool, e) => expr_cpp(e, scope).map(|v| format!("static_cast<bool>({v})")),
            (Kind::Options, Expr::StringLit(_)) | (Kind::Options, Expr::Number(_)) => Err(format!("{what} expects an expression that gives a std::vector<std::string>")),
            (Kind::Options, e) => expr_cpp(e, scope).map(|v| format!("[=] {{ return {v}; }}")),
            (Kind::Values, Expr::StringLit(_)) | (Kind::Values, Expr::Number(_)) => Err(format!("{what} expects an expression that gives a sequence of numbers")),
            (Kind::Values, e) => expr_cpp(e, scope).map(|v| format!("[=] {{ const auto &values = {v}; return std::vector<float>(std::begin(values), std::end(values)); }}")),
            (Kind::Reflected, Expr::Binding(path)) => self.binding_target(path).map(|lv| format!("engine::ui::reflectedPanel({lv})")),
            (Kind::Image, Expr::StringLit(_)) | (Kind::Image, Expr::Number(_)) | (Kind::Image, Expr::Binding(_)) => Err(format!("{what} expects an expression that gives an engine::ui::DynamicImage or a std::shared_ptr<const AtlasImage> (for example state.minimap)")),
            (Kind::Image, e) => expr_cpp(e, scope).map(|v| format!("[=] {{ return engine::ui::imageOf({v}); }}")),
            (Kind::Reflected, _) => Err(format!("{what} must be a two-way binding to a reflected structure (for example ParamPanel($state.params))")),
            (kind, Expr::Binding(path)) => self.binding_target(path).map(|lv| match kind {
                Kind::BindBool => format!("engine::ui::Binding<bool>::ref({lv})"),
                Kind::BindNumber => format!("engine::ui::numberBinding({lv})"),
                Kind::BindInt => format!("engine::ui::intBinding({lv})"),
                Kind::BindString => format!("engine::ui::Binding<std::string>::ref({lv})"),
                Kind::BindGradient => format!("engine::ui::Binding<engine::ui::Gradient>::ref({lv})"),
                Kind::BindCurve => format!("engine::ui::Binding<engine::ui::Curve>::ref({lv})"),
                _ => format!("engine::ui::colorBinding({lv})"),
            }),
            (kind, e) => {
                let read = match kind {
                    Kind::BindBool => ("bool", "static_cast<bool>"),
                    Kind::BindNumber => ("double", "static_cast<double>"),
                    Kind::BindInt => ("int", "static_cast<int>"),
                    Kind::BindString => ("std::string", "engine::ui::text"),
                    Kind::BindGradient => ("engine::ui::Gradient", "engine::ui::Gradient"),
                    Kind::BindCurve => ("engine::ui::Curve", "engine::ui::Curve"),
                    _ => ("engine::ui::Color", "engine::ui::Color"),
                };
                expr_cpp(e, scope).map(|v| format!("engine::ui::Binding<{}>{{[=] {{ return {}({v}); }}, {{}}}}", read.0, read.1))
            }
        };
        match result {
            Ok(v) => Some(v),
            Err(message) => {
                self.err(line, col, message);
                None
            }
        }
    }

    // `$chemin` : la racine doit être un champ @Mutable de la vue.
    fn binding_target(&self, path: &str) -> Result<String, String> {
        let root = path.split('.').next().unwrap_or("");
        match self.scope.fields.get(root) {
            Some(field) if field.wrapper == "@Mutable" => Ok(path_cpp(path, &self.scope)),
            Some(field) => Err(format!(
                "'${path}': '{root}' is {} (read only); a two-way binding needs a @Mutable field",
                field.wrapper
            )),
            None => Err(format!("'${path}': a two-way binding must start with a @Mutable field of the view ('{root}' is not a field)")),
        }
    }

    fn element(&mut self, element: &'a Element, parent: Option<&str>) -> Option<String> {
        let Some(comp) = component(&element.name) else {
            let names = known_list(COMPONENTS.iter().filter(|c| c.name != "Page").map(|c| c.name));
            self.err(element.line, element.col, format!("unknown component '{}' (components: {names})", element.name));
            // Les descendants sont vérifiés quand même (leurs erreurs sont indépendantes) ; rien n'est écrit en cas d'erreur.
            for child in &element.children {
                let _ = self.element(child, None);
            }
            return None;
        };
        if comp.name == "Page" && parent != Some("Tabs") {
            self.err(element.line, element.col, "'Page' is only allowed directly inside 'Tabs'");
            return None;
        }
        if comp.name == "MenuItem" && parent != Some("Menu") && parent != Some("ContextMenu") {
            self.err(element.line, element.col, "'MenuItem' is only allowed inside 'Menu' or 'ContextMenu'");
            return None;
        }
        let args = self.bind_args(element, comp)?;
        let mut cpp_args = Vec::new();
        for (param, value) in comp.params.iter().zip(&args) {
            match value {
                Some(expr) => cpp_args.push(self.arg_cpp(element.line, element.col, comp.name, param, expr)),
                None => cpp_args.push(match param.default {
                    Default::Value(v) => Some(v.to_string()),
                    _ => Some(String::new()),
                }),
            }
        }
        if cpp_args.iter().any(Option::is_none) {
            return None;
        }
        let a: Vec<String> = cpp_args.into_iter().map(Option::unwrap).collect();

        // Enfants autorisés.
        match comp.children {
            Children::None if !element.children.is_empty() => {
                self.err(element.line, element.col, format!("'{}' cannot have children", comp.name));
                return None;
            }
            Children::One if element.children.len() != 1 => {
                self.err(element.line, element.col, format!("'{}' takes exactly one child (put several in a VBox)", comp.name));
                return None;
            }
            Children::Pages => {
                if let Some(bad) = element.children.iter().find(|c| c.name != "Page") {
                    self.err(bad.line, bad.col, format!("'Tabs' only contains 'Page' elements, not '{}'", bad.name));
                    return None;
                }
            }
            Children::Menus => {
                if let Some(bad) = element.children.iter().find(|c| c.name != "Menu") {
                    self.err(bad.line, bad.col, format!("'MenuBar' only contains 'Menu' elements, not '{}'", bad.name));
                    return None;
                }
            }
            _ => {}
        }

        let var = self.fresh();
        self.line_directive(element.line);
        if comp.name == "Page" {
            // Le contenu d'un onglet : l'enfant unique, ou une VBox qui contient les enfants.
            if element.children.len() == 1 {
                let child = self.element(&element.children[0], Some("Page"))?;
                self.line_directive(element.line);
                self.emit(format!("auto {var} = std::move({child});"));
            } else {
                self.emit(format!("auto {var} = std::make_unique<engine::ui::VBox>();"));
                for child in &element.children {
                    if let Some(c) = self.element(child, Some("Page")) {
                        self.line_directive(child.line);
                        self.emit(format!("{var}->add(std::move({c}));"));
                    }
                }
            }
            self.page_titles.insert(var.clone(), a[0].clone());
            if !element.modifiers.is_empty() {
                let m = &element.modifiers[0];
                self.err(m.line, m.col, "'Page' takes no modifier (modify its content)");
            }
            return Some(var);
        }

        let ctor = match comp.name {
            "Grid" => {
                // Nombre de colonnes : entier de 1 à 256 ; un littéral hors plage est refusé, une expression est bornée avant la
                // conversion en int (une conversion d'un double hors de la plage d'int est indéfinie).
                if let Some(Expr::Number(n)) = &args[0] {
                    if n.fract() != 0.0 || !(1.0..=256.0).contains(n) {
                        self.err(element.line, element.col, "'Grid' columns must be an integer from 1 to 256");
                    }
                }
                format!("std::make_unique<engine::ui::Grid>(static_cast<int>(std::clamp(static_cast<double>({}), 1.0, 256.0)))", a[0])
            }
            "Tabs" if args[0].is_some() => format!("std::make_unique<engine::ui::Tabs>({})", a[0]),
            "Section" => format!("std::make_unique<engine::ui::Section>({}, {})", a[0], a[1]),
            "Label" | "Button" => format!("std::make_unique<engine::ui::{}>()", comp.name),
            "Checkbox" => format!("std::make_unique<engine::ui::Checkbox>({}, {})", a[0], a[1]),
            "Slider" => format!("std::make_unique<engine::ui::Slider>({}, {}, {}, {})", a[0], a[1], a[2], a[3]),
            "Dropdown" => format!("std::make_unique<engine::ui::Dropdown>({}, {})", a[0], a[1]),
            "TextField" => format!("std::make_unique<engine::ui::TextField>({})", a[0]),
            "NumberField" => format!("std::make_unique<engine::ui::NumberField>({}, {}, {}, {})", a[0], a[1], a[2], a[3]),
            "ColorPicker" => format!("std::make_unique<engine::ui::ColorPicker>({}, {})", a[0], a[1]),
            "Plot" => format!("std::make_unique<engine::ui::Plot>({})", a[0]),
            "RadioButton" => format!("std::make_unique<engine::ui::RadioButton>({}, static_cast<int>({}), {})", a[0], a[1], a[2]),
            "RangeSlider" => format!("std::make_unique<engine::ui::RangeSlider>({}, {}, {}, {}, {})", a[0], a[1], a[2], a[3], a[4]),
            "SpinBox" => format!("std::make_unique<engine::ui::SpinBox>({}, {}, {}, {}, {})", a[0], a[1], a[2], a[3], a[4]),
            "TextArea" => format!("std::make_unique<engine::ui::TextArea>({})", a[0]),
            "ColorWheel" | "GradientEditor" | "CurveEditor" => format!("std::make_unique<engine::ui::{}>({})", comp.name, a[0]),
            "ColorEditor" => format!("std::make_unique<engine::ui::ColorEditor>({}, {})", a[0], a[1]),
            "Dialog" => format!("std::make_unique<engine::ui::Dialog>({}, {}, {})", a[0], a[1], a[2]),
            "Menu" => format!("std::make_unique<engine::ui::Menu>({})", a[0]),
            "MenuItem" if args[2].is_some() => format!("std::make_unique<engine::ui::MenuItem>({}, {}, {})", a[0], a[1], a[2]),
            "MenuItem" => format!("std::make_unique<engine::ui::MenuItem>({}, {})", a[0], a[1]),
            "Notifications" => format!("std::make_unique<engine::ui::Notifications>(static_cast<int>({}), static_cast<float>({}), static_cast<float>({}))", a[0], a[1], a[2]),
            "ImageView" => format!("std::make_unique<engine::ui::ImageView>({})", a[0]),
            "ParamPanel" => a[0].clone(),
            other => format!("std::make_unique<engine::ui::{other}>()"),
        };
        self.emit(format!("auto {var} = {ctor};"));
        if comp.name == "Slider" && args[4].is_some() {
            self.emit(format!("{var}->setDefault({});", a[4]));
        }
        if comp.name == "TextArea" {
            self.emit(format!("{var}->setLineNumbers({}).setReadOnly({}).setWrap({});", a[1], a[2], a[3]));
        }
        if comp.name == "Label" || comp.name == "Button" {
            let text_expr = args[0];
            match text_expr {
                Some(e) if !is_constant_text(e) => self.emit(format!("{var}->bindText([=] {{ return {}; }});", a[0])),
                _ => self.emit(format!("{var}->setText({});", a[0])),
            }
        }

        for modifier in &element.modifiers {
            self.modifier(&var, comp, modifier);
        }

        for child in &element.children {
            if let Some(c) = self.element(child, Some(comp.name)) {
                self.line_directive(child.line);
                match comp.children {
                    Children::Body => self.emit(format!("{var}->body().add(std::move({c}));")),
                    Children::Items => self.emit(format!("{var}->items().add(std::move({c}));")),
                    Children::Pages => {
                        let title = self.page_titles.remove(&c).unwrap_or_default();
                        self.emit(format!("{var}->addPage({title}, std::move({c}));"));
                    }
                    _ => self.emit(format!("{var}->add(std::move({c}));")),
                }
            }
        }
        Some(var)
    }

    fn modifier(&mut self, var: &str, comp: &Component, m: &'a Modifier) {
        let (line, col) = (m.line, m.col);
        let name = m.name.as_str();
        if name.starts_with("on") && name.len() > 2 {
            self.event(var, comp, m);
            return;
        }
        let extra = extra_modifiers(comp.name);
        if !COMMON_MODIFIERS.contains(&name) && !extra.contains(&name) {
            let all = COMMON_MODIFIERS.iter().chain(extra.iter()).chain(comp.events.iter());
            self.err(line, col, format!("'{}' has no modifier '{name}' (modifiers: {})", comp.name, known_list(all)));
            return;
        }
        self.line_directive(line);
        let one = |g: &mut Self| -> Option<&'a Expr> {
            if m.args.len() != 1 || !m.named_args.is_empty() {
                g.err(line, col, format!("'.{name}' takes exactly one argument"));
                return None;
            }
            Some(&m.args[0])
        };
        match name {
            "width" | "height" => {
                // Dialog : la taille est celle de la fenêtre (le widget lui-même ne prend pas de place dans l'arbre).
                let panel;
                let var = if comp.name == "Dialog" {
                    panel = format!("(&{var}->panel())");
                    panel.as_str()
                } else {
                    var
                };
                let member = name;
                let setter = if name == "width" { "setWidth" } else { "setHeight" };
                if m.args.len() > 1 {
                    self.err(line, col, format!("'.{name}' takes one size (a number, .fit or .fill) and min:, max:, weight:"));
                    return;
                }
                let weight = match m.named_args.get("weight") {
                    Some(e) => match expr_cpp(e, &self.scope) {
                        Ok(v) => format!("static_cast<float>({v})"),
                        Err(e) => return self.err(line, col, e),
                    },
                    None => "1.0f".into(),
                };
                match m.args.first() {
                    Some(Expr::EnumCase(case)) if case == "fill" => self.emit(format!("{var}->{setter}(engine::ui::SizeSpec::fill({weight}));")),
                    Some(Expr::EnumCase(case)) if case == "fit" => self.emit(format!("{var}->{setter}(engine::ui::SizeSpec::fit());")),
                    Some(Expr::EnumCase(case)) => return self.err(line, col, format!("'.{name}(.{case})': expected a number, .fit or .fill")),
                    Some(Expr::StringLit(_)) | Some(Expr::Binding(_)) => return self.err(line, col, format!("'.{name}' expects a number, .fit or .fill")),
                    Some(e) => match expr_cpp(e, &self.scope) {
                        Ok(v) => self.emit(format!("{var}->{setter}(engine::ui::SizeSpec::fixed(static_cast<float>({v})));")),
                        Err(e) => return self.err(line, col, e),
                    },
                    None => {}
                }
                for (key, value) in &m.named_args {
                    match key.as_str() {
                        "min" | "max" => match expr_cpp(value, &self.scope) {
                            Ok(v) => self.emit(format!("{var}->{member}.{key} = static_cast<float>({v});")),
                            Err(e) => self.err(line, col, e),
                        },
                        "weight" => {}
                        other => self.err(line, col, format!("'.{name}' has no argument '{other}' (arguments: min, max, weight)")),
                    }
                }
            }
            "margin" => {
                if m.args.len() > 1 {
                    return self.err(line, col, "'.margin' takes one value; for two axes write .margin(horizontal: h, vertical: v)");
                }
                if let Some(first) = m.args.first() {
                    match expr_cpp(first, &self.scope) {
                        Ok(v) => self.emit(format!("{var}->setMargin(engine::ui::Insets::all(static_cast<float>({v})));")),
                        Err(e) => return self.err(line, col, e),
                    }
                }
                for (key, value) in &m.named_args {
                    let targets: &[&str] = match key.as_str() {
                        "left" => &["left"],
                        "top" => &["top"],
                        "right" => &["right"],
                        "bottom" => &["bottom"],
                        "horizontal" => &["left", "right"],
                        "vertical" => &["top", "bottom"],
                        other => {
                            self.err(line, col, format!("'.margin' has no argument '{other}' (arguments: left, top, right, bottom, horizontal, vertical)"));
                            continue;
                        }
                    };
                    match expr_cpp(value, &self.scope) {
                        Ok(v) => {
                            for t in targets {
                                self.emit(format!("{var}->margin.{t} = static_cast<float>({v});"));
                            }
                        }
                        Err(e) => self.err(line, col, e),
                    }
                }
            }
            "align" => {
                let align = |g: &mut Self, e: &Expr| -> Option<&'static str> {
                    match e {
                        Expr::EnumCase(c) => match c.as_str() {
                            "start" => Some("engine::ui::Align::kStart"),
                            "center" => Some("engine::ui::Align::kCenter"),
                            "end" => Some("engine::ui::Align::kEnd"),
                            "stretch" => Some("engine::ui::Align::kStretch"),
                            other => {
                                g.err(line, col, format!("unknown alignment '.{other}' (.start, .center, .end, .stretch)"));
                                None
                            }
                        },
                        _ => {
                            g.err(line, col, "'.align' expects .start, .center, .end or .stretch");
                            None
                        }
                    }
                };
                if m.args.len() > 1 {
                    return self.err(line, col, "'.align' takes one value; for two axes write .align(x: .start, y: .end)");
                }
                if let Some(first) = m.args.first() {
                    if let Some(a) = align(self, first) {
                        self.emit(format!("{var}->alignX = {a};"));
                        self.emit(format!("{var}->alignY = {a};"));
                    }
                }
                for (key, value) in &m.named_args {
                    let member = match key.as_str() {
                        "x" => "alignX",
                        "y" => "alignY",
                        other => {
                            self.err(line, col, format!("'.align' has no argument '{other}' (arguments: x, y)"));
                            continue;
                        }
                    };
                    if let Some(a) = align(self, value) {
                        self.emit(format!("{var}->{member} = {a};"));
                    }
                }
            }
            "span" => {
                if let Some(e) = one(self) {
                    match expr_cpp(e, &self.scope) {
                        Ok(v) => self.emit(format!("{var}->columnSpan = static_cast<int>({v});")),
                        Err(e) => self.err(line, col, e),
                    }
                }
            }
            "id" | "variant" => {
                if let Some(e) = one(self) {
                    if !is_constant_text(e) {
                        return self.err(line, col, format!("'.{name}' expects a constant string"));
                    }
                    let v = expr_cpp(e, &self.scope).unwrap_or_default();
                    let setter = if name == "id" { "setId" } else { "setStyleName" };
                    self.emit(format!("{var}->{setter}({v});"));
                }
            }
            "tooltip" => {
                if let Some(e) = one(self) {
                    match expr_cpp(e, &self.scope) {
                        Ok(v) if matches!(e, Expr::StringLit(_)) => self.emit(format!("{var}->setTooltip({v});")),
                        Ok(v) => self.emit(format!("{var}->setTooltip(engine::ui::text({v}));")),
                        Err(e) => self.err(line, col, e),
                    }
                }
            }
            "visible" | "enabled" => {
                if let Some(e) = one(self) {
                    let (set, bind) = if name == "visible" { ("setVisible", "bindVisible") } else { ("setEnabled", "bindEnabled") };
                    match e {
                        Expr::Bool(b) => self.emit(format!("{var}->{set}({b});")),
                        Expr::StringLit(_) | Expr::Number(_) => self.err(line, col, format!("'.{name}' expects a boolean expression")),
                        _ => match expr_cpp(e, &self.scope) {
                            Ok(v) => self.emit(format!("{var}->{bind}([=] {{ return static_cast<bool>({v}); }});")),
                            Err(e) => self.err(line, col, e),
                        },
                    }
                }
            }
            "style" => {
                if !m.args.is_empty() || m.named_args.is_empty() {
                    return self.err(line, col, "'.style' takes named theme properties, for example .style(background: \"color.primary\")");
                }
                for (key, value) in &m.named_args {
                    let Some((_, cpp)) = STYLE_PROPERTIES.iter().find(|(n, _)| n == key) else {
                        self.err(line, col, format!("unknown theme property '{key}' (properties: {})", known_list(STYLE_PROPERTIES.iter().map(|(n, _)| *n))));
                        continue;
                    };
                    let text = match value {
                        e @ Expr::StringLit(_) if is_constant_text(e) => expr_cpp(e, &self.scope).unwrap_or_default(),
                        Expr::Number(n) => format!("std::string({})", cpp_string(&format!("{n}"))),
                        _ => {
                            self.err(line, col, format!("'.style({key}: ...)' expects a token name, a color \"#rrggbb\" or a number"));
                            continue;
                        }
                    };
                    self.emit(format!("{var}->overrideStyle(engine::ui::StyleProperty::{cpp}, {text});"));
                }
            }
            "overflow" => {
                if let Some(e) = one(self) {
                    match e {
                        Expr::EnumCase(c) if c == "visible" || c == "hidden" => {
                            let o = if c == "visible" { "kVisible" } else { "kHidden" };
                            self.emit(format!("{var}->overflow = engine::ui::Overflow::{o};"));
                        }
                        _ => self.err(line, col, "'.overflow' expects .visible or .hidden (to scroll: ScrollView)"),
                    }
                }
            }
            "textAlign" => {
                if let Some(e) = one(self) {
                    match e {
                        Expr::EnumCase(c) if ["start", "center", "end"].contains(&c.as_str()) => {
                            let a = match c.as_str() {
                                "start" => "kStart",
                                "center" => "kCenter",
                                _ => "kEnd",
                            };
                            self.emit(format!("{var}->setTextAlign(engine::ui::Align::{a});"));
                        }
                        _ => self.err(line, col, "'.textAlign' expects .start, .center or .end"),
                    }
                }
            }
            "shortcut" => {
                if let Some(e) = one(self) {
                    if !is_constant_text(e) {
                        return self.err(line, col, "'.shortcut' expects a constant string, for example .shortcut(\"Ctrl+R\")");
                    }
                    let v = expr_cpp(e, &self.scope).unwrap_or_default();
                    self.emit(format!("{var}->setShortcut({v});"));
                }
            }
            "fit" => {
                if let Some(e) = one(self) {
                    match e {
                        Expr::EnumCase(c) if ["contain", "cover", "stretch"].contains(&c.as_str()) => {
                            let f = match c.as_str() {
                                "contain" => "kContain",
                                "cover" => "kCover",
                                _ => "kStretch",
                            };
                            self.emit(format!("{var}->setFit(engine::ui::ImageFit::{f});"));
                        }
                        _ => self.err(line, col, "'.fit' expects .contain, .cover or .stretch"),
                    }
                }
            }
            "range" => {
                if m.args.len() != 2 || !m.named_args.is_empty() {
                    return self.err(line, col, "'.range' takes two numbers (min, max), without names");
                }
                match (expr_cpp(&m.args[0], &self.scope), expr_cpp(&m.args[1], &self.scope)) {
                    (Ok(lo), Ok(hi)) => self.emit(format!("{var}->setRange(static_cast<float>({lo}), static_cast<float>({hi}));")),
                    (Err(e), _) | (_, Err(e)) => self.err(line, col, e),
                }
            }
            _ => {}
        }
    }

    // Un événement NOMME une fonction C++ (règle 12) : `objet.méthode`, `Espace::fonction` (appel direct) ou `nom` (action du
    // registre de l'interface, retrouvée à l'appel).
    fn event(&mut self, var: &str, comp: &Component, m: &'a Modifier) {
        let (line, col) = (m.line, m.col);
        let name = m.name.as_str();
        if !["onClick", "onChange", "onHover", "onFocus"].contains(&name) {
            return self.err(line, col, format!("unknown event '{name}' (events: onClick, onChange, onHover, onFocus)"));
        }
        if !comp.events.contains(&name) {
            return self.err(line, col, format!("'{}' has no event '{name}' (events: {})", comp.name,
                                               if comp.events.is_empty() { "none".to_string() } else { known_list(comp.events.iter()) }));
        }
        if m.args.len() != 1 || !m.named_args.is_empty() {
            return self.err(line, col, format!("'.{name}' names one C++ function, for example .{name}(state.regenerate)"));
        }
        let target = match &m.args[0] {
            Expr::Identifier(n) => n.clone(),
            Expr::PropertyAccess { obj, prop } if obj != "?" => format!("{obj}.{prop}"),
            _ => {
                return self.err(
                    line,
                    col,
                    format!("'.{name}' names a C++ function (state.regenerate, Tools::save or a registered action name); it cannot be an expression or a call"),
                )
            }
        };
        let root = target.split(['.', ':']).next().unwrap_or("");
        if self.scope.funcs.contains_key(&target) {
            return self.err(line, col, format!("'.{name}({target})': '{target}' is a func of the view; an event must name a C++ function (no logic in the interface language)"));
        }
        self.line_directive(line);
        let handler = if target.contains('.') || target.contains("::") {
            if target.contains('.') && !self.scope.fields.contains_key(root) {
                return self.err(line, col, format!("'.{name}({target})': '{root}' is not a field of the view"));
            }
            let call = path_cpp(&target, &self.scope);
            format!(
                "engine::ui::handler([=](auto &&...cui_a) -> decltype({call}(std::forward<decltype(cui_a)>(cui_a)...)) {{ return {call}(std::forward<decltype(cui_a)>(cui_a)...); }})"
            )
        } else {
            format!("cui_ui.actions().bind({})", cpp_string(&target))
        };
        self.emit(format!("{var}->{name}({handler});"));
    }
}

// ── Fichier ──────────────────────────────────────────────────────────────────────────────────────────────────────────

fn cpp_type(dsl: &str) -> String {
    match dsl {
        "string" | "String" => "std::string".into(),
        "Int" => "int".into(),
        "Double" => "double".into(),
        "Float" => "float".into(),
        "Bool" => "bool".into(),
        other => other.into(),
    }
}

pub struct Output {
    pub header: String,
    pub source: String,
}

// Vérifie et génère ; en cas d'erreur, renvoie TOUTES les erreurs trouvées (ligne, colonne, message).
pub fn compile(ast: &FileAst, file: &str, stem: &str) -> Result<Output, Vec<ValidationError>> {
    let file_for_line = file.replace('\\', "/");
    let mut header = Vec::new();
    let mut source = Vec::new();
    let mut errors = Vec::new();

    header.push(format!("// AUTO-GENERATED by cui (engine/ui) - do not edit. Source: {file_for_line}"));
    header.push("#pragma once".into());
    header.push(String::new());
    header.push("#include \"engine/ui/reflect.hpp\"".into());
    header.push("#include \"engine/ui/ui.hpp\"".into());
    header.push("#include \"engine/ui/widgets.hpp\"".into());
    header.push("#include \"engine/ui/controls.hpp\"".into());
    header.push("#include \"engine/ui/color.hpp\"".into());
    header.push("#include \"engine/ui/menus.hpp\"".into());
    for import in ast.imports.iter().filter(|i| i.path != IMPORT) {
        header.push(format!("#include \"{}\"", import.path));
    }
    header.push(String::new());
    header.push("#include <algorithm>".into());
    header.push("#include <memory>".into());
    header.push("#include <string>".into());
    header.push("#include <vector>".into());
    header.push(String::new());
    header.push("namespace cui".into());
    header.push("{".into());

    source.push(format!("// AUTO-GENERATED by cui (engine/ui) - do not edit. Source: {file_for_line}"));
    source.push(format!("#include \"{stem}.gen.h\""));
    source.push(String::new());
    source.push("namespace cui".into());
    source.push("{".into());

    for view in &ast.views {
        let mut diags: Vec<Diag> = Vec::new();
        let scope = Scope::new(view);

        // Champs.
        let mut members = Vec::new();
        let mut helper_params = Vec::new();
        let mut helper_assign = Vec::new();
        let mut locals = Vec::new();
        for field in &view.fields {
            // Préfixe réservé au code généré (cui_ui, cui_view, cui_w0...) : un champ ou une constante de ce nom y entrerait en
            // collision ; `view` et les autres noms restent libres.
            let declared = match field {
                ViewField::Wrapped(w) => Some((w.name.as_str(), w.line, w.col)),
                ViewField::Let(l) => Some((l.name.as_str(), l.line, l.col)),
                _ => None,
            };
            if let Some((name, line, col)) = declared {
                if name.starts_with("cui_") {
                    diags.push((line, col, format!("'{name}': names starting with 'cui_' are reserved for the generated code")));
                }
            }
            match field {
                ViewField::Wrapped(w) => {
                    let ty = cpp_type(&w.type_name);
                    // Le type écrit dans l'en-tête est rapporté à la ligne du .cui (une erreur C++ « type inconnu » y renvoie).
                    members.push(format!("#line {} \"{file_for_line}\"", w.line.max(1)));
                    match w.wrapper.as_str() {
                        "@Mutable" | "@Observed" => {
                            let constness = if w.wrapper == "@Observed" { "const " } else { "" };
                            if w.default.is_some() {
                                diags.push((w.line, w.col, format!("'{}' is a {} reference: it has no default value", w.name, w.wrapper)));
                            }
                            members.push(format!("    {constness}{ty} *{} = nullptr;", w.name));
                            helper_params.push(format!("{constness}{ty} &{}", w.name));
                            helper_assign.push(format!("cui_view.{0} = &{0};", w.name));
                            locals.push(format!("    [[maybe_unused]] {constness}{ty} *const {0} = this->{0};", w.name));
                        }
                        "@Snapshot" => {
                            let init = match &w.default {
                                Some(e) => match expr_cpp(e, &scope) {
                                    Ok(v) => format!("{{{v}}}"),
                                    Err(e) => {
                                        diags.push((w.line, w.col, e));
                                        "{}".into()
                                    }
                                },
                                None => "{}".into(),
                            };
                            members.push(format!("    {ty} {}{init};", w.name));
                            helper_params.push(format!("{ty} {}", w.name));
                            helper_assign.push(format!("cui_view.{0} = std::move({0});", w.name));
                            locals.push(format!("    [[maybe_unused]] const auto {0} = this->{0};", w.name));
                        }
                        other => diags.push((
                            w.line,
                            w.col,
                            format!("{other} is not supported with engine/ui: keep the state in C++ and bind it with @Mutable"),
                        )),
                    }
                }
                ViewField::Let(l) => match expr_cpp(&l.value, &scope) {
                    Ok(v) => {
                        let ty = l.type_name.as_deref().map(cpp_type);
                        let value = if matches!(l.value, Expr::StringLit(_)) && is_constant_text(&l.value) {
                            // constexpr : un littéral, pas std::string
                            v.trim_start_matches("std::string(").trim_end_matches(')').to_string()
                        } else {
                            v
                        };
                        match ty {
                            Some(t) if t != "std::string" => members.push(format!("    static constexpr {t} {} = {value};", l.name)),
                            _ if matches!(l.value, Expr::StringLit(_)) => members.push(format!("    static constexpr const char *{} = {value};", l.name)),
                            _ => members.push(format!("    static constexpr auto {} = {value};", l.name)),
                        }
                    }
                    Err(e) => diags.push((l.line, l.col, e)),
                },
                ViewField::Func(f) => {
                    let mut fscope = Scope::new(view);
                    fscope.params = f.params.iter().map(|p| p.name.clone()).collect();
                    let params = f.params.iter().map(|p| format!("{} {}", cpp_type(&p.type_name), p.name)).collect::<Vec<_>>().join(", ");
                    match expr_cpp(&f.body, &fscope) {
                        Ok(body) => members.push(format!("    static {} {}({params}) {{ return {body}; }}", cpp_type(&f.return_type), f.name)),
                        Err(e) => diags.push((f.line, f.col, e)),
                    }
                }
                ViewField::Cpp(block) => members.push(format!("    {}", block.content)),
            }
        }

        // Arbre.
        let mut g = Gen { scope, file: file_for_line.clone(), lines: Vec::new(), diags: Vec::new(), counter: 0, page_titles: BTreeMap::new(), current_line: view.line };
        let root = g.element(&view.root, None);
        diags.extend(g.diags.drain(..));

        if !diags.is_empty() {
            for (line, col, message) in diags {
                errors.push(ValidationError { view: view.name.clone(), message, is_warning: false, line, col });
            }
            continue;
        }
        let root = root.unwrap_or_default();

        header.push(String::new());
        header.push(format!("// Vue `{}` ({file_for_line}:{}). Les objets liés doivent vivre plus longtemps que l'interface.", view.name, view.line));
        header.push(format!("struct {}", view.name));
        header.push("{".into());
        header.extend(members);
        header.push(String::new());
        header.push("    std::unique_ptr<engine::ui::Widget> build(engine::ui::Ui &cui_ui) const;".into());
        header.push("};".into());
        header.push(String::new());
        header.push(format!(
            "inline std::unique_ptr<engine::ui::Widget> build{}(engine::ui::Ui &cui_ui{}{})",
            view.name,
            if helper_params.is_empty() { "" } else { ", " },
            helper_params.join(", ")
        ));
        header.push("{".into());
        header.push(format!("    {} cui_view;", view.name));
        for a in &helper_assign {
            header.push(format!("    {a}"));
        }
        header.push("    return cui_view.build(cui_ui);".into());
        header.push("}".into());

        source.push(String::new());
        source.push(format!("#line {} \"{file_for_line}\"", view.line.max(1)));
        source.push(format!("std::unique_ptr<engine::ui::Widget> {}::build(engine::ui::Ui &cui_ui) const", view.name));
        source.push("{".into());
        source.push("    (void)cui_ui;".into());
        source.extend(locals);
        source.extend(g.lines);
        source.push(format!("    return {root};"));
        source.push("}".into());
    }

    header.push(String::new());
    header.push("} // namespace cui".into());
    source.push(String::new());
    source.push("} // namespace cui".into());

    if errors.is_empty() {
        Ok(Output { header: header.join("\n") + "\n", source: source.join("\n") + "\n" })
    } else {
        Err(errors)
    }
}

// ── Description du langage pour les éditeurs (`cui spec`) ───────────────────────────────────────────────────────────────

fn component_doc(name: &str) -> &'static str {
    match name {
        "VBox" => "Children one under the other (spacing from the theme).",
        "HBox" => "Children side by side (spacing from the theme).",
        "Panel" => "VBox with the theme style \"panel\" (background, border, shadow, image).",
        "Overlay" => "Children stacked on top of each other, each aligned with .align.",
        "Grid" => "Columns; a column holding a .width(.fill) child takes the remaining width; .span(n) covers several columns.",
        "ScrollView" => "One child taller than the view: wheel and scrollbar, clipped.",
        "Tabs" => "Pages (Page elements), one visible at a time.",
        "Page" => "A page of Tabs: its title, then its content.",
        "Section" => "Foldable block with a title.",
        "Label" => "Text; an interpolated string is re-read every frame.",
        "Button" => "Click with the mouse, or Enter / Space with the focus.",
        "Checkbox" => "Boolean bound with $path.",
        "Slider" => "Bounded number. Click: value under the pointer; drag: moves the current value (Shift: finer); right click: default.",
        "Dropdown" => "Choice in a list (popup).",
        "TextField" => "Editable text, committed by Enter or by losing the focus; Escape cancels.",
        "NumberField" => "Number typed with the keyboard; an invalid entry is refused.",
        "ColorPicker" => "Color: swatch and R, G, B (A) sliders.",
        "Plot" => "Bars of a sequence of numbers re-read every frame.",
        "Separator" => "Thin line.",
        "ParamPanel" => "Every field of a reflected structure as a widget chosen from its type and range (UI-12).",
        "RadioButton" => "One choice of a group: the buttons bound to the same int; selected when it equals `value`.",
        "RangeSlider" => "Two handles (low, high) on one track; the handles cannot cross.",
        "SpinBox" => "Number field with - and + buttons (and the wheel).",
        "TextArea" => "Multi-line text editor: selection, clipboard, undo / redo (Ctrl+Z / Ctrl+Y), word wrap, optional line numbers, read only.",
        "ColorWheel" => "Hue ring and saturation-value square.",
        "ColorEditor" => "Wheel, format (RGB, HSV, HSL) with its sliders, alpha, hexadecimal field, before / after preview.",
        "GradientEditor" => "Gradient bar: click adds a stop, drag moves it, right click removes it; the chosen stop is edited below.",
        "CurveEditor" => "Curve in [0, 1]: click adds a point, drag moves it, right click removes it.",
        "Dialog" => "Modal window, open while `open` is true; Escape, its close button or a click outside (dismiss) closes it. .width / .height size the window.",
        "MenuBar" => "Row of Menu; hovering another title while a menu is open switches to it.",
        "Menu" => "Title that opens its items (MenuItem, Menu for a submenu, Separator).",
        "MenuItem" => "Menu entry: click, Enter or its shortcut (installed, works with the menu closed) runs onClick; optional checked binding.",
        "ContextMenu" => "Invisible child of a widget: a right click on that widget opens its items at the pointer.",
        "Notifications" => "Stack of Ui::notify messages: at most `max` and `maxHeight` pixels (0: no limit), the oldest go.",
        "ImageView" => "Image produced by the game (DynamicImage: minimap, compass...), re-read every frame; .fit(.contain|.cover|.stretch).",
        _ => "",
    }
}

fn kind_name(kind: Kind) -> &'static str {
    match kind {
        Kind::Text => "text",
        Kind::BindBool => "bool ($binding or expression)",
        Kind::BindNumber => "number ($binding or expression)",
        Kind::BindInt => "int ($binding or expression)",
        Kind::BindString => "string ($binding or expression)",
        Kind::BindColor => "color ($binding or expression)",
        Kind::BindGradient => "gradient ($binding or expression)",
        Kind::BindCurve => "curve ($binding or expression)",
        Kind::Image => "expression -> DynamicImage or std::shared_ptr<const AtlasImage>",
        Kind::Number => "number",
        Kind::Bool => "bool",
        Kind::Options => "expression -> std::vector<std::string>",
        Kind::Values => "expression -> sequence of numbers",
        Kind::Reflected => "$binding to a reflected structure",
    }
}

fn modifier_doc(name: &str) -> &'static str {
    match name {
        "width" | "height" => "Size: a number (fixed), .fit or .fill; named min:, max:, weight:.",
        "margin" => "Outer margin: a number, or left:, top:, right:, bottom:, horizontal:, vertical:.",
        "align" => "Alignment in the parent's room: .start, .center, .end, .stretch; or x:, y:.",
        "span" => "Number of Grid columns covered.",
        "id" => "Identifier (Widget::find).",
        "tooltip" => "Tooltip text.",
        "visible" | "enabled" => "true / false, or a boolean expression re-read every frame.",
        "style" => "Local theme override: property: \"token\" | \"#rrggbb\" | number | image name.",
        "variant" => "Theme style name (widgets.<name> of the theme).",
        "textAlign" => "Label text alignment: .start, .center, .end.",
        "range" => "Plot: fixed bounds (min, max).",
        "shortcut" => "Button: keyboard shortcut that clicks it (\"Ctrl+R\", \"F5\").",
        "fit" => "ImageView: .contain (whole image), .cover (fills, cropped), .stretch (fills, distorted).",
        "overflow" => "What goes beyond the widget: .visible (default, still clickable) or .hidden (clipped).",
        "font" => "",
        "onClick" | "onChange" | "onHover" | "onFocus" => "Event: names a C++ function (state.method, Ns::function) or a registered action name.",
        _ => "",
    }
}

// JSON des composants, modificateurs, événements et propriétés de thème : la source unique des extensions d'éditeur.
pub fn spec_json() -> String {
    use crate::json::json_escape as e;
    let mut comps = Vec::new();
    for c in COMPONENTS {
        let params = c
            .params
            .iter()
            .map(|p| {
                let default = match p.default {
                    Default::Required => "null".to_string(),
                    Default::Value(v) => format!("\"{}\"", e(v)),
                    Default::Omit => "\"\"".to_string(),
                };
                format!("{{\"name\":\"{}\",\"kind\":\"{}\",\"default\":{}}}", p.name, e(kind_name(p.kind)), default)
            })
            .collect::<Vec<_>>()
            .join(",");
        let children = match c.children {
            Children::None => "none",
            Children::Many => "many",
            Children::One => "one",
            Children::Body => "many",
            Children::Pages => "pages",
            Children::Page => "many",
            Children::Items => "many",
            Children::Menus => "menus",
        };
        let events = c.events.iter().map(|ev| format!("\"{ev}\"")).collect::<Vec<_>>().join(",");
        let extra = extra_modifiers(c.name).iter().map(|m| format!("\"{m}\"")).collect::<Vec<_>>().join(",");
        comps.push(format!(
            "{{\"name\":\"{}\",\"doc\":\"{}\",\"params\":[{params}],\"children\":\"{children}\",\"events\":[{events}],\"modifiers\":[{extra}]}}",
            c.name,
            e(component_doc(c.name))
        ));
    }
    let mods = COMMON_MODIFIERS
        .iter()
        .chain(["textAlign", "range", "fit", "shortcut", "onClick", "onChange", "onHover", "onFocus"].iter())
        .map(|m| format!("{{\"name\":\"{m}\",\"doc\":\"{}\"}}", e(modifier_doc(m))))
        .collect::<Vec<_>>()
        .join(",");
    let props = STYLE_PROPERTIES.iter().map(|(n, _)| format!("\"{n}\"")).collect::<Vec<_>>().join(",");
    format!(
        "{{\"import\":\"{IMPORT}\",\"components\":[{}],\"modifiers\":[{mods}],\"styleProperties\":[{props}],\"alignments\":[\"start\",\"center\",\"end\",\"stretch\"],\"sizes\":[\"fit\",\"fill\"],\"overflows\":[\"visible\",\"hidden\"],\"fits\":[\"contain\",\"cover\",\"stretch\"],\"wrappers\":[\"@Mutable\",\"@Observed\",\"@Snapshot\"]}}",
        comps.join(",")
    )
}
