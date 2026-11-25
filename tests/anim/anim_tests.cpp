// ANIM-02 / ANIM-03 : clips (interpolations, boucle, événements), poses (mélange, masque, couches additives, matrices monde),
// machine à états (conditions, déclencheurs, fondu, fin de clip, événements) et courbes de propriétés. Sans GPU.
#include <limits>
#include <stdexcept>
#include <doctest/doctest.h>

#include <orosmo/anim/clip.hpp>
#include <orosmo/anim/curves.hpp>
#include <orosmo/anim/pose.hpp>
#include <orosmo/anim/state_machine.hpp>

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace an = orosmo::anim;
namespace as = orosmo::assets;
using orosmo::core::Quat;
using orosmo::core::Vec3;

namespace
{

constexpr float kHalfPi = 1.57079632679f;

as::AnimationChannel channel(int node, as::AnimationPath path, as::Interpolation interp, std::vector<float> times, std::vector<float> values)
{
    as::AnimationChannel c;
    c.node = node;
    c.path = path;
    c.interpolation = interp;
    c.componentCount = path == as::AnimationPath::kRotation ? 4 : 3;
    c.times = std::move(times);
    c.values = std::move(values);
    return c;
}

// Clip qui déplace le nœud 0 en X de `x0` à `x1` en `duration` secondes.
an::Clip moveX(float x0, float x1, float duration, std::size_t nodes = 2, const char *name = "move")
{
    as::AnimationData data;
    data.name = name;
    data.channels.push_back(channel(0, as::AnimationPath::kTranslation, as::Interpolation::kLinear, {0.0f, duration}, {x0, 0, 0, x1, 0, 0}));
    auto clip = an::Clip::fromData(data, nodes);
    REQUIRE(clip.has_value());
    return *clip;
}

an::Pose zeroPose(std::size_t n = 2)
{
    an::Pose p;
    p.nodes.resize(n);
    return p;
}

bool same(const Vec3 &a, const Vec3 &b, float eps = 1e-4f) { return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(eps))); }

} // namespace

