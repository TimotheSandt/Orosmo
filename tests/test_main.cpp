// Point d'entrée des tests (doctest). Options : voir `--help` (filtres `-ts=`, `-tc=`, liste `-ltc`).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>

#include <orosmo/core/log.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__SANITIZE_ADDRESS__)
// libshaderc 2023.8 (Ubuntu 24.04, apt) alloue ses options avec `operator new` et les libère avec `free` : ASan signale un
// alloc-dealloc-mismatch qui ne vient pas de notre code. On désactive CE contrôle seulement, pour les tests (docs/GAPS.md G76).
extern "C" const char *__asan_default_options() { return "alloc_dealloc_mismatch=0"; }
#endif

// Diagnostic : écrit « [trace] suite :: cas » (stderr, vidé aussitôt) au début de chaque cas quand DOCTEST_TRACE ou CI est défini. Un plantage
// (accès invalide, erreur de bus) avant la fin du rapport laisse sinon la sortie standard, mise en tampon, sans le nom du cas fautif.
namespace
{
struct TraceListener : doctest::IReporter
{
    explicit TraceListener(const doctest::ContextOptions &) {}
    static bool enabled() { return std::getenv("DOCTEST_TRACE") != nullptr || std::getenv("CI") != nullptr; }
    void report_query(const doctest::QueryData &) override {}
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats &) override {}
    void test_case_start(const doctest::TestCaseData &tc) override
    {
        if (enabled())
        {
            std::fprintf(stderr, "[trace] %s :: %s\n", tc.m_test_suite, tc.m_name);
            std::fflush(stderr);
        }
    }
    void test_case_reenter(const doctest::TestCaseData &) override {}
    void test_case_end(const doctest::CurrentTestCaseStats &) override {}
    void test_case_exception(const doctest::TestCaseException &) override {}
    void subcase_start(const doctest::SubcaseSignature &) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData &) override {}
    void log_message(const doctest::MessageData &) override {}
    void test_case_skipped(const doctest::TestCaseData &) override {}
};
} // namespace
DOCTEST_REGISTER_LISTENER("trace", 1, TraceListener);

int main(int argc, char **argv)
{
    orosmo::core::Logger::SetMinimumLevel(orosmo::core::L_FATAL);

#if defined(__linux__) && (GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4))
    // Les tests créent des fenêtres cachées. Sous Wayland, une fenêtre jamais affichée ne reçoit pas de rappel d'image : une
    // présentation avec vsync (FIFO) y attend sans fin (G191). Sous une session Wayland qui a aussi X11 (XWayland), les tests
    // passent donc par X11, comme la CI (xvfb). ENGINE_TEST_WAYLAND=1 garde Wayland (cas sans fenêtre cachée seulement).
    if (std::getenv("WAYLAND_DISPLAY") != nullptr && std::getenv("DISPLAY") != nullptr && std::getenv("ENGINE_TEST_WAYLAND") == nullptr)
    {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
#endif

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
