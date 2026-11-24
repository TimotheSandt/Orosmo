# Image du site vitrine et du wiki d'Orosmo (M14, WEB-03). Construction en deux étapes :
#   1. node : npm ci (package-lock.json) puis astro build -> /site/dist
#   2. nginx : sert dist/ en statique sur le port 80 (aucun Node à l'exécution)
# Versions épinglées pour un build reproductible. Construire depuis la racine du dépôt :
#   docker build -t orosmo-site site/
# Lancer : docker run -d --name orosmo-site -p 8080:80 orosmo-site   (docs/DEPLOIEMENT_SITE.md)

FROM node:24.20.0-alpine3.23 AS build
WORKDIR /site
ENV ASTRO_TELEMETRY_DISABLED=1
COPY package.json package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY . .
# Adresse publique du site (URL canoniques, plan du site) ; facultative.
ARG SITE_URL
ENV SITE_URL=${SITE_URL}
RUN npm run build && npm run check-links

FROM nginx:1.30.4-alpine
LABEL org.opencontainers.image.title="orosmo-site" \
      org.opencontainers.image.description="Site vitrine et wiki du moteur Orosmo (Astro + Starlight, servi par nginx)" \
      org.opencontainers.image.source="https://github.com/TimotheSandt/Orosmo"
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /site/dist /usr/share/nginx/html
EXPOSE 80
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -q --spider http://127.0.0.1/ || exit 1
