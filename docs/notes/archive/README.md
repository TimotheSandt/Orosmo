# Archive de travaux anciens (stashes Git du dépôt local, 2025-2026)

Les 17 stashes de l'auteur ont été analysés le 2026-11-17 (patchs complets hors dépôt : `../ProceduralGeneration-backups/stashes/stash-<n>.patch`). Seuls deux contiennent du travail encore utile pour le plan ; ils sont conservés ici **tels quels** (chemins d'avant M00 : `Libraries/…`), comme référence, pas comme code à appliquer.

| Fichier | Origine | Contenu | Utile pour |
| --- | --- | --- | --- |
| `stash-0-terrain-lod.patch` | « add LOD » sur `feat/vulkan`, 2026-05-10 | `Grid` : 3 niveaux de détail par distance (pas 1, 2, 4 ; seuils 80 et 160), sélection du niveau selon la caméra | TER-03 (maillage à niveaux de détail), M3 |
| `stash-3-vulkan-clear-attachments.patch` | « vulkan more » sur `feat/vulkan`, 2026-04-24 | `VulkanRenderState.cpp` : `ClearAttachments` (`vkCmdClearAttachments`, rectangle de nettoyage courant) | G41 (`Renderer::Clear` est un no-op sous Vulkan), M4 |

Autres stashes (non conservés dans le dépôt) : sans intérêt pour le plan, car périmés ou déjà remplacés — anciennes versions du `TextRenderer` (stb_truetype, remplacé par l'atlas MSDF), de l'`InputManager`, de `Mesh`/`Shader`/`Camera` (août 2025), correctifs d'interface (propagation du drapeau « sale », CRTP) déjà intégrés, retouches de `Makefile`. Un stash (`readme`, 2026-01-15) contient un fichier **`LICENSE` GPL-3.0** et un ancien `README` : à garder en tête pour la décision de licence (Q5, Apache-2.0 envisagée).

## Branche `UI` (analysée le 2026-11-21, gardée)

15 commits d'avril-mai 2026 (61 fichiers, 6 640 lignes) ; `feature/ui-dsl-spec` y est entièrement contenue. C'est l'ancêtre de ce
qui est dans `dev` : la plupart des idées y ont déjà été reprises ou remplacées.

| Dans la branche | Dans `dev` aujourd'hui |
| --- | --- |
| Précompilateur `.cui` en Python (`tools/cui/`), lexer, parseur, générateur | `tools/cui-rs` (Rust), cible `orosmo/ui` |
| Widget `Text` : échelle, ancrage (`TextAnchor`), taille automatique, contenu composé de morceaux et de valeurs liées (`TextContent`) | `Label` (`setTextAlign`), chaînes `"…{expr}…"` du langage, `TextArea` ; `TextRenderer` (polices par taille, ancrage) |
| Tests : bornes en pixels et pourcentages, ancrage centré, survol selon l'échelle | `tests/ui/` (mise en page, survol, menus, éditeurs) |
| Extension VS Code `cui-language` | `tools/vscode-cui` (diagnostics, complétion, survol) |
| « Ne pas recompiler si rien n'a changé » | `cui run` : « unchanged, skipping » (cache par empreinte) |
| README et cahier des charges du langage | `docs/UI_LANGUAGE.md` |

Deux idées **pas reprises**, à garder pour plus tard (G128) :

1. **Registre des composants lu dans les en-têtes C++** : des annotations `@cui-component`, `@cui-modifier`, `@cui-expose`,
   `@cui-enum` posées dans les en-têtes, que le précompilateur scanne pour savoir quels composants, modificateurs et fonctions
   le langage peut nommer. Dans `dev`, la table des composants est écrite dans le compilateur (`tools/cui-rs/src/engine.rs`,
   `cui spec`) : ajouter un composant demande de modifier le compilateur. Un registre lu dans les en-têtes permettrait à un
   module ou un addon (M11) d'ajouter ses propres widgets au langage sans toucher à `cui`.
2. **Fonctions liées avec cache** (`BindFunc`, `BindMethods`) : le résultat d'une fonction appelée depuis une vue est gardé tant
   que ses arguments n'ont pas changé (« sale » / « propre »), sauf si elle est marquée `@cui-volatile` (recalculée à chaque
   image). Les `Binding<T>` de `dev` relisent leur valeur à chaque image : sans effet tant que les expressions sont simples ; à
   reprendre si une vue appelle une fonction coûteuse (M8, interface avancée).

