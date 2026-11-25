---
title: "Animer des propriétés (courbes)"
description: "Animer une position de caméra, une couleur de lumière ou un paramètre de matériau avec des courbes et PropertyAnimation."
sidebar:
  order: 2
---

Une `anim::Curve<T>` donne une valeur (`float`, `Vec2`, `Vec3` ou `Vec4`) en fonction du temps, à partir de clés. Une `anim::PropertyAnimation` associe des courbes nommées à des fonctions qui posent la valeur (`setter`) et les applique toutes à un temps donné. Le moteur ne lie rien tout seul : c'est vous qui écrivez le `setter` qui change la caméra, la lumière ou le matériau.

## Exemple minimal

Adapté de `tests/anim/anim_tests.cpp` (cas « property animation drives a camera position, a light colour and a material parameter »). La démo n'utilise pas `PropertyAnimation`.

```cpp
#include <orosmo/anim/curves.hpp>

namespace an = orosmo::anim;
using orosmo::core::Vec3;
using orosmo::core::Vec4;

Vec3 cameraPosition(0.0f);
Vec4 lightColor(0.0f);
float roughness = 0.0f;

an::Curve<Vec3> path;                       // linéaire par défaut
path.addKey(0.0f, Vec3(0, 0, 0));
path.addKey(2.0f, Vec3(0, 0, -4));

an::Curve<Vec4> color;
color.addKey(0.0f, {1, 0, 0, 1});
color.addKey(1.0f, {0, 0, 1, 1});

an::Curve<float> rough(an::CurveInterpolation::kStep);
rough.addKey(0.0f, 0.2f);
rough.addKey(1.5f, 0.9f);

an::PropertyAnimation anim;
(void)anim.bind<Vec3>("camera.position", path, [&](const Vec3 &v) { cameraPosition = v; });
(void)anim.bind<Vec4>("light.color", color, [&](const Vec4 &v) { lightColor = v; });
(void)anim.bind<float>("material.roughness", rough, [&](const float &v) { roughness = v; });

// Chaque image : un temps, puis on applique les valeurs à ce que l'on veut modifier.
anim.apply(0.5f);      // cameraPosition = (0, 0, -1), lightColor.r = 0.5, roughness = 0.2
```

Les setters ici écrivent dans des variables locales ; dans une application on y met par exemple `camera.setPosition(v)` ou `material->setParams(...)` ; pour la lumière, on modifie la couleur de la lumière concernée (voir les guides de la [caméra](/guides/rendu-3d/scene/), des [lumières](/guides/rendu-3d/lumieres/) et des [matériaux](/guides/rendu-3d/materiaux-pbr/)). Le temps vient de l'appelant (`apply(time)`) : pas d'horloge interne.

## Courbes

`Curve<T>(interpolation)` : le constructeur prend un `CurveInterpolation`.

| Valeur | Effet |
|---|---|
| `kStep` | Garde la valeur de la clé précédente jusqu'à la suivante. |
| `kLinear` (défaut) | Interpolation linéaire entre deux clés. |
| `kCubic` | Hermite cubique : utilise `outTangent` de la clé k et `inTangent` de la clé k+1, en unités de valeur par seconde. |

| Appel | Effet |
|---|---|
| `addKey(time, value)` | Ajoute une clé (tangentes nulles). |
| `addKey(CurveKey<T>{time, value, inTangent, outTangent})` | Ajoute une clé avec ses tangentes (pour `kCubic`). |
| `evaluate(time)` | Valeur au temps donné. Avant la première clé ou après la dernière : la valeur de cette clé est tenue. Sans clé : `T{}`. |
| `duration()` | Temps de la dernière clé (0 sans clé). |
| `keys()`, `interpolation()` | Inspection. |

Les clés restent triées par temps ; ajouter une clé à un temps déjà pris remplace l'ancienne.

```cpp
an::Curve<float> ease(an::CurveInterpolation::kCubic);
ease.addKey({0.0f, 0.0f, 0.0f, 0.0f});     // temps, valeur, inTangent, outTangent
ease.addKey({1.0f, 1.0f, 0.0f, 0.0f});     // départ et arrivée en douceur
float mid = ease.evaluate(0.5f);           // 0.5
```

## PropertyAnimation

| Appel | Effet |
|---|---|
| `bind<T>(name, curve, setter)` | Ajoute une piste. `T` est `float`, `Vec2`, `Vec3` ou `Vec4` (précisez-le : `bind<float>(...)`). `kAlreadyExists` si le nom est pris, `kInvalidArgument` si le setter est vide. |
| `unbind(name)` | Retire la piste. |
| `apply(time)` | Pose la valeur de chaque piste au temps `time`, dans l'ordre d'ajout. |
| `duration()` | Plus grande durée des pistes. |
| `trackCount()` | Nombre de pistes. |

Un setter peut appeler `bind` ou `unbind` sur la même animation : le changement prend effet à la fin du `apply` en cours. Hors de `apply`, il est immédiat.

Pour jouer une animation en boucle ou une seule fois, on prépare soi-même le temps :

```cpp
float t = std::fmod(elapsed, anim.duration());   // en boucle
anim.apply(t);
```

(`advanceClipTime` de [`clip.hpp`](/guides/animation/squelettes/) fait la même chose pour les clips, avec `LoopMode`, mais il n'est pas lié à `PropertyAnimation`.)

## Limites

- Courbes `kStep`, `kLinear`, `kCubic` et liaison par setters seulement.
- Seuls `float`, `Vec2`, `Vec3` et `Vec4` ont des courbes. Pas de courbe de rotation par quaternion (une `Vec4` est interpolée composante par composante, sans normalisation), pas d'éditeur de courbes, pas de lecture depuis un fichier.
- Pas de bouclage ni de vitesse intégrés à `PropertyAnimation` : le temps passé à `apply` est celui de l'appelant.

## Voir aussi

- [Animer un personnage](/guides/animation/squelettes/)
- [Dessiner une scène](/guides/rendu-3d/scene/)
- [Temps, pas fixe et événements](/guides/noyau/temps-evenements/)
