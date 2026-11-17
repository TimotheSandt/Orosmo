#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace bench
{

class CounterSink
{
  public:
    void Add(const std::string &name, double value);

    // Returns the average of each accumulated counter over sampleCount iterations.
    std::map<std::string, double> Averages(std::size_t sampleCount) const;

  private:
    std::map<std::string, double> m_sums;
};

class Scenario
{
  public:
    virtual ~Scenario() = default;

    // Stable identifier used to look up the scenario in the registry.
    virtual std::string Name() const = 0;

    // Prepares resources. Must be safely callable again after a Teardown()
    // (required when measureSetup is enabled).
    virtual void Setup() {}

    // The sole measured unit of work. No allocation, no resource loading here.
    virtual void RunIteration() = 0;

    // Releases resources allocated by Setup(). Must leave the scenario in a
    // state where Setup() can be called again.
    virtual void Teardown() {}

    // Optional: push named counters to the sink after each iteration.
    virtual void CollectCounters(CounterSink &sink) const { (void)sink; }
};

using ScenarioFactory = std::function<std::unique_ptr<Scenario>()>;

class ScenarioRegistry
{
  public:
    void Register(const std::string &name, ScenarioFactory factory);
    bool Contains(const std::string &name) const;

    // Returns nullptr if name is not registered.
    std::unique_ptr<Scenario> Create(const std::string &name) const;

  private:
    std::unordered_map<std::string, ScenarioFactory> m_factories;
};

} // namespace bench
