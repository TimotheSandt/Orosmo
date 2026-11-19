# UI

Interface : composants, mise en page, liaisons de données et rendu du texte (FreeType + msdfgen).

- **Rôle** : `UIManager`, `Component`/`Container`, `Layout` (`HBox`, `VBox`), widgets (`Text`, `Label`), thème, `TextRenderer`, liaisons réactives (`Bind*`). Les vues sont écrites en `.cui` et précompilées en C++ par `tools/cui-rs` (spec : [docs/UI_DSL_Spec.md](../../../docs/UI_DSL_Spec.md)).
- **API publique** : `include/UI/`.
- **Dépendances** : `Graphics`, `Utilities`, FreeType, msdfgen.
- Voir l'avertissement sur les cycles dans `src/Graphics/README.md`.
