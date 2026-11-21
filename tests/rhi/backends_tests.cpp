// Outillage des tests GPU : la liste des backends disponibles (rhi/backends.hpp) et FOR_EACH_BACKEND.
#include <doctest/doctest.h>

#include "rhi/backends.hpp"
#include "support/test_support.hpp"

#include <algorithm>

using orosmo::rhi::Backend;

TEST_SUITE("rhi/backends")
{
    TEST_CASE("availableBackends only lists compiled backends that can create a device; OpenGL is there when a context exists")
    {
        for (const Backend backend : tests::availableBackends())
        {
            CAPTURE(tests::backendName(backend));
            CHECK(orosmo::rhi::isBackendCompiled(backend));
        }
        orosmo::rhi::DeviceDesc desc;
        desc.backend = Backend::kOpenGL;
        desc.surface.size = {8, 8};
        const bool canCreate = orosmo::rhi::isBackendCompiled(Backend::kOpenGL) && orosmo::rhi::createDevice(desc).has_value();
        const auto &list = tests::availableBackends();
        const bool listed = std::find(list.begin(), list.end(), Backend::kOpenGL) != list.end();
        const char *restricted = std::getenv("ENGINE_TEST_BACKENDS");
        if (restricted == nullptr || *restricted == '\0')
        {
            CHECK(listed == canCreate);
        }
        if (tests::gpuRequired() && restricted == nullptr)
        {
            CHECK(listed); // la CI Linux garantit OpenGL 4.3 (G100)
        }
    }

    TEST_CASE("the backend list of ENGINE_TEST_BACKENDS is parsed without case, duplicates or unknown names")
    {
        const auto list = tests::detail::parseBackendList("OpenGL, vulkan;opengl,bogus");
        REQUIRE(list.size() == 2);
        CHECK((list[0] == Backend::kOpenGL));
        CHECK((list[1] == Backend::kVulkan));
        CHECK(tests::detail::parseBackendList("").empty());
    }

    TEST_CASE("FOR_EACH_BACKEND runs the body once per available backend")
    {
        std::size_t runs = 0;
        FOR_EACH_BACKEND(backend)
        {
            CAPTURE(tests::backendName(backend));
            CHECK(tests::isBackendAvailable(backend));
            ++runs;
        }
        // doctest rejoue le cas une fois par sous-cas : chaque passage exécute au plus un backend.
        CHECK(runs <= 1);
    }
}
