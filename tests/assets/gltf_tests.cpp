// ASSET-01 : import et export glTF 2.0 (cgltf). Le modèle de test est construit par le code (pas de fichier d'exemple copié,
// A70), écrit en .glb et en .gltf, relu, puis comparé champ par champ ; un .gltf écrit à la main couvre les cas que l'export ne
// produit pas (data URI, bande de triangles, matrice de nœud, extension requise inconnue).
#include <doctest/doctest.h>

#include "support/result_support.hpp"
#include "support/temp_dir.hpp"

#include <orosmo/assets/model.hpp>
#include <orosmo/renderer/image.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>

namespace as = orosmo::assets;
using orosmo::core::ErrorCode;
using orosmo::core::Mat4;
using orosmo::core::Quat;
using orosmo::core::Vec2;
using orosmo::core::Vec3;
using orosmo::core::Vec4;

namespace
{

std::vector<std::byte> pngBytes(const std::filesystem::path &dir)
{
    orosmo::renderer::Image img;
    img.width = 2;
    img.height = 1;
    img.rgba = {10, 20, 30, 255, 40, 50, 60, 255};
    REQUIRE(orosmo::renderer::savePng(img, dir / "tmp.png").has_value());
    std::ifstream in(dir / "tmp.png", std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto *p = reinterpret_cast<const std::byte *>(raw.data());
    return {p, p + raw.size()};
}

// Bras articulé : une bande de 3 segments sur Y, deux os (racine et coude), un clip qui plie le coude, un matériau texturé,
// une caméra et une lumière spot.
as::Model makeArm(const std::filesystem::path &dir)
{
    as::Model m;
    as::MeshPrimitive prim;
    for (int y = 0; y <= 3; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            prim.positions.emplace_back(static_cast<float>(x) * 0.5f - 0.25f, static_cast<float>(y), 0.0f);
            prim.normals.emplace_back(0.0f, 0.0f, 1.0f);
            prim.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
            prim.uv0.emplace_back(static_cast<float>(x), static_cast<float>(y) / 3.0f);
            const float w = y <= 1 ? 0.0f : (y == 2 ? 0.5f : 1.0f);
            prim.joints.emplace_back(0, 1, 0, 0);
            prim.weights.emplace_back(1.0f - w, w, 0.0f, 0.0f);
        }
    }
    for (std::uint32_t s = 0; s < 3; ++s)
    {
        const std::uint32_t a = s * 2;
        prim.indices.insert(prim.indices.end(), {a, a + 1, a + 3, a, a + 3, a + 2});
    }
    prim.material = 0;
    prim.boundsMin = Vec3(-0.25f, 0.0f, 0.0f);
    prim.boundsMax = Vec3(0.25f, 3.0f, 0.0f);
    m.meshes.push_back({"arm", {prim}});

    as::ImageData image;
    image.name = "albedo";
    image.encoded = pngBytes(dir);
    image.mimeType = "image/png";
    m.images.push_back(image);
    m.samplers.push_back({as::WrapMode::kClampToEdge, as::WrapMode::kMirroredRepeat, true});
    m.textures.push_back({0, 0});

    as::MaterialData mat;
    mat.name = "skin";
    mat.baseColorFactor = Vec4(0.8f, 0.6f, 0.4f, 0.5f);
    mat.baseColor.texture = 0;
    mat.metallicFactor = 0.25f;
    mat.roughnessFactor = 0.75f;
    mat.normal.texture = 0;
    mat.normal.scale = 0.5f;
    mat.emissiveFactor = Vec3(1.0f, 0.5f, 0.0f);
    mat.emissiveStrength = 4.0f;
    mat.alphaMode = as::AlphaMode::kMask;
    mat.alphaCutoff = 0.3f;
    mat.doubleSided = true;
    m.materials.push_back(mat);

    as::CameraData cam;
    cam.name = "view";
    cam.yfov = 0.9f;
    cam.znear = 0.05f;
    cam.zfar = 500.0f;
    m.cameras.push_back(cam);
    as::LightData light;
    light.name = "lamp";
    light.kind = as::LightKind::kSpot;
    light.color = Vec3(1.0f, 0.9f, 0.8f);
    light.intensity = 120.0f;
    light.range = 20.0f;
    light.innerConeAngle = 0.2f;
    light.outerConeAngle = 0.6f;
    m.lights.push_back(light);

    // Nœuds : 0 racine (maillage + peau), 1 os racine, 2 os du coude (enfant de 1), 3 caméra, 4 lumière.
    m.nodes.resize(5);
    m.nodes[0].name = "root";
    m.nodes[0].mesh = 0;
    m.nodes[0].skin = 0;
    m.nodes[0].children = {1, 3, 4};
    m.nodes[1].name = "shoulder";
    m.nodes[1].parent = 0;
    m.nodes[1].children = {2};
    m.nodes[2].name = "elbow";
    m.nodes[2].parent = 1;
    m.nodes[2].translation = Vec3(0.0f, 1.5f, 0.0f);
    m.nodes[3].name = "camera";
    m.nodes[3].parent = 0;
    m.nodes[3].camera = 0;
    m.nodes[3].translation = Vec3(0.0f, 1.5f, 6.0f);
    m.nodes[4].name = "light";
    m.nodes[4].parent = 0;
    m.nodes[4].light = 0;
    m.nodes[4].rotation = glm::angleAxis(-0.5f, Vec3(1.0f, 0.0f, 0.0f));
    m.nodes[4].scale = Vec3(1.0f, 2.0f, 1.0f);

    as::SkinData skin;
    skin.name = "armature";
    skin.joints = {1, 2};
    skin.skeleton = 1;
    skin.inverseBindMatrices = {Mat4(1.0f), glm::translate(Mat4(1.0f), Vec3(0.0f, -1.5f, 0.0f))};
    m.skins.push_back(skin);

    as::AnimationData anim;
    anim.name = "bend";
    as::AnimationChannel rot;
    rot.node = 2;
    rot.path = as::AnimationPath::kRotation;
    rot.componentCount = 4;
    rot.times = {0.0f, 0.5f, 1.0f};
    for (float a : {0.0f, 0.8f, 0.0f})
    {
        const Quat q = glm::angleAxis(a, Vec3(0.0f, 0.0f, 1.0f));
        rot.values.insert(rot.values.end(), {q.x, q.y, q.z, q.w});
    }
    as::AnimationChannel move;
    move.node = 1;
    move.path = as::AnimationPath::kTranslation;
    move.interpolation = as::Interpolation::kStep;
    move.times = {0.0f, 2.0f};
    move.values = {0, 0, 0, 0, 1, 0};
    anim.channels = {rot, move};
    anim.duration = 2.0f;
    m.animations.push_back(anim);

    m.scenes.push_back({"main", {0}});
    m.defaultScene = 0;
    return m;
}

bool near(const Vec4 &a, const Vec4 &b) { return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec4(1e-5f))); }
bool near(const Vec3 &a, const Vec3 &b) { return near(Vec4(a, 0.0f), Vec4(b, 0.0f)); }

