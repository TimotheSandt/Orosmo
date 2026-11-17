# Plan d'implémentation — système de benchmark

## Objectif

Construire un système de benchmark générique : exécuter des scénarios définis par
l'utilisateur, mesurer la durée de chaque itération avec une phase de chauffe — et,
en option, le coût de `Setup()` —, agréger des statistiques déterministes et produire
un rapport JSON comparable d'une exécution à l'autre.

Le système ne dépend d'aucun scénario concret : les scénarios sont fournis via une
interface, le cœur du système ne connaît que cette interface.

---

## Architecture

```
benchmark_main ──> Runner ──> Scenario (interface)
                     │
                     ├──> ScenarioRegistry   (nom -> fabrique)
                     ├──> Statistics         (agrégation)
                     └──> Report             (sérialisation JSON)
```

| Module | Responsabilité unique |
|---|---|
| `Scenario` | Décrit un travail mesurable via une interface abstraite. |
| `ScenarioRegistry` | Associe un nom à une fabrique de scénario. |
| `Runner` | Orchestre chauffe, mesure et cycle de vie d'un scénario. |
| `Statistics` | Calcule les agrégats à partir d'un tableau d'échantillons. |
| `Report` | Structure le résultat et le sérialise en JSON. |
| `benchmark_main` | Point d'entrée CLI : charge la config, lance le `Runner`. |

---

## Modèle de données

Toutes les durées sont exprimées en **millisecondes** (`double`).

```cpp
namespace bench
{

struct ScenarioConfig
{
    std::string name;            // identifiant du scénario à exécuter
    double      warmupSeconds;   // durée de chauffe ; mesures ignorées
    double      measureSeconds;  // durée minimale de la phase de mesure
    std::size_t minSamples;      // nombre minimal d'échantillons exigés
    std::size_t maxSamples;      // garde-fou mémoire ; 0 = illimité
    bool        measureSetup;    // si true : mesure aussi le coût de Setup()
    std::size_t setupSamples;    // répétitions Setup/Teardown si measureSetup
};

struct BenchmarkConfig
{
    std::vector<ScenarioConfig> scenarios;
    std::string                 outputPath; // chemin du rapport JSON
};

struct Statistics
{
    std::size_t samples    = 0;
    double      totalMs    = 0.0;  // somme des durées mesurées
    double      meanMs     = 0.0;
    double      stddevMs   = 0.0;  // écart-type de population
    double      minMs      = 0.0;
    double      maxMs      = 0.0;
    double      p50Ms      = 0.0;
    double      p95Ms      = 0.0;
    double      p99Ms      = 0.0;
    double      throughput = 0.0;  // itérations par seconde
};

struct ScenarioResult
{
    std::string                   name;
    ScenarioConfig                config;          // config rejouée, traçabilité
    Statistics                    iterationStats;  // statistiques de RunIteration
    bool                          hasSetupStats = false;
    Statistics                    setupStats;      // valide si hasSetupStats == true
    std::map<std::string, double> counters;        // compteurs contextuels moyennés
};

struct BenchmarkReport
{
    std::string                 timestamp;   // ISO 8601 UTC
    std::string                 platform;
    std::string                 build;
    std::vector<ScenarioResult> results;
};

} // namespace bench
```

---

## Interface Scénario

Un scénario implémente cette interface. Le `Runner` ne manipule jamais autre chose.

