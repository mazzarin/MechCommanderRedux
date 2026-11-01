#include "stdafx.h"
#include "MCTest.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "object/MCEffectSystem.h"
#include "object/MCExplosionType.h"
#include "object/MCFire.h"
#include "object/MCTrain.h"
#include "object/MCTrainManager.h"
#include "object/MCWeaponShotInfo.h"
#include "sprite/MCArmAppearance.h"
#include "sprite/MCInBetween.h"
#include "sprite/MCVfxAppearance.h"

namespace
{
    /// <summary>A mover that records the damage of every hit it takes.</summary>
    class RecordingMover : public MCBigGameObject
    {
    public:
        RecordingMover() { ObjectClass = MCObjectClass::Mover; }

        int32_t CalcHitLocation(std::optional<MCVector3D> attackerPosition, int32_t, int32_t, int32_t) override
        {
            return 0;
        }

        int32_t HandleWeaponHit(MCWeaponShotInfo& shotInfo, int) override
        {
            Hits.push_back(shotInfo.Damage);
            return 0;
        }

        std::vector<float> Hits;
    };

    /// <summary>The damage of a blast of <paramref name="damage"/> in chunks of <paramref name="chunkSize"/>.</summary>
    std::vector<float> BlastHits(float damage, float chunkSize)
    {
        const auto type = std::make_shared<MCExplosionType>();
        type->DamageChunkSize = chunkSize;
        const auto explosion = std::make_shared<MCBigGameObject>();
        explosion->ExplDamage = damage;
        const auto mover = std::make_shared<RecordingMover>();
        type->HandleCollision(*explosion, *mover);
        return mover->Hits;
    }
}

TEST_CASE("fires: starting a fire past the ring's size burns out the oldest")
{
    MCEffectSystem effects(2, 5.0f);
    const auto first = std::make_shared<MCFire>();
    const auto second = std::make_shared<MCFire>();
    const auto third = std::make_shared<MCFire>();
    effects.StartFire(*first);
    effects.StartFire(*second);
    CHECK(!first->BurningOut);
    effects.StartFire(*third);
    CHECK(first->BurningOut);
    CHECK(!second->BurningOut);
    CHECK_EQ(std::ranges::count_if(effects.FiresBurning(),
                                   [&first](const std::weak_ptr<MCFire>& fire) { return fire.lock() == first; }),
             0);

    // A fire that ends leaves its slot empty: the next one finishes nothing.
    effects.EndFire(*second);
    const auto fourth = std::make_shared<MCFire>();
    effects.StartFire(*fourth);
    CHECK(!third->BurningOut);
}

TEST_CASE("fires: burn time added stops short of the mission's longest burn")
{
    MCTestContextScope scope;
    scope.Context().SetEffectSystem(std::make_unique<MCEffectSystem>(4, 5.0f));
    const auto fire = std::make_shared<MCFire>();
    fire->Flames.resize(2);
    fire->Flames[0].TimeLeftToBurn = 1.0f;
    fire->Flames[1].TimeLeftToBurn = 4.5f;
    fire->AddTimeLeftToBurn(2.0f);
    CHECK_EQ(fire->Flames[0].TimeLeftToBurn, 3.0f);
    // 4.5 + 2 would pass 5: unchanged.
    CHECK_EQ(fire->Flames[1].TimeLeftToBurn, 4.5f);
}

TEST_CASE("explosions: a mover takes the blast in whole chunks, with no falloff (OB-153)")
{
    // 10 in chunks of 5: two hits of 5.
    CHECK(BlastHits(10.0f, 5.0f) == std::vector<float>({5.0f, 5.0f}));
    // Original behaviour (OB-153): 12 in chunks of 5 is three hits of 5, 15 in all.
    CHECK(BlastHits(12.0f, 5.0f) == std::vector<float>({5.0f, 5.0f, 5.0f}));
    // A blast smaller than a chunk is one hit of the blast.
    CHECK(BlastHits(3.0f, 5.0f) == std::vector<float>({3.0f}));
    // No damage, no hit.
    CHECK(BlastHits(0.0f, 5.0f).empty());
}

TEST_CASE("trains: a removed train stays alive, and the manager has no train limit")
{
    MCTrainManager manager;
    std::vector<std::shared_ptr<MCTrain>> trains;

    for (int32_t i = 0; i < 100; i++)
    {
        trains.push_back(manager.CreateTrain());
    }

    CHECK_EQ(manager.NumTrains(), 100u);
    manager.RemoveTrain(*trains[10]);
    CHECK(!manager.Holds(*trains[10]));
    CHECK_EQ(manager.NumTrains(), 99u);
    // Still a live train for the cars pointing at it.
    trains[10]->Speed = 3.0f;
    CHECK_EQ(trains[10]->Speed, 3.0f);

    // Faithful: removing a train the list doesn't hold drops the last one instead.
    manager.RemoveTrain(*trains[10]);
    CHECK(!manager.Holds(*trains[99]));
    CHECK(manager.Holds(*trains[98]));
}

namespace
{
    /// <summary>
    /// An appearance of a type no appearance type list holds: it lets the type go before the appearance's destructor
    /// would drop it from the list.
    /// </summary>
    template <typename Appearance> struct Unlisted : Appearance
    {
        ~Unlisted() override { this->AppearType.reset(); }
    };