void checkSame(const as::Model &a, const as::Model &b)
{
    REQUIRE(b.meshes.size() == 1);
    REQUIRE(b.meshes[0].primitives.size() == 1);
    const auto &pa = a.meshes[0].primitives[0];
    const auto &pb = b.meshes[0].primitives[0];
    CHECK(b.meshes[0].name == "arm");
    CHECK(pb.positions == pa.positions);
    CHECK(pb.normals == pa.normals);
    CHECK(pb.tangents == pa.tangents);
    CHECK(pb.uv0 == pa.uv0);
    CHECK(pb.joints == pa.joints);
    CHECK(pb.weights == pa.weights);
    CHECK(pb.indices == pa.indices);
    CHECK(pb.material == 0);
    CHECK(near(pb.boundsMin, pa.boundsMin));
    CHECK(near(pb.boundsMax, pa.boundsMax));

    REQUIRE(b.materials.size() == 1);
    const auto &ma = a.materials[0];
    const auto &mb = b.materials[0];
    CHECK(mb.name == "skin");
    CHECK(near(mb.baseColorFactor, ma.baseColorFactor));
    CHECK(mb.baseColor.texture == 0);
    CHECK(mb.metallicFactor == doctest::Approx(0.25f));
    CHECK(mb.roughnessFactor == doctest::Approx(0.75f));
    CHECK(mb.normal.texture == 0);
    CHECK(mb.normal.scale == doctest::Approx(0.5f));
    CHECK(near(mb.emissiveFactor, ma.emissiveFactor));
    CHECK(mb.emissiveStrength == doctest::Approx(4.0f));
    CHECK((mb.alphaMode == as::AlphaMode::kMask));
    CHECK(mb.alphaCutoff == doctest::Approx(0.3f));
    CHECK(mb.doubleSided);

    REQUIRE(b.textures.size() == 1);
    REQUIRE(b.samplers.size() == 1);
    CHECK((b.samplers[0].wrapS == as::WrapMode::kClampToEdge));
    CHECK((b.samplers[0].wrapT == as::WrapMode::kMirroredRepeat));
    CHECK(b.samplers[0].nearest);
    REQUIRE(b.images.size() == 1);
    CHECK(b.images[0].encoded == a.images[0].encoded);
    CHECK(b.images[0].mimeType == "image/png");

    REQUIRE(b.cameras.size() == 1);
    CHECK(b.cameras[0].yfov == doctest::Approx(0.9f));
    CHECK(b.cameras[0].zfar == doctest::Approx(500.0f));
    REQUIRE(b.lights.size() == 1);
    CHECK((b.lights[0].kind == as::LightKind::kSpot));
    CHECK(b.lights[0].intensity == doctest::Approx(120.0f));
    CHECK(b.lights[0].outerConeAngle == doctest::Approx(0.6f));

    REQUIRE(b.nodes.size() == 5);
    CHECK(b.nodes[2].parent == 1);
    CHECK(b.nodes[0].children == std::vector<int>{1, 3, 4});
    CHECK(b.nodes[3].camera == 0);
    CHECK(b.nodes[4].light == 0);
    CHECK(near(Vec4(b.nodes[4].rotation.x, b.nodes[4].rotation.y, b.nodes[4].rotation.z, b.nodes[4].rotation.w),
               Vec4(a.nodes[4].rotation.x, a.nodes[4].rotation.y, a.nodes[4].rotation.z, a.nodes[4].rotation.w)));
    CHECK(near(b.nodes[4].scale, a.nodes[4].scale));

    REQUIRE(b.skins.size() == 1);
    CHECK(b.skins[0].joints == std::vector<int>{1, 2});
    CHECK(b.skins[0].skeleton == 1);
    CHECK(b.skins[0].inverseBindMatrices[1] == a.skins[0].inverseBindMatrices[1]);

    REQUIRE(b.animations.size() == 1);
    REQUIRE(b.animations[0].channels.size() == 2);
    CHECK(b.animations[0].duration == doctest::Approx(2.0f));
    const auto &ca = a.animations[0].channels[0];
    const auto &cb = b.animations[0].channels[0];
    CHECK((cb.path == as::AnimationPath::kRotation));
    CHECK(cb.componentCount == 4);
    CHECK(cb.times == ca.times);
    CHECK(cb.values == ca.values);
    CHECK((b.animations[0].channels[1].interpolation == as::Interpolation::kStep));

    REQUIRE(b.scenes.size() == 1);
    CHECK(b.defaultScene == 0);
    CHECK(b.scenes[0].roots == std::vector<int>{0});
}

