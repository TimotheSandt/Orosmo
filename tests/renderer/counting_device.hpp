// Périphérique de test qui délègue tout à un vrai périphérique et note chaque `updateBuffer` (tampon, plage) et chaque tampon créé
// (mémoire) : compteurs observables des écritures par image (perf M5, anneaux par image en vol).
#pragma once

#include <orosmo/rhi/device.hpp>

#include <cstdint>
#include <map>
#include <vector>

namespace tests
{

class CountingDevice final : public orosmo::rhi::Device
{
  public:
    struct Write
    {
        orosmo::rhi::BufferHandle buffer;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    explicit CountingDevice(orosmo::rhi::Device &inner) : inner_(inner) {}

    std::vector<Write> writes;
    std::map<std::uint64_t, orosmo::rhi::MemoryKind> memory; // key(handle) -> mémoire demandée
    static std::uint64_t key(orosmo::rhi::BufferHandle h) { return (std::uint64_t{h.index} << 32) | h.generation; }

    using Result0 = orosmo::rhi::Result<>;
    const orosmo::rhi::Capabilities &capabilities() const noexcept override { return inner_.capabilities(); }
    orosmo::rhi::Result<orosmo::rhi::BufferHandle> createBuffer(const orosmo::rhi::BufferDesc &desc, std::span<const std::byte> initial) override
    {
        auto r = inner_.createBuffer(desc, initial);
        if (r)
        {
            memory[key(*r)] = desc.memory;
        }
        return r;
    }
    orosmo::rhi::Result<orosmo::rhi::TextureHandle> createTexture(const orosmo::rhi::TextureDesc &d) override { return inner_.createTexture(d); }
    // Échantillonneurs : `failSamplerAt` = rang (0, 1…) de la création qui échoue (-1 : aucune) ; `liveSamplers` = créés - détruits.
    int failSamplerAt = -1;
    int samplersRequested = 0;
    int liveSamplers = 0;
    orosmo::rhi::Result<orosmo::rhi::SamplerHandle> createSampler(const orosmo::rhi::SamplerDesc &d) override
    {
        if (samplersRequested++ == failSamplerAt)
        {
            return orosmo::core::makeError(orosmo::core::ErrorCode::kBusy, "CountingDevice: injected sampler failure");
        }
        auto r = inner_.createSampler(d);
        liveSamplers += r ? 1 : 0;
        return r;
    }
    orosmo::rhi::Result<orosmo::rhi::ShaderHandle> createShader(const orosmo::rhi::ShaderDesc &d) override { return inner_.createShader(d); }
    orosmo::rhi::Result<orosmo::rhi::PipelineHandle> createGraphicsPipeline(const orosmo::rhi::GraphicsPipelineDesc &d) override
    {
        return inner_.createGraphicsPipeline(d);
    }
    orosmo::rhi::Result<orosmo::rhi::PipelineHandle> createComputePipeline(const orosmo::rhi::ComputePipelineDesc &d) override
    {
        return inner_.createComputePipeline(d);
    }
    orosmo::rhi::Result<orosmo::rhi::BindGroupHandle> createBindGroup(const orosmo::rhi::BindGroupDesc &d) override { return inner_.createBindGroup(d); }
    orosmo::rhi::Result<orosmo::rhi::QueryPoolHandle> createQueryPool(std::uint32_t count, const char *name) override
    {
        return inner_.createQueryPool(count, name);
    }
    void destroy(orosmo::rhi::BufferHandle h) override { inner_.destroy(h); }
    void destroy(orosmo::rhi::TextureHandle h) override { inner_.destroy(h); }
    void destroy(orosmo::rhi::SamplerHandle h) override
    {
        liveSamplers -= h.isNull() ? 0 : 1;
        inner_.destroy(h);
    }
    void destroy(orosmo::rhi::ShaderHandle h) override { inner_.destroy(h); }
    void destroy(orosmo::rhi::PipelineHandle h) override { inner_.destroy(h); }
    void destroy(orosmo::rhi::BindGroupHandle h) override { inner_.destroy(h); }
    void destroy(orosmo::rhi::QueryPoolHandle h) override { inner_.destroy(h); }
    orosmo::rhi::Result<orosmo::rhi::TextureDesc> textureDesc(orosmo::rhi::TextureHandle t) const override { return inner_.textureDesc(t); }
    Result0 updateBuffer(orosmo::rhi::BufferHandle b, std::uint64_t offset, std::span<const std::byte> data) override
    {
        writes.push_back({b, offset, data.size()});
        return inner_.updateBuffer(b, offset, data);
    }
    Result0 updateTexture(orosmo::rhi::TextureHandle t, const orosmo::rhi::TextureCopy &region, std::span<const std::byte> data) override
    {
        return inner_.updateTexture(t, region, data);
    }
    orosmo::rhi::Result<std::vector<std::byte>> readBuffer(orosmo::rhi::BufferHandle b, std::uint64_t offset, std::uint64_t size) override
    {
        return inner_.readBuffer(b, offset, size);
    }
    orosmo::rhi::Result<std::vector<std::byte>> readTexture(orosmo::rhi::TextureHandle t, const orosmo::rhi::TextureCopy &region) override
    {
        return inner_.readTexture(t, region);
    }
    orosmo::rhi::Result<bool> timestampsReady(orosmo::rhi::QueryPoolHandle p, std::uint32_t first, std::uint32_t count) override
    {
        return inner_.timestampsReady(p, first, count);
    }
    orosmo::rhi::Result<std::vector<std::uint64_t>> readTimestamps(orosmo::rhi::QueryPoolHandle p, std::uint32_t first, std::uint32_t count) override
    {
        return inner_.readTimestamps(p, first, count);
    }
    std::unique_ptr<orosmo::rhi::CommandEncoder> createCommandEncoder() override { return inner_.createCommandEncoder(); }
    Result0 submit(std::unique_ptr<orosmo::rhi::CommandEncoder> e) override { return inner_.submit(std::move(e)); }
    orosmo::rhi::Result<orosmo::rhi::TextureHandle> beginFrame() override { return inner_.beginFrame(); }
    Result0 present() override { return inner_.present(); }
    Result0 resize(orosmo::rhi::Extent2D size) override { return inner_.resize(size); }
    void setVSync(bool enabled) override { inner_.setVSync(enabled); }
    void setDirectPresentation(bool enabled) override { inner_.setDirectPresentation(enabled); }
    void waitIdle() override { inner_.waitIdle(); }
    std::uint64_t validationErrorCount() const noexcept override { return inner_.validationErrorCount(); }
    orosmo::rhi::DeviceStats stats() const noexcept override { return inner_.stats(); }

  private:
    orosmo::rhi::Device &inner_;
};

} // namespace tests
