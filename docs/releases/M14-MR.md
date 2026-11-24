# M14 : site vitrine et wiki du moteur (`milestone/M14`)

## Résumé

Jalon M14 (A43), commencé avant M6 : **aucun code C++ touché**. Le dépôt a maintenant un **site vitrine et un wiki** en
français dans `site/` (Astro + Starlight), une **image Docker** nginx qui les sert, et un **workflow de CI** qui construit le
site et l'image sur chaque PR qui touche `site/` (sans publier) et publie l'image sur **GHCR** au tag `v*` posé sur `main`.
Rien n'a été publié : la mise en ligne suit M6 (A43). Spécifications d'hébergement proposées en **A73** (à confirmer).

## Contenu

- **Vitrine** : vision, captures de référence de la démo, fonctions **Fait** de `docs/02` seulement (M6 « en préparation »), liens vers les releases GitHub et le wiki.
- **Histoire et méthode de développement** : le texte de transparence demandé par WEB-02 (commencé par l'auteur avec OpenGL, IA mal utilisée au début, pause puis arrêt, reprise avec Claude utilisé correctement sous la direction de l'auteur), lié depuis l'accueil et le pied de page, et depuis le `README.md`.
- **Wiki** : « Utiliser le moteur » (installation, `orosmo init`/`doctor`/`update`, Make et CMake, `.cui`, thèmes, exemples) et « Architecture interne » (couches, RHI, renderer, interface, compilateur `cui`, shaders), chaque page citant ses sources dans `docs/` et les `README.md` des modules.
- **Déploiement** : `site/Dockerfile` (node 24.20.0 alpine → nginx 1.30.4 alpine), `.github/workflows/site.yml`, `docs/DEPLOIEMENT_SITE.md` (coûts : 0 € sur GHCR public, serveur à la charge de l'auteur ; `docker run` manuel ; mise à jour par jalon).

## Identifiants

WEB-01 **Fait** ; WEB-02, WEB-03, WEB-04 **Partiel** (publication non faite) ; WEB-05 **Absent** (optionnel, A73). Totaux (167 lignes) : Fait 43, Partiel 43, Absent 62.

## Changements incompatibles

Aucun (moteur, paquet de release et CI existante inchangés).

## Commande de démo

```bash
cd site && npm ci && npm run build && npm run check-links
docker build -t orosmo-site:test site/ && docker run -d --name orosmo-site-test -p 8089:80 orosmo-site:test
curl -sf localhost:8089/histoire/ >/dev/null && echo OK; docker rm -f orosmo-site-test
```

## Tests

Linux x64, Node 24.20.0, Docker 29.1.3, et depuis un clone propre de la branche : `npm ci`, `npm run build` (16 pages), `npm run check-links` (470 liens internes, 0 cassé), `docker build`, `docker run` + `curl` (200 sur l'accueil et le wiki, 404 sinon, santé `healthy`), actionlint (0 erreur), `make check` (OK).

## Non prouvé

Workflow jamais exécuté sur GitHub, aucune image sur GHCR (G202) ; pas de serveur ni de domaine (G203) ; pas de staging (G204) ; contenu à mettre à jour à la fusion de M6 (G205) ; `ci.yml` sans filtre de chemins pour `site/` (G206).

## À décider

A73 (hébergement) ; G206 (`paths-ignore` dans `ci.yml`).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