```cpp
namespace bench
{

class CounterSink
{
  public:
    // Accumule une valeur pour un compteur nommé. La somme est divisée
    // par le nombre d'échantillons à la fin pour obtenir une moyenne.
    void Add(const std::string &name, double value);
};

class Scenario
{
  public:
    virtual ~Scenario() = default;

    // Identifiant stable d'une exécution à l'autre.
    virtual std::string Name() const = 0;

    // Prépare les ressources. Doit pouvoir être rappelé après un
    // Teardown() : obligatoire dès que measureSetup est activé.
    virtual void Setup() {}

    // Une itération mesurable. C'est le SEUL code chronométré.
    virtual void RunIteration() = 0;

    // Libère les ressources de Setup() ; laisse le scénario dans un
    // état permettant un nouvel appel à Setup().
    virtual void Teardown() {}

    // Compteurs contextuels relevés après chaque itération (optionnel).
    virtual void CollectCounters(CounterSink &sink) const { (void)sink; }
};

using ScenarioFactory = std::function<std::unique_ptr<Scenario>()>;

class ScenarioRegistry
{
  public:
    void Register(const std::string &name, ScenarioFactory factory);
    bool Contains(const std::string &name) const;
    std::unique_ptr<Scenario> Create(const std::string &name) const; // nullptr si absent
};

} // namespace bench
```

**Règle d'isolation :** pendant la phase d'itération, `RunIteration` est le seul code
chronométré ; toute allocation ou tout chargement de ressource va dans `Setup` /
`Teardown`. Le coût de `Setup` peut, lui, être mesuré dans une phase distincte
(configuration `measureSetup`).

**Contrat Setup/Teardown :** un `Setup()` suivi d'un `Teardown()` doit ramener le
scénario à un état où `Setup()` peut être rappelé. Ce contrat devient obligatoire dès
que `measureSetup` est activé, car le Runner enchaîne alors plusieurs paires.

---

## Cycle de vie du Runner

Pour chaque `ScenarioConfig`, dans l'ordre :

```
scenario = registry.Create(cfg.name)
    └─ si nullptr -> consigner une erreur, passer au scénario suivant

// Phase 0 : benchmark de Setup — seulement si cfg.measureSetup
setupDurations.clear()
si cfg.measureSetup :
    scenario->Setup() ; scenario->Teardown()   // paire de chauffe, jetée
    pour i de 1 à cfg.setupSamples :
        tStart = steady_clock::now()
        scenario->Setup()
        tEnd   = steady_clock::now()
        setupDurations.push_back( durée tStart->tEnd en ms )
        scenario->Teardown()

// Phase d'itération : préparation unique
scenario->Setup()

// Phase de chauffe
t0 = steady_clock::now()
tant que (now() - t0) < cfg.warmupSeconds :
    scenario->RunIteration()              // durée jetée

// Phase de mesure
samples.clear()                            // vector<double>, réservé à minSamples
sink   = CounterSink{}
t0 = steady_clock::now()
boucle :
    si cfg.maxSamples != 0 et samples.size() >= cfg.maxSamples : arrêt
    elapsed = now() - t0
    si elapsed >= cfg.measureSeconds et samples.size() >= cfg.minSamples : arrêt

    tStart = steady_clock::now()
    scenario->RunIteration()
    tEnd   = steady_clock::now()
    samples.push_back( durée tStart->tEnd en ms )
    scenario->CollectCounters(sink)

scenario->Teardown()

// Agrégation
result.name           = cfg.name
result.config         = cfg
result.iterationStats = ComputeStatistics(samples)
result.counters       = sink.Moyennes(samples.size())
si cfg.measureSetup :
    result.hasSetupStats = true
    result.setupStats    = ComputeStatistics(setupDurations)
report.results.push_back(result)
```

Condition d'arrêt de la mesure : `elapsed >= measureSeconds` **et**
`samples >= minSamples` doivent être vraies simultanément. Cela garantit à la fois
une durée minimale et un effectif minimal pour que les statistiques aient un sens.

La phase 0 (benchmark de `Setup`) s'exécute avant la phase d'itération et se termine
par un `Teardown()` : la phase d'itération démarre donc toujours sur un `Setup()`
propre. Sa première paire `Setup/Teardown` sert de chauffe et n'est pas conservée —
`setupDurations` contient donc exactement `cfg.setupSamples` valeurs.

---

## Mesure du temps

| Décision | Choix | Raison |
|---|---|---|
| Horloge | `std::chrono::steady_clock` | Monotone ; insensible aux ajustements de l'horloge système. |
| Type de durée | `std::chrono::duration<double, std::milli>` | Précision sous-milliseconde sans perte. |
| Unité stockée | milliseconde (`double`) | Unité unique dans tout le système et le rapport. |

