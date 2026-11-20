// Mesures de performance reproductibles : configuration, scénarios, exécuteur, statistiques et rapport JSON.
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace orosmo::core::bench
{

struct ScenarioConfig
{
    std::string name;
    double warmupSeconds = 0.5;
    double measureSeconds = 5.0;
    std::size_t minSamples = 100;
    std::size_t maxSamples = 0;
    bool measureSetup = false;
    std::size_t setupSamples = 30;
};

struct BenchmarkConfig
{
    std::vector<ScenarioConfig> scenarios;
    std::string outputPath = "benchmark_report.json";
    std::string build = "release";
};

struct Statistics
{
    std::size_t samples = 0;
    double totalMs = 0.0;
    double meanMs = 0.0;
    double stddevMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double throughput = 0.0; // itérations par seconde
};

// Trie une copie des échantillons (en ms) puis calcule l'ensemble des agrégats.
// Renvoie une structure mise à zéro si la liste est vide.
Statistics computeStatistics(std::vector<double> samples);

class CounterSink
{
  public:
    void add(const std::string &name, double value);

    // Renvoie la moyenne de chaque compteur accumulé sur sampleCount itérations.
    std::map<std::string, double> averages(std::size_t sampleCount) const;

  private:
    std::map<std::string, double> mSums;
};

class Scenario
{
  public:
    virtual ~Scenario() = default;

    // Identifiant stable utilisé pour retrouver le scénario dans le registre.
    virtual std::string name() const = 0;

    // Prépare les ressources. Doit pouvoir être rappelée après teardown()
    // (requis lorsque measureSetup est activé).
    virtual void setup() {}

    // Unité de travail mesurée. Pas d'allocation ni de chargement de ressource ici.
    virtual void runIteration() = 0;

    // Libère les ressources allouées par setup(). Doit laisser le scénario prêt
    // pour un nouvel appel à setup().
    virtual void teardown() {}

    // Optionnel : pousse des compteurs nommés après chaque itération.
    virtual void collectCounters(CounterSink &sink) const { (void)sink; }
};

using ScenarioFactory = std::function<std::unique_ptr<Scenario>()>;

class ScenarioRegistry
{
  public:
    void registerScenario(const std::string &name, ScenarioFactory factory);
    bool contains(const std::string &name) const;

    // Renvoie nullptr si le nom n'est pas enregistré.
    std::unique_ptr<Scenario> create(const std::string &name) const;

  private:
    std::unordered_map<std::string, ScenarioFactory> mFactories;
};

struct ScenarioResult
{
    std::string name;
    ScenarioConfig config;
    Statistics iterationStats;
    bool hasSetupStats = false;
    Statistics setupStats;
    std::map<std::string, double> counters;
};

struct BenchmarkReport
{
    std::string timestamp;
    std::string platform;
    std::string build;
    std::vector<ScenarioResult> results;
};

// Sérialise le rapport au format JSON dans outputPath.
// Lève std::runtime_error si le fichier ne peut pas être ouvert.
void writeReport(const BenchmarkReport &report, const std::string &outputPath);

class Runner
{
  public:
    explicit Runner(const BenchmarkConfig &config);
    BenchmarkReport run(ScenarioRegistry &registry);

  private:
    BenchmarkConfig mConfig; // par valeur : un temporaire passé au constructeur ne doit pas pendre
};

} // namespace orosmo::core::bench
