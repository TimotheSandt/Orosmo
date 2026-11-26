// Site vitrine et wiki d'Orosmo (M14, WEB-01 et WEB-02). Site statique : `npm run build` écrit dist/,
// servi par nginx dans l'image Docker (Dockerfile). Hébergement : docs/QUESTIONS.md A74, docs/DEPLOIEMENT_SITE.md.
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

const repo = 'https://github.com/TimotheSandt/Orosmo';

export default defineConfig({
  // Adresse publique (A74 : Cloudflare Pages, domaine de l'auteur). SITE_URL au build la remplace (test local, autre domaine).
  // Starlight en tire la balise canonique de chaque page et le plan du site.
  site: process.env.SITE_URL || 'https://orosmo.gasshog.fr',
  telemetry: false,
  // Anciennes adresses (avant la refonte du wiki).
  redirects: {
    '/utiliser/installation': '/demarrer/installation/',
    '/utiliser/projet': '/demarrer/premier-projet/',
    '/utiliser/build': '/reference/construction/',
    '/utiliser/exemples': '/demarrer/premiere-application/',
    '/utiliser/langage-cui': '/guides/interface/langage-cui/',
    '/utiliser/themes': '/guides/interface/themes/',
    '/architecture/couches': '/interne/couches/',
    '/architecture/rhi': '/interne/rhi/',
    '/architecture/renderer': '/interne/renderer/',
    '/architecture/shaders': '/interne/shaders/',
    '/architecture/ui': '/interne/ui/',
    '/architecture/compilateur-cui': '/interne/compilateur-cui/',
    '/etat': '/projet/etat/',
    '/histoire': '/projet/histoire/',
  },
  integrations: [
    starlight({
      title: 'Orosmo',
      description: 'Orosmo : moteur 3D et 2D générique en C++23, RHI Vulkan et OpenGL, interface en langage .cui.',
      defaultLocale: 'root',
      locales: { root: { label: 'Français', lang: 'fr' } },
      favicon: '/favicon.svg',
      social: [{ icon: 'github', label: 'GitHub', href: repo }],
      editLink: { baseUrl: `${repo}/edit/dev/site/` },
      customCss: ['./src/styles/custom.css'],
      components: { Footer: './src/components/Footer.astro' },
      credits: false,
      sidebar: [
        { label: 'Accueil', link: '/' },
        { label: 'Démarrer', items: [{ autogenerate: { directory: 'demarrer' } }] },
        {
          label: 'Guides',
          items: [
            { label: 'Application', collapsed: true, items: [{ autogenerate: { directory: 'guides/application' } }] },
            { label: 'Rendu 3D', collapsed: true, items: [{ autogenerate: { directory: 'guides/rendu-3d' } }] },
            { label: 'Rendu 2D', collapsed: true, items: [{ autogenerate: { directory: 'guides/rendu-2d' } }] },
            { label: 'Contenus', collapsed: true, items: [{ autogenerate: { directory: 'guides/contenus' } }] },
            { label: 'Animation', collapsed: true, items: [{ autogenerate: { directory: 'guides/animation' } }] },
            { label: 'Interface', collapsed: true, items: [{ autogenerate: { directory: 'guides/interface' } }] },
            { label: 'Noyau', collapsed: true, items: [{ autogenerate: { directory: 'guides/noyau' } }] },
            { label: 'Avancé', collapsed: true, items: [{ autogenerate: { directory: 'guides/avance' } }] },
          ],
        },
        { label: 'Référence', items: [{ autogenerate: { directory: 'reference' } }] },
        { label: 'Interne', collapsed: true, items: [{ autogenerate: { directory: 'interne' } }] },
        { label: 'Projet', collapsed: true, items: [{ autogenerate: { directory: 'projet' } }] },
      ],
    }),
  ],
});
