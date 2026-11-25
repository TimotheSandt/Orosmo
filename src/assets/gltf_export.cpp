// Export glTF 2.0 par cgltf_write (ASSET-01) : un `Model` est traduit en `cgltf_data` dont tous les tableaux sont réservés à leur
// taille exacte avant d'être remplis (les objets de cgltf se désignent par pointeur : aucune réallocation n'est permise), puis
// écrit en .glb (un tampon binaire inclus) ou en .gltf (+ .bin).
#include "orosmo/assets/model.hpp"

#include <cgltf_write.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

namespace
{

// cgltf insère les chaînes telles quelles dans le JSON : guillemets, barres obliques inverses et caractères de contrôle sont échappés ici.
std::string jsonEscaped(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (c < 0x20)
                {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 15];
                }
                else
                {
                    out += ch;
                }
        }
    }
    return out;
}

// URI glTF : tout octet hors des caractères non réservés (et de '/') est encodé en %XX, '%' littéral compris, pour que l'import (qui décode
// les %XX) retrouve le nom natif.
std::string uriEncoded(const std::string &s)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
    {
        const auto c = static_cast<unsigned char>(ch);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                           c == '~' || c == '/';
        if (plain)
        {
            out += ch;
        }
        else
        {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

bool writeFile(const std::filesystem::path &path, std::span<const std::uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    out.close(); // la fermeture vide le tampon : son échec (disque plein) doit être vu
    return !out.fail();
}

void putU32(std::vector<std::uint8_t> &v, std::uint32_t x)
{
    for (int i = 0; i < 4; ++i)
    {
        v.push_back(static_cast<std::uint8_t>((x >> (8 * i)) & 0xFFu));
    }
}

// Conteneur GLB 2.0 : en-tête, bloc JSON (rempli d'espaces à 4 octets), bloc binaire (rempli de zéros) s'il y en a un.
// Les longueurs GLB tiennent sur 32 bits : au-delà, std::nullopt (le contrôle précède toute allocation).
std::optional<std::vector<std::uint8_t>> glbContainer(const std::string &json, const std::vector<std::uint8_t> &bin)
{
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint32_t>::max();
    if (json.size() > kMax || bin.size() > kMax)
    {
        return std::nullopt;
    }
    const std::uint64_t jsonPadded = (json.size() + 3) / 4 * 4;
    const std::uint64_t binPadded = (bin.size() + 3) / 4 * 4;
    const std::uint64_t total = 12 + 8 + jsonPadded + (bin.empty() ? 0 : 8 + binPadded);
    if (jsonPadded > kMax || binPadded > kMax || total > kMax)
    {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(total);
    putU32(out, 0x46546C67u); // "glTF"
    putU32(out, 2);
    putU32(out, static_cast<std::uint32_t>(total));
    putU32(out, static_cast<std::uint32_t>(jsonPadded));
    putU32(out, 0x4E4F534Au); // "JSON"
    out.insert(out.end(), json.begin(), json.end());
    out.resize(20 + jsonPadded, static_cast<std::uint8_t>(' '));
    if (!bin.empty())
    {
        putU32(out, static_cast<std::uint32_t>(binPadded));
        putU32(out, 0x004E4942u); // "BIN\0"
        out.insert(out.end(), bin.begin(), bin.end());
        out.resize(out.size() + (binPadded - bin.size()), 0);
    }
    return out;
}

char *lit(const char *s) { return const_cast<char *>(s); }

cgltf_wrap_mode wrapOf(WrapMode mode) noexcept
{
    switch (mode)
    {
        case WrapMode::kClampToEdge: return cgltf_wrap_mode_clamp_to_edge;
        case WrapMode::kMirroredRepeat: return cgltf_wrap_mode_mirrored_repeat;
        default: return cgltf_wrap_mode_repeat;
    }
}

class Writer
{
  public:
    explicit Writer(const Model &model) : model_(model) {}

    core::Result<> write(const std::filesystem::path &path)
    {
        const std::string ext = path.extension().string();
        const bool glb = ext == ".glb" || ext == ".GLB";
        if (!glb && ext != ".gltf" && ext != ".GLTF")
        {
            return makeError(ErrorCode::kInvalidArgument, "saveGltf: extension must be .gltf or .glb: " + path.generic_string());
        }
        if (auto valid = model_.validate(); !valid)
        {
            return valid;
        }
        for (const auto &mesh : model_.meshes)
        {
            if (mesh.primitives.empty())
            {
                return makeError(ErrorCode::kInvalidArgument, "saveGltf: mesh \"" + mesh.name + "\" has no primitive (glTF requires at least one)");
            }
        }
        reserve();
        const std::filesystem::path dir = path.parent_path();
        build(dir);

        const std::string binName = path.stem().string() + ".bin";
        buffer_.size = bin_.size();
        buffer_.uri = glb || bin_.empty() ? nullptr : uri(binName);
        data_.buffers = bin_.empty() ? nullptr : &buffer_;
        data_.buffers_count = bin_.empty() ? 0 : 1;
        if (glb)
        {
            data_.bin = bin_.data();
            data_.bin_size = bin_.size();
        }
        // JSON sérialisé en mémoire (cgltf_write_file ignore les erreurs de fwrite/fclose) puis écrit avec contrôle.
        cgltf_options options{};
        options.type = glb ? cgltf_file_type_glb : cgltf_file_type_gltf;
        const cgltf_size expected = cgltf_write(&options, nullptr, 0, &data_);
        std::string json(expected, '\0');
        if (expected == 0 || cgltf_write(&options, json.data(), expected, &data_) != expected)
        {
            return makeError(ErrorCode::kIo, "saveGltf: cannot serialize " + path.generic_string());
        }
        json.pop_back(); // terminateur nul de cgltf_write

        const auto fail = [&](const std::filesystem::path &what) { return makeError(ErrorCode::kIo, "saveGltf: cannot write " + what.generic_string()); };
        // Noms provisoires et de sauvegarde propres à cette opération : on ne choisit jamais un fichier existant (une sauvegarde de l'utilisateur
        // comme `scene.bin.bak` n'est ni remplacée ni supprimée).
        // `file_type::none` : statut indéterminable (dossier non parcourable…) : erreur kIo, jamais une boucle sans fin ; un nombre borné
        // d'essais protège aussi contre un dossier rempli de noms `.orosmo-*`.
        const auto sibling = [&](const std::filesystem::path &base, const char *tag) -> core::Result<std::filesystem::path>
        {
            constexpr int kMaxAttempts = 1000;
            for (int n = 0; n < kMaxAttempts; ++n)
            {
                std::filesystem::path p = base;
                p += std::string(".orosmo-") + tag + std::to_string(n);
                std::error_code ignored; // « absent » renseigne un code d'erreur ENOENT : seul le type compte
                const std::filesystem::file_type type = std::filesystem::symlink_status(p, ignored).type();
                if (type == std::filesystem::file_type::not_found)
                {
                    return p;
                }
                if (type == std::filesystem::file_type::none)
                {
                    break;
                }
            }
            return fail(base);
        };
        std::error_code ec;
        const auto jsonTmpResult = sibling(path, "tmp");
        if (!jsonTmpResult)
        {
            return std::unexpected(jsonTmpResult.error());
        }
        const std::filesystem::path &jsonTmp = *jsonTmpResult;
        if (glb)
        {
            const auto container = glbContainer(json, bin_);
            if (!container)
            {
                return makeError(ErrorCode::kOutOfRange, "saveGltf: " + path.generic_string() + " exceeds the 4 GiB limit of a .glb container");
            }
            if (!writeFile(jsonTmp, *container))
            {
                std::filesystem::remove(jsonTmp, ec);
                return fail(path);
            }
            std::filesystem::rename(jsonTmp, path, ec);
            if (ec)
            {
                std::filesystem::remove(jsonTmp, ec);
                return fail(path);
            }
            return {};
        }

        // .gltf + .bin : les deux fichiers sont d'abord écrits en entier sous un nom provisoire ; l'ancien .bin est mis de côté pendant la
        // publication et remis en place si celle du JSON échoue, de sorte que l'ancienne paire reste cohérente.
        const std::filesystem::path binPath = dir / binName;
        const auto binTmpResult = sibling(binPath, "tmp");
        const auto binBakResult = sibling(binPath, "bak");
        if (!binTmpResult || !binBakResult)
        {
            return std::unexpected(!binTmpResult ? binTmpResult.error() : binBakResult.error());
        }
        const std::filesystem::path &binTmp = *binTmpResult;
        const std::filesystem::path &binBak = *binBakResult;
        const bool hasBin = !bin_.empty();
        const auto cleanup = [&]
        {
            std::error_code ignored;
            std::filesystem::remove(jsonTmp, ignored);
            std::filesystem::remove(binTmp, ignored);
        };
        const std::span<const std::uint8_t> jsonBytes(reinterpret_cast<const std::uint8_t *>(json.data()), json.size());
        if (!writeFile(jsonTmp, jsonBytes))
        {
            cleanup();
            return fail(path);
        }
        if (hasBin && !writeFile(binTmp, bin_))
        {
            cleanup();
            return fail(binPath);
        }
        bool hadBin = false;
        if (hasBin)
        {
            hadBin = std::filesystem::exists(binPath, ec);
            if (hadBin)
            {
                std::filesystem::rename(binPath, binBak, ec);
                if (ec)
                {
                    cleanup();
                    return fail(binPath);
                }
            }
            std::filesystem::rename(binTmp, binPath, ec);
            if (ec)
            {
                if (hadBin)
                {
                    std::filesystem::rename(binBak, binPath, ec);
                }
                cleanup();
                return fail(binPath);
            }
        }
        std::filesystem::rename(jsonTmp, path, ec);
        if (ec)
        {
            std::error_code ignored;
            if (hasBin)
            {
                std::filesystem::remove(binPath, ignored);
                if (hadBin)
                {
                    std::filesystem::rename(binBak, binPath, ignored);
                }
            }
            cleanup();
            return fail(path);
        }
        if (hadBin)
        {
            std::filesystem::remove(binBak, ec);
        }
        return {};
    }

  private:
    char *str(const std::string &s)
    {
        if (s.empty())
        {
            return nullptr;
        }
        strings_.push_back(jsonEscaped(s));
        return strings_.back().data();
    }
    char *uri(const std::string &s)
    {
        strings_.push_back(jsonEscaped(uriEncoded(s)));
        return strings_.back().data();
    }

    // Nombre exact d'accesseurs et de vues : chaque accesseur a sa vue ; chaque image incluse a sa vue.
    void reserve()
    {
        std::size_t accessors = 0;
        std::size_t primitives = 0;
        for (const auto &mesh : model_.meshes)
        {
            for (const auto &p : mesh.primitives)
            {
                ++primitives;
                accessors += 2; // POSITION, indices
                accessors += !p.normals.empty() + !p.tangents.empty() + !p.uv0.empty() + !p.uv1.empty() + !p.colors.empty() +
                              !p.joints.empty() + !p.weights.empty();
            }
        }
        for (const auto &skin : model_.skins)
        {
            accessors += skin.joints.empty() ? 0 : 1;
        }
        // Aucune cible de morphing n'est exportée (docs/FORMATS.md) : les pistes de poids sont omises, et les clips qui n'ont plus de piste aussi.
        keptAnimations_.clear();
        std::size_t channels = 0;
        for (const auto &anim : model_.animations)
        {
            const std::size_t kept = static_cast<std::size_t>(std::count_if(anim.channels.begin(), anim.channels.end(), exported));
            if (kept > 0)
            {
                keptAnimations_.push_back(&anim);
                channels += kept;
            }
        }
        accessors += channels * 2;
        std::size_t views = accessors;
        for (const auto &img : model_.images)
        {
            views += img.encoded.empty() ? 0 : 1;
        }
        accessors_.reserve(accessors);
        views_.reserve(views);
        primitives_.reserve(primitives);
        attributes_.reserve(primitives);
        animSamplers_.reserve(keptAnimations_.size());
        animChannels_.reserve(keptAnimations_.size());
        sceneRoots_.reserve(model_.scenes.size());
        skinJoints_.reserve(model_.skins.size());
        nodeChildren_.reserve(model_.nodes.size());
    }

    cgltf_buffer_view *addView(const void *bytes, std::size_t size, cgltf_buffer_view_type type)
    {
        while (bin_.size() % 4 != 0)
        {
            bin_.push_back(0);
        }
        cgltf_buffer_view view{};
        view.buffer = &buffer_;
        view.offset = bin_.size();
        view.size = size;
        view.type = type;
        const auto *p = static_cast<const std::uint8_t *>(bytes);
        bin_.insert(bin_.end(), p, p + size);
        views_.push_back(view);
        return &views_.back();
    }

    cgltf_accessor *addAccessor(const void *bytes, std::size_t count, cgltf_type type, cgltf_component_type component,
                                cgltf_buffer_view_type target, bool minMax = false)
    {
        const std::size_t comps = cgltf_num_components(type);
        const std::size_t compSize = cgltf_component_size(component);
        cgltf_accessor acc{};
        acc.component_type = component;
        acc.type = type;
        acc.count = count;
        acc.stride = comps * compSize;
        acc.buffer_view = addView(bytes, count * comps * compSize, target);
        if (minMax && component == cgltf_component_type_r_32f && count > 0)
        {
            const auto *f = static_cast<const float *>(bytes);
            acc.has_min = true;
            acc.has_max = true;
            for (std::size_t c = 0; c < comps; ++c)
            {
                acc.min[c] = std::numeric_limits<float>::max();
                acc.max[c] = std::numeric_limits<float>::lowest();
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                for (std::size_t c = 0; c < comps; ++c)
                {
                    acc.min[c] = std::min(acc.min[c], f[i * comps + c]);
                    acc.max[c] = std::max(acc.max[c], f[i * comps + c]);
                }
            }
        }
        accessors_.push_back(acc);
        return &accessors_.back();
    }

    template <typename T> void attribute(std::vector<cgltf_attribute> &out, const char *name, cgltf_attribute_type type, int index,
                                         const std::vector<T> &values, cgltf_type accType, cgltf_component_type comp, bool minMax = false)
    {
        if (values.empty())
        {
            return;
        }
        cgltf_attribute attr{};
        attr.name = lit(name);
        attr.type = type;
        attr.index = index;
        attr.data = addAccessor(values.data(), values.size(), accType, comp, cgltf_buffer_view_type_vertices, minMax);
        out.push_back(attr);
    }

    cgltf_texture_view textureView(const TextureRef &ref)
    {
        cgltf_texture_view view{};
        view.texture = ref.valid() ? &textures_[static_cast<std::size_t>(ref.texture)] : nullptr;
        view.texcoord = ref.texCoord;
        view.scale = ref.scale;
        return view;
    }

    void build(const std::filesystem::path &dir)
    {
        data_.asset.version = lit("2.0");
        data_.asset.generator = lit("Orosmo");

        samplers_.resize(model_.samplers.size());
        for (std::size_t i = 0; i < samplers_.size(); ++i)
        {
            const SamplerData &s = model_.samplers[i];
            samplers_[i].wrap_s = wrapOf(s.wrapS);
            samplers_[i].wrap_t = wrapOf(s.wrapT);
            samplers_[i].mag_filter = s.nearest ? cgltf_filter_type_nearest : cgltf_filter_type_undefined;
            samplers_[i].min_filter = s.nearest ? cgltf_filter_type_nearest : cgltf_filter_type_undefined;
        }
        images_.resize(model_.images.size());
        for (std::size_t i = 0; i < images_.size(); ++i)
        {
            const ImageData &img = model_.images[i];
            images_[i].name = str(img.name);
            if (!img.encoded.empty())
            {
                images_[i].buffer_view = addView(img.encoded.data(), img.encoded.size(), cgltf_buffer_view_type_invalid);
                images_[i].mime_type = str(img.mimeType.empty() ? kPng : img.mimeType);
            }
            else if (!img.path.empty())
            {
                // Un chemin absolu est exprimé relativement au dossier (absolu) du fichier écrit ; un chemin relatif l'est déjà.
                std::filesystem::path rel = img.path;
                if (img.path.is_absolute())
                {
                    std::error_code ec;
                    const std::filesystem::path base = std::filesystem::absolute(dir.empty() ? std::filesystem::path(".") : dir, ec).lexically_normal();
                    const std::filesystem::path r = ec ? std::filesystem::path() : img.path.lexically_relative(base);
                    rel = r.empty() ? img.path : r;
                }
                images_[i].uri = uri(rel.generic_string());
            }
        }
        textures_.resize(model_.textures.size());
        for (std::size_t i = 0; i < textures_.size(); ++i)
        {
            const TextureData &t = model_.textures[i];
            textures_[i].image = t.image >= 0 ? &images_[static_cast<std::size_t>(t.image)] : nullptr;
            textures_[i].sampler = t.sampler >= 0 ? &samplers_[static_cast<std::size_t>(t.sampler)] : nullptr;
        }
        materials_.resize(model_.materials.size());
        for (std::size_t i = 0; i < materials_.size(); ++i)
        {
            const MaterialData &m = model_.materials[i];
            cgltf_material &out = materials_[i];
            out.name = str(m.name);
            out.has_pbr_metallic_roughness = true;
            std::memcpy(out.pbr_metallic_roughness.base_color_factor, &m.baseColorFactor[0], sizeof(float) * 4);
            out.pbr_metallic_roughness.base_color_texture = textureView(m.baseColor);
            out.pbr_metallic_roughness.metallic_factor = m.metallicFactor;
            out.pbr_metallic_roughness.roughness_factor = m.roughnessFactor;
            out.pbr_metallic_roughness.metallic_roughness_texture = textureView(m.metallicRoughness);
            out.normal_texture = textureView(m.normal);
            out.occlusion_texture = textureView(m.occlusion);
            out.emissive_texture = textureView(m.emissive);
            std::memcpy(out.emissive_factor, &m.emissiveFactor[0], sizeof(float) * 3);
            if (m.emissiveStrength != 1.0f)
            {
                out.has_emissive_strength = true;
                out.emissive_strength.emissive_strength = m.emissiveStrength;
            }
            out.alpha_mode = m.alphaMode == AlphaMode::kMask    ? cgltf_alpha_mode_mask
                             : m.alphaMode == AlphaMode::kBlend ? cgltf_alpha_mode_blend
                                                                : cgltf_alpha_mode_opaque;
            out.alpha_cutoff = m.alphaCutoff;
            out.double_sided = m.doubleSided;
            out.unlit = m.unlit;
        }

        meshes_.resize(model_.meshes.size());
        for (std::size_t i = 0; i < meshes_.size(); ++i)
        {
            const MeshData &mesh = model_.meshes[i];
            const std::size_t first = primitives_.size();
            for (const MeshPrimitive &p : mesh.primitives)
            {
                attributes_.emplace_back();
                auto &attrs = attributes_.back();
                attrs.reserve(9);
                attribute(attrs, "POSITION", cgltf_attribute_type_position, 0, p.positions, cgltf_type_vec3, cgltf_component_type_r_32f, true);
                attribute(attrs, "NORMAL", cgltf_attribute_type_normal, 0, p.normals, cgltf_type_vec3, cgltf_component_type_r_32f);
                attribute(attrs, "TANGENT", cgltf_attribute_type_tangent, 0, p.tangents, cgltf_type_vec4, cgltf_component_type_r_32f);
                attribute(attrs, "TEXCOORD_0", cgltf_attribute_type_texcoord, 0, p.uv0, cgltf_type_vec2, cgltf_component_type_r_32f);
                attribute(attrs, "TEXCOORD_1", cgltf_attribute_type_texcoord, 1, p.uv1, cgltf_type_vec2, cgltf_component_type_r_32f);
                attribute(attrs, "COLOR_0", cgltf_attribute_type_color, 0, p.colors, cgltf_type_vec4, cgltf_component_type_r_32f);
                attribute(attrs, "JOINTS_0", cgltf_attribute_type_joints, 0, p.joints, cgltf_type_vec4, cgltf_component_type_r_16u);
                attribute(attrs, "WEIGHTS_0", cgltf_attribute_type_weights, 0, p.weights, cgltf_type_vec4, cgltf_component_type_r_32f);

                cgltf_primitive prim{};
                prim.type = cgltf_primitive_type_triangles;
                prim.attributes = attrs.data();
                prim.attributes_count = attrs.size();
                prim.material = p.material >= 0 ? &materials_[static_cast<std::size_t>(p.material)] : nullptr;
                const bool small = std::all_of(p.indices.begin(), p.indices.end(), [](std::uint32_t v) { return v < 0xFFFFu; });
                if (small)
                {
                    indices16_.emplace_back(p.indices.begin(), p.indices.end());
                    prim.indices = addAccessor(indices16_.back().data(), p.indices.size(), cgltf_type_scalar, cgltf_component_type_r_16u,
                                               cgltf_buffer_view_type_indices);
                }
                else
                {
                    prim.indices = addAccessor(p.indices.data(), p.indices.size(), cgltf_type_scalar, cgltf_component_type_r_32u,
                                               cgltf_buffer_view_type_indices);
                }
                primitives_.push_back(prim);
            }
            meshes_[i].name = str(mesh.name);
            meshes_[i].primitives = mesh.primitives.empty() ? nullptr : &primitives_[first];
            meshes_[i].primitives_count = mesh.primitives.size();
        }

        cameras_.resize(model_.cameras.size());
        for (std::size_t i = 0; i < cameras_.size(); ++i)
        {
            const CameraData &c = model_.cameras[i];
            cgltf_camera &out = cameras_[i];
            out.name = str(c.name);
            if (c.perspective)
            {
                out.type = cgltf_camera_type_perspective;
                out.data.perspective.yfov = c.yfov;
                out.data.perspective.has_aspect_ratio = c.aspectRatio > 0.0f;
                out.data.perspective.aspect_ratio = c.aspectRatio;
                out.data.perspective.znear = c.znear;
                out.data.perspective.has_zfar = c.zfar > 0.0f;
                out.data.perspective.zfar = c.zfar;
            }
            else
            {
                out.type = cgltf_camera_type_orthographic;
                out.data.orthographic.xmag = c.xmag;
                out.data.orthographic.ymag = c.ymag;
                out.data.orthographic.znear = c.znear;
                out.data.orthographic.zfar = c.zfar;
            }
        }
        lights_.resize(model_.lights.size());
        for (std::size_t i = 0; i < lights_.size(); ++i)
        {
            const LightData &l = model_.lights[i];
            cgltf_light &out = lights_[i];
            out.name = str(l.name);
            out.type = l.kind == LightKind::kDirectional ? cgltf_light_type_directional
                       : l.kind == LightKind::kSpot      ? cgltf_light_type_spot
                                                         : cgltf_light_type_point;
            std::memcpy(out.color, &l.color[0], sizeof(float) * 3);
            out.intensity = l.intensity;
            out.range = l.range;
            out.spot_inner_cone_angle = l.innerConeAngle;
            out.spot_outer_cone_angle = l.outerConeAngle;
        }

        skins_.resize(model_.skins.size());
        nodes_.resize(model_.nodes.size());
        for (std::size_t i = 0; i < nodes_.size(); ++i)
        {
            const NodeData &n = model_.nodes[i];
            cgltf_node &out = nodes_[i];
            out.name = str(n.name);
            out.parent = n.parent >= 0 ? &nodes_[static_cast<std::size_t>(n.parent)] : nullptr;
            nodeChildren_.emplace_back();
            for (int c : n.children)
            {
                nodeChildren_.back().push_back(&nodes_[static_cast<std::size_t>(c)]);
            }
            out.children = nodeChildren_.back().empty() ? nullptr : nodeChildren_.back().data();
            out.children_count = nodeChildren_.back().size();
            out.has_translation = true;
            out.has_rotation = true;
            out.has_scale = true;
            std::memcpy(out.translation, &n.translation[0], sizeof(float) * 3);
            out.rotation[0] = n.rotation.x;
            out.rotation[1] = n.rotation.y;
            out.rotation[2] = n.rotation.z;
            out.rotation[3] = n.rotation.w;
            std::memcpy(out.scale, &n.scale[0], sizeof(float) * 3);
            out.mesh = n.mesh >= 0 ? &meshes_[static_cast<std::size_t>(n.mesh)] : nullptr;
            out.skin = n.skin >= 0 ? &skins_[static_cast<std::size_t>(n.skin)] : nullptr;
            out.camera = n.camera >= 0 ? &cameras_[static_cast<std::size_t>(n.camera)] : nullptr;
            out.light = n.light >= 0 ? &lights_[static_cast<std::size_t>(n.light)] : nullptr;
        }
        for (std::size_t i = 0; i < skins_.size(); ++i)
        {
            const SkinData &s = model_.skins[i];
            cgltf_skin &out = skins_[i];
            out.name = str(s.name);
            skinJoints_.emplace_back();
            for (int j : s.joints)
            {
                skinJoints_.back().push_back(&nodes_[static_cast<std::size_t>(j)]);
            }
            out.joints = skinJoints_.back().empty() ? nullptr : skinJoints_.back().data();
            out.joints_count = skinJoints_.back().size();
            out.skeleton = s.skeleton >= 0 ? &nodes_[static_cast<std::size_t>(s.skeleton)] : nullptr;
            if (!s.joints.empty())
            {
                out.inverse_bind_matrices = addAccessor(s.inverseBindMatrices.data(), s.inverseBindMatrices.size(), cgltf_type_mat4,
                                                        cgltf_component_type_r_32f, cgltf_buffer_view_type_invalid);
            }
        }

        animations_.resize(keptAnimations_.size());
        for (std::size_t i = 0; i < animations_.size(); ++i)
        {
            const AnimationData &a = *keptAnimations_[i];
            animSamplers_.emplace_back();
            animChannels_.emplace_back();
            auto &samplers = animSamplers_.back();
            auto &channels = animChannels_.back();
            samplers.reserve(a.channels.size());
            channels.reserve(a.channels.size());
            for (const AnimationChannel &ch : a.channels)
            {
                if (!exported(ch))
                {
                    continue;
                }
                cgltf_animation_sampler s{};
                s.input = addAccessor(ch.times.data(), ch.times.size(), cgltf_type_scalar, cgltf_component_type_r_32f,
                                      cgltf_buffer_view_type_invalid, true);
                const cgltf_type valueType = ch.componentCount == 4 ? cgltf_type_vec4 : ch.componentCount == 3 ? cgltf_type_vec3 : cgltf_type_scalar;
                const std::size_t valueCount = ch.values.size() / cgltf_num_components(valueType);
                s.output = addAccessor(ch.values.data(), valueCount, valueType, cgltf_component_type_r_32f, cgltf_buffer_view_type_invalid);
                s.interpolation = ch.interpolation == Interpolation::kStep          ? cgltf_interpolation_type_step
                                  : ch.interpolation == Interpolation::kCubicSpline ? cgltf_interpolation_type_cubic_spline
                                                                                     : cgltf_interpolation_type_linear;
                samplers.push_back(s);
                cgltf_animation_channel c{};
                c.sampler = &samplers.back();
                c.target_node = &nodes_[static_cast<std::size_t>(ch.node)];
                c.target_path = ch.path == AnimationPath::kRotation  ? cgltf_animation_path_type_rotation
                                : ch.path == AnimationPath::kScale   ? cgltf_animation_path_type_scale
                                : ch.path == AnimationPath::kWeights ? cgltf_animation_path_type_weights
                                                                     : cgltf_animation_path_type_translation;
                channels.push_back(c);
            }
            animations_[i].name = str(a.name);
            animations_[i].samplers = samplers.empty() ? nullptr : samplers.data();
            animations_[i].samplers_count = samplers.size();
            animations_[i].channels = channels.empty() ? nullptr : channels.data();
            animations_[i].channels_count = channels.size();
        }

        scenes_.resize(model_.scenes.size());
        for (std::size_t i = 0; i < scenes_.size(); ++i)
        {
            sceneRoots_.emplace_back();
            for (int r : model_.scenes[i].roots)
            {
                sceneRoots_.back().push_back(&nodes_[static_cast<std::size_t>(r)]);
            }
            scenes_[i].name = str(model_.scenes[i].name);
            scenes_[i].nodes = sceneRoots_.back().empty() ? nullptr : sceneRoots_.back().data();
            scenes_[i].nodes_count = sceneRoots_.back().size();
        }

        auto set = [](auto &vec, auto *&ptr, cgltf_size &count)
        {
            ptr = vec.empty() ? nullptr : vec.data();
            count = vec.size();
        };
        set(meshes_, data_.meshes, data_.meshes_count);
        set(materials_, data_.materials, data_.materials_count);
        set(accessors_, data_.accessors, data_.accessors_count);
        set(views_, data_.buffer_views, data_.buffer_views_count);
        set(images_, data_.images, data_.images_count);
        set(textures_, data_.textures, data_.textures_count);
        set(samplers_, data_.samplers, data_.samplers_count);
        set(skins_, data_.skins, data_.skins_count);
        set(cameras_, data_.cameras, data_.cameras_count);
        set(lights_, data_.lights, data_.lights_count);
        set(nodes_, data_.nodes, data_.nodes_count);
        set(scenes_, data_.scenes, data_.scenes_count);
        set(animations_, data_.animations, data_.animations_count);
        data_.scene = model_.defaultScene >= 0 ? &scenes_[static_cast<std::size_t>(model_.defaultScene)] : nullptr;
    }

    static bool exported(const AnimationChannel &ch) noexcept { return ch.path != AnimationPath::kWeights; }

    inline static const std::string kPng = "image/png";

    const Model &model_;
    cgltf_data data_{};
    cgltf_buffer buffer_{};
    std::vector<std::uint8_t> bin_;
    std::vector<cgltf_accessor> accessors_;
    std::vector<cgltf_buffer_view> views_;
    std::vector<cgltf_image> images_;
    std::vector<cgltf_sampler> samplers_;
    std::vector<cgltf_texture> textures_;
    std::vector<cgltf_material> materials_;
    std::vector<cgltf_primitive> primitives_;
    std::vector<std::vector<cgltf_attribute>> attributes_;
    std::vector<std::vector<std::uint16_t>> indices16_;
    std::vector<cgltf_mesh> meshes_;
    std::vector<cgltf_camera> cameras_;
    std::vector<cgltf_light> lights_;
    std::vector<cgltf_node> nodes_;
    std::vector<std::vector<cgltf_node *>> nodeChildren_;
    std::vector<cgltf_skin> skins_;
    std::vector<std::vector<cgltf_node *>> skinJoints_;
    std::vector<const AnimationData *> keptAnimations_;
    std::vector<cgltf_animation> animations_;
    std::vector<std::vector<cgltf_animation_sampler>> animSamplers_;
    std::vector<std::vector<cgltf_animation_channel>> animChannels_;
    std::vector<cgltf_scene> scenes_;
    std::vector<std::vector<cgltf_node *>> sceneRoots_;
    std::deque<std::string> strings_; // chaînes échappées : adresses stables jusqu'à la fin de l'écriture
};

} // namespace

core::Result<> saveGltf(const Model &model, const std::filesystem::path &path)
{
    Writer writer(model);
    return writer.write(path);
}

} // namespace orosmo::assets
