// UI-08 (A79 lot L8) : platform::runCommand.
#include <doctest/doctest.h>

#include <orosmo/platform/process.hpp>

TEST_SUITE("platform/process")
{
    TEST_CASE("a command ending with a shell comment still runs")
    {
        auto result = orosmo::platform::runCommand("echo ok # rebuild");
        REQUIRE(result.has_value());
        CHECK(result->exitCode == 0);
        CHECK(result->output.find("ok") != std::string::npos);
    }
}
