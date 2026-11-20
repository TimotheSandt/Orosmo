#include "orosmo/renderer/draw_list.hpp"

#include <atomic>

namespace orosmo::renderer
{

namespace
{
// État d'allocation des identifiants d'atlas : un seul compteur pour tout le processus (défini ici et non dans l'en-tête).
std::atomic<std::uint64_t> gNextAtlasId{1};
} // namespace

std::uint64_t newAtlasId() noexcept { return gNextAtlasId.fetch_add(1, std::memory_order_relaxed); }

} // namespace orosmo::renderer
