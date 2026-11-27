// Bibliothèque de test de `platform::DynamicLibrary` (M8, A79 lot L1), construite par `make test` à côté de l'exécutable des
// tests (`bin/tests/test_module.so|.dylib|.dll`), hors de la liste des sources des tests. Elle n'est liée à AUCUNE archive du
// moteur : `Logger::AddLog` et `Logger::KeptLogCount` restent indéfinis ici et sont résolus dans l'exécutable hôte au
// chargement (`ENGINE_HOST_LDFLAGS` / `ENGINE_MODULE_LDFLAGS` de tools/engine-sdk/engine.mk). Le test vérifie que le message
// écrit d'ici arrive dans le journal de l'hôte : une copie du moteur dans la bibliothèque aurait son propre journal.
#include <orosmo/core/log.hpp>

#include <cstddef>

#if defined(_WIN32)
#define OROSMO_TEST_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define OROSMO_TEST_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

OROSMO_TEST_MODULE_EXPORT int orosmoTestModuleAdd(int a, int b) { return a + b; }

// Écrit `message` dans le journal du moteur et renvoie le nombre de messages que CE journal garde, vu depuis la bibliothèque.
OROSMO_TEST_MODULE_EXPORT std::size_t orosmoTestModuleLog(const char *message)
{
    LOG_INFO(message);
    return orosmo::core::Logger::KeptLogCount();
}

// Variable `static` locale d'une fonction `inline` : GCC sous Linux en fait par défaut un symbole STB_GNU_UNIQUE, unique dans le
// processus malgré RTLD_LOCAL, qui rend aussi `dlclose` sans effet. ENGINE_MODULE_CXXFLAGS passe -fno-gnu-unique : chaque copie
// chargée garde son propre compteur et repart de zéro après un rechargement (tests/platform/dynamic_library_tests.cpp).
inline int &orosmoTestModuleCounter()
{
    static int counter = 0;
    return counter;
}

OROSMO_TEST_MODULE_EXPORT int orosmoTestModuleIncrement() { return ++orosmoTestModuleCounter(); }
