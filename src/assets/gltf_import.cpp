// Import glTF 2.0 par cgltf (ASSET-01) : le fichier est lu, validé, puis copié dans les types CPU de `model.hpp`. Rien ne garde
// de pointeur vers les données de cgltf, libérées à la fin de l'import.
#include "orosmo/assets/model.hpp"
#include "orosmo/core/log.hpp"

#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>

namespace orosmo::assets
{

using core::ErrorCode;
using core::makeError;

namespace
{

struct CgltfDeleter
{
    void operator()(cgltf_data *data) const noexcept { cgltf_free(data); }
};
using CgltfPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

const char *resultName(cgltf_result r) noexcept
{
    switch (r)
    {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "glTF 1.0 is not supported";
        default: return "unknown error";
    }
}

// Extensions comprises par l'import. Une extension *requise* hors de cette liste refuse le fichier (le rendu serait faux).
constexpr std::array<std::string_view, 4> kSupportedExtensions{"KHR_lights_punctual", "KHR_materials_unlit",
                                                               "KHR_materials_emissive_strength", "KHR_mesh_quantization"};

std::string nameOf(const char *name) { return name != nullptr ? std::string(name) : std::string(); }

template <typename T> int indexOr(cgltf_size (*fn)(const cgltf_data *, const T *), const cgltf_data *data, const T *object)
{
    return object != nullptr ? static_cast<int>(fn(data, object)) : -1;
}

template <typename Vec> core::Result<std::vector<Vec>> unpack(const cgltf_accessor *accessor, std::size_t components, std::string_view what)
{
    std::vector<Vec> out;
    if (accessor == nullptr)
    {
        return out;
    }
    const cgltf_size n = cgltf_num_components(accessor->type);
    if (n != components)
    {
        return makeError(ErrorCode::kParse, "glTF: " + std::string(what) + " has " + std::to_string(n) + " components, expected " +
                                                std::to_string(components));
    }
    out.resize(accessor->count);
    const cgltf_size written = cgltf_accessor_unpack_floats(accessor, reinterpret_cast<float *>(out.data()), accessor->count * n);
    if (written != accessor->count * n)
    {
        return makeError(ErrorCode::kParse, "glTF: cannot read " + std::string(what));
    }
    return out;
}

WrapMode wrapOf(cgltf_wrap_mode mode) noexcept
{
    switch (mode)
    {
        case cgltf_wrap_mode_clamp_to_edge: return WrapMode::kClampToEdge;
        case cgltf_wrap_mode_mirrored_repeat: return WrapMode::kMirroredRepeat;
        default: return WrapMode::kRepeat;
    }
}

TextureRef textureRef(const cgltf_data *data, const cgltf_texture_view &view, float scale)
{
    TextureRef ref;
    ref.texture = indexOr(cgltf_texture_index, data, view.texture);
    ref.texCoord = view.texcoord;
    ref.scale = scale;
    return ref;
}

// Triangles d'une primitive : liste, bande ou éventail convertis en liste. Points et lignes : rien (l'appelant ignore la primitive).
std::vector<std::uint32_t> triangleIndices(const cgltf_primitive &prim, std::size_t vertexCount)
{
    std::vector<std::uint32_t> raw;
    if (prim.indices != nullptr)
    {
        raw.resize(prim.indices->count);
        cgltf_accessor_unpack_indices(prim.indices, raw.data(), sizeof(std::uint32_t), raw.size());
    }
    else
    {
        raw.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            raw[i] = static_cast<std::uint32_t>(i);
        }
    }
    if (prim.type == cgltf_primitive_type_triangles)
    {
        raw.resize(raw.size() - raw.size() % 3);
        return raw;
    }
    std::vector<std::uint32_t> out;
    if (raw.size() < 3)
    {
        return out;
    }
    for (std::size_t i = 2; i < raw.size(); ++i)
    {
        if (prim.type == cgltf_primitive_type_triangle_strip)
        {
            // Ordre alterné pour garder le même sens de rotation sur toute la bande.
            if (i % 2 == 0)
            {
                out.insert(out.end(), {raw[i - 2], raw[i - 1], raw[i]});
            }
            else
            {
                out.insert(out.end(), {raw[i - 1], raw[i - 2], raw[i]});
            }
        }
        else
        {
            out.insert(out.end(), {raw[0], raw[i - 1], raw[i]});
        }
    }
    return out;
}

core::Result<MeshPrimitive> importPrimitive(const cgltf_data *data, const cgltf_primitive &prim, const std::string &meshName)
{
    MeshPrimitive out;
    out.material = indexOr(cgltf_material_index, data, prim.material);
    for (cgltf_size a = 0; a < prim.attributes_count; ++a)
    {
        const cgltf_attribute &attr = prim.attributes[a];
        const cgltf_accessor *acc = attr.data;
        const std::string what = meshName + "." + nameOf(attr.name);
        core::Result<> ok{};
        switch (attr.type)
        {
            case cgltf_attribute_type_position:
                if (auto v = unpack<core::Vec3>(acc, 3, what)) out.positions = std::move(*v);
                else ok = std::unexpected(v.error());
                break;
            case cgltf_attribute_type_normal:
                if (auto v = unpack<core::Vec3>(acc, 3, what)) out.normals = std::move(*v);
                else ok = std::unexpected(v.error());
                break;
            case cgltf_attribute_type_tangent:
                if (auto v = unpack<core::Vec4>(acc, 4, what)) out.tangents = std::move(*v);
                else ok = std::unexpected(v.error());
                break;
            case cgltf_attribute_type_texcoord:
                if (attr.index > 1)
                {
                    break; // TEXCOORD_2 et plus : non utilisés par les matériaux du moteur
                }
                if (auto v = unpack<core::Vec2>(acc, 2, what)) (attr.index == 0 ? out.uv0 : out.uv1) = std::move(*v);
                else ok = std::unexpected(v.error());
                break;
            case cgltf_attribute_type_color:
                if (attr.index != 0)
                {
                    break;
                }
                if (cgltf_num_components(acc->type) == 3)
                {
                    if (auto v = unpack<core::Vec3>(acc, 3, what))
                    {
                        out.colors.reserve(v->size());
                        for (const auto &c : *v)
                        {
                            out.colors.emplace_back(c, 1.0f);
                        }
                    }
                    else
                    {
                        ok = std::unexpected(v.error());
                    }
                }
                else if (auto v = unpack<core::Vec4>(acc, 4, what))
                {
                    out.colors = std::move(*v);
                }
                else
                {
                    ok = std::unexpected(v.error());
                }
                break;
            case cgltf_attribute_type_joints:
                if (attr.index != 0)
                {
                    LOG_WARNING("glTF: ", what, ": more than 4 joints per vertex, only the first set is used");
                    break;
                }
                if (cgltf_num_components(acc->type) != 4)
                {
                    ok = makeError(ErrorCode::kParse, "glTF: " + what + " must be VEC4");
                    break;
                }
                out.joints.resize(acc->count);
                for (cgltf_size i = 0; i < acc->count; ++i)
                {
                    cgltf_uint j[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(acc, i, j, 4);
                    out.joints[i] = glm::u16vec4(j[0], j[1], j[2], j[3]);
                }
                break;
            case cgltf_attribute_type_weights:
                if (attr.index != 0)
                {
                    break;
                }
                if (auto v = unpack<core::Vec4>(acc, 4, what)) out.weights = std::move(*v);
                else ok = std::unexpected(v.error());
                break;
            default: break;
        }
        if (!ok)
        {
            return std::unexpected(ok.error());
        }
    }
    if (out.positions.empty())
    {
        return makeError(ErrorCode::kParse, "glTF: primitive of " + meshName + " has no POSITION");
    }
    out.indices = triangleIndices(prim, out.positions.size());
    out.boundsMin = out.positions[0];
    out.boundsMax = out.positions[0];
    for (const auto &p : out.positions)
    {
        out.boundsMin = glm::min(out.boundsMin, p);
        out.boundsMax = glm::max(out.boundsMax, p);
    }
    return out;
}

core::Result<std::vector<float>> unpackAll(const cgltf_accessor *acc, std::string_view what)
{
    if (acc == nullptr)
    {
        return makeError(ErrorCode::kParse, "glTF: missing accessor for " + std::string(what));
    }
    std::vector<float> out(acc->count * cgltf_num_components(acc->type));
    if (cgltf_accessor_unpack_floats(acc, out.data(), out.size()) != out.size())
    {
        return makeError(ErrorCode::kParse, "glTF: cannot read " + std::string(what));
    }
    return out;
}

core::Result<Model> convert(const cgltf_data *data, const std::filesystem::path &baseDir, std::string_view name)
{
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
    {
        const std::string_view ext = data->extensions_required[i];
        if (std::find(kSupportedExtensions.begin(), kSupportedExtensions.end(), ext) == kSupportedExtensions.end())
        {
            return makeError(ErrorCode::kUnsupported, "glTF: " + std::string(name) + " requires unsupported extension " + std::string(ext));
        }
    }
    Model model;

    model.samplers.reserve(data->samplers_count);
    for (cgltf_size i = 0; i < data->samplers_count; ++i)
    {
        const cgltf_sampler &s = data->samplers[i];
        SamplerData out;
        out.wrapS = wrapOf(s.wrap_s);
        out.wrapT = wrapOf(s.wrap_t);
        out.nearest = s.mag_filter == cgltf_filter_type_nearest;
        model.samplers.push_back(out);
    }

    model.images.reserve(data->images_count);
    for (cgltf_size i = 0; i < data->images_count; ++i)
    {
        const cgltf_image &img = data->images[i];
        ImageData out;
        out.name = nameOf(img.name);
        out.mimeType = nameOf(img.mime_type);
        if (img.buffer_view != nullptr)
        {
            const cgltf_buffer_view *view = img.buffer_view;
            if (view->buffer == nullptr || view->buffer->data == nullptr)
            {
                return makeError(ErrorCode::kParse, "glTF: image " + std::to_string(i) + " points to an unloaded buffer");
            }
            const auto *p = static_cast<const std::byte *>(view->buffer->data) + view->offset;
            out.encoded.assign(p, p + view->size);
        }
        else if (img.uri != nullptr && std::strncmp(img.uri, "data:", 5) == 0)
        {
            const char *comma = std::strchr(img.uri, ',');
            const std::string_view header(img.uri, comma != nullptr ? static_cast<std::size_t>(comma - img.uri) : 0);
            if (comma == nullptr || header.find(";base64") == std::string_view::npos)
            {
                return makeError(ErrorCode::kParse, "glTF: image " + std::to_string(i) + ": data URI is not base64");
            }
            const std::size_t chars = std::strlen(comma + 1);
            const std::size_t size = chars / 4 * 3 - (chars >= 1 && comma[chars] == '=' ? 1 : 0) - (chars >= 2 && comma[chars - 1] == '=' ? 1 : 0);
            void *decoded = nullptr;
            cgltf_options options{};
            if (cgltf_load_buffer_base64(&options, size, comma + 1, &decoded) != cgltf_result_success)
            {
                return makeError(ErrorCode::kParse, "glTF: image " + std::to_string(i) + ": bad base64");
            }
            const auto *p = static_cast<const std::byte *>(decoded);
            out.encoded.assign(p, p + size);
            std::free(decoded);
            if (out.mimeType.empty() && header.size() > 5)
            {
                out.mimeType = std::string(header.substr(5, header.find(';') - 5));
            }
        }
        else if (img.uri != nullptr)
        {
            std::string uri = img.uri;
            uri.resize(cgltf_decode_uri(uri.data()));
            out.path = (baseDir / std::filesystem::path(uri)).lexically_normal();
        }
        model.images.push_back(std::move(out));
    }

    model.textures.reserve(data->textures_count);
    for (cgltf_size i = 0; i < data->textures_count; ++i)
    {
        const cgltf_texture &t = data->textures[i];
        TextureData out;
        out.image = indexOr(cgltf_image_index, data, t.image != nullptr ? t.image : t.basisu_image);
        out.sampler = indexOr(cgltf_sampler_index, data, t.sampler);
        model.textures.push_back(out);
    }

    model.materials.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i)
    {
        const cgltf_material &m = data->materials[i];
        MaterialData out;
        out.name = nameOf(m.name);
        if (m.has_pbr_metallic_roughness)
        {
            const auto &pbr = m.pbr_metallic_roughness;
            out.baseColorFactor = core::Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
            out.baseColor = textureRef(data, pbr.base_color_texture, 1.0f);
            out.metallicFactor = pbr.metallic_factor;
            out.roughnessFactor = pbr.roughness_factor;
            out.metallicRoughness = textureRef(data, pbr.metallic_roughness_texture, 1.0f);
        }
        out.normal = textureRef(data, m.normal_texture, m.normal_texture.scale);
        out.occlusion = textureRef(data, m.occlusion_texture, m.occlusion_texture.scale);
        out.emissive = textureRef(data, m.emissive_texture, 1.0f);
        out.emissiveFactor = core::Vec3(m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
        out.emissiveStrength = m.has_emissive_strength ? m.emissive_strength.emissive_strength : 1.0f;
        out.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::kMask
                        : m.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::kBlend
                                                                 : AlphaMode::kOpaque;
        out.alphaCutoff = m.alpha_cutoff;
        out.doubleSided = m.double_sided;
        out.unlit = m.unlit;
        model.materials.push_back(std::move(out));
    }

    model.meshes.reserve(data->meshes_count);
    for (cgltf_size i = 0; i < data->meshes_count; ++i)
    {
        const cgltf_mesh &m = data->meshes[i];
        MeshData out;
        out.name = m.name != nullptr ? std::string(m.name) : "mesh" + std::to_string(i);
        for (cgltf_size p = 0; p < m.primitives_count; ++p)
        {
            const cgltf_primitive &prim = m.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
                prim.type != cgltf_primitive_type_triangle_fan)
            {
                LOG_WARNING("glTF: ", std::string(name), ": ", out.name, " primitive ", p, " is points or lines, skipped");
                continue;
            }
            if (prim.has_draco_mesh_compression)
            {
                return makeError(ErrorCode::kUnsupported, "glTF: " + out.name + " uses Draco compression");
            }
            auto imported = importPrimitive(data, prim, out.name);
            if (!imported)
            {
                return std::unexpected(imported.error());
            }
            out.primitives.push_back(std::move(*imported));
        }
        model.meshes.push_back(std::move(out));
    }

    model.cameras.reserve(data->cameras_count);
    for (cgltf_size i = 0; i < data->cameras_count; ++i)
    {
        const cgltf_camera &c = data->cameras[i];
        CameraData out;
        out.name = nameOf(c.name);
        if (c.type == cgltf_camera_type_orthographic)
        {
            out.perspective = false;
            out.xmag = c.data.orthographic.xmag;
            out.ymag = c.data.orthographic.ymag;
            out.znear = c.data.orthographic.znear;
            out.zfar = c.data.orthographic.zfar;
        }
        else
        {
            out.yfov = c.data.perspective.yfov;
            out.aspectRatio = c.data.perspective.has_aspect_ratio ? c.data.perspective.aspect_ratio : 0.0f;
            out.znear = c.data.perspective.znear;
            out.zfar = c.data.perspective.has_zfar ? c.data.perspective.zfar : 0.0f;
        }
        model.cameras.push_back(std::move(out));
    }

    model.lights.reserve(data->lights_count);
    for (cgltf_size i = 0; i < data->lights_count; ++i)
    {
        const cgltf_light &l = data->lights[i];
        LightData out;
        out.name = nameOf(l.name);
        out.kind = l.type == cgltf_light_type_directional ? LightKind::kDirectional
                   : l.type == cgltf_light_type_spot      ? LightKind::kSpot
                                                          : LightKind::kPoint;
        out.color = core::Vec3(l.color[0], l.color[1], l.color[2]);
        out.intensity = l.intensity;
        out.range = l.range;
        out.innerConeAngle = l.spot_inner_cone_angle;
        out.outerConeAngle = l.spot_outer_cone_angle;
        model.lights.push_back(std::move(out));
    }

    model.nodes.resize(data->nodes_count);
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node &n = data->nodes[i];
        NodeData &out = model.nodes[i];
        out.name = nameOf(n.name);
        out.parent = indexOr(cgltf_node_index, data, n.parent);
        for (cgltf_size c = 0; c < n.children_count; ++c)
        {
            out.children.push_back(static_cast<int>(cgltf_node_index(data, n.children[c])));
        }
        if (n.has_matrix)
        {
            const core::Mat4 m = glm::make_mat4(n.matrix);
            // Décomposition TRS (glTF interdit le cisaillement dans `matrix` des nœuds animés ; pour les autres, l'approximation
            // TRS est celle des importeurs courants).
            out.translation = core::Vec3(m[3]);
            core::Vec3 cols[3] = {core::Vec3(m[0]), core::Vec3(m[1]), core::Vec3(m[2])};
            out.scale = core::Vec3(glm::length(cols[0]), glm::length(cols[1]), glm::length(cols[2]));
            if (glm::dot(glm::cross(cols[0], cols[1]), cols[2]) < 0.0f)
            {
                out.scale.x = -out.scale.x;
            }
            core::Mat3 rot;
            for (int c = 0; c < 3; ++c)
            {
                rot[c] = out.scale[c] != 0.0f ? cols[c] / out.scale[c] : core::Vec3(0.0f);
            }
            out.rotation = glm::normalize(glm::quat_cast(rot));
        }
        else
        {
            if (n.has_translation)
            {
                out.translation = core::Vec3(n.translation[0], n.translation[1], n.translation[2]);
            }
            if (n.has_rotation)
            {
                out.rotation = core::Quat(n.rotation[3], n.rotation[0], n.rotation[1], n.rotation[2]);
            }
            if (n.has_scale)
            {
                out.scale = core::Vec3(n.scale[0], n.scale[1], n.scale[2]);
            }
        }
        out.mesh = indexOr(cgltf_mesh_index, data, n.mesh);
        out.skin = indexOr(cgltf_skin_index, data, n.skin);
        out.camera = indexOr(cgltf_camera_index, data, n.camera);
        out.light = indexOr(cgltf_light_index, data, n.light);
    }