TEST_SUITE("anim/clip")
{
    TEST_CASE("sampling: linear, step and cubic translation; slerp rotation; untracked nodes keep their value; clamped outside")
    {
        as::AnimationData data;
        data.name = "c";
        data.channels.push_back(channel(0, as::AnimationPath::kTranslation, as::Interpolation::kLinear, {0, 1}, {0, 0, 0, 2, 4, 0}));
        data.channels.push_back(channel(1, as::AnimationPath::kScale, as::Interpolation::kStep, {0, 1}, {1, 1, 1, 3, 3, 3}));
        const Quat q90 = glm::angleAxis(kHalfPi, Vec3(0, 0, 1));
        data.channels.push_back(channel(2, as::AnimationPath::kRotation, as::Interpolation::kLinear, {0, 1}, {0, 0, 0, 1, q90.x, q90.y, q90.z, q90.w}));
        // Cubique : in, valeur, out par clé ; 0 → 1 avec tangente de sortie 2 à la première clé.
        data.channels.push_back(
            channel(3, as::AnimationPath::kTranslation, as::Interpolation::kCubicSpline, {0, 1}, {0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0}));
        auto clip = an::Clip::fromData(data, 5);
        REQUIRE(clip.has_value());
        CHECK(clip->duration() == doctest::Approx(1.0f));

        an::Pose pose = zeroPose(5);
        pose.nodes[4].translation = Vec3(7, 7, 7);
        REQUIRE(clip->sample(0.5f, pose).has_value());
        CHECK(same(pose.nodes[0].translation, {1, 2, 0}));
        CHECK(same(pose.nodes[1].scale, {1, 1, 1}));
        const Vec3 rotated = pose.nodes[2].rotation * Vec3(1, 0, 0);
        CHECK(same(rotated, {std::cos(kHalfPi / 2), std::sin(kHalfPi / 2), 0}));
        // Hermite à t = 0,5 : 0,5 × v1 + 0,125 × out0 = 0,5 + 0,25.
        CHECK(pose.nodes[3].translation.x == doctest::Approx(0.75f));
        CHECK(same(pose.nodes[4].translation, {7, 7, 7}));

        REQUIRE(clip->sample(5.0f, pose).has_value());
        CHECK(same(pose.nodes[0].translation, {2, 4, 0}));
        CHECK(same(pose.nodes[1].scale, {3, 3, 3}));
        REQUIRE(clip->sample(-1.0f, pose).has_value());
        CHECK(same(pose.nodes[0].translation, {0, 0, 0}));

        an::Pose wrong = zeroPose(2);
        CHECK((clip->sample(0.0f, wrong).error().code == orosmo::core::ErrorCode::kInvalidArgument));
    }

    TEST_CASE("a channel aimed at a missing node, or with a wrong value count, is refused")
    {
        as::AnimationData data;
        data.channels.push_back(channel(3, as::AnimationPath::kTranslation, as::Interpolation::kLinear, {0, 1}, {0, 0, 0, 1, 1, 1}));
        CHECK((an::Clip::fromData(data, 2).error().code == orosmo::core::ErrorCode::kParse));
        data.channels[0].node = 0;
        data.channels[0].values.pop_back();
        CHECK((an::Clip::fromData(data, 2).error().code == orosmo::core::ErrorCode::kParse));
        as::Model empty;
        CHECK((an::Clip::fromModel(empty, 0).error().code == orosmo::core::ErrorCode::kOutOfRange));
    }

    TEST_CASE("clip time: loop wraps, once stops at the end; events crossed once, including across the wrap and at time 0")
    {
        an::ClipTime t = an::advanceClipTime(0.75f, 0.5f, 1.0f, an::LoopMode::kLoop);
        CHECK(t.time == doctest::Approx(0.25f));
        CHECK(t.wrapped);
        t = an::advanceClipTime(0.75f, 0.5f, 1.0f, an::LoopMode::kOnce);
        CHECK(t.time == doctest::Approx(1.0f));
        CHECK(t.finished);
        CHECK_FALSE(t.wrapped);

        an::Clip clip = moveX(0, 1, 1.0f);
        clip.addEvent(0.5f, "step");
        clip.addEvent(0.0f, "start");
        clip.addEvent(9.0f, "end"); // borné à la durée
        REQUIRE(clip.events().size() == 3);
        CHECK(clip.events().front().name == "start");
        CHECK(clip.events().back().time == doctest::Approx(1.0f));

        std::vector<an::AnimationEvent> out;
        clip.collectEvents(-1.0f, 0.0f, 0, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].name == "start");
        out.clear();
        clip.collectEvents(0.25f, 0.5f, 0, out);
        REQUIRE(out.size() == 1);
        CHECK(out[0].name == "step");
        out.clear();
        clip.collectEvents(0.75f, 0.25f, 1, out); // tour : end puis start
        REQUIRE(out.size() == 2);
        CHECK(out[0].name == "end");
        CHECK(out[1].name == "start");

        // Plusieurs tours : un clip d'1 s depuis 0,25 avancé de 2,25 s franchit 0,75 deux fois.
        const an::ClipTime many = an::advanceClipTime(0.25f, 2.25f, 1.0f, an::LoopMode::kLoop);
        CHECK(many.laps == 2);
        CHECK(many.time == doctest::Approx(0.5f));
        out.clear();
        an::Clip marked = moveX(0, 1, 1.0f);
        marked.addEvent(0.75f, "m");
        marked.collectEvents(0.25f, many.time, many.laps, out);
        CHECK(out.size() == 2);
        // Ordre des franchissements : A(0,3) B(0,75) de 0,25 à 0,5 sur 2 tours -> A B A B A.
        an::Clip ab = moveX(0, 1, 1.0f);
        ab.addEvent(0.75f, "B");
        ab.addEvent(0.3f, "A");
        out.clear();
        ab.collectEvents(0.25f, 0.5f, 2, out);
        REQUIRE(out.size() == 5);
        const char *expected[] = {"A", "B", "A", "B", "A"};
        for (std::size_t i = 0; i < 5; ++i)
        {
            CHECK(out[i].name == expected[i]);
        }
        // Entrées extrêmes : pas de conversion indéfinie, laps plafonné.
        CHECK(an::advanceClipTime(0.0f, 1.0f, 1e-20f, an::LoopMode::kLoop).laps == an::kMaxLaps);
        const float inf = std::numeric_limits<float>::infinity();
        const an::ClipTime nonFinite = an::advanceClipTime(0.0f, inf, 1.0f, an::LoopMode::kLoop);
        CHECK(nonFinite.laps == an::kMaxLaps);
        CHECK(nonFinite.time == 0.0f);
        CHECK(an::advanceClipTime(0.25f, 0.25f, 1.0f, an::LoopMode::kLoop).laps == 0);
        CHECK(an::advanceClipTime(0.75f, 0.5f, 1.0f, an::LoopMode::kLoop).laps == 1);
    }
}

