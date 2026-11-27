// Backends du RHI disponibles pour les tests : compilés ET créables sur cette machine.
//
//   ENGINE_TEST_BACKENDS=opengl,vulkan   restreint (et désigne) les backends à tester ; sans la variable, tous ceux qui sont
//                                        compilés et créables sont testés.
//   ENGINE_TEST_REQUIRE_GPU=1            un backend désigné par ENGINE_TEST_BACKENDS mais indisponible fait ÉCHOUER le test ;
//                                        sans désignation, c'est l'absence de TOUT backend qui échoue (comme avant : l'absence
//                                        d'OpenGL sur la CI Linux ne passe pas pour un environnement sans affichage, G100).
//
// FOR_EACH_BACKEND(backend) répète le corps du cas de test une fois par backend, dans un SUBCASE nommé d'après lui (doctest
// affiche « SUBCASE: opengl » à chaque échec). Aucun backend : le cas est ignoré avec « Skipped (no driver) ».
#pragma once

#include <doctest/doctest.h>

#include <orosmo/rhi/device.hpp>

#include "support/test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tests
{

inline const char *backendName(orosmo::rhi::Backend backend) noexcept
{
    switch (backend)
    {
    case orosmo::rhi::Backend::kVulkan:
        return "vulkan";
    case orosmo::rhi::Backend::kOpenGL:
        return "opengl";
    case orosmo::rhi::Backend::kMetal:
        return "metal";
    }
    return "unknown";
}

namespace detail
{

struct BackendProbe
{
    std::vector<orosmo::rhi::Backend> available;
    std::vector<orosmo::rhi::Backend> requested;   // désignés par ENGINE_TEST_BACKENDS (vide : aucune désignation)
    std::map<orosmo::rhi::Backend, std::string> why; // raison de l'indisponibilité d'un backend
};

inline std::vector<orosmo::rhi::Backend> parseBackendList(const char *text)
{
    std::vector<orosmo::rhi::Backend> out;
    std::string current;
    const auto flush = [&]
    {
        std::transform(current.begin(), current.end(), current.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto candidate : {orosmo::rhi::Backend::kVulkan, orosmo::rhi::Backend::kOpenGL, orosmo::rhi::Backend::kMetal})
        {
            if (current == backendName(candidate) && std::find(out.begin(), out.end(), candidate) == out.end())
            {
                out.push_back(candidate);
            }
        }
        current.clear();
    };
    for (const char *p = text; *p != '\0'; ++p)
    {
        if (*p == ',' || *p == ' ' || *p == ';')
        {
            flush();
        }
        else
        {
            current.push_back(*p);
        }
    }
    flush();
    return out;
}

inline const BackendProbe &backendProbe()
{
    REQUIRE_GPU_ALLOWED();
    static const BackendProbe probe = []
    {
        BackendProbe result;
        const char *env = std::getenv("ENGINE_TEST_BACKENDS");
        if (env != nullptr && *env != '\0')
        {
            result.requested = parseBackendList(env);
        }
        const std::vector<orosmo::rhi::Backend> candidates =
            result.requested.empty() ? std::vector{orosmo::rhi::Backend::kOpenGL, orosmo::rhi::Backend::kVulkan, orosmo::rhi::Backend::kMetal}
                                     : result.requested;
        for (const orosmo::rhi::Backend backend : candidates)
        {
            if (!orosmo::rhi::isBackendCompiled(backend))
            {
                result.why[backend] = "not compiled";
                continue;
            }
            orosmo::rhi::DeviceDesc desc;
            desc.backend = backend;
            desc.surface.size = {8, 8};
            auto created = orosmo::rhi::createDevice(desc);
            if (created)
            {
                result.available.push_back(backend);
            }
            else
            {
                result.why[backend] = created.error().message;
            }
        }
        return result;
    }();
    return probe;
}

} // namespace detail

// Backends compilés et créables hors écran sur cette machine (essai de création fait une seule fois), restreints par
// ENGINE_TEST_BACKENDS.
inline const std::vector<orosmo::rhi::Backend> &availableBackends() { return detail::backendProbe().available; }

inline bool isBackendAvailable(orosmo::rhi::Backend backend)
{
    const auto &list = availableBackends();
    return std::find(list.begin(), list.end(), backend) != list.end();
}

// Backends sur lesquels un cas de test doit tourner. Gère l'absence : échec si ENGINE_TEST_REQUIRE_GPU l'exige, sinon
// avertissement « Skipped (no driver) » et liste vide (le corps de FOR_EACH_BACKEND ne s'exécute pas).
inline std::vector<orosmo::rhi::Backend> backendsToRun()
{
    const detail::BackendProbe &probe = detail::backendProbe();
    if (gpuRequired())
    {
        for (const orosmo::rhi::Backend backend : probe.requested)
        {
            if (!isBackendAvailable(backend))
            {
                const auto it = probe.why.find(backend);
                FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: " << backendName(backend) << " backend ("
                                                                             << (it != probe.why.end() ? it->second : std::string("unknown")) << ")");
            }
        }
    }
    if (probe.available.empty())
    {
        if (gpuRequired())
        {
            FAIL("Required by ENGINE_TEST_REQUIRE_GPU but unavailable: no RHI backend can create a device");
        }
        WARN_MESSAGE(false, "Skipped (no driver): no RHI backend can create a device");
    }
    return probe.available;
}

} // namespace tests

// Répète le corps pour chaque backend disponible : `FOR_EACH_BACKEND(backend) { Gpu gpu(backend); ... }`.
#define FOR_EACH_BACKEND(backendVar) \
    for (const orosmo::rhi::Backend backendVar : tests::backendsToRun()) \
        DOCTEST_SUBCASE(tests::backendName(backendVar))