    model.skins.reserve(data->skins_count);
    for (cgltf_size i = 0; i < data->skins_count; ++i)
    {
        const cgltf_skin &s = data->skins[i];
        SkinData out;
        out.name = nameOf(s.name);
        out.skeleton = indexOr(cgltf_node_index, data, s.skeleton);
        for (cgltf_size j = 0; j < s.joints_count; ++j)
        {
            out.joints.push_back(static_cast<int>(cgltf_node_index(data, s.joints[j])));
        }
        out.inverseBindMatrices.assign(s.joints_count, core::Mat4(1.0f));
        if (s.inverse_bind_matrices != nullptr)
        {
            auto floats = unpackAll(s.inverse_bind_matrices, "inverseBindMatrices");
            if (!floats)
            {
                return std::unexpected(floats.error());
            }
            if (floats->size() < s.joints_count * 16)
            {
                return makeError(ErrorCode::kParse, "glTF: skin " + std::to_string(i) + " has fewer inverse bind matrices than joints");
            }
            for (cgltf_size j = 0; j < s.joints_count; ++j)
            {
                out.inverseBindMatrices[j] = glm::make_mat4(floats->data() + j * 16);
            }
        }
        model.skins.push_back(std::move(out));
    }

    model.animations.reserve(data->animations_count);
    for (cgltf_size i = 0; i < data->animations_count; ++i)
    {
        const cgltf_animation &a = data->animations[i];
        AnimationData out;
        out.name = a.name != nullptr ? std::string(a.name) : "animation" + std::to_string(i);
        for (cgltf_size c = 0; c < a.channels_count; ++c)
        {
            const cgltf_animation_channel &ch = a.channels[c];
            if (ch.target_node == nullptr || ch.sampler == nullptr)
            {
                continue; // cible absente : permise par glTF (extension), rien à animer
            }
            AnimationChannel channel;
            channel.node = static_cast<int>(cgltf_node_index(data, ch.target_node));
            switch (ch.target_path)
            {
                case cgltf_animation_path_type_translation: channel.path = AnimationPath::kTranslation; break;
                case cgltf_animation_path_type_rotation: channel.path = AnimationPath::kRotation; break;
                case cgltf_animation_path_type_scale: channel.path = AnimationPath::kScale; break;
                case cgltf_animation_path_type_weights: channel.path = AnimationPath::kWeights; break;
                default: continue;
            }
            channel.interpolation = ch.sampler->interpolation == cgltf_interpolation_type_step           ? Interpolation::kStep
                                    : ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline ? Interpolation::kCubicSpline
                                                                                                          : Interpolation::kLinear;
            auto times = unpackAll(ch.sampler->input, "animation input");
            auto values = unpackAll(ch.sampler->output, "animation output");
            if (!times || !values)
            {
                return std::unexpected(!times ? times.error() : values.error());
            }
            channel.times = std::move(*times);
            channel.values = std::move(*values);
            const std::size_t keys = channel.times.size();
            const std::size_t groups = channel.interpolation == Interpolation::kCubicSpline ? 3 : 1;
            if (keys == 0 || channel.values.size() % (keys * groups) != 0)
            {
                return makeError(ErrorCode::kParse, "glTF: animation " + out.name + " channel " + std::to_string(c) + ": value count mismatch");
            }
            channel.componentCount = static_cast<std::uint32_t>(channel.values.size() / (keys * groups));
            out.duration = std::max(out.duration, channel.times.back());
            out.channels.push_back(std::move(channel));
        }
        model.animations.push_back(std::move(out));
    }

