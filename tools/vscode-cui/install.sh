#!/bin/sh
# Installe l'extension du langage d'interface (.cui) dans VS Code (Linux, macOS) : copie du dossier dans ~/.vscode/extensions.
# Usage : sh tools/vscode-cui/install.sh [dossier des extensions]   (puis « Developer: Reload Window » dans VS Code)
set -eu
SRC=$(cd "$(dirname "$0")" && pwd)
VERSION=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$SRC/package.json" | head -n 1)
DEST_ROOT=${1:-"$HOME/.vscode/extensions"}
DEST="$DEST_ROOT/proceduralgeneration.cui-language-$VERSION"
mkdir -p "$DEST/syntaxes"
cp "$SRC/package.json" "$SRC/main.js" "$SRC/language-configuration.json" "$DEST/"
cp "$SRC/syntaxes/cui.tmLanguage.json" "$DEST/syntaxes/"
echo "CUI extension $VERSION installed in $DEST"
for old in "$DEST_ROOT"/proceduralgeneration.cui-language-*; do
    [ "$old" = "$DEST" ] || echo "  an older version is still there: $old (remove it by hand)"
done
echo "Reload VS Code (Developer: Reload Window). The compiler is found in tools/cui (make cui-tool) or set cui.toolPath."
