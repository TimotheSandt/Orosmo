---
title: Guide de style
description: "Page de référence pour les rédacteurs, avec les composants du wiki et le format d'une option."
draft: true
sidebar:
  hidden: true
---

Cette page montre les composants à utiliser. Les règles sont dans `site/CHARTE.md`.

## Paragraphe et code

Un paragraphe court, une idée. Le nom d'un symbole s'écrit en code en ligne, par exemple `orosmo::Logger`.

```cpp title="exemple.cpp"
#include <cstdio>

int main() {
    std::puts("bonjour");
    return 0;
}
```

## Tableau d'options

| Option | Type | Défaut | Effet |
|---|---|---|---|
| `--api` | `vulkan` ou `opengl` | `vulkan` | Choisit le backend graphique. |
| `--size` | `WxH` | `1280x720` | Taille de la fenêtre. |

```bash
apps/demo/orosmo --api opengl --size 800x600
```

## Encadrés

:::note
Une information utile qui ne change pas le résultat.
:::

:::caution
Un piège ou un risque de perte de données.
:::
