# Charte graphique du wiki

Un wiki sobre, dans l'esprit des docs Rust, Godot ou MDN : lisible, sans effet ni animation. Le style vit dans
`src/styles/custom.css` (variables `--sl-*` de Starlight). Exemple de chaque composant : page `/guide-style/` (brouillon : visible avec `npm run dev`, absente du build, du plan du site et de la recherche).

## Palette
- Une couleur d'accent : bleu `#1f5fbf` en clair (7,0:1 sur blanc), `#7fb0ff` en sombre (8,5:1 sur `#16191f`). Contraste AA respecté dans les deux thèmes.
- Le reste est gris neutre. Pas de dégradé, pas d'ombre décorative, pas d'autre couleur sauf celles des encadrés Starlight (note, attention).

## Typographie
- Polices du système (`system-ui`, monospace système) : rien à télécharger.
- Corps 16 px, interligne 1,7, largeur de texte 46 rem. Titres en graisse 600, pas de majuscules forcées.

## Blocs de code
- Toujours avec le langage (` ```cpp `, ` ```json `, ` ```bash `) et un exemple qui compile. Un titre (`title="..."`) quand le fichier compte.
- Code en ligne pour les noms de types, fonctions, options et chemins, jamais pour de l'emphase.

## Documenter une option
Un tableau par groupe d'options, ces colonnes dans cet ordre, puis un exemple de code :

| Option | Type | Défaut | Effet |
|---|---|---|---|

L'exemple suit le tableau et montre l'option utilisée. Une option absente ou partielle le dit dans la colonne Effet.

## Encadrés
Seulement `:::note` (information utile) et `:::caution` (risque de perte, piège). Pas de `tip` ni `danger`, au plus deux encadrés par page.

## Images
Captures réelles du moteur uniquement (`src/assets/captures/`, format webp), avec texte alternatif et légende. Pas de logo, d'illustration ni d'image décorative.

## Rédaction
- Titres courts (quelques mots), un seul `h1` par page, pas de niveau sauté.
- Phrases simples. Le « comment » avec un exemple, le « pourquoi » en une ou deux phrases.
- Pas de gras décoratif : le gras est réservé à un terme défini pour la première fois. Pas d'emojis, pas de formules marketing.
