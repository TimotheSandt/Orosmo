# site : vitrine et wiki d'Orosmo (M14)

Site statique construit avec **Astro** et **Starlight** (`WEB-01`, `WEB-02`), servi par nginx dans une image Docker
(`WEB-03`), publiée sur GHCR par `.github/workflows/site.yml` (`WEB-04`). Hébergement et procédure :
[`docs/DEPLOIEMENT_SITE.md`](../docs/DEPLOIEMENT_SITE.md), décision `A73` (`docs/QUESTIONS.md`).

## Commandes

Node 22.23.4 ou plus récent (vérifié : Node 24.20.0, npm 11.19.0).

```bash
npm ci                 # dépendances exactes de package-lock.json
npm run dev            # serveur d'édition, http://localhost:4321
npm run build          # site statique dans dist/
npm run check-links    # liens internes de dist/ (après build)
npm run preview        # sert dist/
docker build -t orosmo-site:test .   # image nginx (depuis ce dossier)
```

## Organisation

| Chemin | Rôle |
| --- | --- |
| `astro.config.mjs` | configuration : titre, langue (français), barre latérale, pied de page, `SITE_URL` |
| `src/content/docs/index.mdx` | accueil (vitrine) : vision, captures, fonctions **Fait**, liens |
| `src/content/docs/histoire.md` | « Histoire et méthode de développement » (transparence, WEB-02) |
| `src/content/docs/etat.md` | état du moteur : jalons, partiel et absent |
| `src/content/docs/utiliser/` | wiki utilisateur : installation, `orosmo init`/`doctor`, Make et CMake, `.cui`, thèmes, exemples |
| `src/content/docs/architecture/` | wiki interne : couches, RHI, renderer, interface, compilateur `.cui`, shaders |
| `src/components/Footer.astro` | pied de page (lien « Histoire et méthode », releases, code) |
| `src/assets/captures/` | captures de la démo (images de référence de `docs/BASELINE/reference/`, en WebP) |
| `src/content/i18n/fr.json` | textes de l'interface de Starlight remplacés |
| `scripts/check-links.mjs` | vérification des liens internes |
| `Dockerfile`, `nginx.conf`, `.dockerignore` | image de production |

## Règles de contenu

- Une fonction n'apparaît comme disponible que si elle est **Fait** dans `docs/02` avec sa preuve (`docs/STATUS.md`) ;
  le reste est dit « partiel », « absent » ou « en préparation ».
- Les pages résument `docs/` et les `README.md` des modules, et citent leurs sources ; aucune API inventée.
- Pas de gros binaires : captures en WebP de quelques dizaines de Ko.

## Dépendances

`astro` 7.4.13, `@astrojs/starlight` 0.43.1, `sharp` 0.35.5 (optimisation des images), versions exactes ; images
`node:24.20.0-alpine3.23` et `nginx:1.30.4-alpine`. Aucune dépendance vers le moteur C++.

## Limites

Rien n'est publié avant le premier tag `v*` posé sur `main` après M6 (G202) ; pas de serveur ni de domaine choisis (G203) ;
pas de prévisualisation sur PR (WEB-05, G204) ; contenu à relire à chaque jalon (G205).