void writeText(const std::filesystem::path &path, const std::string &text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

} // namespace

TEST_SUITE("assets/gltf")
{
    TEST_CASE("a generated skinned and animated model round-trips through .glb and .gltf + .bin")
    {
        tests::TempDir dir("engine_gltf");
        const as::Model model = makeArm(dir.path);
        REQUIRE(model.validate().has_value());
        const auto stats = model.stats();
        CHECK(stats.vertices == 8);
        CHECK(stats.triangles == 6);

        REQUIRE(as::saveGltf(model, dir.path / "arm.glb").has_value());
        auto glb = as::loadGltf(dir.path / "arm.glb");
        REQUIRE_MESSAGE(glb.has_value(), (glb ? "" : glb.error().message));
        checkSame(model, *glb);

        REQUIRE(as::saveGltf(model, dir.path / "arm.gltf").has_value());
        CHECK(std::filesystem::exists(dir.path / "arm.bin"));
        auto gltf = as::loadGltf(dir.path / "arm.gltf");
        REQUIRE_MESSAGE(gltf.has_value(), (gltf ? "" : gltf.error().message));
        checkSame(model, *gltf);

        // Ré-export du modèle relu : même contenu (l'export est stable).
        REQUIRE(as::saveGltf(*glb, dir.path / "again.glb").has_value());
        auto again = as::loadGltf(dir.path / "again.glb");
        REQUIRE(again.has_value());
        checkSame(model, *again);
    }

    TEST_CASE("world matrices compose parent before child")
    {
        tests::TempDir dir("engine_gltf");
        as::Model model = makeArm(dir.path);
        // Racine : translation (10,0,0) et échelle 2 sur Y. Le coude est à (0,1.5,0) dans le repère de son parent : composé « parent d'abord »,
        // il vaut y = 2 * 1.5 = 3 ; composé dans l'ordre inverse (enfant d'abord) il resterait à 1.5.
        model.nodes[0].translation = Vec3(10.0f, 0.0f, 0.0f);
        model.nodes[0].scale = Vec3(1.0f, 2.0f, 1.0f);
        const auto world = model.worldMatrices();
        REQUIRE(world.size() == 5);
        CHECK(world[2][3].x == doctest::Approx(10.0f));
        CHECK(world[2][3].y == doctest::Approx(3.0f));
        CHECK(world[3][3].z == doctest::Approx(6.0f));
    }

    TEST_CASE("hand-written .gltf: data URI buffer, triangle strip, node matrix, external image path")
    {
        tests::TempDir dir("engine_gltf");
        // 4 sommets (float vec3) en bande : 48 octets, encodés en base64.
        const float pos[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
        std::string b64;
        {
            static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const auto *p = reinterpret_cast<const unsigned char *>(pos);
            for (std::size_t i = 0; i < sizeof(pos); i += 3)
            {
                const unsigned v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
                b64 += {t[(v >> 18) & 63], t[(v >> 12) & 63], t[(v >> 6) & 63], t[v & 63]};
            }
        }
        const std::string json = R"({"asset":{"version":"2.0"},
 "buffers":[{"byteLength":48,"uri":"data:application/octet-stream;base64,)" + b64 + R"("}],
 "bufferViews":[{"buffer":0,"byteLength":48}],
 "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
 "images":[{"uri":"textures/my%20albedo.png"}],
 "textures":[{"source":0}],
 "materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"extensions":{"KHR_materials_unlit":{}}}],
 "meshes":[{"primitives":[{"attributes":{"POSITION":0},"mode":5,"material":0}]}],
 "nodes":[{"mesh":0,"matrix":[2,0,0,0, 0,2,0,0, 0,0,2,0, 5,6,7,1]}],
 "scenes":[{"nodes":[0]}]})";
        writeText(dir.path / "strip.gltf", json);
        auto model = as::loadGltf(dir.path / "strip.gltf");
        REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
        REQUIRE(model->meshes.size() == 1);
        REQUIRE(model->meshes[0].primitives.size() == 1);
        const auto &prim = model->meshes[0].primitives[0];
        CHECK(prim.positions.size() == 4);
        CHECK(prim.indices == std::vector<std::uint32_t>{0, 1, 2, 2, 1, 3}); // bande → liste, même sens de rotation
        CHECK(model->materials[0].unlit);
        CHECK(model->images[0].path.filename() == "my albedo.png");
        CHECK(model->images[0].path.parent_path().filename() == "textures");
        CHECK(near(model->nodes[0].translation, Vec3(5, 6, 7)));
        CHECK(near(model->nodes[0].scale, Vec3(2, 2, 2)));
        CHECK(model->defaultScene == 0);
    }

    TEST_CASE("errors: missing file, garbage, unsupported required extension, broken reference")
    {
        tests::TempDir dir("engine_gltf");
        CHECK((tests::errorOf(as::loadGltf(dir.path / "absent.glb")) == ErrorCode::kIo));
        writeText(dir.path / "junk.gltf", "{ not json");
        CHECK((tests::errorOf(as::loadGltf(dir.path / "junk.gltf")) == ErrorCode::kParse));
        writeText(dir.path / "draco.gltf", R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"],
"extensionsRequired":["KHR_draco_mesh_compression"]})");
        auto draco = as::loadGltf(dir.path / "draco.gltf");
        REQUIRE_FALSE(draco.has_value());
        CHECK((draco.error().code == ErrorCode::kUnsupported));
        CHECK(draco.error().message.find("KHR_draco_mesh_compression") != std::string::npos);
        writeText(dir.path / "nobuf.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":12,"uri":"missing.bin"}]})");
        CHECK((tests::errorOf(as::loadGltf(dir.path / "nobuf.gltf")) == ErrorCode::kIo));
        // L'extension requise inconnue est refusée avant le chargement des tampons : un tampon externe manquant ne la masque pas.
        writeText(dir.path / "extbuf.gltf", R"({"asset":{"version":"2.0"},"extensionsRequired":["VENDOR_unknown_ext"],
"buffers":[{"byteLength":12,"uri":"missing.bin"}]})");
        CHECK((tests::errorOf(as::loadGltf(dir.path / "extbuf.gltf")) == ErrorCode::kUnsupported));

        as::Model bad = makeArm(dir.path);
        bad.meshes[0].primitives[0].indices.push_back(99);
        bad.meshes[0].primitives[0].indices.push_back(0);
        bad.meshes[0].primitives[0].indices.push_back(1);
        CHECK((tests::errorOf(bad.validate()) == ErrorCode::kParse));
        CHECK((tests::errorOf(as::saveGltf(bad, dir.path / "bad.glb")) == ErrorCode::kParse));
        CHECK((tests::errorOf(as::saveGltf(makeArm(dir.path), dir.path / "x.obj")) == ErrorCode::kInvalidArgument));
    }

    TEST_CASE("validate: the parent link must be listed by the parent, in both directions")
    {
        tests::TempDir dir("engine_gltf");
        as::Model model = makeArm(dir.path);
        REQUIRE(model.validate().has_value());
        model.nodes[0].children = {1, 4}; // le nœud 3 déclare le parent 0 mais n'est plus dans ses enfants
        CHECK((tests::errorOf(model.validate()) == ErrorCode::kParse));

        // Deux racines distinctes sans lien : accepté.
        as::Model roots = makeArm(dir.path);
        roots.nodes[3].parent = -1;
        roots.nodes[0].children = {1, 4};
        CHECK(roots.validate().has_value());

        as::Model twice = makeArm(dir.path);
        twice.nodes[0].children = {1, 3, 4, 3};
        CHECK((tests::errorOf(twice.validate()) == ErrorCode::kParse));
    }

    TEST_CASE("validate: cycles are found, and a long valid chain is accepted")
    {
        as::Model cycle;
        cycle.nodes.resize(3);
        for (int i = 0; i < 3; ++i)
        {
            cycle.nodes[static_cast<std::size_t>(i)].parent = (i + 2) % 3;
            cycle.nodes[static_cast<std::size_t>(i)].children = {(i + 1) % 3};
        }
        CHECK((tests::errorOf(cycle.validate()) == ErrorCode::kParse));

        as::Model self;
        self.nodes.resize(1);
        self.nodes[0].parent = 0;
        self.nodes[0].children = {0};
        CHECK((tests::errorOf(self.validate()) == ErrorCode::kParse));

        as::Model chain;
        constexpr int kLength = 50000;
        chain.nodes.resize(kLength);
        for (int i = 0; i < kLength; ++i)
        {
            auto &n = chain.nodes[static_cast<std::size_t>(i)];
            n.parent = i - 1;
            if (i + 1 < kLength)
            {
                n.children = {i + 1};
            }
        }
        chain.scenes.push_back({"s", {0}});
        CHECK(chain.validate().has_value());
    }

    TEST_CASE("validate: animation key times are non-negative and strictly increasing, scene roots have no parent")
    {
        tests::TempDir dir("engine_gltf");
        const auto withTimes = [&](std::vector<float> times)
        {
            as::Model m = makeArm(dir.path);
            auto &ch = m.animations[0].channels[1]; // translation, 3 composantes
            ch.times = times;
            ch.values.assign(times.size() * 3, 0.0f);
            return m;
        };
        CHECK(withTimes({0.0f, 1.0f}).validate().has_value());
        CHECK(withTimes({2.0f}).validate().has_value()); // une seule clé valide
        CHECK((tests::errorOf(withTimes({1.0f, 0.0f}).validate()) == ErrorCode::kParse));
        CHECK((tests::errorOf(withTimes({0.0f, 0.0f}).validate()) == ErrorCode::kParse));
        CHECK((tests::errorOf(withTimes({-1.0f, 1.0f}).validate()) == ErrorCode::kParse));
        CHECK((tests::errorOf(withTimes({0.0f, std::nanf("")}).validate()) == ErrorCode::kParse));

        as::Model m = makeArm(dir.path);
        m.scenes[0].roots = {0, 1}; // 1 est l'enfant de 0 : dessiné deux fois
        CHECK((tests::errorOf(m.validate()) == ErrorCode::kParse));
    }

    TEST_CASE("export: names with quotes, backslashes and control characters, and URIs with a literal percent sign, round-trip")
    {
        tests::TempDir dir("engine_gltf");
        as::Model model = makeArm(dir.path);
        model.nodes[1].name = "bone \"A\"\\\tend";
        model.meshes[0].name = "mesh\n2";
        as::ImageData external;
        external.name = "ext";
        external.path = "textures/a%20b.png";
        model.images.push_back(external);
        const std::filesystem::path file = dir.path / "odd%20name.gltf";
        REQUIRE(as::saveGltf(model, file).has_value());
        CHECK(std::filesystem::exists(dir.path / "odd%20name.bin"));
        auto back = as::loadGltf(file);
        REQUIRE_MESSAGE(back.has_value(), (back ? "" : back.error().message));
        CHECK(back->nodes[1].name == model.nodes[1].name);
        CHECK(back->meshes[0].name == "mesh\n2");
        REQUIRE(back->images.size() == 2);
        CHECK(back->images[1].path.filename() == "a%20b.png");
    }

    TEST_CASE("export: a failed publication keeps the previous .bin and leaves no temporary file")
    {
        tests::TempDir dir("engine_gltf");
        const as::Model model = makeArm(dir.path);
        writeText(dir.path / "scene.bin", "OLD-BINARY");
        std::filesystem::create_directory(dir.path / "scene.gltf"); // la publication du JSON ne peut pas réussir
        auto r = as::saveGltf(model, dir.path / "scene.gltf");
        REQUIRE_FALSE(r.has_value());
        CHECK((r.error().code == ErrorCode::kIo));
        std::ifstream in(dir.path / "scene.bin", std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(content == "OLD-BINARY");
        for (const auto &entry : std::filesystem::directory_iterator(dir.path))
        {
            CHECK(entry.path().filename().string().find(".orosmo-") == std::string::npos);
        }
    }

    TEST_CASE("export: the 16-bit index 65535 is reserved, a mesh without primitive is refused, weights tracks are dropped")
    {
        tests::TempDir dir("engine_gltf");
        as::Model big;
        as::MeshPrimitive prim;
        prim.positions.assign(65536, Vec3(0.0f));
        prim.indices = {0, 1, 65535};
        big.meshes.push_back({"big", {prim}});
        big.nodes.resize(1);
        big.nodes[0].mesh = 0;
        big.scenes.push_back({"s", {0}});
        big.defaultScene = 0;
        REQUIRE(as::saveGltf(big, dir.path / "big.gltf").has_value());
        std::ifstream in(dir.path / "big.gltf");
        std::stringstream text;
        text << in.rdbuf();
        CHECK(text.str().find("5125") != std::string::npos); // UNSIGNED_INT, pas UNSIGNED_SHORT (5123)
        auto back = as::loadGltf(dir.path / "big.gltf");
        REQUIRE(back.has_value());
        CHECK(back->meshes[0].primitives[0].indices == prim.indices);

        as::Model empty;
        empty.meshes.push_back({"none", {}});
        auto refused = as::saveGltf(empty, dir.path / "empty.gltf");
        REQUIRE_FALSE(refused.has_value());
        CHECK((refused.error().code == ErrorCode::kInvalidArgument));
        CHECK_FALSE(std::filesystem::exists(dir.path / "empty.gltf"));

        as::Model arm = makeArm(dir.path);
        as::AnimationChannel weights;
        weights.node = 0;
        weights.path = as::AnimationPath::kWeights;
        weights.componentCount = 1;
        weights.times = {0.0f, 1.0f};
        weights.values = {0.0f, 1.0f};
        arm.animations[0].channels.push_back(weights);
        as::AnimationData onlyWeights;
        onlyWeights.name = "morph";
        onlyWeights.channels = {weights};
        arm.animations.push_back(onlyWeights);
        REQUIRE(arm.validate().has_value());
        REQUIRE(as::saveGltf(arm, dir.path / "w.glb").has_value());
        auto reloaded = as::loadGltf(dir.path / "w.glb");
        REQUIRE(reloaded.has_value());
        REQUIRE(reloaded->animations.size() == 1);
        CHECK(reloaded->animations[0].channels.size() == 2);
    }

    TEST_CASE("an image path is absolute after import and is re-expressed relative to the exported file")
    {
        tests::TempDir dir("engine_gltf");
        std::filesystem::create_directories(dir.path / "assets");
        std::filesystem::create_directories(dir.path / "out");
        writeText(dir.path / "assets" / "scene.gltf", R"({"asset":{"version":"2.0"},"images":[{"uri":"albedo.png"}]})");
        const std::filesystem::path previous = std::filesystem::current_path();
        std::filesystem::current_path(dir.path);
        auto model = as::loadGltf("assets/scene.gltf");
        std::filesystem::current_path(previous);
        REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
        REQUIRE(model->images.size() == 1);
        CHECK(model->images[0].path.is_absolute());
        REQUIRE(as::saveGltf(*model, dir.path / "out" / "copie.gltf").has_value());
        auto copy = as::loadGltf(dir.path / "out" / "copie.gltf");
        REQUIRE(copy.has_value());
        CHECK(std::filesystem::equivalent(copy->images[0].path.parent_path(), dir.path / "assets"));
    }

    TEST_CASE("names are decoded from their JSON escapes, surrogate pairs included")
    {
        tests::TempDir dir("engine_gltf");
        writeText(dir.path / "esc.gltf", R"({"asset":{"version":"2.0"},"nodes":[{"name":"a\"b\\c\u00e9\ud83d\ude00\n"}]})");
        auto model = as::loadGltf(dir.path / "esc.gltf");
        REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
        REQUIRE(model->nodes.size() == 1);
        CHECK(model->nodes[0].name == "a\"b\\c\xC3\xA9\xF0\x9F\x98\x80\n");
    }

    TEST_CASE("export: an existing .bak or .tmp neighbour belongs to the user and is never replaced or removed")
    {
        tests::TempDir dir("engine_gltf");
        const as::Model model = makeArm(dir.path);
        writeText(dir.path / "scene.bin", "OLD");
        writeText(dir.path / "scene.bin.bak", "USER-BACKUP");
        writeText(dir.path / "scene.bin.tmp", "USER-TMP");
        writeText(dir.path / "scene.gltf.tmp", "USER-JSON-TMP");
        REQUIRE(as::saveGltf(model, dir.path / "scene.gltf").has_value());
        const auto read = [&](const char *name)
        {
            std::ifstream in(dir.path / name, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        CHECK(read("scene.bin.bak") == "USER-BACKUP");
        CHECK(read("scene.bin.tmp") == "USER-TMP");
        CHECK(read("scene.gltf.tmp") == "USER-JSON-TMP");
        CHECK(read("scene.bin") != "OLD");
        CHECK(as::loadGltf(dir.path / "scene.gltf").has_value());
    }

    TEST_CASE("export: a directory whose entries cannot be inspected gives kIo instead of looping")
    {
        tests::TempDir dir("engine_gltf");
        const as::Model model = makeArm(dir.path);
        const std::filesystem::path locked = dir.path / "locked";
        std::filesystem::create_directory(locked);
        namespace fs = std::filesystem;
        fs::permissions(locked, fs::perms::none);
        // Sans droit de parcours, symlink_status rend file_type::none ; sous root ou sous Windows les droits ne s'appliquent pas.
        std::error_code probe;
        const bool enforced = fs::symlink_status(locked / "x", probe).type() == fs::file_type::none;
        if (enforced)
        {
            const auto saved = as::saveGltf(model, locked / "scene.gltf");
            REQUIRE_FALSE(saved.has_value());
            CHECK((saved.error().code == ErrorCode::kIo));
        }
        else
        {
            MESSAGE("permissions not enforced here (root or Windows): case skipped");
        }
        fs::permissions(locked, fs::perms::owner_all);
    }
}