    model.scenes.reserve(data->scenes_count);
    for (cgltf_size i = 0; i < data->scenes_count; ++i)
    {
        const cgltf_scene &s = data->scenes[i];
        SceneData out;
        out.name = nameOf(s.name);
        for (cgltf_size n = 0; n < s.nodes_count; ++n)
        {
            out.roots.push_back(static_cast<int>(cgltf_node_index(data, s.nodes[n])));
        }
        model.scenes.push_back(std::move(out));
    }
    model.defaultScene = indexOr(cgltf_scene_index, data, data->scene);
    if (model.defaultScene < 0 && !model.scenes.empty())
    {
        model.defaultScene = 0;
    }
    if (auto valid = model.validate(); !valid)
    {
        return std::unexpected(valid.error());
    }
    return model;
}

} // namespace

core::Result<Model> parseGltf(std::span<const std::byte> bytes, const std::filesystem::path &baseDir, std::string_view name)
{
    if (bytes.empty())
    {
        return makeError(ErrorCode::kInvalidArgument, "parseGltf: no bytes (" + std::string(name) + ")");
    }
    cgltf_options options{};
    cgltf_data *raw = nullptr;
    cgltf_result r = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
    CgltfPtr data(raw);
    if (r != cgltf_result_success)
    {
        return makeError(ErrorCode::kParse, "glTF: cannot parse " + std::string(name) + ": " + resultName(r));
    }
    // cgltf_load_buffers attend le chemin du fichier glTF : il en retire le nom pour résoudre les URI relatives.
    const std::string anchor = (baseDir / std::filesystem::path(std::string(name)).filename()).string();
    r = cgltf_load_buffers(&options, data.get(), anchor.c_str());
    if (r != cgltf_result_success)
    {
        return makeError(r == cgltf_result_file_not_found || r == cgltf_result_io_error ? ErrorCode::kIo : ErrorCode::kParse,
                         "glTF: cannot load the buffers of " + std::string(name) + ": " + resultName(r));
    }
    r = cgltf_validate(data.get());
    if (r != cgltf_result_success)
    {
        return makeError(ErrorCode::kParse, "glTF: " + std::string(name) + " is invalid: " + resultName(r));
    }
    return convert(data.get(), baseDir, name);
}

core::Result<Model> loadGltf(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return makeError(ErrorCode::kIo, "loadGltf: cannot open " + path.generic_string());
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        return makeError(ErrorCode::kIo, "loadGltf: cannot read " + path.generic_string());
    }
    std::filesystem::path dir = path.parent_path();
    if (dir.empty())
    {
        dir = ".";
    }
    return parseGltf(std::as_bytes(std::span(bytes)), dir, path.filename().generic_string());
}

} // namespace orosmo::assets
