# anim

Couche 5 de `docs/03` §1 : animation. Archive : `libanim.a` (`src/anim/`), API publique `include/orosmo/anim/`. Dépend de `assets`,
`renderer`, `rhi` et `core`. Namespace `orosmo::anim`. Créé en M6 (lot E, `docs/05`, A70). Tout est déterministe : aucune horloge,
aucun aléa ; le temps vient de l'appelant.

| En-tête | Rôle |
| --- | --- |
| `pose.hpp` | `Transform` (TRS), `Pose` (une transformation par nœud du `assets::Model`) ; `restPose`, `nodeParents`, `worldMatrices` ; `blend` / `blendPoses` (slerp, masque par nœud) ; couches additives `additiveDelta` / `applyAdditive` (poids, masque) — ANIM-02 |
| `clip.hpp` | `Clip` : animation glTF (translation, rotation, échelle ; STEP, LINEAR avec slerp, CUBICSPLINE de glTF), `sample` dans une pose, événements nommés (`addEvent`, `collectEvents` avec bouclage) ; `advanceClipTime` (`LoopMode::kOnce` / `kLoop`) — ANIM-02 |
| `state_machine.hpp` | `StateMachine` : états (clip, boucle, vitesse), transitions par booléen, déclencheur consommé et/ou fin de clip, fondu enchaîné, événements (`takeEvents`) — ANIM-02 |
| `curves.hpp` | `Curve<T>` (float, vec2, vec3, vec4 ; palier, linéaire, Hermite cubique) ; `PropertyAnimation` : courbes nommées liées à des fonctions qui posent la valeur (caméra, lumière, matériau, transformation) — ANIM-03 |
| `skinning.hpp` | `SkinnedModel` : skinning GPU en compute (`shaders/rhi/skin.comp`, 4 os par sommet, palette en tampon de stockage) qui écrit le tampon de sommets d'un `assets::RenderModel` créé avec `skinnable` ; `jointPalette`, `skinVertices` (référence CPU) — ANIM-01 |

Exemple :

```cpp
auto clip = orosmo::anim::Clip::fromModel(model, 0);                 // première animation du glTF
orosmo::anim::StateMachine sm(orosmo::anim::restPose(model));
sm.addState({"walk", &*clip});
sm.start("walk");
// chaque image :
sm.update(dt);
skinned.setPose(sm.pose());   // matrices des nœuds + palettes
skinned.dispatch(encoder);    // avant beginFrame du SceneRenderer, dans la même soumission
render.draw(scene, camera);
```

Limites : pas de cibles de forme, d'IK ni de mouvement de racine (ANIM-05, P2) ; un maillage GPU dessiné par plusieurs nœuds
skinnés est déformé pour le premier seulement ; normales et tangentes déformées par mat3(palette) (exact sans échelle non uniforme
dans les os) ; pendant un fondu de la machine à états, aucune autre transition n'est évaluée. Les foules (une palette par instance,
ANIM-04) viennent au lot F. Voir `docs/GAPS.md` (G198).