TEST_SUITE("anim/pose")
{
    TEST_CASE("blend, mask and additive layers")
    {
        an::Pose a = zeroPose();
        an::Pose b = zeroPose();
        b.nodes[0].translation = Vec3(4, 0, 0);
        b.nodes[1].translation = Vec3(0, 8, 0);
        b.nodes[1].rotation = glm::angleAxis(kHalfPi, Vec3(0, 1, 0));
        auto mixed = an::blendPoses(a, b, 0.25f);
        REQUIRE(mixed.has_value());
        CHECK(same(mixed->nodes[0].translation, {1, 0, 0}));
        CHECK(glm::angle(mixed->nodes[1].rotation) == doctest::Approx(kHalfPi * 0.25f).epsilon(1e-3));

        const std::vector<float> mask{1.0f, 0.0f};
        auto masked = an::blendPoses(a, b, 1.0f, mask);
        REQUIRE(masked.has_value());
        CHECK(same(masked->nodes[0].translation, {4, 0, 0}));
        CHECK(same(masked->nodes[1].translation, {0, 0, 0}));
        CHECK((an::blendPoses(a, zeroPose(3), 0.5f).error().code == orosmo::core::ErrorCode::kInvalidArgument));

        // Delta de b par rapport à a, ajouté à une autre base : la base plus le mouvement ; poids 0 : base inchangée.
        auto delta = an::additiveDelta(b, a);
        REQUIRE(delta.has_value());
        an::Pose base = zeroPose();
        base.nodes[0].translation = Vec3(0, 0, 10);
        base.nodes[1].scale = Vec3(2.0f);
        auto full = an::applyAdditive(base, *delta, 1.0f);
        REQUIRE(full.has_value());
        CHECK(same(full->nodes[0].translation, {4, 0, 10}));
        CHECK(same(full->nodes[1].rotation * Vec3(1, 0, 0), {0, 0, -1}));
        CHECK(same(full->nodes[1].scale, {2, 2, 2}));
        auto none = an::applyAdditive(base, *delta, 0.0f);
        REQUIRE(none.has_value());
        CHECK(none->nodes[0] == base.nodes[0]);
        auto half = an::applyAdditive(base, *delta, 0.5f, mask);
        REQUIRE(half.has_value());
        CHECK(same(half->nodes[0].translation, {2, 0, 10}));
        CHECK(same(half->nodes[1].translation, {0, 0, 0}));
    }

    TEST_CASE("world matrices compose parent before child, in any node order")
    {
        an::Pose pose = zeroPose(3);
        pose.nodes[0].translation = Vec3(0, 1, 0); // enfant de 2
        pose.nodes[1].translation = Vec3(5, 0, 0); // racine
        pose.nodes[2].translation = Vec3(0, 0, 2); // enfant de 1
        pose.nodes[2].rotation = glm::angleAxis(kHalfPi, Vec3(0, 0, 1));
        const std::vector<int> parents{2, -1, 1};
        auto world = an::worldMatrices(parents, pose);
        REQUIRE(world.has_value());
        CHECK(same(Vec3((*world)[0][3]), {4, 0, 2}));
        CHECK((an::worldMatrices(std::vector<int>{-1}, pose).error().code == orosmo::core::ErrorCode::kInvalidArgument));
    }
}

