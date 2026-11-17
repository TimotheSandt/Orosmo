#include "Benchmark/Scenario.h"

namespace bench
{

void CounterSink::Add(const std::string &name, double value)
{
    m_sums[name] += value;
}

std::map<std::string, double> CounterSink::Averages(std::size_t sampleCount) const
{
    std::map<std::string, double> result;
    if (sampleCount == 0)
    {
        return result;
    }
    const double n = static_cast<double>(sampleCount);
    for (const auto &kv : m_sums)
    {
        result[kv.first] = kv.second / n;
    }
    return result;
}

void ScenarioRegistry::Register(const std::string &name, ScenarioFactory factory)
{
    m_factories[name] = std::move(factory);
}

bool ScenarioRegistry::Contains(const std::string &name) const
{
    return m_factories.count(name) != 0;
}

std::unique_ptr<Scenario> ScenarioRegistry::Create(const std::string &name) const
{
    const auto it = m_factories.find(name);
    if (it == m_factories.end())
    {
        return nullptr;
    }
    return it->second();
}

} // namespace bench