Ne **jamais** utiliser `system_clock` (non monotone) pour mesurer une durée.
`high_resolution_clock` est souvent un alias non garanti monotone : éviter.

---

## Calcul statistique

`ComputeStatistics` copie les échantillons, trie la copie en ordre croissant, puis
calcule tous les agrégats. `N` = nombre d'échantillons. La même fonction sert pour les
échantillons d'itération et pour ceux de `Setup`.

| Agrégat | Formule |
|---|---|
| `totalMs` | `Σ sample[i]` |
| `meanMs` | `totalMs / N` |
| `stddevMs` | `sqrt( (1/N) · Σ (sample[i] − mean)² )` (écart-type de population) |
| `minMs` / `maxMs` | `sorted.front()` / `sorted.back()` |
| `throughput` | `N / (totalMs / 1000)` (itérations par seconde) |

**Percentile** par interpolation linéaire, sur le tableau trié `s[0..N-1]`, pour
`p ∈ [0, 100]` :

```
si N == 0 : retourner 0
si N == 1 : retourner s[0]
rank = (p / 100) · (N - 1)
lo   = floor(rank)
frac = rank - lo
hi   = min(lo + 1, N - 1)
retourner s[lo] + frac · (s[hi] - s[lo])
```

`p50` = médiane, `p95` et `p99` mettent en évidence les pires cas. Cette méthode est
déterministe : mêmes échantillons → mêmes percentiles.

Cas limite : si `N == 0`, tous les agrégats valent `0` et le scénario est consigné
comme « sans échantillon » dans le rapport.

---

## Format du rapport

Sortie JSON, lisible par un humain et par un outil. `schema_version` permet de faire
évoluer le format sans casser les lecteurs existants.

```json
{
  "schema_version": 1,
  "timestamp": "2026-05-16T14:30:00Z",
  "environment": {
    "platform": "windows",
    "build": "release"
  },
  "results": [
    {
      "name": "example",
      "warmup_seconds": 0.5,
      "measure_seconds": 5.0,
      "measure_setup": true,
      "iteration": {
        "samples": 612,
        "total_ms": 5012.70,
        "mean_ms": 8.19,
        "stddev_ms": 1.42,
        "min_ms": 6.05,
        "max_ms": 23.10,
        "p50_ms": 7.98,
        "p95_ms": 10.66,
        "p99_ms": 15.20,
        "throughput": 122.1
      },
      "setup": {
        "samples": 30,
        "total_ms": 4275.00,
        "mean_ms": 142.50,
        "stddev_ms": 9.30,
        "min_ms": 131.00,
        "max_ms": 168.40,
        "p50_ms": 141.00,
        "p95_ms": 159.20,
        "p99_ms": 167.00,
        "throughput": 7.0
      },
      "counters": {
        "items_processed": 1024.0
      }
    }
  ]
}
```

| Champ | Type | Description |
|---|---|---|
| `schema_version` | entier | Version du format de rapport. |
| `timestamp` | chaîne | Date/heure de l'exécution, ISO 8601 UTC. |
| `environment` | objet | Plateforme et type de build (`debug` / `release`). |
| `results[]` | tableau | Un objet par scénario exécuté. |
| `results[].measure_setup` | booléen | Indique si le coût de `Setup` a été mesuré. |
| `results[].iteration` | objet | Statistiques des itérations `RunIteration`. |
| `results[].setup` | objet | Statistiques de `Setup` ; **absent** si `measure_setup` vaut `false`. |
| `results[].counters` | objet | Compteurs contextuels moyennés ; `{}` si aucun. |

Les objets `iteration` et `setup` ont la même forme : ce sont les champs de
`Statistics` (`samples`, `total_ms`, `mean_ms`, `stddev_ms`, `min_ms`, `max_ms`,
`p50_ms`, `p95_ms`, `p99_ms`, `throughput`).

---

