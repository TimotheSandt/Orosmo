#include <doctest/doctest.h>

#include <orosmo/core/error.hpp>

#include <stdexcept>

using namespace orosmo::core;


namespace
{
Result<int> parsePositive(int v)
{
    if (v < 0)
        return makeError(ErrorCode::kOutOfRange, "negative");
    return v;
}

Result<> mayFail(bool fail)
{
    if (fail)
        return makeError(ErrorCode::kIo, "disk");
    return {};
}

Result<int> chain(int v)
{
    auto r = parsePositive(v);
    if (!r)
        return std::unexpected(r.error());
    return *r * 2;
}

void throwingHandler(const char *, const char *msg, std::source_location) { throw std::runtime_error(msg); }
} // namespace

TEST_SUITE("core/error")
{
    TEST_CASE("Result carries a value or an Error")
    {
        CHECK(parsePositive(3).value() == 3);
        auto bad = parsePositive(-1);
        REQUIRE_FALSE(bad.has_value());
        CHECK((bad.error().code == ErrorCode::kOutOfRange));
        CHECK(bad.error().message == "negative");
        CHECK(mayFail(false).has_value());
        CHECK((mayFail(true).error().code == ErrorCode::kIo));
    }

    TEST_CASE("errors propagate through a chain")
    {
        CHECK(chain(4).value() == 8);
        CHECK((chain(-4).error().code == ErrorCode::kOutOfRange));
    }

    TEST_CASE("describe names the code, the message and the location")
    {
        auto e = parsePositive(-1).error();
        const std::string d = e.describe();
        CHECK(d.find("kOutOfRange") != std::string::npos);
        CHECK(d.find("negative") != std::string::npos);
        CHECK(d.find("error_tests.cpp") != std::string::npos);
        CHECK(std::string(toString(ErrorCode::kParse)) == "kParse");
    }

    TEST_CASE("ENGINE_ASSERT_ALWAYS calls the handler on failure and not on success")
    {
        auto previous = setAssertHandler(&throwingHandler);
        CHECK_NOTHROW(ENGINE_ASSERT_ALWAYS(1 + 1 == 2, "math works"));
        CHECK_THROWS_WITH_AS(ENGINE_ASSERT_ALWAYS(1 + 1 == 3, "broken math"), "broken math", std::runtime_error);
        setAssertHandler(previous);
    }
}