    /// <summary>Puts FrameLength back as it was when it goes.</summary>
    struct FrameLengthKept
    {
        float Saved = FrameLength;
        ~FrameLengthKept() { FrameLength = Saved; }
    };

    /// <summary>An in-between as (frame, next, step), for comparing.</summary>
    std::optional<std::tuple<int32_t, int32_t, int32_t>> Steps(const std::optional<MCInBetween>& inBetween)
    {
        if (!inBetween.has_value())
        {
            return std::nullopt;
        }

        return std::tuple{inBetween->Frame, inBetween->Next, inBetween->Step};
    }
}

/// <summary>
/// Port-only (HD art's in-betweens, sprite/MCInBetween.h): each frame of an animation is divided into steps up to 60 a
/// second, the frame showing in its first and its in-betweens toward the next frame in the rest. An effect goes from
/// its last frame toward its first only once it has looped (an explosion, which ends there, holds its last), not at a
/// loop's end that goes back elsewhere (LoopStart), nor in a sub-state that doesn't loop; a weapon effect stops at its
/// last.
/// </summary>
TEST_CASE("effects: a frame's in-betweens go toward the next frame, and back to the first once the animation loops")
{
    constexpr float Rate = 15.0f;
    constexpr float SlowRate = 7.0f;
    constexpr int32_t RateSteps = 4;
    CHECK(InBetweenSteps(Rate) == RateSteps);
    CHECK(InBetweenSteps(SlowRate) == 9);
    CHECK(InBetweenSteps(InBetweenRate) == 1);
    CHECK(InBetweenSteps(0.0f) == 1);
    CHECK(InBetweenSteps(1.0f) == MaxInBetweenSteps);
    CHECK(!InBetweenOf(2, 3, 0.2f, Rate).has_value());
    CHECK(Steps(InBetweenOf(2, 3, 0.6f, Rate)) == std::make_tuple(2, 3, 2));
    CHECK(Steps(InBetweenOf(2, 3, 0.99f, Rate)) == std::make_tuple(2, 3, RateSteps - 1));

    constexpr uint32_t Frames = 3;
    constexpr float Halfway = 0.5f;
    const auto halfway = [](int32_t frame, int32_t next) { return std::tuple{frame, next, RateSteps / 2}; };
    auto type = std::make_shared<MCVfxAppearanceType>();
    type->States.resize(ActorStateCount);
    type->States[0].NumFrames = Frames;
    type->States[0].FrameRate = Rate;
    Unlisted<MCVfxAppearance> vfx;
    vfx.Init(type);
    CHECK(!vfx.InBetween().has_value());

    // Played at 60 frames a second, the first frames show their in-betweens toward the next; the last holds until the
    // animation has looped once.
    const FrameLengthKept kept;
    FrameLength = 1.0f / InBetweenRate;
    vfx.Update();
    REQUIRE(vfx.CurrentFrame == 0);
    vfx.CurrentFrame = 1;
    vfx.FrameShare = Halfway;
    CHECK(Steps(vfx.InBetween()) == halfway(1, 2));
    vfx.CurrentFrame = static_cast<int32_t>(Frames) - 1;
    CHECK(!vfx.InBetween().has_value());

    vfx.SetTypeId(MCActorState::Normal, NoSubState);
    bool looped = false;

    for (uint32_t refresh = 0; refresh < Frames * RateSteps * 2 && !looped; ++refresh)
    {
        looped = vfx.Update() == 0;
    }

    REQUIRE(looped);
    CHECK(vfx.Looped);
    vfx.CurrentFrame = static_cast<int32_t>(Frames) - 1;
    vfx.FrameShare = Halfway;
    CHECK(Steps(vfx.InBetween()) == halfway(static_cast<int32_t>(Frames) - 1, 0));

    // A loop that goes back to LoopStart has none at its end.
    vfx.LoopStart = 0;
    vfx.LoopEnd = static_cast<int32_t>(Frames) - 1;
    vfx.CurrentFrame = vfx.LoopEnd - 1;
    CHECK(!vfx.InBetween().has_value());
    vfx.LoopEnd = -1;

    // A sub-state that doesn't loop holds its last frame, looped or not.
    MCActorData sub;
    sub.NumFrames = Frames;
    sub.Loop = false;
    type->SubStates = {sub};
    vfx.CurrentSubState = 0;
    vfx.Looped = true;
    vfx.CurrentFrame = static_cast<int32_t>(Frames) - 1;
    CHECK(!vfx.InBetween().has_value());
    vfx.CurrentFrame = 0;
    CHECK(Steps(vfx.InBetween()) == halfway(0, 1));

    // A weapon effect stops on its last frame.
    auto arm = std::make_shared<MCArmAppearanceType>();
    arm->ActorData.NumFrames = Frames;
    arm->ActorData.FrameRate = Rate;
    Unlisted<MCArmAppearance> weapon;
    weapon.Init(arm);
    CHECK(!weapon.InBetween().has_value());
    weapon.CurrentFrame = 1;
    weapon.FrameShare = Halfway;
    CHECK(Steps(weapon.InBetween()) == halfway(1, 2));
    weapon.CurrentFrame = static_cast<int32_t>(Frames) - 1;
    CHECK(!weapon.InBetween().has_value());
}
