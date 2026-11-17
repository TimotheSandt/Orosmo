#!/bin/sh
# Contrôle d'hygiène du dépôt (M00) : aucun binaire ni sortie de compilation suivis,
# pas de collision de casse, pas de fins de ligne CRLF hors scripts Windows.
set -eu
cd "$(dirname "$0")/.."
fail=0
bad=$(git ls-files | grep -iE '\.(exe|dll|lib|a|o|obj|pdb|zip|so|pch|ilk|exp|bundle)$|(^|/)(target|build|dist|obj|vcpkg_installed)/' || true)
[ -z "$bad" ] || { echo "Fichiers générés ou binaires suivis :"; echo "$bad"; fail=1; }
dup=$(git ls-files | tr 'A-Z' 'a-z' | sort | uniq -d || true)
[ -z "$dup" ] || { echo "Noms ne différant que par la casse :"; echo "$dup"; fail=1; }
crlf=$(git ls-files --eol | grep 'i/crlf' | grep -vE '\.(ps1|bat|cmd|nsi)$' || true)
[ -z "$crlf" ] || { echo "CRLF dans l'index :"; echo "$crlf"; fail=1; }
[ $fail -eq 0 ] && echo "check-repo : OK"
exit $fail
