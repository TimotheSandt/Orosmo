# Déploiement du site vitrine et du wiki (M14)

Jalon M14 (`WEB-01` à `WEB-05`, `docs/05-PLAN_PAR_JALONS.md`). Spécifications d'hébergement : `docs/QUESTIONS.md`, **A74**
(proposées, appliquées par défaut, à confirmer par l'auteur). Code : `site/` (voir `site/README.md`), CI :
`.github/workflows/site.yml`.

## Vue d'ensemble

```
site/ (Astro + Starlight)
  │  PR qui touche site/ ──▶ CI « Site » : npm ci, astro build, liens internes, docker build   (rien n'est publié)
  │  tag v* sur main     ──▶ CI « Site » : docker build + push ──▶ ghcr.io/<propriétaire>/orosmo-site:<version>, :latest
  ▼
serveur de l'auteur : docker pull + docker run (manuel)
```

- **Registre** : GitHub Container Registry (GHCR), `ghcr.io/<propriétaire du dépôt>/orosmo-site` (en minuscules).
- **Image** : `site/Dockerfile`, deux étapes épinglées : `node:24.20.0-alpine3.23` construit le site (`npm ci`,
  `astro build`, vérification des liens), `nginx:1.30.4-alpine` sert `dist/` sur le port 80 (`site/nginx.conf` ; environ
  100 Mo ; `HEALTHCHECK` intégré).
- **Déploiement** : manuel (aucun accès au serveur dans les secrets de la CI).
- **Staging sur PR (WEB-05)** : non retenu pour l'instant (A74 point 4, G205).

## Coûts

| Poste | Coût | Remarque |
| --- | --- | --- |
| GitHub Actions (CI « Site ») | 0 € | minutes gratuites des dépôts publics |
| GHCR | 0 € | stockage et transfert gratuits pour un paquet **public** ; vérifier la visibilité du paquet après la première publication (G203) |
| Serveur, nom de domaine, certificat | à la charge de l'auteur | n'importe quel hôte qui fait tourner Docker (une petite machine virtuelle suffit : site statique, aucun état) ; certificat gratuit possible (Let's Encrypt, par un proxy inverse) |

## Publier une version (CI)

1. L'auteur fusionne le jalon dans `main` et pose le tag de version sur `main` : `git tag v0.6.0 && git push origin v0.6.0`
   (le même tag déclenche aussi `build-engine.yml`, les releases du moteur).
2. Le job `publish` de `site.yml` vérifie que le commit du tag est dans `main` (sinon il échoue), construit l'image et la
   pousse avec les étiquettes `0.6.0`, `0.6` et `latest`. Authentification par `GITHUB_TOKEN` (`packages: write`), aucun
   secret à créer.
3. Facultatif : la variable de dépôt `SITE_URL` (Settings → Secrets and variables → Actions → Variables) donne l'adresse
   publique au build (URL canoniques, plan du site `sitemap-index.xml`).
4. Relance manuelle possible : Actions → Site → *Run workflow* sur `main` (étiquettes `latest` et `sha-<commit>`).

## Déployer sur le serveur (manuel)

```bash
docker pull ghcr.io/<propriétaire>/orosmo-site:latest          # ou :0.6.0 pour une version précise
docker rm -f orosmo-site 2>/dev/null                           # ancienne instance
docker run -d --name orosmo-site --restart unless-stopped -p 80:80 ghcr.io/<propriétaire>/orosmo-site:latest
curl -sf http://localhost/ >/dev/null && echo OK
docker inspect -f '{{.State.Health.Status}}' orosmo-site       # healthy après quelques secondes
```

Pour HTTPS, placer un proxy inverse devant (Caddy, Traefik ou nginx avec Let's Encrypt) et publier le conteneur sur un port
local (`-p 127.0.0.1:8080:80`). Retour arrière : relancer avec l'étiquette de la version précédente (`:0.5.0`).

## Construire et tester en local (sans registre)

```bash
cd site && npm ci && npm run build && npm run check-links      # dist/ ; npm run dev pour l'édition (http://localhost:4321)
docker build -t orosmo-site:test site/                          # depuis la racine du dépôt
docker run -d --name orosmo-site-test -p 8089:80 orosmo-site:test
curl -sf localhost:8089/ && curl -sf localhost:8089/architecture/rhi/
docker rm -f orosmo-site-test && docker rmi orosmo-site:test
```

Si Docker Hub refuse les images de base (limite anonyme, « 429 Too Many Requests »), les récupérer par le miroir de Google
puis les renommer : `docker pull mirror.gcr.io/library/node:24.20.0-alpine3.23 && docker tag mirror.gcr.io/library/node:24.20.0-alpine3.23 node:24.20.0-alpine3.23`
(idem pour `nginx:1.30.4-alpine`).

## Mise à jour par jalon

À chaque fin de jalon, avec `docs/releases/<jalon>.md` :

1. **Contenu** : relire `site/src/content/docs/` contre `docs/STATUS.md` et `docs/02` : la page d'accueil (`index.mdx`)
   ne liste que des fonctions **Fait** ; `etat.md` (tableau des jalons, partiels et absents) ; les pages du wiki touchées
   par le jalon (nouveau module : une page dans `architecture/`, nouvel usage : une page dans `utiliser/`). Chaque page cite
   ses sources dans le dépôt.
2. **Captures** : au besoin, régénérer les images de référence (`make demo-capture`) et les convertir en WebP dans
   `site/src/assets/captures/` (quelques dizaines de Ko chacune ; pas de gros binaires).
3. **Version** : `site/package.json` suit `ENGINE_VERSION` (`mk/config.mk`).
4. **Dépendances** : mettre à jour les versions épinglées (npm, images de base) seulement dans une branche dédiée, en
   vérifiant `npm ci && npm run build` et `docker build`.
5. **Vérifier** : `npm run build && npm run check-links`, `docker build`, puis la PR (job `build` de `site.yml`).
6. **Publier** : le tag de version posé par l'auteur sur `main` publie l'image ; redéployer sur le serveur (ci-dessus).
