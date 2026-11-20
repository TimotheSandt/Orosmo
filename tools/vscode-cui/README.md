# Extension VS Code du langage d'interface (`.cui`)

- **Coloration** : composants, modificateurs, événements (`.onClick`…), liaisons `$state.x`, interpolation `\(…)`, `@cui-cpp`.
- **Diagnostics** pendant la frappe : ceux du compilateur (`cui check --stdin`), à la ligne et à la colonne du `.cui`.
- **Complétion** : composants (avec leurs arguments obligatoires), arguments nommés, modificateurs et événements après `.`,
  alignements et tailles (`.center`, `.fill`), propriétés de `.style(...)`, puis jetons et images des thèmes du dossier ouvert
  (`**/themes/*.json`).
- **Survol** : signature et rôle d'un composant ou d'un modificateur.

Les composants viennent du compilateur lui-même (`cui spec`) : reconstruire `cui` (`make cui-tool`) suffit pour que l'extension
connaisse un nouveau composant. Le compilateur est cherché dans `tools/cui`, `tools/target/release/cui`, `engine/tools/cui` (projet
créé par `engine init`), puis le `PATH` ; réglage `cui.toolPath` sinon.

```bash
sh tools/vscode-cui/install.sh          # Linux, macOS : ~/.vscode/extensions/orosmo.cui-language-<version>
.\tools\vscode-cui\install.ps1          # Windows
```
Puis « Developer: Reload Window ». Journal : commande « CUI: Show the extension log ».
