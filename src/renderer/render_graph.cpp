#include "orosmo/renderer/render_graph.hpp"

#include "orosmo/core/profiler.hpp"

#include <algorithm>
#include <functional>
#include <queue>
#include <set>
#include <utility>

namespace orosmo::renderer
{

using core::ErrorCode;
using core::makeError;

namespace
{

const RgTextureDesc &emptyTextureDesc()
{
    static const RgTextureDesc desc{};
    return desc;
}
} // namespace

// ── Contexte ────────────────────────────────────────────────────────────────────────────────────────────────────────

rhi::TextureHandle RenderPassContext::texture(RgTexture texture) const
{
    return texture.index < graph_.textures_.size() ? graph_.textures_[texture.index].handle : rhi::TextureHandle{};
}

rhi::BufferHandle RenderPassContext::buffer(RgBuffer buffer) const
{
    return buffer.index < graph_.buffers_.size() ? graph_.buffers_[buffer.index].handle : rhi::BufferHandle{};
}

const RgTextureDesc &RenderPassContext::desc(RgTexture texture) const
{
    return texture.index < graph_.textures_.size() ? graph_.textures_[texture.index].desc : emptyTextureDesc();
}

// ── Déclaration ─────────────────────────────────────────────────────────────────────────────────────────────────────

RenderGraph::PassBuilder &RenderGraph::PassBuilder::read(RgTexture texture)
{
    graph_->addUse(pass_, {.texture = true, .resource = texture.index, .access = Access::kRead});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::read(RgBuffer buffer)
{
    graph_->addUse(pass_, {.texture = false, .resource = buffer.index, .access = Access::kRead});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::readFinal(RgTexture texture)
{
    graph_->addUse(pass_, {.texture = true, .resource = texture.index, .access = Access::kReadFinal});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::readFinal(RgBuffer buffer)
{
    graph_->addUse(pass_, {.texture = false, .resource = buffer.index, .access = Access::kReadFinal});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::write(RgTexture texture)
{
    graph_->addUse(pass_, {.texture = true, .resource = texture.index, .access = Access::kWrite});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::write(RgBuffer buffer)
{
    graph_->addUse(pass_, {.texture = false, .resource = buffer.index, .access = Access::kWrite});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::writeColor(RgTexture texture, rhi::LoadOp load, std::array<float, 4> clearColor)
{
    graph_->addUse(pass_, {.texture = true, .resource = texture.index, .access = Access::kColor, .load = load, .clearColor = clearColor});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::writeDepth(RgTexture texture, rhi::LoadOp load, float clearDepth)
{
    graph_->addUse(pass_, {.texture = true, .resource = texture.index, .access = Access::kDepth, .load = load, .clearDepth = clearDepth});
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::sideEffect()
{
    graph_->passes_[pass_].sideEffect = true;
    graph_->compiled_ = false;
    return *this;
}

RenderGraph::~RenderGraph() { releaseTransients(); }

void RenderGraph::reset()
{
    passes_.clear();
    textures_.clear();
    buffers_.clear();
    order_.clear();
    kept_.clear();
    compiled_ = false;
}

RgTexture RenderGraph::importTexture(std::string name, rhi::TextureHandle texture, const RgTextureDesc &desc)
{
    textures_.push_back({.name = std::move(name), .desc = desc, .imported = true, .handle = texture});
    compiled_ = false;
    return {static_cast<std::uint32_t>(textures_.size() - 1)};
}

RgBuffer RenderGraph::importBuffer(std::string name, rhi::BufferHandle buffer, const RgBufferDesc &desc)
{
    buffers_.push_back({.name = std::move(name), .desc = desc, .imported = true, .handle = buffer});
    compiled_ = false;
    return {static_cast<std::uint32_t>(buffers_.size() - 1)};
}

RgTexture RenderGraph::createTexture(std::string name, const RgTextureDesc &desc)
{
    textures_.push_back({.name = std::move(name), .desc = desc, .imported = false, .handle = {}});
    compiled_ = false;
    return {static_cast<std::uint32_t>(textures_.size() - 1)};
}

RgBuffer RenderGraph::createBuffer(std::string name, const RgBufferDesc &desc)
{
    buffers_.push_back({.name = std::move(name), .desc = desc, .imported = false, .handle = {}});
    compiled_ = false;
    return {static_cast<std::uint32_t>(buffers_.size() - 1)};
}

RgTexture RenderGraph::findTexture(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < textures_.size(); ++i)
    {
        if (textures_[i].name == name)
        {
            return {static_cast<std::uint32_t>(i)};
        }
    }
    return {};
}

RgBuffer RenderGraph::findBuffer(std::string_view name) const noexcept
{
    for (std::size_t i = 0; i < buffers_.size(); ++i)
    {
        if (buffers_[i].name == name)
        {
            return {static_cast<std::uint32_t>(i)};
        }
    }
    return {};
}

RenderGraph::PassBuilder RenderGraph::addPass(std::string name, PassType type, PassExecute execute)
{
    passes_.push_back({.name = std::move(name), .type = type, .execute = std::move(execute), .uses = {}, .sideEffect = false});
    compiled_ = false;
    return PassBuilder(*this, static_cast<std::uint32_t>(passes_.size() - 1));
}

std::optional<RenderGraph::PassBuilder> RenderGraph::pass(std::string_view name)
{
    for (std::size_t i = 0; i < passes_.size(); ++i)
    {
        if (passes_[i].name == name)
        {
            return PassBuilder(*this, static_cast<std::uint32_t>(i));
        }
    }
    return std::nullopt;
}

void RenderGraph::addUse(std::uint32_t pass, const ResourceUse &use)
{
    passes_[pass].uses.push_back(use); // un handle invalide est signalé par compile (validate)
    compiled_ = false;
}

// ── Compilation ─────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<> RenderGraph::validate() const
{
    std::set<std::string_view> passNames;
    for (const Pass &p : passes_)
    {
        if (!passNames.insert(p.name).second)
        {
            return makeError(ErrorCode::kAlreadyExists, "RenderGraph: two passes are named '" + p.name + "'");
        }
        if (!p.execute)
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: pass '" + p.name + "' has no execute function");
        }
    }
    std::set<std::string_view> resourceNames;
    for (const TextureResource &t : textures_)
    {
        if (!resourceNames.insert(t.name).second)
        {
            return makeError(ErrorCode::kAlreadyExists, "RenderGraph: two resources are named '" + t.name + "'");
        }
        if (!t.imported && (t.desc.size.width == 0 || t.desc.size.height == 0))
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: transient texture '" + t.name + "' has an empty size");
        }
        if (t.imported && t.handle.isNull())
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: imported texture '" + t.name + "' is null");
        }
    }
    for (const BufferResource &b : buffers_)
    {
        if (!resourceNames.insert(b.name).second)
        {
            return makeError(ErrorCode::kAlreadyExists, "RenderGraph: two resources are named '" + b.name + "'");
        }
        if (!b.imported && b.desc.size == 0)
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: transient buffer '" + b.name + "' has a zero size");
        }
        if (b.imported && b.handle.isNull())
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: imported buffer '" + b.name + "' is null");
        }
    }
    for (const Pass &p : passes_)
    {
        std::optional<rhi::Extent2D> attachmentSize;
        bool hasDepth = false;
        std::size_t colors = 0;
        for (const ResourceUse &u : p.uses)
        {
            if ((u.texture && u.resource >= textures_.size()) || (!u.texture && u.resource >= buffers_.size()))
            {
                return makeError(ErrorCode::kInvalidArgument, "RenderGraph: pass '" + p.name + "' uses an invalid resource handle");
            }
            if (u.access != Access::kColor && u.access != Access::kDepth)
            {
                continue;
            }
            if (p.type != PassType::kRaster)
            {
                return makeError(ErrorCode::kInvalidArgument, "RenderGraph: pass '" + p.name + "' declares attachments but is not a raster pass");
            }
            if (u.access == Access::kDepth)
            {
                if (hasDepth)
                {
                    return makeError(ErrorCode::kInvalidArgument, "RenderGraph: pass '" + p.name + "' declares two depth attachments");
                }
                hasDepth = true;
            }
            else
            {
                ++colors;
            }
            const rhi::Extent2D size = textures_[u.resource].desc.size;
            if (attachmentSize && (attachmentSize->width != size.width || attachmentSize->height != size.height))
            {
                return makeError(ErrorCode::kInvalidArgument,
                                 "RenderGraph: pass '" + p.name + "' has attachments of different sizes ('" + textures_[u.resource].name + "')");
            }
            attachmentSize = size;
        }
        if (p.type == PassType::kRaster && colors == 0 && !hasDepth)
        {
            return makeError(ErrorCode::kInvalidArgument, "RenderGraph: raster pass '" + p.name + "' has no attachment");
        }
        const std::uint32_t maxColors = device_.capabilities().maxColorAttachments;
        if (maxColors > 0 && colors > maxColors)
        {
            return makeError(ErrorCode::kUnsupported, "RenderGraph: pass '" + p.name + "' has more color attachments than Capabilities::maxColorAttachments");
        }
    }
    return {};
}

core::Result<> RenderGraph::compile()
{
    compiled_ = false;
    order_.clear();
    kept_.assign(passes_.size(), false);
    if (auto r = validate(); !r)
    {
        return r;
    }

    // Arêtes, ressource par ressource, en suivant l'ordre de déclaration des passes qui y accèdent :
    //   - écriture après écriture : chaque écrivain suit le précédent ; il n'en DÉPEND (arête de données, qui garde le précédent)
    //     que s'il consomme l'ancien contenu : attachement en `kLoad`, `write` générique (écriture partielle possible) ou lecture
    //     dans la même passe. Un attachement effacé (`kClear`) ou ignoré (`kDontCare`) ne fait qu'ordonner ;
    //   - lecture après écriture : `read` suit le dernier écrivain déclaré avant lui (ou tous, s'il n'y en a pas), `readFinal`
    //     suit le dernier écrivain ;
    //   - écriture après lecture : un écrivain suit les lecteurs de la version précédente (ordre seulement : ces lecteurs ne
    //     sont pas gardés pour autant).
    // `predecessors` sert à l'ordre, `dataPredecessors` (sans les écritures après lecture) aux passes gardées.
    // Une passe qui consomme une ressource TRANSITOIRE sans producteur (lecture sans écrivain, premier attachement en `kLoad`) est
    // notée dans `unproduced` : refusée plus bas si elle est gardée (son contenu serait indéfini). Une ressource importée sans
    // écrivain est lue telle quelle (contenu fourni par l'appelant).
    const std::size_t n = passes_.size();
    std::vector<std::vector<std::pair<std::uint32_t, std::string>>> successors(n); // (passe suivante, ressource)
    std::vector<std::vector<std::uint32_t>> predecessors(n);
    std::vector<std::vector<std::uint32_t>> dataPredecessors(n);
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::set<std::pair<std::uint32_t, std::uint32_t>> dataEdges;
    std::vector<std::pair<std::uint32_t, std::string>> unproduced; // (passe, ressource transitoire lue sans producteur)
    const auto addEdge = [&](std::uint32_t from, std::uint32_t to, const std::string &resource, bool data)
    {
        if (from == to)
        {
            return;
        }
        if (edges.insert({from, to}).second)
        {
            successors[from].push_back({to, resource});
            predecessors[to].push_back(from);
        }
        if (data && dataEdges.insert({from, to}).second)
        {
            dataPredecessors[to].push_back(from);
        }
    };
    const auto linkResource = [&](bool texture, std::uint32_t resource, const std::string &name, bool imported)
    {
        std::optional<std::uint32_t> lastWriter;
        std::vector<std::uint32_t> versionReaders; // lecteurs de la version de lastWriter
        std::vector<std::uint32_t> finalReaders;
        for (std::uint32_t p = 0; p < n; ++p)
        {
            bool writes = false;
            bool reads = false;
            bool readsFinal = false;
            bool loads = false;    // attachement en kLoad : lit l'ancien contenu
            bool overwrites = false; // write générique : contenu précédent peut-être gardé en partie
            for (const ResourceUse &u : passes_[p].uses)
            {
                if (u.texture != texture || u.resource != resource)
                {
                    continue;
                }
                if (u.access == Access::kRead)
                {
                    reads = true;
                }
                else if (u.access == Access::kReadFinal)
                {
                    readsFinal = true;
                }
                else
                {
                    writes = true;
                    if (u.access == Access::kWrite)
                    {
                        overwrites = true;
                    }
                    else if (u.load == rhi::LoadOp::kLoad)
                    {
                        loads = true;
                    }
                }
            }
            if (writes)
            {
                const bool consumes = loads || overwrites || reads || readsFinal;
                if (lastWriter)
                {
                    addEdge(*lastWriter, p, name, consumes);
                }
                else if (!imported && (loads || reads || readsFinal))
                {
                    unproduced.push_back({p, name});
                }
                for (const std::uint32_t r : versionReaders)
                {
                    addEdge(r, p, name, false);
                }
                versionReaders.clear();
                lastWriter = p;
            }
            else if (readsFinal || (reads && !lastWriter))
            {
                finalReaders.push_back(p);
            }
            else if (reads)
            {
                addEdge(*lastWriter, p, name, true);
                versionReaders.push_back(p);
            }
        }
        if (lastWriter)
        {
            for (const std::uint32_t r : finalReaders)
            {
                addEdge(*lastWriter, r, name, true);
            }
        }
        else if (!imported)
        {
            for (const std::uint32_t r : finalReaders)
            {
                unproduced.push_back({r, name});
            }
        }
    };
    for (std::uint32_t t = 0; t < textures_.size(); ++t)
    {
        linkResource(true, t, textures_[t].name, textures_[t].imported);
    }
    for (std::uint32_t b = 0; b < buffers_.size(); ++b)
    {
        linkResource(false, b, buffers_[b].name, buffers_[b].imported);
    }

    // Tri topologique stable (à égalité, l'ordre de déclaration).
    std::vector<std::uint32_t> inDegree(n, 0);
    for (std::uint32_t p = 0; p < n; ++p)
    {
        inDegree[p] = static_cast<std::uint32_t>(predecessors[p].size());
    }
    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;
    for (std::uint32_t p = 0; p < n; ++p)
    {
        if (inDegree[p] == 0)
        {
            ready.push(p);
        }
    }
    std::vector<std::uint32_t> sorted;
    sorted.reserve(n);
    while (!ready.empty())
    {
        const std::uint32_t p = ready.top();
        ready.pop();
        sorted.push_back(p);
        for (const auto &[next, resource] : successors[p])
        {
            if (--inDegree[next] == 0)
            {
                ready.push(next);
            }
        }
    }
    if (sorted.size() != n)
    {
        // Un cycle existe parmi les passes restantes. Chacune a un prédécesseur restant : en remontant les prédécesseurs, on
        // retombe forcément sur une passe déjà vue, qui ferme le cycle.
        std::vector<bool> done(n, false);
        for (const std::uint32_t p : sorted)
        {
            done[p] = true;
        }
        std::uint32_t current = 0;
        while (done[current])
        {
            ++current;
        }
        std::vector<std::uint32_t> path;
        std::vector<int> seenAt(n, -1);
        while (seenAt[current] < 0)
        {
            seenAt[current] = static_cast<int>(path.size());
            path.push_back(current);
            for (const std::uint32_t before : predecessors[current])
            {
                if (!done[before])
                {
                    current = before;
                    break;
                }
            }
        }
        // path[seenAt[current]..] parcourt le cycle à rebours : on le remet dans le sens des dépendances.
        std::vector<std::uint32_t> cycle(path.begin() + seenAt[current], path.end());
        std::reverse(cycle.begin(), cycle.end());
        std::string message = "RenderGraph: dependency cycle: ";
        for (std::size_t i = 0; i < cycle.size(); ++i)
        {
            const std::uint32_t from = cycle[i];
            const std::uint32_t to = cycle[(i + 1) % cycle.size()];
            std::string via;
            for (const auto &[next, resource] : successors[from])
            {
                if (next == to)
                {
                    via = resource;
                }
            }
            message += "'" + passes_[from].name + "' -(" + via + ")-> ";
        }
        message += "'" + passes_[cycle.front()].name + "'";
        return makeError(ErrorCode::kInvalidArgument, message);
    }

    // Passes gardées : effets de bord et écrivains de ressources importées, puis tout ce qu'elles attendent.
    std::vector<std::uint32_t> stack;
    for (std::uint32_t p = 0; p < n; ++p)
    {
        bool root = passes_[p].sideEffect;
        for (const ResourceUse &u : passes_[p].uses)
        {
            const bool reading = u.access == Access::kRead || u.access == Access::kReadFinal;
            if (!reading && (u.texture ? textures_[u.resource].imported : buffers_[u.resource].imported))
            {
                root = true;
            }
        }
        if (root)
        {
            kept_[p] = true;
            stack.push_back(p);
        }
    }
    while (!stack.empty())
    {
        const std::uint32_t p = stack.back();
        stack.pop_back();
        for (const std::uint32_t before : dataPredecessors[p])
        {
            if (!kept_[before])
            {
                kept_[before] = true;
                stack.push_back(before);
            }
        }
    }
    for (const auto &[p, resource] : unproduced)
    {
        if (kept_[p])
        {
            kept_.assign(passes_.size(), false);
            return makeError(ErrorCode::kInvalidArgument,
                             "RenderGraph: pass '" + passes_[p].name + "' reads transient resource '" + resource + "' that no pass writes");
        }
    }
    for (const std::uint32_t p : sorted)
    {
        if (kept_[p])
        {
            order_.push_back(p);
        }
    }
    compiled_ = true;
    return {};
}

std::vector<std::string> RenderGraph::executionOrder() const
{
    std::vector<std::string> names;
    for (const std::uint32_t p : order_)
    {
        names.push_back(passes_[p].name);
    }
    return names;
}

std::vector<std::string> RenderGraph::culledPasses() const
{
    std::vector<std::string> names;
    if (!compiled_)
    {
        return names;
    }
    for (std::size_t p = 0; p < passes_.size(); ++p)
    {
        if (!kept_[p])
        {
            names.push_back(passes_[p].name);
        }
    }
    return names;
}

rhi::TextureHandle RenderGraph::physicalTexture(RgTexture texture) const
{
    return texture.index < textures_.size() ? textures_[texture.index].handle : rhi::TextureHandle{};
}

rhi::BufferHandle RenderGraph::physicalBuffer(RgBuffer buffer) const
{
    return buffer.index < buffers_.size() ? buffers_[buffer.index].handle : rhi::BufferHandle{};
}

// ── Ressources transitoires ─────────────────────────────────────────────────────────────────────────────────────────

core::Result<rhi::TextureHandle> RenderGraph::acquireTexture(const RgTextureDesc &desc, std::int64_t until)
{
    // Même description et libre à cette position : réutilisée (image précédente, ou ressource de vie disjointe dans l'image).
    for (PooledTexture &entry : texturePool_)
    {
        if (entry.desc == desc && entry.busyUntil < 0)
        {
            entry.busyUntil = until;
            entry.lastFrame = frameCounter_;
            return entry.handle;
        }
    }
    rhi::TextureDesc td;
    td.format = desc.format;
    td.size = {desc.size.width, desc.size.height, 1};
    td.mipLevels = desc.mipLevels;
    td.usage = desc.usage;
    td.debugName = "rendergraph.transient." + std::to_string(texturePool_.size());
    auto created = device_.createTexture(td);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    ++texturesCreated_;
    texturePool_.push_back({.desc = desc, .handle = *created, .lastFrame = frameCounter_, .busyUntil = until});
    return *created;
}

core::Result<rhi::BufferHandle> RenderGraph::acquireBuffer(const RgBufferDesc &desc, std::int64_t until)
{
    for (PooledBuffer &entry : bufferPool_)
    {
        if (entry.desc == desc && entry.busyUntil < 0)
        {
            entry.busyUntil = until;
            entry.lastFrame = frameCounter_;
            return entry.handle;
        }
    }
    rhi::BufferDesc bd;
    bd.size = desc.size;
    bd.usage = desc.usage;
    bd.debugName = "rendergraph.transient." + std::to_string(bufferPool_.size());
    auto created = device_.createBuffer(bd);
    if (!created)
    {
        return std::unexpected(created.error());
    }
    ++buffersCreated_;
    bufferPool_.push_back({.desc = desc, .handle = *created, .lastFrame = frameCounter_, .busyUntil = until});
    return *created;
}

core::Result<> RenderGraph::allocateTransients()
{
    ++frameCounter_;
    for (PooledTexture &entry : texturePool_)
    {
        entry.busyUntil = -1;
    }
    for (PooledBuffer &entry : bufferPool_)
    {
        entry.busyUntil = -1;
    }
    // Vie de chaque transitoire : [première, dernière] position dans l'ordre d'exécution ; usages déduits des accès.
    struct Life
    {
        std::int64_t first = -1;
        std::int64_t last = -1;
        rhi::TextureUsage usage = rhi::TextureUsage::kNone;
    };
    std::vector<Life> textureLife(textures_.size());
    std::vector<Life> bufferLife(buffers_.size());
    for (std::size_t pos = 0; pos < order_.size(); ++pos)
    {
        for (const ResourceUse &u : passes_[order_[pos]].uses)
        {
            Life &life = u.texture ? textureLife[u.resource] : bufferLife[u.resource];
            if (life.first < 0)
            {
                life.first = static_cast<std::int64_t>(pos);
            }
            life.last = static_cast<std::int64_t>(pos);
            if (u.texture)
            {
                life.usage = life.usage | (u.access == Access::kRead || u.access == Access::kReadFinal ? rhi::TextureUsage::kSampled
                                                                      : (u.access == Access::kWrite ? rhi::TextureUsage::kNone : rhi::TextureUsage::kRenderTarget));
            }
        }
    }
    for (std::size_t t = 0; t < textures_.size(); ++t)
    {
        if (!textures_[t].imported)
        {
            textures_[t].handle = {};
            textures_[t].desc.usage = textures_[t].desc.usage | textureLife[t].usage;
        }
    }
    for (BufferResource &b : buffers_)
    {
        if (!b.imported)
        {
            b.handle = {};
        }
    }
    // Attribution dans l'ordre d'exécution : une entrée libérée après la dernière utilisation d'une ressource sert à la suivante.
    for (std::size_t pos = 0; pos < order_.size(); ++pos)
    {
        const auto position = static_cast<std::int64_t>(pos);
        for (PooledTexture &entry : texturePool_)
        {
            if (entry.busyUntil >= 0 && entry.busyUntil < position)
            {
                entry.busyUntil = -1;
            }
        }
        for (PooledBuffer &entry : bufferPool_)
        {
            if (entry.busyUntil >= 0 && entry.busyUntil < position)
            {
                entry.busyUntil = -1;
            }
        }
        for (std::size_t t = 0; t < textures_.size(); ++t)
        {
            if (!textures_[t].imported && textureLife[t].first == position)
            {
                auto handle = acquireTexture(textures_[t].desc, textureLife[t].last);
                if (!handle)
                {
                    return makeError(handle.error().code, "RenderGraph: transient texture '" + textures_[t].name + "': " + handle.error().message);
                }
                textures_[t].handle = *handle;
            }
        }
        for (std::size_t b = 0; b < buffers_.size(); ++b)
        {
            if (!buffers_[b].imported && bufferLife[b].first == position)
            {
                auto handle = acquireBuffer(buffers_[b].desc, bufferLife[b].last);
                if (!handle)
                {
                    return makeError(handle.error().code, "RenderGraph: transient buffer '" + buffers_[b].name + "': " + handle.error().message);
                }
                buffers_[b].handle = *handle;
            }
        }
    }
    trimPool();
    return {};
}

void RenderGraph::trimPool()
{
    const auto stale = [&](std::uint64_t lastFrame) { return frameCounter_ - lastFrame > kTransientKeepFrames; };
    std::erase_if(texturePool_,
                  [&](const PooledTexture &entry)
                  {
                      if (!stale(entry.lastFrame))
                      {
                          return false;
                      }
                      device_.destroy(entry.handle);
                      return true;
                  });
    std::erase_if(bufferPool_,
                  [&](const PooledBuffer &entry)
                  {
                      if (!stale(entry.lastFrame))
                      {
                          return false;
                      }
                      device_.destroy(entry.handle);
                      return true;
                  });
}

void RenderGraph::releaseTransients()
{
    for (const PooledTexture &entry : texturePool_)
    {
        device_.destroy(entry.handle);
    }
    for (const PooledBuffer &entry : bufferPool_)
    {
        device_.destroy(entry.handle);
    }
    texturePool_.clear();
    bufferPool_.clear();
    for (TextureResource &t : textures_)
    {
        if (!t.imported)
        {
            t.handle = {};
        }
    }
    for (BufferResource &b : buffers_)
    {
        if (!b.imported)
        {
            b.handle = {};
        }
    }
}

// ── Exécution ───────────────────────────────────────────────────────────────────────────────────────────────────────

core::Result<> RenderGraph::runPass(rhi::CommandEncoder &encoder, const Pass &pass)
{
    RenderPassContext context(device_, encoder, *this, pass.name, frame_);
    const bool encoderFailed = encoder.error().has_value(); // erreur antérieure : pas imputée à cette passe
    encoder.beginDebugGroup(pass.name.c_str());
    core::Result<> result;
    if (pass.type == PassType::kRaster)
    {
        rhi::RenderPassDesc desc;
        desc.debugName = pass.name;
        for (const ResourceUse &u : pass.uses)
        {
            if (u.access == Access::kColor)
            {
                rhi::ColorAttachment a;
                a.texture = textures_[u.resource].handle;
                a.load = u.load;
                a.clearColor = u.clearColor;
                desc.colors.push_back(a);
            }
            else if (u.access == Access::kDepth)
            {
                rhi::DepthAttachment d;
                d.texture = textures_[u.resource].handle;
                d.load = u.load;
                d.clearDepth = u.clearDepth;
                desc.depth = d;
            }
        }
        encoder.beginRenderPass(desc);
        result = pass.execute(context);
        encoder.endRenderPass(); // toujours refermée, même si la passe a échoué
    }
    else
    {
        result = pass.execute(context);
    }
    encoder.endDebugGroup();
    if (!result)
    {
        // L'erreur rendue par la passe elle-même prime sur celle de l'encodeur.
        return makeError(result.error().code, "pass '" + pass.name + "': " + result.error().message, result.error().where);
    }
    if (!encoderFailed && encoder.error())
    {
        // Commande refusée par l'encodeur pendant cette passe (ignorée par le RHI) : l'image est arrêtée comme pour une erreur.
        const core::Error &error = *encoder.error();
        return makeError(error.code, "pass '" + pass.name + "': rejected command: " + error.message, error.where);
    }
    return {};
}

core::Result<> RenderGraph::execute(rhi::CommandEncoder &encoder)
{
    if (!compiled_)
    {
        if (auto r = compile(); !r)
        {
            return r;
        }
    }
    if (auto r = allocateTransients(); !r)
    {
        return r;
    }
    for (const std::uint32_t p : order_)
    {
        const Pass &pass = passes_[p];
        // Une mesure par passe : minuterie GPU si le Profiler a un fournisseur (horodatages hors de toute passe de rendu), sinon CPU.
        auto result = core::Profiler::ProfileGPU(profilePrefix_ + pass.name, [&]() { return runPass(encoder, pass); });
        if (!result)
        {
            return result;
        }
    }
    return {};
}

} // namespace orosmo::renderer