TEST_SUITE("anim/state machine")
{
    TEST_CASE("conditions start a cross-fade; atEnd returns from a one-shot clip; triggers are consumed; events reported")
    {
        an::Clip idle = moveX(0, 1, 1.0f, 2, "idle");
        an::Clip walk = moveX(10, 10, 1.0f, 2, "walk");
        an::Clip wave = moveX(-1, -1, 0.5f, 2, "wave");
        walk.addEvent(0.0f, "footstep");
        wave.addEvent(0.5f, "waved");

        an::StateMachine sm(zeroPose());
        REQUIRE(sm.addState({"idle", &idle}).has_value());
        REQUIRE(sm.addState({"walk", &walk}).has_value());
        REQUIRE(sm.addState({"wave", &wave, an::LoopMode::kOnce}).has_value());
        CHECK((sm.addState({"idle", &idle}).error().code == orosmo::core::ErrorCode::kAlreadyExists));
        REQUIRE(sm.addTransition({.from = "idle", .to = "walk", .duration = 0.5f, .condition = "moving"}).has_value());
        REQUIRE(sm.addTransition({.from = "", .to = "wave", .duration = 0.0f, .condition = "wave"}).has_value());
        REQUIRE(sm.addTransition({.from = "wave", .to = "idle", .duration = 0.0f, .atEnd = true}).has_value());
        CHECK((sm.addTransition({.from = "idle", .to = "nowhere", .condition = "x"}).error().code == orosmo::core::ErrorCode::kNotFound));
        CHECK((sm.addTransition({.from = "idle", .to = "walk"}).error().code == orosmo::core::ErrorCode::kInvalidArgument));
        CHECK((sm.update(0.1f).error().code == orosmo::core::ErrorCode::kInvalidArgument));

        REQUIRE(sm.start("idle").has_value());
        REQUIRE(sm.update(0.25f).has_value());
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(0.25f));

        sm.setBool("moving", true);
        REQUIRE(sm.update(0.25f).has_value()); // le fondu commence : poids 0, idle à 0,5
        CHECK(sm.current() == "walk");
        CHECK(sm.inTransition());
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(0.5f));
        REQUIRE(sm.update(0.25f).has_value()); // poids 0,5 : idle à 0,75, walk à 10
        CHECK(sm.blendWeight() == doctest::Approx(0.5f));
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(5.375f));
        REQUIRE(sm.update(0.25f).has_value()); // fondu terminé
        CHECK_FALSE(sm.inTransition());
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(10.0f));
        auto events = sm.takeEvents();
        REQUIRE(events.size() == 1);
        CHECK(events[0].state == "walk");
        CHECK(events[0].name == "footstep");

        sm.setTrigger("wave");
        REQUIRE(sm.update(0.1f).has_value());
        CHECK(sm.current() == "wave");
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(-1.0f));
        REQUIRE(sm.update(0.1f).has_value());
        CHECK(sm.current() == "wave"); // le déclencheur a été consommé : pas de nouvelle entrée
        REQUIRE(sm.update(0.5f).has_value()); // fin du clip : retour à idle (coupe nette)
        CHECK(sm.current() == "idle");
        CHECK(sm.pose().nodes[0].translation.x == doctest::Approx(0.0f));
        events = sm.takeEvents();
        REQUIRE(events.size() == 1);
        CHECK(events[0].name == "waved");
    }

    TEST_CASE("deterministic: the same updates give the same pose")
    {
        an::Clip idle = moveX(0, 1, 0.7f);
        auto run = [&]
        {
            an::StateMachine sm(zeroPose());
            REQUIRE(sm.addState({"idle", &idle, an::LoopMode::kLoop, 1.3f}).has_value());
            REQUIRE(sm.start("idle").has_value());
            for (int i = 0; i < 100; ++i)
            {
                REQUIRE(sm.update(1.0f / 60.0f).has_value());
            }
            return sm.pose().nodes[0].translation.x;
        };
        CHECK(run() == run());
    }
}