## Organisation des fichiers

Module isolé, sans dépendance vers un scénario concret. En-têtes avec `#pragma once`.

```
benchmark/
  BenchmarkConfig.h     structures de configuration
  Scenario.h            interface Scenario + CounterSink
  ScenarioRegistry.h    déclaration ; .cpp pour l'implémentation
  ScenarioRegistry.cpp
  Statistics.h          ComputeStatistics + Percentile
  Statistics.cpp
  Runner.h              orchestration
  Runner.cpp
  Report.h              structures de rapport + sérialisation JSON
  Report.cpp
  benchmark_main.cpp    point d'entrée CLI
```

Ajouter une cible de build distincte produisant un binaire `benchmark` séparé du
binaire principal.

---

## Étapes d'implémentation

| # | Étape | Critère d'acceptation |
|---|---|---|
| 1 | Structures (`BenchmarkConfig`, `Statistics`, `ScenarioResult`, `BenchmarkReport`). | Compile ; aucune dépendance circulaire entre en-têtes. |
| 2 | `Statistics` : `ComputeStatistics` + `Percentile`. | Test unitaire : sur `{1,2,3,4,5}`, `mean=3`, `p50=3`, `max=5`. |
| 3 | Interface `Scenario` + `CounterSink` + `ScenarioRegistry`. | Un scénario factice s'enregistre, se crée ; nom inconnu → `nullptr`. |
| 4 | `Runner` : chauffe, mesure, cycle de vie de l'itération. | Scénario calibré (`sleep` fixe) → `samples` et `meanMs` dans la marge attendue. |
| 5 | Phase 0 : benchmark de `Setup` (`measureSetup`). | `measureSetup=false` → phase 0 ignorée ; `true` → `setupStats.samples == setupSamples`. |
| 6 | `Report` : sérialisation JSON. | Round-trip ; clé `setup` présente ssi `measureSetup` est vrai. |
| 7 | `benchmark_main` : CLI. | `benchmark <config>` produit un rapport JSON valide. |
| 8 | Vérification de répétabilité. | 3 exécutions du même scénario → écart relatif des moyennes < 5 %. |

---

## Critères de validation

- Une itération vide (`RunIteration` ne fait rien) produit `meanMs` proche de zéro
  sans plantage ni division par zéro.
- Un scénario absent du registre n'interrompt pas les autres scénarios.
- `N == 0` produit un résultat à zéro, pas une erreur.
- `measureSetup` désactivé : aucune paire `Setup/Teardown` supplémentaire n'est
  exécutée et la clé `setup` est absente du rapport.
- `measureSetup` activé : `setupStats` contient exactement `setupSamples`
  échantillons, la paire de chauffe exclue.
- Deux exécutions consécutives, conditions identiques, donnent des moyennes dont
  l'écart relatif reste sous le seuil défini à l'étape 8.

---

## Points de vigilance

- `RunIteration` ne contient que le travail à mesurer : pas d'allocation, pas de
  chargement de ressource (les placer dans `Setup`).
- Toujours prévoir une phase de chauffe non nulle pour absorber initialisation
  tardive, remplissage de cache et stabilisation des ressources.
- Ne comparer que des rapports produits sur la même plateforme et le même build.
- Conserver `environment` et `config` avec chaque résultat : un chiffre sans son
  contexte d'exécution n'est pas comparable.
- Éviter les mesures trop courtes : `measureSeconds` et `minSamples` doivent rester
  assez grands pour que l'écart-type soit représentatif.
- Dès que `measureSetup` est activé, le scénario doit supporter des paires
  `Setup/Teardown` répétées : aucune ressource globale fuyante, aucun état accumulé
  d'un cycle à l'autre.

---

## Extensions possibles

Une fois la base validée (étapes 1 à 7) :

- comparaison automatique d'un rapport à un rapport de référence ;
- seuils d'échec (faire échouer le binaire si une régression dépasse une marge) ;
- export CSV en complément du JSON ;
- intégration CI ;
- suivi historique des performances.
