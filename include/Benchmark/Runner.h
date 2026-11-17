#pragma once

#include "BenchmarkConfig.h"
#include "Report.h"
#include "Scenario.h"
#include "Statistics.h"


namespace bench
{

class Runner
{
  public:
    explicit Runner(const BenchmarkConfig &config);
    BenchmarkReport Run(ScenarioRegistry &registry);

  private:
    BenchmarkConfig m_config; // by value: a temporary passed to the constructor must not dangle
};

} // namespace bench
