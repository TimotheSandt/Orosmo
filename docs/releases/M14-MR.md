# M14 : site vitrine et wiki du moteur (`milestone/M14`)

## Résumé

Jalon M14 (A43), commencé avant M6 et intégré après `v0.6.0` : **aucun code C++ du moteur touché** (doc et commentaires seulement). Le dépôt a maintenant un **site vitrine et un wiki** en
français dans `site/` (Astro + Starlight), une **image Docker** nginx qui les sert, et un **workflow de CI** qui construit le
site et l'image sur chaque PR qui touche `site/` (sans publier) et publie l'image sur **GHCR** au tag `v*` posé sur `main`.
Rien n'a été publié : la mise en ligne suit M6 (A43). Spécifications d'hébergement proposées en **A74** (à confirmer).

## Contenu

- **Vitrine** : vision, captures de référence de la démo, fonctions **Fait** de `docs/02` seulement (M6 « en préparation »), liens vers les releases GitHub et le wiki.
- **Histoire et méthode de développement** : le texte de transparence demandé par WEB-02 (commencé par l'auteur avec OpenGL, IA mal utilisée au début, pause puis arrêt, reprise avec Claude utilisé correctement sous la direction de l'auteur), lié depuis l'accueil et le pied de page, et depuis le `README.md`.
- **Wiki** : « Utiliser le moteur » (installation, `orosmo init`/`doctor`/`update`, Make et CMake, `.cui`, thèmes, exemples) et « Architecture interne » (couches, RHI, renderer, interface, compilateur `cui`, shaders), chaque page citant ses sources dans `docs/` et les `README.md` des modules.
- **Déploiement** : `site/Dockerfile` (node 24.20.0 alpine → nginx 1.30.4 alpine), `.github/workflows/site.yml`, `docs/DEPLOIEMENT_SITE.md` (coûts : 0 € sur GHCR public, serveur à la charge de l'auteur ; `docker run` manuel ; mise à jour par jalon).

## Refonte du site (après M6)

- **Charte wiki sobre** (`site/CHARTE.md`) : menu en barre latérale gauche, pas de logo (favicon neutre en attendant celui de l'auteur).
- **Contenu réorganisé** (64 pages) : Démarrer, Guides par usage (application, rendu 3D, rendu 2D, contenus, animation, interface, noyau, avancé), Référence, Interne, Projet ; accueil « Je veux… ». Le site couvre M6.
- **Tutoriels vérifiés** : jeu 3D en 11 étapes (programme assemblé compilé en `-Wall -Wextra -Wpedantic -Werror` et exécuté sous OpenGL, personnage animé CesiumMan CC-BY-4.0), application 2D, menu `.cui`.
- **Pièges documentés** là où on les rencontre ; défauts du moteur trouvés en rédigeant notés en G209 à G212, propositions d'utilisabilité de l'API en A75 (non appliquées).
- **Relecture** par deux tours d'agents « nouvel utilisateur » ; nouveau contrôle `npm run check-anchors`.

## Identifiants

WEB-01 **Fait** ; WEB-02, WEB-03, WEB-04 **Partiel** (publication non faite) ; WEB-05 **Absent** (optionnel, A74). Totaux (167 lignes, M6 et M14 intégrés) : Fait 53, Partiel 44, Absent 51, Archivé 11, Hors moteur 7, Incompatible 1.

## Changements incompatibles

Aucun (moteur, paquet de release et CI existante inchangés).

## Commande de démo

```bash
cd site && npm ci && npm run build && npm run check-links && npm run check-anchors
docker build -t orosmo-site:test site/ && docker run -d --name orosmo-site-test -p 8089:80 orosmo-site:test
curl -sf localhost:8089/projet/histoire/ >/dev/null && echo OK; docker rm -f orosmo-site-test
```

## Tests

Linux x64, Node 24.20.0, Docker 29.1.3, et depuis un clone propre de la branche : `npm ci`, `npm run build` (64 pages), `npm run check-links` (5375 liens internes, 0 cassé), `npm run check-anchors` (1659 ancres, 0 cassée), `docker build`, `docker run` + `curl` (200 sur l'accueil et le wiki, 404 sinon, santé `healthy`), actionlint (0 erreur), `make check` (OK).

## Non prouvé

Workflow jamais exécuté sur GitHub, aucune image sur GHCR (G203) ; pas de serveur ni de domaine (G204) ; pas de staging (G205) ; contenu à relire à chaque jalon (G206) ; ombre d'un modèle animé vérifiée à la main seulement (G212) ; `ci.yml` sans filtre de chemins pour `site/` (G207).

## À décider

A74 (hébergement) ; A75 (utilisabilité de l'API) ; G207 (`paths-ignore` dans `ci.yml`).

🤖 Generated with [Claude Code](https://claude.com/claude-code)
