// Monde d'entités de la démo, montré par l'inspecteur d'entités des outils de debug (F12, onglet Entities ; UI-04, A79 lot L10).
// La démo ne rend pas encore sa scène par `scene::World` (façade d'application, lot L13) : ce monde contient une entité
// « caméra » dont le composant `DemoCamera` reflète la caméra de la démo. Avant les outils, la caméra est recopiée dans le
// composant (`pull`) ; après, une valeur modifiée dans l'inspecteur est rendue à la caméra (`push`).
#pragma once

#include <orosmo/core/error.hpp>
#include <orosmo/core/math.hpp>
#include <orosmo/core/reflection.hpp>
#include <orosmo/renderer/camera.hpp>
#include <orosmo/scene/world.hpp>

namespace demo
{

struct DemoCamera
{
    orosmo::core::Vec3 position{0.0f};
    orosmo::core::Vec3 direction{0.0f, 0.0f, -1.0f};
};

class DebugWorld
{
  public:
    orosmo::core::Result<> init();
    orosmo::scene::World &world() noexcept { return world_; }
    orosmo::scene::Entity cameraEntity() const noexcept { return cameraEntity_; }
    void pull(const orosmo::renderer::Camera &camera);
    // Vrai si la caméra a été changée (valeur modifiée depuis `pull`).
    bool push(orosmo::renderer::Camera &camera) const;

  private:
    orosmo::scene::World world_;
    orosmo::scene::ComponentType<DemoCamera> camera_{};
    orosmo::scene::Entity cameraEntity_{};
    DemoCamera pulled_{};
};

} // namespace demo

template <> struct orosmo::core::Reflect<demo::DemoCamera>
{
    static constexpr auto fields()
    {
        return std::make_tuple(field("position", &demo::DemoCamera::position, {}, "camera position (world, meters)"),
                               field("direction", &demo::DemoCamera::direction, {}, "view direction (normalized when applied)"));
    }
};
