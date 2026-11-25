// Site vitrine et wiki d'Orosmo (M14, WEB-01 et WEB-02). Site statique : `npm run build` écrit dist/,
// servi par nginx dans l'image Docker (Dockerfile). Hébergement : docs/QUESTIONS.md A74, docs/DEPLOIEMENT_SITE.md.
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

const repo = 'https://github.com/TimotheSandt/Orosmo';

export default defineConfig({
  // Adresse publique : à remplacer par le domaine choisi par l'auteur (SITE_URL au build, sinon une valeur locale).
  site: process.env.SITE_URL || 'http://localhost:8080',
  telemetry: false,
  integrations: [
    starlight({
      title: 'Orosmo',
      description: 'Orosmo : moteur 3D et 2D générique en C++23, RHI Vulkan et OpenGL, interface en langage .cui.',
      defaultLocale: 'root',
      locales: { root: { label: 'Français', lang: 'fr' } },
      logo: { src: './src/assets/logo.svg', alt: 'Orosmo' },
      favicon: '/favicon.svg',
      social: [{ icon: 'github', label: 'GitHub', href: repo }],
      editLink: { baseUrl: `${repo}/edit/dev/site/` },
      customCss: ['./src/styles/custom.css'],
      components: { Footer: './src/components/Footer.astro' },
      credits: false,
      sidebar: [
        {
          label: 'Orosmo',
          items: [
            { label: 'Accueil', link: '/' },
            { label: 'Histoire et méthode', slug: 'histoire' },
            { label: 'État du moteur', slug: 'etat' },
          ],
        },
        { label: 'Utiliser le moteur', items: [{ autogenerate: { directory: 'utiliser' } }] },
        { label: 'Architecture interne', items: [{ autogenerate: { directory: 'architecture' } }] },
      ],
    }),
  ],
});
