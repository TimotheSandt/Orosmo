// RHI-01 : handles opaques et table générationnelle (aucun GPU requis).
#include <doctest/doctest.h>

#include <orosmo/rhi/device.hpp>
#include <orosmo/rhi/handle.hpp>

#include "support/result_support.hpp"

using namespace orosmo::rhi;

TEST_SUITE("rhi/handle")
{
    TEST_CASE("a default handle is null and never valid")
    {
        BufferHandle h;
        CHECK(h.isNull());
        CHECK_FALSE(static_cast<bool>(h));
        HandlePool<BufferTag, int> pool;
        CHECK(pool.get(h) == nullptr);
        CHECK_FALSE(pool.contains(h));
        CHECK_FALSE(pool.remove(h).has_value());
    }

    TEST_CASE("a handle stays valid until its resource is removed")
    {
        HandlePool<BufferTag, int> pool;
        const BufferHandle a = pool.add(10);
        const BufferHandle b = pool.add(20);
        REQUIRE(pool.get(a) != nullptr);
        CHECK(*pool.get(a) == 10);
        CHECK(*pool.get(b) == 20);
        CHECK(pool.size() == 2);
        CHECK(pool.remove(a).value() == 10);
        CHECK(pool.get(a) == nullptr);
        CHECK(*pool.get(b) == 20);
        CHECK(pool.size() == 1);
    }

    TEST_CASE("a recycled slot gets a new generation: the old handle is detected as stale")
    {
        HandlePool<TextureTag, int> pool;
        const TextureHandle first = pool.add(1);
        pool.remove(first);
        const TextureHandle second = pool.add(2);
        CHECK(second.index == first.index); // le même emplacement...
        CHECK(second.generation != first.generation); // ... mais une autre génération
        CHECK(pool.get(first) == nullptr);
        REQUIRE(pool.get(second) != nullptr);
        CHECK(*pool.get(second) == 2);
        CHECK_FALSE(pool.remove(first).has_value()); // retirer deux fois est sans effet
        CHECK(pool.size() == 1);
    }

    TEST_CASE("handles of different kinds are distinct types")
    {
        CHECK_FALSE((std::is_same_v<BufferHandle, TextureHandle>));
        CHECK_FALSE((std::is_convertible_v<BufferHandle, TextureHandle>));
        CHECK(BufferHandle{1, 1} == BufferHandle{1, 1});
        CHECK_FALSE(BufferHandle{1, 1} == BufferHandle{1, 2});
    }

    TEST_CASE("forEach visits only live resources")
    {
        HandlePool<BufferTag, int> pool;
        const auto a = pool.add(1);
        pool.add(2);
        pool.add(3);
        pool.remove(a);
        int sum = 0;
        pool.forEach([&](BufferHandle, int v) { sum += v; });
        CHECK(sum == 5);
    }
}

TEST_SUITE("rhi/format")
{
    TEST_CASE("formatInfo gives the texel size and kind of every format")
    {
        CHECK(formatInfo(Format::kRGBA8Unorm).bytesPerTexel == 4);
        CHECK(formatInfo(Format::kRG16Float).bytesPerTexel == 4);
        CHECK(formatInfo(Format::kRGBA16Float).bytesPerTexel == 8);
        CHECK(formatInfo(Format::kR32Float).bytesPerTexel == 4);
        CHECK((formatInfo(Format::kR32Uint).kind == FormatKind::kUint));
        CHECK(formatInfo(Format::kD32Float).isDepth());
        CHECK(formatInfo(Format::kD24UnormS8Uint).isDepth());
        CHECK_FALSE(formatInfo(Format::kRGBA8Srgb).isDepth());
        CHECK(formatInfo(Format::kUndefined).bytesPerTexel == 0);
    }

    TEST_CASE("flag enums combine and test")
    {
        const BufferUsage usage = BufferUsage::kVertex | BufferUsage::kCopyDst;
        CHECK(hasFlag(usage, BufferUsage::kVertex));
        CHECK(hasFlag(usage, BufferUsage::kCopyDst));
        CHECK_FALSE(hasFlag(usage, BufferUsage::kIndex));
        CHECK_FALSE(hasFlag(usage, BufferUsage::kNone));
    }
}

TEST_SUITE("rhi/device")
{
    TEST_CASE("a backend that is not compiled is refused with kUnsupported")
    {
        CHECK(isBackendCompiled(Backend::kOpenGL));
        CHECK_FALSE(isBackendCompiled(Backend::kVulkan));
        DeviceDesc desc;
        desc.backend = Backend::kVulkan;
        auto device = createDevice(desc);
        REQUIRE_FALSE(device.has_value());
        CHECK((tests::errorOf(device) == orosmo::core::ErrorCode::kUnsupported));
    }
}
