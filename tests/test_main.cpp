// Point d'entrée des tests (doctest). Options : voir `--help` (filtres `-ts=`, `-tc=`, liste `-ltc`).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Utilities/Logger.h"

#if defined(__SANITIZE_ADDRESS__)
// libshaderc 2023.8 (Ubuntu 24.04, apt) alloue ses options avec `operator new` et les libère avec `free` : ASan signale un
// alloc-dealloc-mismatch qui ne vient pas de notre code. On désactive CE contrôle seulement, pour les tests (docs/GAPS.md G76).
extern "C" const char *__asan_default_options() { return "alloc_dealloc_mismatch=0"; }
#endif

int main(int argc, char **argv)
{
    Logger::SetMinimumLevel(L_FATAL);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
