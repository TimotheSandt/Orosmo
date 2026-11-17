# Archive de travaux anciens (stashes Git du dépôt local, 2025-2026)

Les 17 stashes de l'auteur ont été analysés le 2026-11-17 (patchs complets hors dépôt : `../ProceduralGeneration-backups/stashes/stash-<n>.patch`). Seuls deux contiennent du travail encore utile pour le plan ; ils sont conservés ici **tels quels** (chemins d'avant M00 : `Libraries/…`), comme référence, pas comme code à appliquer.

| Fichier | Origine | Contenu | Utile pour |
| --- | --- | --- | --- |
| `stash-0-terrain-lod.patch` | « add LOD » sur `feat/vulkan`, 2026-05-10 | `Grid` : 3 niveaux de détail par distance (pas 1, 2, 4 ; seuils 80 et 160), sélection du niveau selon la caméra | TER-03 (maillage à niveaux de détail), M3 |
| `stash-3-vulkan-clear-attachments.patch` | « vulkan more » sur `feat/vulkan`, 2026-04-24 | `VulkanRenderState.cpp` : `ClearAttachments` (`vkCmdClearAttachments`, rectangle de nettoyage courant) | G41 (`Renderer::Clear` est un no-op sous Vulkan), M4 |

Autres stashes (non conservés dans le dépôt) : sans intérêt pour le plan, car périmés ou déjà remplacés — anciennes versions du `TextRenderer` (stb_truetype, remplacé par l'atlas MSDF), de l'`InputManager`, de `Mesh`/`Shader`/`Camera` (août 2025), correctifs d'interface (propagation du drapeau « sale », CRTP) déjà intégrés, retouches de `Makefile`. Un stash (`readme`, 2026-01-15) contient un fichier **`LICENSE` GPL-3.0** et un ancien `README` : à garder en tête pour la décision de licence (Q5, Apache-2.0 envisagée).
