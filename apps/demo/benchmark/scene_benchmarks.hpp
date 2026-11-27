// Mesures du module scene (A79 lot L2) : parcours de 100 000 entités et mise à jour des transformations. Hors de la suite
// doctest (aucune horloge dans les tests) ; `make -C apps/demo benchmark` écrit benchmark_report.json.
#pragma once

#include <orosmo/core/benchmark.hpp>
#include <orosmo/scene/world.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace scene_bench
{
struct Position
{
    orosmo::core::Vec3 value{0.f};
};
struct Velocity
{
    orosmo::core::Vec3 value{1.f, 2.f, 3.f};
};
} // namespace scene_bench

template <> struct orosmo::core::Reflect<scene_bench::Position>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &scene_bench::Position::value)); }
};
template <> struct orosmo::core::Reflect<scene_bench::Velocity>
{
    static constexpr auto fields() { return std::make_tuple(field("value", &scene_bench::Velocity::value)); }
};

namespace scene_bench
{
constexpr int kEntities = 100000;

// view<Position> sur 100 000 entités, puis view<Position, Velocity> sur la moitié qui a une vitesse.
class ViewScenario final : public orosmo::core::bench::Scenario
{
  public:
    std::string name() const override { return "scene-view"; }
    void setup() override
    {
        world_.emplace();
        pos_ = *world_->registerComponent<Position>("Position");
        vel_ = *world_->registerComponent<Velocity>("Velocity");
        for (int i = 0; i < kEntities; ++i)
        {
            const auto e = world_->create();
            world_->add(e, pos_);
            if (i % 2 == 0)
                world_->add(e, vel_);
        }
    }
    void runIteration() override
    {
        world_->view(pos_).each([](orosmo::scene::Entity, Position &p) { p.value.x += 1.f; });
        world_->view(pos_, vel_).each([](orosmo::scene::Entity, Position &p, const Velocity &v) { p.value += v.value * 0.5f; });
    }
    void teardown() override { world_.reset(); }

  private:
    std::optional<orosmo::scene::World> world_;
    orosmo::scene::ComponentType<Position> pos_{};
    orosmo::scene::ComponentType<Velocity> vel_{};
};

// updateTransforms sur 100 000 entités (1 000 racines × 99 enfants), toutes sales à chaque itération.
class TransformScenario final : public orosmo::core::bench::Scenario
{
  public:
    std::string name() const override { return "scene-transforms"; }
    void setup() override
    {
        world_.emplace();
        roots_.clear();
        for (int r = 0; r < 1000; ++r)
        {
            const auto root = world_->create();
            orosmo::scene::Transform t;
            t.position = orosmo::core::DVec3(static_cast<double>(r), 0.0, 0.0);
            (void)world_->setTransform(root, t);
            roots_.push_back(root);
            for (int c = 0; c < 99; ++c)
            {
                const auto child = world_->create();
                t.position = orosmo::core::DVec3(0.0, 1.0, 0.0);
                (void)world_->setTransform(child, t);
                (void)world_->setParent(child, root);
            }
        }
        world_->updateTransforms();
    }
    void runIteration() override
    {
        for (const auto root : roots_)
            world_->markTransformDirty(root);
        world_->updateTransforms();
    }
    void teardown() override { world_.reset(); }

  private:
    std::optional<orosmo::scene::World> world_;
    std::vector<orosmo::scene::Entity> roots_;
};
} // namespace scene_bench