TEST_SUITE("anim/curves")
{
    TEST_CASE("curves: step, linear, cubic; keys kept sorted; values held outside")
    {
        an::Curve<float> linear;
        linear.addKey(1.0f, 10.0f);
        linear.addKey(0.0f, 0.0f);
        CHECK(linear.evaluate(0.25f) == doctest::Approx(2.5f));
        CHECK(linear.evaluate(-3.0f) == doctest::Approx(0.0f));
        CHECK(linear.evaluate(3.0f) == doctest::Approx(10.0f));
        linear.addKey(1.0f, 20.0f); // remplace
        CHECK(linear.keys().size() == 2);
        CHECK(linear.evaluate(0.5f) == doctest::Approx(10.0f));

        an::Curve<Vec3> step(an::CurveInterpolation::kStep);
        step.addKey(0.0f, Vec3(1));
        step.addKey(1.0f, Vec3(2));
        CHECK(same(step.evaluate(0.99f), Vec3(1)));

        an::Curve<float> cubic(an::CurveInterpolation::kCubic);
        cubic.addKey({0.0f, 0.0f, 0.0f, 2.0f});
        cubic.addKey({1.0f, 1.0f, 0.0f, 0.0f});
        CHECK(cubic.evaluate(0.5f) == doctest::Approx(0.75f));
        CHECK(an::Curve<float>().evaluate(1.0f) == 0.0f);
    }

    TEST_CASE("property animation drives a camera position, a light colour and a material parameter")
    {
        Vec3 cameraPosition(0);
        orosmo::core::Vec4 lightColor(0);
        float roughness = 0.0f;
        an::Curve<Vec3> path;
        path.addKey(0.0f, Vec3(0, 0, 0));
        path.addKey(2.0f, Vec3(0, 0, -4));
        an::Curve<orosmo::core::Vec4> color;
        color.addKey(0.0f, {1, 0, 0, 1});
        color.addKey(1.0f, {0, 0, 1, 1});
        an::Curve<float> rough(an::CurveInterpolation::kStep);
        rough.addKey(0.0f, 0.2f);
        rough.addKey(1.5f, 0.9f);

        an::PropertyAnimation anim;
        REQUIRE(anim.bind<Vec3>("camera.position", path, [&](const Vec3 &v) { cameraPosition = v; }).has_value());
        REQUIRE(anim.bind<orosmo::core::Vec4>("light.color", color, [&](const orosmo::core::Vec4 &v) { lightColor = v; }).has_value());
        REQUIRE(anim.bind<float>("material.roughness", rough, [&](const float &v) { roughness = v; }).has_value());
        CHECK((anim.bind<float>("material.roughness", rough, [](const float &) {}).error().code == orosmo::core::ErrorCode::kAlreadyExists));
        CHECK((anim.bind<float>("empty", rough, {}).error().code == orosmo::core::ErrorCode::kInvalidArgument));
        CHECK(anim.duration() == doctest::Approx(2.0f));

        anim.apply(0.5f);
        CHECK(same(cameraPosition, {0, 0, -1}));
        CHECK(lightColor.r == doctest::Approx(0.5f));
        CHECK(roughness == doctest::Approx(0.2f));
        anim.apply(1.75f);
        CHECK(roughness == doctest::Approx(0.9f));
        anim.unbind("material.roughness");
        CHECK(anim.trackCount() == 2);
    }

    TEST_CASE("PropertyAnimation: a setter may unbind/bind during apply (deferred to the end of the pass)")
    {
        an::Curve<float> c;
        c.addKey(0.0f, 1.0f);
        int calledB = 0;
        an::PropertyAnimation anim;
        REQUIRE(anim.bind<float>("a", c, [&](const float &) {
                    anim.unbind("b");
                    anim.unbind("a");
                    CHECK_FALSE(anim.bind<float>("b", c, [](const float &) {}).has_value()); // "b" encore présent
                    CHECK(anim.bind<float>("c", c, [](const float &) {}).has_value());
                }).has_value());
        REQUIRE(anim.bind<float>("b", c, [&](const float &) { ++calledB; }).has_value());
        anim.apply(0.0f);
        CHECK(calledB == 1); // la piste suivante est évaluée dans la passe en cours
        CHECK(anim.trackCount() == 1);
        anim.unbind("c");
        CHECK(anim.trackCount() == 0);
    }

    TEST_CASE("PropertyAnimation: bind then unbind in a setter cancels the pending bind; an exception restores the state")
    {
        an::Curve<float> c;
        c.addKey(0.0f, 1.0f);
        an::PropertyAnimation anim;
        REQUIRE(anim.bind<float>("a", c, [&](const float &) {
                    (void)anim.bind<float>("n", c, [](const float &) {});
                    anim.unbind("n");
                }).has_value());
        anim.apply(0.0f);
        CHECK(anim.trackCount() == 1);

        an::PropertyAnimation thrower;
        bool first = true;
        REQUIRE(thrower.bind<float>("t", c, [&](const float &) {
                    if (first)
                    {
                        first = false;
                        thrower.unbind("t");
                        throw std::runtime_error("boom");
                    }
                }).has_value());
        CHECK_THROWS(thrower.apply(0.0f));
        CHECK(thrower.trackCount() == 0); // le unbind différé a été appliqué malgré l'exception
        CHECK(thrower.bind<float>("t2", c, [](const float &) {}).has_value());
        CHECK(thrower.trackCount() == 1); // applying_ restauré : bind immédiat
    }
}
