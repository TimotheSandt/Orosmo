# UI_LANGUAGE_AUDIT : audit du langage d'interface maison (`.cui`)

Audit du 2026-11-17. Source : `tools/cui-rs/` (Rust, ~4 350 lignes : `lexer.rs`, `parser.rs`, `ast.rs`, `validator.rs`, `generator.rs`, `scanner.rs`, `registry.rs`, `main.rs`, `json.rs`), `apps/demo/src/UI/Views/PerformanceView.cui`, `docs/UI_DSL_Spec.md` (v0.2 « In Progress »), `include/UI/`. Le compilateur a été **exécuté** (binaire construit par `make all-debug`, `tools/cui`) sur la vue réelle et sur des fichiers d'essai écrits pour l'audit (hors dépôt). Ce qui n'est pas visible dans le code est signalé comme tel ; aucune syntaxe n'est inventée.

## 1. Vue d'ensemble

| Élément | Constat |
| --- | --- |
| Compilateur | Programme Rust autonome `cui` : sous-commandes `scan`, `build`, `run`, `redirect`, `check`, `help` (`run_cli` dans `main.rs`). Remplace un ancien précompilateur Python (README de `tools/cui-rs/`). |
| Entrée | Fichiers `.cui` (un seul dans le dépôt : `PerformanceView.cui`, 31 lignes). |
| Sortie | `<fichier>.gen.h` + `<fichier>.gen.cpp` + un fichier « redirect » `<fichier>.cui` qui inclut le `.gen.h` (`write_redirect`). Classe `ui::generated::<Vue>` dérivée de `UI::View`, fabrique `Create<Vue>`, alias `UI::<Vue>`. |
| Registre de composants | Généré par `cui scan` à partir des **commentaires doxygen** des en-têtes de `include/UI/` (`@cui-component`, `@cui-modifier`, `@cui-factory`, `@cui-alias`, `@cui-accepts-children`, `@cui-content-model`, `@cui-extends`). Résultat réel : 5 composants (`Container`, `HBox`, `VBox`, `Text`, `Label`), 0 fonction, 0 enum. |
| Intégration au build | `tools/engine-sdk/engine.mk:194-220` : règle `Generated/%.gen.cpp: src/%.cui $(tous les .h scannés)` : régénération quand un en-tête du moteur change ; hachage du source en tête de `.gen.h` (`// @cui-hash:`) pour sauter les fichiers inchangés ; `OUTPUT_FORMAT_VERSION` dans le hachage. Dépendances donc correctes (au prix de tout régénérer à chaque changement d'en-tête). |
| Éditeur | `tools/vscode-cui/` : coloration syntaxique et diagnostics en appelant `cui check --stdin` (JSON de diagnostics). |
| Tests | **Aucun** test du compilateur (`grep '#\[test\]' tools/` : 0). Les tests C++ de `tests/suites/binding_tests.cpp` (5) et `ui_foundation_tests.cpp` (14) exercent le runtime, pas le compilateur. |

## 2. Grammaire réelle (déduite du lexer et du parser)

Jetons (`lexer.rs:2-40`) : mots-clés `view let func once always var import true false return`, décorateurs `@Observed @Mutable @Snapshot @State @Binding @cui-cpp`, identifiants, entiers, flottants, chaînes (avec `\(…)` interpolé), symboles `-> = : , . :: ( ) { } ? + - * / ;`. Commentaires `//` seulement (`lexer.rs:427`). Les nombres sont des pixels par défaut ; pas de suffixe d'unité vu dans le lexer.

```
file        := import* view*
import      := "import" STRING
view        := "view" IDENT "{" view_stmt* "}"            // exactement 1 élément racine (parser.rs:78)
view_stmt   := wrapped_field | let | func | cpp_block | element
wrapped_field := ("@Observed"|"@Mutable"|"@Snapshot"|"@State"|"@Binding") "var" IDENT ":" type ["=" expr]
type        := IDENT ("::" IDENT)* ["?"]
let         := "let" IDENT [":" type] "=" expr
func        := "func" IDENT "(" params ")" "->" type ( "=" expr | "{" "return" expr [";"] "}" )
cpp_block   := "@cui-cpp" "{" … "}"                       // avertissement du validateur : échappatoire
element     := IDENT [ "(" args ")" ] [ "{" element* "}" ] modifier*
modifier    := "." IDENT [ "(" args ")" ]
args        := arg ("," arg)* ; arg := [IDENT ":"] expr
expr        := unary (("+"|"-"|"*"|"/") unary)*
unary       := ("once"|"always") primary | postfix
postfix     := primary ("." IDENT ["(" args ")"])*
primary     := INT | FLOAT | "true" | "false" | STRING | "." IDENT      // cas d'enum
             | IDENT ("::" IDENT)* [ "(" args ")" ] | "(" expr ")"
```

Pas de : `if`/boucles/`ForEach`, comparaisons, `&&`/`||`, tableaux, plusieurs racines, composants définis en `.cui` (une vue n'est pas composable depuis une autre vue dans le registre actuel), événements.

## 3. Exemple réel et code généré

Source (`apps/demo/src/UI/Views/PerformanceView.cui`, extrait) :

```
view PerformanceView {
    @Observed var window: Window?
    func toMs(d: nanoseconds) -> double = double(d.count()) * 1e-6
    let fpsScale    = 0.50
    VBox {
        Text("FPS: \(window.averageFPS)")
            .scale(fpsScale)
        Text("Render: \(always toMs(Profiler.averageTime("Render"))) ms")
            .scale(metricScale)
    }
    .frame(width: 260, height: 175, anchor: .topLeft)
    .padding(12)
    .throttle(0.25)
}
```

Généré (`PerformanceView.gen.cpp`, extrait réel) :

```cpp
_root->AddChild(UI::CreateText(UI::Bounds(), UI::TextContent("FPS: ", UI::Bind(window, &Window::GetAverageFPS)))
    ->SetTextScale(fpsScale));
_root->AddChild(UI::CreateText(UI::Bounds(), UI::TextContent("Render: ",
    UI::BindAlways([&]() { return toMs(Profiler::GetAverageTime("Render")); }), " ms"))
    ->SetTextScale(metricScale));
...
_root->SetThrottlePeriod(std::chrono::duration<double>(0.25));
```

Constats :
- `window.averageFPS` est résolu en `Window::GetAverageFPS()` (convention `Get` + CamelCase, `UI_DSL_Spec.md` §0) ; les types et méthodes **ne sont pas vérifiés** par le validateur (registre sans fonctions : `functions: []`) : une faute se découvre à la compilation C++ du `.gen.cpp`.
- Les lambdas captent par référence `[&]` (`UI::BindAlways([&]() { … })`) : risque de référence pendante si la vue survit au contexte ; pas d'avertissement.
- Les `let` deviennent `constexpr auto` (`constexpr auto fpsScale = 0.5;`).
- Deux modificateurs « spéciaux » traités par le générateur et non par le registre : `.frame(width:, height:, anchor:)` et `.throttle(...)` (`validator.rs:160`, `generator.rs:846,898`).
- Le widget interactif n'existe pas dans le registre : seul `Text`/`Label` (contenu), `Container`, `HBox`, `VBox`.

## 4. Gestion d'erreurs : essais réels

Fichiers d'essai écrits pour l'audit (hors dépôt). Résultats exacts :

| Essai | Commande | Sortie |
| --- | --- | --- |
| Composant inconnu `Button("x")` | `cui run` | `[cui] bad1.cui: [A:3:9] ERROR: Unknown component 'Button'. Known components: Container, HBox, Label, Text, VBox.` |
| Parenthèse non fermée `Text("hi"` | `cui run` | `[cui] ERROR parsing bad2.cui: Parse error at 4:5: expected RParen (got RBrace)` — ligne 4 (la ligne `}` suivante), pas la ligne 3 où la parenthèse manque |
| Modificateur inconnu `.colour(…)` | `cui run` | `[bad3] … [C:3:19] ERROR: 'Text' has no modifier 'colour'. Known modifiers: autoSize, color, scale, textAnchor, textScale.` |
| Événement `Text("x").onClick(go)` | `cui run` | `[E:4:13] ERROR: 'Text' has no modifier 'onClick'. Known modifiers: autoSize, color, scale, textAnchor, textScale.` |
| Même erreurs en mode éditeur | `cui check --registry …` | JSON `[{"line":3,"col":9,"endLine":3,"endCol":10,"message":"…","severity":"error"}]` |

Évaluation par rapport à UI-05 (« erreurs avec fichier, ligne et colonne du source ») :
- **Ligne et colonne du source `.cui` : oui** pour le lexer, le parseur et le validateur. **Fichier : oui à l'exécution `run`** (préfixe `[cui] <fichier>:` ou `ERROR parsing <fichier>:`) ; absent du JSON de `check` (le fichier est celui du document ouvert).
- Le format varie (`[Vue:ligne:col]` pour le validateur, `Parse error at ligne:col` pour le parseur) ; la colonne d'une erreur « jeton attendu » est celle du **jeton suivant**.
- **Erreurs du C++ généré : non rattachées au `.cui`** : aucune directive `#line` dans `.gen.cpp` (grep `#line` dans `tools/cui-rs/src` : 0). Une faute de type (`window.averageFPZ`) apparaît comme une erreur de compilation dans `engine/Generated/UI/Views/PerformanceView.gen.cpp`.
- Les avertissements existent (`is_warning`) et un compilateur de vue invalide ne génère rien (`diagnostics.iter().any(fatal)` → `Err`).

### Défauts constatés (reproduits)

1. **Nom de l'en-tête généré** : le compilateur écrit `<stem du fichier>.gen.h` (`compile_file` dans `main.rs`) mais le `.gen.cpp` inclut `"<NomDeVue>.gen.h"`. Essai : `st.cui` contenant `view F { … }` produit `st.gen.h`, `st.gen.cpp` et `st.cui` (redirect), alors que `st.gen.cpp` fait `#include "F.gen.h"` → compilation impossible. Le dépôt n'est pas touché parce que la vue de la démo a le même nom que son fichier. À corriger (UI-05) : un nom, ou une vérification dans le validateur.
2. Fichier généré : une vue invalide n'écrit rien mais le fichier « redirect » est écrit **avant** la validation (`write_redirect` précède `validate`), laissant un `X.cui` orphelin dans `Generated/`.
3. `@State` n'est pas écrivable depuis le langage (aucun événement), donc inutile pour l'instant.

## 5. Événements et liaisons

| Besoin (`docs/03` §12, UI-05) | Réel |
| --- | --- |
| Événements `onClick`, `onChange`, `onHover`, `onFocus` liés à des fonctions externes par nom | **Absents** : 0 occurrence dans le lexer, le parseur, le validateur, le générateur et le registre ; `.onClick(go)` échoue (« no modifier »). Côté runtime, `ComponentBase` n'expose ni clic ni focus : seulement `IsMouseOver` et `Bounds::isHover`. |
| Liaison de données typée dans les deux sens | **Un seul sens** : `@Observed` (lecture, `const T*`), `@Mutable` (`T*`), `@Snapshot` (valeur figée). `@State` : local, « 🚧 » dans la spec ; `@Binding` : « 📅 v0.3 ». Interpolation `\(expr)` → `UI::TextContent` avec `UI::Bind(objet, &Classe::Méthode)` (méthode suivie par comparaison, `BindMethods.h`) ou `UI::BindAlways(lambda)` ; `once` fige. |
| `DeferredValue<T>` | Utilisé par le **runtime** (`include/UI/Core/DeferredValue.h`, double tampon : `Set` mis en attente, `Apply` à l'image suivante) pour la saleté des composants ; **le langage ne le manipule pas** directement. 4 tests (`ui_foundation_tests.cpp:72-99`). |
| Registre d'événements par nom (GAME-04) | Absent. |
| Logique métier dans le langage | Limitée : `func f(...) -> T = expr` (une expression), `@cui-cpp { … }` (échappatoire que le validateur déconseille). Une expression peut appeler des fonctions C++ arbitraires (`Profiler.averageTime(...)`) : la séparation « le langage décrit, il ne décide pas » (règle 12) n'est pas imposée par le compilateur. |

## 6. Widgets, mise en page, thèmes (côté runtime)

| Élément | Réel | Preuve |
| --- | --- | --- |
| Widgets | `Text`, `Label` (alias), `Container` | `include/UI/Widgets/`, registre |
| Mise en page | `VBox`, `HBox` (padding, spacing, `childAlignment`, `justifyContent`, `overflowMode` dont `SCROLL`), tailles `px` et `%`, ancrages (`Anchor`) | `include/UI/Layout/`, `Core/Bounds.h`, `Core/Container.h` |
| Pas de | grille, superposition, tailles flexibles/minimales, focus, tabulation, clic | grep `focus|tab|click` : 0 |
| Thème | `UI::Theme`/`Colors`/`IdentifierKind` en C++ (11 couleurs), préréglages `LIGHT`/`DARK` ; **pas de JSON** ; modificateur `.color(r,g,b,a)` en dur dans la vue | `Theme.h`, `PerformanceView.cui:28` |
| Rendu | Les composants créent des `Sprite` et dessinent via `Renderer2D` (`DrawSprite`, `PushClipRect`, `RenderText`) ; **pas de liste de dessin** | `src/UI/Core/Component.cpp`, `Container.cpp` |
| Rechargement à chaud | Aucun (la vue est construite une fois par `Build()`) | `docs/UI_DSL_Spec.md` §2 |

## 7. Ce qui manque pour UI-03 à UI-12

| ID | Manque | Proposition |
| --- | --- | --- |
| UI-03 | Tous les widgets sauf texte : bouton, curseur, case, liste, champ de texte (PLAT-05 requis), sélecteur de couleur, graphique, onglets, défilement (partiellement `SCROLL`), infobulles | Les ajouter comme composants du registre, avec états `hover/pressed/disabled/focus` lus dans le thème |
| UI-04 | Dear ImGui absent | Dépendance à décider (vcpkg `imgui`) ; hors langage |
| UI-05 | Événements ; liaison bidirectionnelle (`@Binding`/`@State` mutables) ; `#line` dans le C++ généré ; correction du nom d'en-tête ; fichier dans les messages `check` ; tests du compilateur | Ajouter `onClick:` etc. comme **arguments nommant une fonction** (`onClick: startGeneration`), générés en appel direct (final) ou recherche par nom (dev) |
| UI-06 | Grille, superposition, tailles flexibles/minimales | Étendre `Container`, puis le registre |
| UI-07 | Thème JSON, jetons (couleur, rayon, police, taille, marge, ombre, bordure), états, clair/sombre à chaud, surcharge locale | Nécessite une bibliothèque JSON (CORE-12) ; `.color(...)` du langage → `.token("color.primary")` |
| UI-08 | Mode interprété ou bibliothèque dynamique | Le générateur produit déjà un arbre « construit par `Build()` » : une seconde cible (interprète du même AST) est envisageable |
| UI-09, UI-10 | Focus, tabulation, inspecteur | Dépendent d'UI-03 et du rendu par listes de dessin (UI-02) |
| UI-11 | `docs/UI_LANGUAGE.md` (la spec `UI_DSL_Spec.md` est en anglais, v0.2 « In Progress », et contient des cases cochées dont la preuve manque) ; tests de compilation valides/invalides | Écrire à partir de §2-§4 de ce rapport ; ajouter `cargo test` + un script `tests/ui_lang/` |
| UI-12 | Dépend de la réflexion (CORE-12, absente) | Après CORE-12 |

## 8. Points à valider (voir `docs/QUESTIONS.md`)

- Les annotations `@cui-*` dans les en-têtes du moteur contredisent la règle de `UI_DSL_Spec.md` (« ne jamais ajouter d'annotations `@cui-*` hors du système UI ») ? Non : elles sont **dans** `include/UI/`. À confirmer.
- Le langage reste-t-il en Rust (`tools/cui-rs`) et dans le dépôt, ou migre-t-il en C++ pour ne pas dépendre de `cargo` ? (Rust est requis par le build aujourd'hui.)
- Garder l'interpolation de chaîne avec `Bind` implicite (spécifique à `Text`) ou la généraliser aux propriétés (couleur, taille) ?
