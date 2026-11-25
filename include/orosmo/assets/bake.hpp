// Cuisson de maillages (ASSET-04) : une primitive glTF devient un format binaire du moteur, plus compact et plus rapide à
// dessiner. Étapes (meshoptimizer) : dédoublonnage des sommets identiques, ordre des triangles pour le cache de sommets, réduction
// du surdessin, ordre des sommets pour la lecture mémoire, quantification des attributs, niveaux de détail (LOD) par
// simplification. Les textures se cuisent avec `encodeKtx2` (ktx2.hpp). Résultat déterministe : mêmes entrées et options, mêmes octets.
//
// Disposition quantifiée d'un sommet (une archive par attribut, tableaux parallèles, vides si l'attribut est absent) :
//   positions  3 x float32 (exactes)          normales / tangentes  4 x snorm16 (w des normales = 0 ; w des tangentes = signe)
//   uv0 / uv1  2 x half (binary16)            couleurs  4 x unorm8        articulations  4 x u16        poids  4 x unorm16
// Avec `quantize = false`, normales, tangentes et UV restent en flottants 32 bits (tableaux `*F`).
//
// Conteneur `.omesh` (petit-boutiste) : « OMSH », version u32 (= 1), nombre de primitives u32, puis par primitive le matériau (i32),
// les bornes (6 float32), le nombre de sommets, un masque d'attributs, la liste des LOD (nombre d'indices chacun), les flux de
// sommets dans l'ordre ci-dessus, puis les indices u32 de chaque LOD. Lecture : bornes vérifiées partout, aucun accès hors tampon.
//
// Limites : les LOD ignorent les attributs (seule la position guide la simplification) ; ils partagent les sommets du LOD 0 (les
// sommets inutilisés par un LOD ne sont pas retirés) ; les cibles de forme ne sont pas gérées (pas importées, ASSET-01).
#pragma once

#include "orosmo/assets/model.hpp"
#include "orosmo/core/error.hpp"
#include "orosmo/core/math.hpp"

#include <glm/gtc/type_precision.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace orosmo::assets
{

struct BakeOptions
{
    bool optimize = true;  // dédoublonnage, cache de sommets, surdessin, lecture mémoire
    bool quantize = true;  // normales, tangentes et UV compressés (voir la disposition ci-dessus)
    std::vector<float> lodRatios{0.5f, 0.25f}; // part des indices du LOD 0 visée par LOD supplémentaire (décroissante)
    float lodError = 0.01f;                    // erreur maximale d'un LOD, relative à l'étendue du maillage
};

struct BakedPrimitive
{
    bool quantized = true;
    std::vector<core::Vec3> positions;
    std::vector<glm::i16vec4> normals;   // snorm16 (quantized)
    std::vector<glm::i16vec4> tangents;  // snorm16 (quantized)
    std::vector<glm::u16vec2> uv0;       // half (quantized)
    std::vector<glm::u16vec2> uv1;       // half (quantized)
    std::vector<core::Vec3> normalsF;    // flottants (non quantized)
    std::vector<core::Vec4> tangentsF;
    std::vector<core::Vec2> uv0F;
    std::vector<core::Vec2> uv1F;
    std::vector<glm::u8vec4> colors;     // unorm8
    std::vector<glm::u16vec4> joints;
    std::vector<glm::u16vec4> weights;   // unorm16
    std::vector<std::vector<std::uint32_t>> lods; // lods[0] = maillage complet (optimisé), puis de plus en plus grossiers
    int material = -1;
    core::Vec3 boundsMin{0.0f};
    core::Vec3 boundsMax{0.0f};

    std::size_t vertexCount() const noexcept { return positions.size(); }
};

// kInvalidArgument : primitive vide, indices non multiples de 3 ou hors bornes, attribut de mauvaise longueur, ratio de LOD non fini, UV
// hors de la plage binary16 avec `quantize` ; à l'écriture, tout compteur qui ne tient pas dans un u32.
core::Result<BakedPrimitive> bakePrimitive(const MeshPrimitive &primitive, const BakeOptions &options = {});

// Retour vers une primitive de travail (indices du LOD demandé, tous les sommets conservés). kOutOfRange si le LOD n'existe pas.
core::Result<MeshPrimitive> unbake(const BakedPrimitive &baked, std::size_t lod = 0);

// Conteneur `.omesh`. écriture : kInvalidArgument si une primitive est incohérente. Lecture : kParse (tronqué, magie, indices hors
// bornes), kVersionMismatch (version inconnue).
core::Result<std::vector<std::byte>> writeBakedMesh(std::span<const BakedPrimitive> primitives);
core::Result<std::vector<BakedPrimitive>> readBakedMesh(std::span<const std::byte> bytes);

} // namespace orosmo::assets
