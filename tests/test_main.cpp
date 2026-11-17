// Point d'entrée des tests (doctest). Options : voir `--help` (filtres `-ts=`, `-tc=`, liste `-ltc`).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Utilities/Logger.h"

int main(int argc, char **argv)
{
    Logger::SetMinimumLevel(L_FATAL);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
