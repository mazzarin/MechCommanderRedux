#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fixtures/MCRegisteredData.h"
#include "ai/MCMovePath.h"
#include "ai/MCTacticalOrder.h"
#include "appear/MCAppearanceTypeList.h"
#include "camera/MCCamera.h"
#include "fakes/MCMemoryFileSource.h"
#include "fixtures/MCRetailData.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCMissionGlobals.h"
#include "object/MCBattleMech.h"
#include "object/MCBattleMechType.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "object/MCMoverMath.h"
#include "object/MCObjectList.h"
#include "object/MCObjectSystem.h"
#include "object/MCTree.h"
#include "object/MCTreeType.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "sprite/MCGVAppearance.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCShape.h"
#include "sprite/MCSpriteManager.h"
#include "sprite/MCSpriteMath.h"
#include "sprite/MCSpriteTree.h"
#include "sprite/MCVfxAppearance.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>Appends <paramref name="value"/> little-endian.</summary>
    void Put32(std::vector<uint8_t>& data, int32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            data.push_back(static_cast<uint8_t>(static_cast<uint32_t>(value) >> (8 * i)));
        }
    }

    /// <summary>A one-frame VFX shape table whose frame's left edge is <paramref name="tag"/>: it names the packet.</summary>
    std::vector<uint8_t> Shape(int32_t tag)
    {
        std::vector<uint8_t> table = {'1', '.', '1', '0'};
        Put32(table, 1);  // count
        Put32(table, 16); // frame 0's offset
        Put32(table, 0);  // no palette
        Put32(table, (1 << 16) | 1);
        Put32(table, 0);
        Put32(table, tag);
        Put32(table, 0);
        Put32(table, tag);
        Put32(table, 0);
        table.insert(table.end(), {3, 1, 0});
        return table;
    }

    /// <summary>The tag of a shape made by <see cref="Shape"/>.</summary>
    int32_t Tag(const std::shared_ptr<MCShape>& shape)
    {
        return VfxShapeMinxy(shape->FrameList(), 0) >> 16;
    }

    /// <summary>
    /// Writes the PAK <paramref name="name"/> of <paramref name="packets"/> (an empty one stores nothing). Written
    /// files go to disk (the memory source's scratch folder), where a PAK's packets open as PAKs of their own, as the
    /// game's loose sprite files do; a file held in memory takes no such children.
    /// </summary>
    void WritePak(std::string_view name, const std::vector<std::vector<uint8_t>>& packets)
    {
        MCPacketFile pak;
        REQUIRE_EQ(pak.Create(name), NO_ERR);
        pak.Reserve(static_cast<int32_t>(packets.size()));

        for (size_t i = 0; i < packets.size(); ++i)
        {
            pak.WritePacket(static_cast<int32_t>(i), packets[i], MCPacketStorage::Raw);
        }
    }

    /// <summary>A PAK of <paramref name="packets"/>, as the bytes of its file.</summary>
    /// <remarks>
    /// The file is written in a scratch folder of its own: callers build PAKs before their test's file source is
    /// installed (as constructor arguments), and the real one would write into the game's folder.
    /// </remarks>
    std::vector<uint8_t> Pak(const std::vector<std::vector<uint8_t>>& packets)
    {
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
        static int32_t made = 0;
        const std::string name = std::format("spritetest{}.pak", made++);
        WritePak(name, packets);
        MCFile file;
        REQUIRE_EQ(file.Open(name), NO_ERR);
        std::vector<uint8_t> bytes(file.FileSize());
        file.Read(bytes);
        return bytes;
    }

    /// <summary>A PAK of the shapes tagged <paramref name="tags"/>.</summary>
    std::vector<uint8_t> ShapePak(std::initializer_list<int32_t> tags)
    {
        std::vector<std::vector<uint8_t>> shapes;

        for (const int32_t tag : tags)
        {
            shapes.push_back(Shape(tag));
        }

        return Pak(shapes);
    }

    /// <summary>The bytes of the FIT text <paramref name="text"/>.</summary>
    std::vector<uint8_t> Fit(std::string_view text)
    {
        return {text.begin(), text.end()};
    }

    /// <summary>
    /// A context with the files a sprite manager opens: the sprite PAKs <c>test90.pak</c> and <c>test.pak</c>, the part
    /// PAKs (one mech, each part's packets tagged by part), the shadows, and the type PAK <c>types.pak</c>.
    /// </summary>
    struct SpriteFiles
    {
        SpriteFiles(std::vector<std::vector<uint8_t>> preferred, std::vector<std::vector<uint8_t>> fallback,
                    std::vector<std::vector<uint8_t>> types)
        {
            // A PAK has at least one packet.
            for (std::vector<std::vector<uint8_t>>& packets :
                 {std::ref(preferred), std::ref(fallback), std::ref(types)})
            {
                if (packets.empty())
                {
                    packets.emplace_back();
                }
            }

            scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
            WritePak("test90.pak", preferred);
            WritePak("test.pak", fallback);
            WritePak("types.pak", types);
            WritePak("shadow.pak", {Shape(900), Shape(901)});

            for (const std::string_view part : {"legs", "torsos", "rArms", "lArms"})
            {
                std::vector<std::vector<uint8_t>> packets;

                for (int32_t i = 0; i < 0x100; ++i)
                {
                    packets.push_back(Shape(1000 * (static_cast<int32_t>(part[0]) % 10) + i));
                }

                WritePak(std::format("{}.pak", part), {Pak(packets)});
            }

            std::expected<std::unique_ptr<MCSpriteManager>, std::string> manager =
                MCSpriteManager::Create("test", false);
            REQUIRE(manager.has_value());
            scope.Context().SetSpriteManager(std::move(*manager));
            std::expected<std::unique_ptr<MCAppearanceTypeList>, std::string> list =
                MCAppearanceTypeList::Create("types");
            REQUIRE(list.has_value());
            scope.Context().SetAppearanceTypeList(std::move(*list));
        }

        ~SpriteFiles() { Turn = savedTurn; }

        MCTestContextScope scope;
        int32_t savedTurn = Turn;
    };

    /// <summary>A VFX type: 5 states, 8 facings each, the normal state mirrored; blowing up 1 has 4 frames, 2 has 3.</summary>
    constexpr std::string_view VfxFit = "FITini\n"
                                        "[Main Info]\n"
                                        "[States]\n"
                                        "uc NumStates = 5\n"
                                        "[State0]\nuc State = 0\nul NumFrames = 1\nf FrameRate = 10.0\n"
                                        "ul BasePacketNumber = 0\nuc NumRotations = 8\nuc Symmetrical = 1\n"
                                        "[State1]\nuc State = 1\nul NumFrames = 4\nf FrameRate = 10.0\n"
                                        "ul BasePacketNumber = 8\nuc NumRotations = 8\nuc Symmetrical = 0\n"
                                        "[State2]\nuc State = 2\nul NumFrames = 1\nf FrameRate = 10.0\n"
                                        "ul BasePacketNumber = 16\nuc NumRotations = 8\nuc Symmetrical = 0\n"
                                        "[State3]\nuc State = 3\nul NumFrames = 3\nf FrameRate = 10.0\n"
                                        "ul BasePacketNumber = 24\nuc NumRotations = 8\nuc Symmetrical = 0\n"
                                        "[State4]\nuc State = 4\nul NumFrames = 1\nf FrameRate = 10.0\n"
                                        "ul BasePacketNumber = 32\nuc NumRotations = 8\nuc Symmetrical = 0\n"
                                        "FITend\n";

    /// <summary>40 shapes tagged by packet number.</summary>
    std::vector<uint8_t> FortyShapes()
    {
        std::vector<std::vector<uint8_t>> shapes;

        for (int32_t i = 0; i < 40; ++i)
        {
            shapes.push_back(Shape(i));
        }

        return Pak(shapes);
    }
}

TEST_CASE("sprites: a shape packet takes its table, after a gesture header if it has one")
{
    const std::vector<uint8_t> table = Shape(7);
    MCRegisteredBlock bare(table.size(), MCDataKind::Shapes);
    std::ranges::copy(table, bare.Bytes().begin());
    const MCAddressKey packetStart = MCAddressKey::OfData(bare.Bytes());
    const MCShape plain(std::move(bare), {}, 3);
    CHECK(MCAddressKey::OfData(plain.FrameList()) == packetStart);
    CHECK_EQ(plain.FrameList().size(), table.size());
    CHECK_EQ(plain.LastTurnUsed, 3);
    CHECK(MCIsRegistered(MCAddressKey::OfData(plain.FrameList())));

    MCRegisteredBlock headed(table.size() + 6, MCDataKind::Shapes);
    std::ranges::copy(table, headed.Bytes().begin() + 6);
    const MCAddressKey headedStart = MCAddressKey::OfData(headed.Bytes());
    const MCShape gesture(std::move(headed), {}, 0);
    CHECK(MCAddressKey::OfData(gesture.FrameList()) == headedStart + 6);
    CHECK_EQ(gesture.FrameList().size(), table.size());

    // A first frame past the end of the packet: no table.
    std::vector<uint8_t> broken = table;
    broken.resize(16);
    MCRegisteredBlock short_(broken.size(), MCDataKind::Shapes);
    std::ranges::copy(broken, short_.Bytes().begin());
    CHECK(MCShape(std::move(short_), {}, 0).FrameList().empty());
}

TEST_CASE("sprites: a registered block is registered with the renderers for its lifetime")
{
    MCAddressKey data;
    {
        MCRegisteredBlock block(64, MCDataKind::Shapes);
        data = MCAddressKey::OfData(block.Bytes());
        REQUIRE(data);
        CHECK(std::ranges::all_of(block.Bytes(), [](uint8_t value) { return value == 0; }));
        const std::optional<MCDataBlock> registered = MCRegisteredBlockOf(data);
        REQUIRE(registered.has_value());
        CHECK(registered->End == data + 64);

        MCRegisteredBlock moved = std::move(block);
        CHECK(block.Empty());
        CHECK(MCAddressKey::OfData(moved.Bytes()) == data);
        CHECK(MCIsRegistered(data));
    }

    CHECK(!MCIsRegistered(data));
    CHECK(MCRegisteredBlock(0, MCDataKind::Shapes).Empty());
}

TEST_CASE("sprites: an appearance's shapes come from the 90-pixel sprite PAK, else from the full-size one")
{
    // Appearance 0 is in the preferred PAK; appearance 1's packet there is empty, so it comes from the other.
    SpriteFiles files({ShapePak({10, 11}), {}}, {ShapePak({20}), ShapePak({30, 31})}, {});
    MCSpriteManager& manager = *SpriteManager();
    CHECK_EQ(manager.NumAppearances(), 3);
    CHECK_EQ(manager.GetNumShapes(0), 2);
    CHECK_EQ(manager.GetNumShapes(1), 2);
    CHECK_EQ(manager.GetNumShapes(2), 0);

    std::shared_ptr<MCShape> shape = manager.GetShapeData(0, 1, 4, {});
    REQUIRE(shape != nullptr);
    CHECK_EQ(Tag(shape), 11);
    CHECK_EQ(shape->LastTurnUsed, 4);
    shape = manager.GetShapeData(1, 0, 4, {});
    REQUIRE(shape != nullptr);
    CHECK_EQ(Tag(shape), 30);

    // A missing packet or appearance gives nothing.
    CHECK(manager.GetShapeData(0, 2, 4, {}) == nullptr);
    CHECK(manager.GetShapeData(7, 0, 4, {}) == nullptr);
    CHECK_EQ(manager.Shapes().size(), 2u);

    // The shadows come with the manager.
    REQUIRE(!manager.MechShadow(1).empty());
    CHECK_EQ(VfxShapeMinxy(manager.MechShadow(1), 0) >> 16, 901);
    CHECK(manager.MechShadow(2).empty());
}

TEST_CASE("sprites: a part shape comes from the mech's packet of that part's PAK")
{
    SpriteFiles files({}, {}, {});
    const std::shared_ptr<MCShape> legs = SpriteManager()->GetMechShapeData(0, 5, MCMechPart::Legs, 1, {}, false);
    REQUIRE(legs != nullptr);
    CHECK_EQ(Tag(legs), 8005); // 'l' % 10 = 8
    const std::shared_ptr<MCShape> torso = SpriteManager()->GetMechShapeData(0, 6, MCMechPart::Torso, 1, {}, true);
    REQUIRE(torso != nullptr);
    CHECK_EQ(Tag(torso), 6006); // 't' % 10 = 6; no 90-pixel parts, so zoomed out gives the same art
    CHECK(SpriteManager()->GetMechShapeData(1, 0, MCMechPart::RightArm, 1, {}, false) == nullptr);
}

TEST_CASE("sprites: a type finds its shapes once, and the cache drops shapes unused for two turns")
{
    SpriteFiles files({FortyShapes()}, {{}}, {Fit(VfxFit)});
    auto type = std::static_pointer_cast<MCVfxAppearanceType>(AppearanceTypeList().GetAppearance(0x02000000));
    REQUIRE(type != nullptr);
    CHECK_EQ(type->ShapeList.size(), 40u);
    CHECK(AppearanceTypeList().GetAppearance(0x02000000) == type);
    CHECK_EQ(type->NumUsers, 2);

    Turn = 10;
    float frameRate = 0.0f;
    bool reverse = false;
    std::shared_ptr<MCShape> shape = type->GetShape(MCActorState::Damaged, NoSubState, 0, frameRate, reverse);
    REQUIRE(shape != nullptr);
    CHECK_EQ(Tag(shape), 16);
    CHECK(shape->Owner.lock() == type);
    CHECK_EQ(SpriteManager()->Shapes().size(), 1u);

    // A hit: the same shape, marked used this turn.
    Turn = 11;
    CHECK(type->GetShape(MCActorState::Damaged, NoSubState, 0, frameRate, reverse) == shape);
    CHECK_EQ(shape->LastTurnUsed, 11);
    CHECK_EQ(SpriteManager()->Shapes().size(), 1u);

    auto appearance = std::make_unique<MCVfxAppearance>();
    appearance->Init(type);
    appearance->CurrentShape = shape;
    shape.reset();

    // Used last turn: kept. Two turns ago: dropped, and the type and its users forget it.
    Turn = 12;
    SpriteManager()->DumpLru();
    CHECK_EQ(SpriteManager()->Shapes().size(), 1u);
    Turn = 13;
    SpriteManager()->DumpLru();
    CHECK(SpriteManager()->Shapes().empty());
    CHECK(type->ShapeList[16].expired());
    CHECK(appearance->CurrentShape.expired());

    // When the type goes, its shapes stay cached without an owner, and the next dump frees them.
    shape = type->GetShape(MCActorState::Normal, NoSubState, 0, frameRate, reverse);
    REQUIRE(shape != nullptr);
    shape.reset();
    appearance.reset();
    CHECK_EQ(AppearanceTypeList().RemoveAppearance(*type), 0);
    CHECK(AppearanceTypeList().Types().empty());
    type.reset();
    REQUIRE_EQ(SpriteManager()->Shapes().size(), 1u);
    CHECK(SpriteManager()->Shapes()[0]->Owner.expired());
    SpriteManager()->DumpLru();
    CHECK(SpriteManager()->Shapes().empty());
}

TEST_CASE("sprites: a VFX state's packet follows the facing, mirrored in a symmetrical state")
{
    SpriteFiles files({FortyShapes()}, {{}}, {Fit(VfxFit)});
    auto type = std::static_pointer_cast<MCVfxAppearanceType>(AppearanceTypeList().GetAppearance(0x02000000));
    REQUIRE(type != nullptr);
    float frameRate = 0.0f;
    bool reverse = true;

    // 8 facings of 45 degrees: 90 degrees is the third.
    CHECK_EQ(Tag(type->GetShape(MCActorState::Normal, NoSubState, 90, frameRate, reverse)), 2);
    CHECK(!reverse);
    CHECK_EQ(frameRate, 10.0f);
    // The normal state is symmetrical: -90 is 90 mirrored.
    CHECK_EQ(Tag(type->GetShape(MCActorState::Normal, NoSubState, -90, frameRate, reverse)), 2);
    CHECK(reverse);
    // The damaged state isn't: -90 is 270, the seventh facing.
    CHECK_EQ(Tag(type->GetShape(MCActorState::Damaged, NoSubState, -90, frameRate, reverse)), 16 + 6);
    CHECK(!reverse);
    // A state the type hasn't gives no shape.
    CHECK(type->GetShape(MCActorState::FallenDamaged, NoSubState, 0, frameRate, reverse) == nullptr);
}

TEST_CASE("sprites: a VFX appearance shows damage as blowing up 1, then 2, then destroyed")
{
    SpriteFiles files({FortyShapes()}, {{}}, {Fit(VfxFit)});
    MCVfxAppearance appearance;
    appearance.Init(AppearanceTypeList().GetAppearance(0x02000000));
    REQUIRE(appearance.AppearType != nullptr);

    // Blowing up 1 has 4 frames, blowing up 2 has 3: each damage level is one of their frames.
    const std::vector<std::tuple<uint32_t, MCActorState, int32_t>> levels = {
        {0, MCActorState::Normal, -1},     {2, MCActorState::BlowingUp1, 2}, {3, MCActorState::BlowingUp1, 3},
        {4, MCActorState::BlowingUp2, 0},  {6, MCActorState::BlowingUp2, 2}, {7, MCActorState::Destroyed, -1},
        {20, MCActorState::Destroyed, -1},
    };

    for (const auto& [level, state, frame] : levels)
    {
        MCTest::Scope scope(std::format("damage level {}", level));
        appearance.SetDamageLvl(level);
        CHECK(appearance.CurrentState == state);
        CHECK_EQ(appearance.CurrentFrame, frame);
        CHECK(appearance.DamageSet);
    }

    CHECK_EQ(appearance.StateExists(MCActorState::BlowingUp2), 3);
    CHECK_EQ(appearance.StateExists(MCActorState::FallenDamaged), 0);
}

/// <summary>
/// OB-187: a VFX state started as under the original's frame lock (StartsAsLocked) takes the lock's 15th of a second for
/// its first step. A state of 15 frames a second then starts on its second frame at any frame rate, as the lock's
/// first step draws it, and plays out when the lock's does (within a frame); started as before, frames faster than the
/// lock's draw its first frame first.
/// </summary>
TEST_CASE("sprites: a VFX state started as under the frame lock takes the lock's frame for its first step (OB-187)")
{
    constexpr std::string_view SlowRate = "f FrameRate = 10.0";
    std::string fit(VfxFit);
    fit.replace(fit.find(SlowRate, fit.find("[State1]")), SlowRate.size(), "f FrameRate = 15.0");
    SpriteFiles files({FortyShapes()}, {{}}, {Fit(fit)});

    struct RestoreFrameLength
    {
        float Saved = FrameLength;
        ~RestoreFrameLength() { FrameLength = Saved; }
    } restore;

    // Blowing up 1 has 4 frames: under the lock it plays out three 15ths after its first step.
    constexpr int32_t BlowingUpFrames = 4;
    constexpr float LockPlayedOut = static_cast<float>(BlowingUpFrames - 1) * LockedFrameLength;
    constexpr float Slack = 1e-4f;
    constexpr int32_t MostSteps = 100;

    for (const float length : {1.0f / 60.0f, 1.0f / 30.0f, LockedFrameLength, 1.0f / 10.0f})
    {
        for (const bool locked : {true, false})
        {
            MCTest::Scope scope(
                std::format("frames of {:.4f} s, {}", length, locked ? "started as locked" : "as before"));
            MCVfxAppearance appearance;
            appearance.Init(AppearanceTypeList().GetAppearance(0x02000000));
            REQUIRE(appearance.AppearType != nullptr);
            appearance.SetTypeId(MCActorState::BlowingUp1, NoSubState);
            appearance.StartsAsLocked = locked;
            FrameLength = length;
            appearance.Update();
            CHECK_EQ(appearance.CurrentFrame, locked || LockedFrameLength <= length ? 1 : 0);

            if (!locked)
            {
                continue;
            }

            // Update gives 0 as the state loops: the step that plays it out.
            int32_t steps = 1;

            while (appearance.Update() != 0)
            {
                ++steps;
                REQUIRE(steps < MostSteps);
            }

            const float playedOut = static_cast<float>(steps) * length;
            CHECK(LockPlayedOut - Slack <= playedOut);
            CHECK(playedOut <= LockPlayedOut + length + Slack);
        }
    }

    // A state set again starts as before unless asked.
    MCVfxAppearance appearance;
    appearance.Init(AppearanceTypeList().GetAppearance(0x02000000));
    appearance.StartsAsLocked = true;
    appearance.SetTypeId(MCActorState::BlowingUp1, NoSubState);
    CHECK(!appearance.StartsAsLocked);
}

/// <summary>
/// OB-187: a tree a mech knocks over starts its fall as under the original's frame lock: with frames of a 30th of a
/// second the first it draws is the fall's second (the expansion's trees' first is empty), as the lock's first step draws.
/// </summary>
TEST_CASE_ISOLATED("game: a knocked-over tree starts its fall as under the original's frame lock (OB-187)")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr float FrameSeconds = 1.0f / 30.0f;
    const std::shared_ptr<MCMover> mech = GetMoverFromPartId(0x200);
    REQUIRE(mech != nullptr);
    std::shared_ptr<MCTree> tree;

    for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
    {
        for (const std::shared_ptr<MCBaseObject> object : *list)
        {
            if (tree == nullptr && object != nullptr && object->ObjectClass == MCObjectClass::Tree)
            {
                tree = std::static_pointer_cast<MCTree>(object);
            }
        }
    }

    REQUIRE(tree != nullptr);
    Eye()->SetPosition(tree->GetPosition());
    MCTestGame::RunFrame(FrameSeconds);
    REQUIRE(!tree->Falling);
    REQUIRE(!tree->Burnt);
    tree->TypeAs<MCTreeType>().HandleCollision(*tree, *mech);
    REQUIRE(tree->Falling);
    MCTestGame::RunFrame(FrameSeconds);
    CHECK(tree->Appearance->CurrentState == MCActorState::BlowingUp1);
    CHECK_EQ(tree->Appearance->CurrentFrame, 1);
}

TEST_CASE(
    "sprites: a ground vehicle's turret follows the body's facings, and only a type with 4 states has the extra one")
{
    constexpr std::string_view gvFit = "FITini\n"
                                       "[Main Info]\nul NumParts = 2\nf TurretOffset = 1.5\n"
                                       "[States]\nuc NumStates = 3\n"
                                       "[State0]\nuc State = 0\nul NumFrames = 1\nf FrameRate = 10.0\n"
                                       "ul BasePacketNumber = 0\nuc NumRotations = 8\n"
                                       "[State1]\nuc State = 1\nul NumFrames = 1\nf FrameRate = 10.0\n"
                                       "ul BasePacketNumber = 16\nuc NumRotations = 8\n"
                                       "[State2]\nuc State = 2\nul NumFrames = 2\nf FrameRate = 10.0\n"
                                       "ul BasePacketNumber = 32\nuc NumRotations = 4\n"
                                       "FITend\n";
    SpriteFiles files({FortyShapes()}, {{}}, {Fit(gvFit)});
    auto type = std::static_pointer_cast<MCGVAppearanceType>(AppearanceTypeList().GetAppearance(0x05000000));
    REQUIRE(type != nullptr);
    CHECK(!type->HasExtraState);
    float frameRate = 0.0f;

    // Body packets 0..7, the turret's 8..15; 135 degrees is the fourth facing, -45 is 315 (the eighth).
    CHECK_EQ(Tag(type->GetShape(MCGVActorState::Normal, 135, 0, frameRate)), 3);
    CHECK_EQ(Tag(type->GetShape(MCGVActorState::Normal, 135, 1, frameRate)), 8 + 3);
    CHECK_EQ(Tag(type->GetShape(MCGVActorState::Damaged, -45, 1, frameRate)), 16 + 8 + 7);
    CHECK(type->GetShape(MCGVActorState::Normal, 0, 2, frameRate) == nullptr);

    MCGVAppearance appearance;
    appearance.Init(type);
    CHECK_EQ(appearance.NumParts, 2);
    appearance.SetTypeId(MCGVActorState::Destroyed);
    CHECK(appearance.CurrentState == MCGVActorState::Destroyed);
    CHECK_EQ(appearance.StateExists(MCGVActorState::Destroyed), 2);
    // No extra state: the state stays.
    appearance.SetTypeId(MCGVActorState::Extra);
    CHECK(appearance.CurrentState == MCGVActorState::Destroyed);
    CHECK_EQ(appearance.StateExists(MCGVActorState::Extra), 0);
}

namespace
{
    /// <summary>
    /// A mech tree: gesture 2 (stand) with 8 leg facings and 16 upper-body ones, symmetrical; gesture 14 (a fall) with
    /// 32 facings, not symmetrical. Parts in its own PAK files 0.
    /// </summary>
    std::string MechFit(bool withTable)
    {
        std::string fit = "FITini\n"
                          "[Main Info]\nst Name = \"Test Mech\"\nul legFileNumber = 0\nul torsoFileNumber = 0\n"
                          "ul rightArmFileNumber = 0\nul leftArmFileNumber = 0\n"
                          "[Parts]\nuc NumParts = 4\n"
                          "[SpecialInfo]\nf fb_d_xlat = 0.0\nul jumpAirborne = 0\nul jumpHold = 0\n"
                          "f jumpStartLandTime = 0.0\nf jumpMaxDistance = 0.0\nf jumpGravity = 0.0\n"
                          "f jumpStartVel = 0.0\nul r_fb_w_fb_frame = 1\nul r_ff_w_ff_frame = 2\n"
                          "ul s_fb_w_fb_frame = 3\nul s_ff_w_ff_frame = 4\nul walk_to_w_r_frame = 5\n"
                          "ul run_to_r_w_frame = 6\n";

        if (withTable)
        {
            fit += "[TransitionTable]\nc[3] TransitionArray = 7, 8, 9\n";
        }

        fit += "[Gestures]\nuc NumGestures = 15\n";

        for (int32_t i = 0; i < 15; ++i)
        {
            const bool fall = i == 14;
            fit += std::format("[Gestures{}]\nuc State = 0\nul NumFrames = {}\nf FrameRate = {}\n"
                               "ul BasePacketNumber = 0\nuc[4] NumRotations = {}\nuc Symmetrical = {}\n"
                               "uc ArmSymmetrical = {}\nuc ForwardResult = 0\nuc ReverseResult = 0\n"
                               "f StartVelocity = 0.0\nf EndVelocity = 0.0\n",
                               i, i == 2 || fall ? 10 : 0, fall ? "-15.0" : "15.0",
                               fall ? "32, 32, 32, 32" : "8, 16, 16, 16", fall ? 0 : 1, fall ? 0 : 1);
        }

        return fit + "FITend\n";
    }
}

TEST_CASE("sprites: a mech part's packet follows its gesture and facing, mirrored where the gesture is symmetrical")
{
    SpriteFiles files({}, {}, {Fit(MechFit(false))});
    auto tree = std::static_pointer_cast<MCSpriteTree>(AppearanceTypeList().GetAppearance(0x01000000));
    REQUIRE(tree != nullptr);
    CHECK_EQ(tree->NumGestures, 15);
    CHECK(!tree->TransitionArray.has_value());
    CHECK_EQ(tree->SpecialInfo.WalkToWSFrame, NoFrame);

    // Stand (gesture 2, the third gesture stored): the legs have 9 stored facings, the mirrored upper body 17.
    std::optional<MCGesturePacket> where = tree->GesturePacket(2, 0, 90.0f);
    REQUIRE(where.has_value());
    CHECK(where->FilePart == MCMechPart::Legs);
    CHECK_EQ(where->Packet, 2u * 9 + 2);
    CHECK_EQ(where->ListIndex, PartShapeStart[MCMechPart::Legs] + 2 * 9 + 2);
    CHECK(!where->Reverse);
    where = tree->GesturePacket(2, 0, -90.0f);
    CHECK_EQ(where->Packet, 2u * 9 + 2);
    CHECK(where->Reverse);
    // Straight ahead is never mirrored.
    CHECK(!tree->GesturePacket(2, 0, -1.0f)->Reverse);

    // The tree's parts 2 and 3 are the left and the right arm's sprites.
    where = tree->GesturePacket(2, 2, 45.0f);
    CHECK(where->FilePart == MCMechPart::LeftArm);
    CHECK_EQ(where->Packet, 2u * 17 + 2);
    CHECK_EQ(where->ListIndex, PartShapeStart[MCMechPart::LeftArm] + 2 * 17 + 2);
    CHECK(tree->GesturePacket(2, 3, 45.0f)->FilePart == MCMechPart::RightArm);

    // A fall (gesture 14, the 13th stored) has only every fourth facing, unmirrored: 100 degrees is facing 9, drawn as 8.
    where = tree->GesturePacket(14, 1, 100.0f);
    CHECK_EQ(where->Packet, 12u * 32 + 8);
    where = tree->GesturePacket(14, 1, -100.0f);
    CHECK_EQ(where->Packet, 12u * 32 + 24);
    CHECK(!where->Reverse);

    // The way the drawn facing points: its first angle, mirrored with the sprite.
    int32_t reverse = 0;
    float frameRate = 0.0f;
    std::optional<MCDrawnFacing> drawn = tree->DrawnFacing(2, 0, 90.0f);
    REQUIRE(drawn.has_value());
    CHECK_EQ(drawn->Degrees, 80.0f);
    CHECK(!drawn->Mirrored);
    drawn = tree->DrawnFacing(2, 0, -90.0f);
    CHECK_EQ(drawn->Degrees, -80.0f);
    CHECK(drawn->Mirrored);
    // Just left of straight ahead: drawn as straight ahead, though the frame's mirroring (SetGesture's) mirrors it.
    drawn = tree->DrawnFacing(2, 0, -1.0f);
    CHECK_EQ(drawn->Degrees, 0.0f);
    CHECK(!drawn->Mirrored);
    tree->SetGesture(2, 0, -1.0f, reverse, frameRate);
    CHECK_EQ(reverse, 1);
    CHECK_EQ(tree->DrawnFacing(14, 1, 100.0f)->Degrees, 8 * 360.0f / 33.0f);

    // Turn middles (the legs' 9 facings round, 40 degrees apart): none in a facing's first half; in its second, halfway
    // to the next facing, mirrored on the other side; from straight ahead on the other side, halfway to the next facing
    // mirrored; on the other side of straight back, straight back.
    constexpr float Apart = 40.0f;
    CHECK(!tree->TurnMiddle(2, 10.0f).has_value());
    std::optional<MCTurnMiddle> middle = tree->TurnMiddle(2, 25.0f);
    REQUIRE(middle.has_value());
    CHECK_EQ(middle->Step, TurnStep);
    CHECK_EQ(middle->Toward, 1);
    CHECK(!middle->TowardMirrored);
    CHECK_EQ(middle->Degrees, Apart / 2);
    where = tree->GesturePacket(2, 0, middle->TowardRotation);
    CHECK_EQ(where->Packet, 2u * 9 + 1);
    CHECK(!where->Reverse);
    middle = tree->TurnMiddle(2, -25.0f);
    REQUIRE(middle.has_value());
    CHECK_EQ(middle->Step, TurnMirroredStep);
    CHECK_EQ(middle->Toward, 1);
    CHECK(middle->TowardMirrored);
    CHECK_EQ(middle->Degrees, -Apart / 2);
    middle = tree->TurnMiddle(2, -70.0f);
    REQUIRE(middle.has_value());
    CHECK_EQ(middle->Step, TurnStep);
    CHECK_EQ(middle->Toward, 2);
    CHECK(!middle->TowardMirrored);
    CHECK_EQ(middle->Degrees, -1.5f * Apart);
    CHECK(!tree->TurnMiddle(2, 165.0f).has_value());
    middle = tree->TurnMiddle(2, -165.0f);
    REQUIRE(middle.has_value());
    CHECK_EQ(middle->Step, TurnStep);
    CHECK_EQ(middle->Toward, 4);
    CHECK(middle->TowardMirrored);
    CHECK_EQ(middle->Degrees, 180.0f);
    // A fall has every fourth facing only; a gesture without frames, none.
    CHECK(!tree->TurnMiddle(14, 25.0f).has_value());
    CHECK(!tree->TurnMiddle(3, 25.0f).has_value());
    CHECK(MCStrideGesture(4));
    CHECK(!MCStrideGesture(2));

    // A gesture without frames has no shapes; the frame rate is made positive.
    CHECK(!tree->GesturePacket(3, 0, 0.0f).has_value());
    CHECK(!tree->DrawnFacing(3, 0, 0.0f).has_value());
    tree->SetGesture(14, 1, 10.0f, reverse, frameRate);
    CHECK_EQ(frameRate, 15.0f);

    // The shape is the mech's packet of that part PAK.
    const std::shared_ptr<MCShape> shape = tree->GetGesture(2, 0, 90.0f, reverse, frameRate, false);
    REQUIRE(shape != nullptr);
    CHECK_EQ(Tag(shape), 8000 + 2 * 9 + 2);
    CHECK(tree->GetGesture(2, 0, 90.0f, reverse, frameRate, false) == shape);
}

TEST_CASE("sprites: a mech's drawn facing eases toward its own the short way round, never lagging too far")
{
    constexpr float Step = 9.0f;
    constexpr float Lag = 90.0f;
    // Short of the step, it arrives; else it turns a step, across straight back if that is the short way.
    CHECK_EQ(MCEaseFacing(0.0f, 5.0f, Step, Lag), 5.0f);
    CHECK_EQ(MCEaseFacing(10.0f, 0.0f, Step, Lag), 1.0f);
    CHECK_EQ(MCEaseFacing(170.0f, -170.0f, Step, Lag), 179.0f);
    CHECK_EQ(MCEaseFacing(-175.0f, 175.0f, Step, Lag), 176.0f);
    // A turn too far to follow leaves it the lag behind.
    CHECK_EQ(MCEaseFacing(0.0f, 150.0f, Step, Lag), 60.0f);
    CHECK_EQ(MCEaseFacing(0.0f, -150.0f, Step, Lag), -60.0f);
}

TEST_CASE("sprites: a mech's transitions come from its own table when it has one")
{
    SpriteFiles files({}, {}, {Fit(MechFit(true))});
    auto withTable = std::static_pointer_cast<MCSpriteTree>(AppearanceTypeList().GetAppearance(0x01000000));
    REQUIRE(withTable != nullptr);
    REQUIRE(withTable->TransitionArray.has_value());
    CHECK_EQ(withTable->Transition(1), 8);
    CHECK_EQ(withTable->Transition(3), 0);

    // The default table: from standing (state 1) to walking (goal 2), stand then walk.
    MCSpriteTree plain;
    const int32_t row = (2 + 1 * 9) * 10;
    CHECK_EQ(plain.Transition(row), 3);
    CHECK_EQ(plain.Transition(row + 1), 4);
    CHECK_EQ(plain.Transition(row + 2), -1);
}

TEST_CASE("sprites: a stride fitted to a stop plays its frames over the room left, its rate nearest 1 within bounds")
{
    constexpr int32_t StrideFrames = 30;
    constexpr float MetresPerFrame = 0.5f;
    constexpr float Close = 0.0001f;

    // 10 frames left of the stride over 5 m: as drawn, the stop starts at the stride's end.
    std::optional<MCStridePlan> plan = MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 5.0f);
    REQUIRE(plan.has_value());
    CHECK(std::fabs(plan->Rate - 1.0f) < Close);
    CHECK_EQ(plan->Strides, 0);

    // Over 20 m: a whole stride more first.
    plan = MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 20.0f);
    REQUIRE(plan.has_value());
    CHECK(std::fabs(plan->Rate - 1.0f) < Close);
    CHECK_EQ(plan->Strides, 1);

    // Over 4.5 m: the stride played a ninth faster (its feet slide a little).
    plan = MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 4.5f);
    REQUIRE(plan.has_value());
    CHECK(std::fabs(plan->Rate - 10.0f / 9.0f) < Close);
    CHECK_EQ(plan->Strides, 0);

    // Over 2 m the stride would play 2.5 times as fast, over 11.5 m too slow or, a stride more, too fast: no fit.
    CHECK(!MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 2.0f).has_value());
    CHECK(!MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 11.5f).has_value());
    CHECK(!MCPlanStride(10.0f, StrideFrames, MetresPerFrame, 0.0f).has_value());
}

TEST_CASE("sprites: braking, the expansion's stops slow down (OB-186), and a stop's metres follow its eased speed")
{
    // The Turkina's walk, walk to stand and run to walk, which keep their start speed to the end.
    MCSpriteTree tree;
    constexpr int32_t Gestures = 9;
    tree.NumGestures = Gestures;
    tree.Gestures.resize(Gestures);
    tree.Gestures[MCStopGesture::Walk] = {
        .NumFrames = 30, .FrameRate = 15.0f, .StartVelocity = 7.5f, .EndVelocity = 7.5f};
    tree.Gestures[MCStopGesture::WalkToStand] = {
        .NumFrames = 14, .FrameRate = 15.0f, .StartVelocity = 7.5f, .EndVelocity = 7.5f};
    tree.Gestures[MCStopGesture::RunToWalk] = {
        .NumFrames = 6, .FrameRate = 15.0f, .StartVelocity = 15.0f, .EndVelocity = 15.0f};
    constexpr float Close = 0.0001f;

    const auto velocities = [&](int32_t gesture, bool braking)
    {
        const MCGestureVelocities v = MCStopVelocities(tree, gesture, braking);
        return std::pair(v.Start, v.End);
    };

    CHECK(velocities(MCStopGesture::WalkToStand, false) == std::pair(7.5f, 7.5f));
    CHECK(velocities(MCStopGesture::WalkToStand, true) == std::pair(7.5f, 0.0f));
    CHECK(velocities(MCStopGesture::RunToWalk, true) == std::pair(15.0f, 7.5f));
    CHECK(velocities(MCStopGesture::Walk, true) == std::pair(7.5f, 7.5f));

    // Half way through the walk to stand, half the speed; braking it covers 0.5 m a frame less a fourteenth each frame.
    CHECK(std::fabs(MCFrameSpeed({7.5f, 0.0f}, 14, 7) - 3.75f) < Close);
    CHECK(std::fabs(MCGestureMetres(tree, MCStopGesture::WalkToStand, 0.0f, true) - 3.75f) < Close);
    CHECK(std::fabs(MCGestureMetres(tree, MCStopGesture::WalkToStand, 0.0f, false) - 7.0f) < Close);
    CHECK(std::fabs(MCGestureMetres(tree, MCStopGesture::Walk, 10.5f, true) - 9.75f) < Close);

    // Without the art drawn (no part shapes here), braking keeps the tree's own transitions: a walk stands at once.
    const int32_t walkToStand = MCStopTransition(tree, MCStopState::Stand, MCStopState::Walk, 0, true);
    CHECK_EQ(walkToStand, tree.Transition((MCStopState::Stand + MCStopState::Walk * 9) * 10));
    CHECK_EQ(walkToStand, MCStopGesture::Stand);
}

TEST_CASE("sprites: a jump leaves the ground at the bottom of its crouch and lands at the top of its climb")
{
    // Down for two frames, up for three, then down again.
    const std::array<float, 8> heights = {0.0f, -1.0f, -2.0f, -1.0f, 1.0f, 2.0f, 1.5f, 0.0f};
    const MCJumpFrames frames = MCFindJumpFrames(heights);
    CHECK_EQ(frames.LiftOff, 3);
    CHECK_EQ(frames.TouchDown, 6);

    // No crouch: it never leaves the ground.
    const std::array<float, 3> flat = {0.0f, 1.0f, 2.0f};
    CHECK_EQ(MCFindJumpFrames(flat).LiftOff, 0);
    CHECK_EQ(MCFindJumpFrames(flat).TouchDown, 0);
}

/// The .jmp file holds each jet's frames in a row the file's frames long, and the jump gesture's extra last frame
/// (OB-175) keeps the jets where the file's last frame put them.
TEST_CASE("sprites: a jump jet's offset comes from its own row of the jump file, held past the file's last frame")
{
    MCBattleMechType type;
    // Two jets of three frames: x is 10 * jet + frame, y and z mark the jet.
    type.JumpData = {0.0f,  1.0f, 2.0f, 1.0f,  1.0f, 2.0f, 2.0f,  1.0f, 2.0f,
                     10.0f, 3.0f, 4.0f, 11.0f, 3.0f, 4.0f, 12.0f, 3.0f, 4.0f};

    CHECK_EQ(type.JumpJetOffset(0, 0).X, 0.0f);
    CHECK_EQ(type.JumpJetOffset(0, 2).X, 2.0f);
    CHECK_EQ(type.JumpJetOffset(1, 0).X, 10.0f);
    CHECK_EQ(type.JumpJetOffset(1, 1).Y, 3.0f);
    CHECK_EQ(type.JumpJetOffset(1, 2).Z, 4.0f);

    // The gesture's frame past the file holds the last one, for either jet.
    CHECK_EQ(type.JumpJetOffset(0, 3).X, 2.0f);
    CHECK_EQ(type.JumpJetOffset(1, 3).X, 12.0f);

    // No file: no offset.
    MCBattleMechType empty;
    CHECK_EQ(empty.JumpJetOffset(1, 5).X, 0.0f);
}

TEST_CASE("sprites: the shadow facing is one of 32, wrapped and clamped")
{
    CHECK_EQ(CalcRotation(0.0f, 0x20), 0);
    CHECK_EQ(CalcRotation(90.0f, 0x20), 8);
    // -90 is 270: 33 * 270 / 360 = 24.75.
    CHECK_EQ(CalcRotation(-90.0f, 0x20), 24);
    CHECK_EQ(CalcRotation(359.0f, 0x20), 31);
}

namespace
{
    /// <summary>What one mech part drew: its shape (numbered in the order first seen), frame and mirroring.</summary>
    struct DrawnPart
    {
        int32_t Shape = -1;
        int32_t Frame = 0;
        int32_t Reverse = 0;

        bool operator==(const DrawnPart&) const = default;
    };

    /// <summary>Numbers each shape the first time it is seen, so a log can tell shapes apart.</summary>
    struct ShapeNumbers
    {
        int32_t Number(const std::weak_ptr<MCShape>& shape)
        {
            const std::shared_ptr<MCShape> pinned = shape.lock();

            if (pinned == nullptr)
            {
                return -1;
            }

            const auto found = std::ranges::find(_Seen, pinned.get(),
                                                 [](const std::weak_ptr<MCShape>& seen) { return seen.lock().get(); });

            if (found != _Seen.end())
            {
                return static_cast<int32_t>(found - _Seen.begin());
            }

            _Seen.push_back(pinned);
            return static_cast<int32_t>(_Seen.size()) - 1;
        }

        std::vector<std::weak_ptr<MCShape>> _Seen;
    };
}

/// <summary>
/// Diagnostic for a report of a mech drawing one arm (or an arm on the wrong side) while it faces down the screen. The
/// mech of mission 1 is given the tree of --mech &lt;name&gt; (default "madcat"), turned through every facing standing
/// and walking, and each frame's arms are logged; two arms drawing the same shape, frame and mirroring draw one arm.
/// </summary>
TEST_CASE_ISOLATED("game: a mech draws both arms at every facing, standing and walking")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // --gpudraw off|on|mirror: who draws the frame surfaces (the shots are the CPU's picture, so "on" leaves out what
    // only the GPU drew).
    if (const std::optional<std::string_view> drawing = MCTest::Option("gpudraw"); drawing.has_value())
    {
        const std::optional<MCGpuDrawing> kind = MCGpuDrawingFromName(*drawing);
        REQUIRE(kind.has_value());
        MCRenderer::RequestGpuDrawing(*kind);
    }

    REQUIRE(MCTestGame::StartMission(1));

    // --window <width>x<height>: the (hidden) window's size, which the mission's screen follows.
    if (const std::optional<std::string_view> window = MCTest::Option("window"); window.has_value())
    {
        int32_t width = 0;
        int32_t height = 0;
        const size_t x = window->find('x');
        REQUIRE(x != std::string_view::npos);
        std::from_chars(window->data(), window->data() + x, width);
        std::from_chars(window->data() + x + 1, window->data() + window->size(), height);
        REQUIRE(SDL_SetWindowSize(&MCInput::Display()->Window(), width, height));
        SDL_SyncWindow(&MCInput::Display()->Window());

        for (int32_t frame = 0; frame < 4; ++frame)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    std::cout << std::format("  renderer {}, gpu drawing {}, screen {}x{}\n", MCInput::Display()->RendererName(),
                             static_cast<int32_t>(MCRenderer::GpuDrawing()), MCInput::Display()->Screen().XMax + 1,
                             MCInput::Display()->Screen().YMax + 1);
    // One of the player's mechs, which takes move orders.
    const std::shared_ptr<MCMover> mover = GetMoverFromPartId(0x200);
    REQUIRE(mover != nullptr);
    REQUIRE(mover->ObjectClass == MCObjectClass::BattleMech);
    const auto mech = std::static_pointer_cast<MCBattleMech>(mover);
    const auto actor = std::static_pointer_cast<MCMechActor>(mech->Appearance);
    REQUIRE(actor != nullptr);

    const std::string treeName(MCTest::Option("mech").value_or("madcat"));
    const std::shared_ptr<MCSpriteTree> tree = MCTestGame::FindMechTree(treeName);
    REQUIRE(tree != nullptr);
    tree->PreloadGestures();
    REQUIRE_EQ(actor->Init(tree, mech), 0);
    Eye()->ChangeTarget(mech, true);

    ShapeNumbers numbers;
    int32_t oneArm = 0;

    // Runs a frame and logs (and checks) what each part drew.
    const auto step = [&](std::string_view what, float aim, std::optional<std::string> shotName)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
        MCMechParts<DrawnPart> drawn;

        for (int32_t part = 0; part < MechPartCount; ++part)
        {
            drawn[part] = {numbers.Number(actor->PartShape[part]), actor->CurrentFrame[part], actor->Reverse[part]};
        }

        const DrawnPart& right = drawn[MCMechPart::RightArm];
        const DrawnPart& left = drawn[MCMechPart::LeftArm];
        const bool same = right.Shape >= 0 && right == left;
        const bool missing = (right.Shape < 0 && !actor->RightArmGone) || (left.Shape < 0 && !actor->LeftArmGone);

        if (same || missing)
        {
            oneArm++;
        }

        std::cout << std::format(
            "  {:8} {:7.2f} actor {:8.3f} torso {:6.2f} g{:2} | legs s{:3} f{:2} r{} | torso s{:3} f{:2} r{} | "
            "rarm s{:3} f{:2} r{} | larm s{:3} f{:2} r{} | order {}{}{}{}{}\n",
            what, aim, MCActorFacing(*mech), mech->TorsoRotation, actor->CurrentGesture, drawn[MCMechPart::Legs].Shape,
            drawn[MCMechPart::Legs].Frame, drawn[MCMechPart::Legs].Reverse, drawn[MCMechPart::Torso].Shape,
            drawn[MCMechPart::Torso].Frame, drawn[MCMechPart::Torso].Reverse, right.Shape, right.Frame, right.Reverse,
            left.Shape, left.Frame, left.Reverse, static_cast<int32_t>(actor->PartOrder[0]),
            static_cast<int32_t>(actor->PartOrder[1]), static_cast<int32_t>(actor->PartOrder[2]),
            static_cast<int32_t>(actor->PartOrder[3]),
            same ? "  <-- one arm (both the same)" : (missing ? "  <-- an arm has no shape" : ""));

        if (shotName.has_value())
        {
            MCScreenInput::SaveShot(*shotName, MCScreenInput::ScreenHash());
            MCScreenInput::SaveShownShot(*shotName + "_shown");
        }
    };

    // Standing, turned through every facing (0 faces the camera, 180 away from it).
    for (int32_t tenths = -1800; tenths < 1800; tenths += 25)
    {
        const float facing = static_cast<float>(tenths) / 10.0f;
        MCTest::Scope scope(std::format("standing at {}", facing));

        for (int32_t frame = 0; frame < 2; ++frame)
        {
            MCTestGame::FaceMech(*mech, facing);
            step("standing", facing, std::nullopt);
        }
    }

    // --arm-gone right|left: the walks are made with that arm blown off (as BattleMech::update marks it).
    if (const std::optional<std::string_view> gone = MCTest::Option("arm-gone"); gone.has_value())
    {
        actor->RightArmGone = *gone == "right";
        actor->LeftArmGone = *gone == "left";
    }

    // Walking, ordered 500 units each way.
    const std::shared_ptr<MCMechWarrior> pilot = mech->GetPilot();
    REQUIRE(pilot != nullptr);

    for (const float direction : {0.0f, 180.0f, 90.0f, -90.0f, 45.0f, -45.0f, 5.0f, -5.0f})
    {
        MCTest::Scope scope(std::format("walking toward {}", direction));
        const MCVector3D start = mech->GetPosition();
        const double radians = static_cast<double>(direction) * std::numbers::pi / 180.0;
        const MCVector3D goal(start.X + static_cast<float>(500.0 * std::cos(radians)),
                              start.Y + static_cast<float>(500.0 * std::sin(radians)), start.Z);
        pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, goal, -1, 0);

        for (int32_t frame = 0; frame < 90; ++frame)
        {
            // Shots while it faces within 10 degrees of the camera, where the arms swap sprites.
            std::optional<std::string> shotName;

            if (std::abs(MCActorFacing(*mech)) < 10.0)
            {
                shotName = std::format("walk_{:+04.0f}_{:02}_facing_{:+06.2f}", direction, frame, MCActorFacing(*mech));
            }

            step("walking", direction, shotName);
        }
    }

    std::cout << std::format("  {} frames drew one arm\n", oneArm);
    CHECK_EQ(oneArm, 0);
}

namespace
{
    /// <summary>Points <c>SpritePath</c> at <paramref name="path"/> for its lifetime.</summary>
    struct SpritePathScope
    {
        explicit SpritePathScope(std::string_view path) : _Saved(SpritePath) { SpritePath = path; }

        ~SpritePathScope() { SpritePath = _Saved; }

        std::string _Saved;
    };
}

TEST_CASE("game: every mech sprite tree in the retail sprite PAK loads and finds its standing shapes")
{
    std::unique_ptr<MCMemoryFileSource> data =
        MCRetailData::Load({"data\\sprites\\sprites.pak", "data\\sprites\\shapes.pak", "data\\sprites\\shapes90.pak",
                            "data\\sprites\\legs.pak", "data\\sprites\\torsos.pak", "data\\sprites\\rarms.pak",
                            "data\\sprites\\larms.pak", "data\\sprites\\shadow.pak"});

    if (data == nullptr)
    {
        return;
    }

    // The sprite files are loose on disk in the install, where their packets open as PAKs: write them out.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> images;

    for (const std::string_view name : {"sprites", "shapes", "shapes90", "legs", "torsos", "rarms", "larms", "shadow"})
    {
        const std::string gamePath = std::format("data\\sprites\\{}.pak", name);
        const std::optional<std::span<const uint8_t>> image = data->FindImage(gamePath);
        REQUIRE(image.has_value());
        images.emplace_back(gamePath, std::vector<uint8_t>(image->begin(), image->end()));
    }

    MCTestContextScope scope;
    scope.Context().SetFiles(std::move(data));

    for (const auto& [gamePath, bytes] : images)
    {
        MCFile file;
        REQUIRE_EQ(file.Create(gamePath), NO_ERR);
        file.Write(bytes);
    }

    const SpritePathScope spritePath("data\\sprites\\");
    std::expected<std::unique_ptr<MCSpriteManager>, std::string> manager = MCSpriteManager::Create("shapes", false);
    std::expected<std::unique_ptr<MCAppearanceTypeList>, std::string> list = MCAppearanceTypeList::Create("sprites");
    REQUIRE(manager.has_value());
    REQUIRE(list.has_value());

    scope.Context().SetSpriteManager(std::move(*manager));
    scope.Context().SetAppearanceTypeList(std::move(*list));
    MCPacketFile pak;
    REQUIRE_EQ(pak.Open("data\\sprites\\sprites.pak"), NO_ERR);
    int32_t trees = 0;

    for (int32_t packet = 0; packet < pak.GetNumPackets(); ++packet)
    {
        // A tree is the packet whose FIT has a "Parts" block.
        MCFitIniFile fit;

        if (pak.SeekPacket(packet) != 0 || pak.GetPacketSize() == 0 ||
            fit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())) != 0 || fit.SeekBlock("Parts") != 0)
        {
            continue;
        }

        MCTest::Scope entry(std::format("packet {}", packet));
        auto tree = std::static_pointer_cast<MCSpriteTree>(AppearanceTypeList().GetAppearance(0x01000000u | packet));
        REQUIRE(tree != nullptr);
        CHECK(!tree->Gestures.empty());

        for (int32_t part = 0; part < MechPartCount; ++part)
        {
            for (float facing = -180.0f; facing < 180.0f; facing += 22.5f)
            {
                int32_t reverse = 0;
                float frameRate = 0.0f;
                CHECK(tree->GetGesture(2, part, facing, reverse, frameRate, true) != nullptr);
            }
        }

        trees++;
    }

    std::cout << std::format("  {} mech trees\n", trees);
    CHECK(trees > 0);
}

namespace
{
    /// <summary>A frame of a mech's move to a halt: its gesture, legs frame, speed, where it is and each part's frame.</summary>
    struct StopFrame
    {
        int32_t Gesture = 0;
        int32_t Frame = 0;
        float Speed = 0.0f;
        float StrideRate = 1.0f;
        MCVector3D Position;
        MCMechParts<int32_t> Parts{};

        /// <summary>Whether the torso and arms show the legs' frame.</summary>
        bool InStep() const
        {
            return std::ranges::all_of(Parts, [this](int32_t part) { return part == Frame; });
        }
    };

    /// <summary>
    /// Moves mission 1's player mech, drawn and moving as <paramref name="treeName"/>, <paramref name="metres"/> along
    /// <paramref name="way"/> degrees (running when <paramref name="run"/>) until it stands still, and hands each frame;
    /// <paramref name="end"/> gets where its path ended.
    /// </summary>
    std::vector<StopFrame> MoveToHalt(std::string_view treeName, bool run, float way, float metres, MCVector3D& end)
    {
        const auto mech = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(0x200));
        REQUIRE(mech != nullptr);
        const auto actor = std::static_pointer_cast<MCMechActor>(mech->Appearance);
        const std::shared_ptr<MCSpriteTree> tree = MCTestGame::FindMechTree(treeName);
        REQUIRE(tree != nullptr);
        tree->PreloadGestures();
        REQUIRE_EQ(actor->Init(tree, mech), 0);
        const std::shared_ptr<MCMechWarrior> pilot = mech->GetPilot();
        constexpr float FrameSeconds = 1.0f / 15.0f;
        constexpr int32_t SettleFrames = 15;
        constexpr int32_t MostFrames = 1200;
        constexpr int32_t StillFrames = 15;
        constexpr uint32_t RunParam = 1;

        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        const double radians = way * MCMoverMath::DegreesToRadians;
        const MCVector3D from = mech->GetPosition();
        const float units = metres * WorldUnitsPerMeter;
        pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player,
                                MCVector3D(from.X + static_cast<float>(std::cos(radians)) * units,
                                           from.Y + static_cast<float>(std::sin(radians)) * units, from.Z),
                                -1, run ? RunParam : 0);
        std::vector<StopFrame> frames;
        int32_t still = 0;
        bool moved = false;

        for (int32_t frame = 0; frame < MostFrames && still < StillFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
            const MCMovePath& path = pilot->GetMovePath();

            if (path.NumStepsWhenNotPaused > 0)
            {
                end = path.StepList[static_cast<size_t>(path.NumStepsWhenNotPaused - 1)].Destination;
            }

            frames.push_back({actor->CurrentGesture, actor->CurrentFrame[MCMechPart::Legs],
                              actor->GetVelocityMagnitude(), actor->StrideRate, mech->GetPosition(),
                              actor->CurrentFrame});
            moved = moved || actor->CurrentGesture != MCStopGesture::Stand;
            still = moved && actor->CurrentGesture == MCStopGesture::Stand && !actor->InTransition ? still + 1 : 0;
        }

        REQUIRE(moved);
        REQUIRE(still == StillFrames);
        return frames;
    }

    /// <summary>The frames' gestures, a run of each once, for the log.</summary>
    std::string GestureRuns(const std::vector<StopFrame>& frames)
    {
        std::string runs;

        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (i == 0 || frames[i].Gesture != frames[i - 1].Gesture)
            {
                runs += std::format(" {}@{}", frames[i].Gesture, frames[i].Frame);
            }
        }

        return runs;
    }
} // namespace

/// <summary>
/// With MechBraking a mech whose move ends brakes to a halt through its stopping art: mission 1's mech, drawn as an
/// original mech, one that leaves its stride part way for the stop (commando), one with its own stops in its table
/// (madcat), and expansion mechs whose stops never slow in their data (OB-186), with (ShadowCat) and without (Turkina)
/// a run to walk, walks 120 metres or runs 240 and halts. Braking, its stride gives way to the stop where that starts
/// (the walk's end or its frame for the stop, the run's frame for its run to walk or, without, its end), it never
/// drops from a stride to standing, it only slows from there, it stands where the original stops (within the slack),
/// and its stride is never fitted beyond the rate bounds; without, it plays no stop, its legs dropping from the stride
/// to standing as the original's. Braking, a torso and arms in step with the legs stay in step through a gesture, its
/// end too (they looped to the gesture's first frame as the legs held its last: the stop's forward lean flashed as it
/// ended).
/// </summary>
TEST_CASE_ISOLATED("game: with MechBraking a mech brakes to a halt through its stopping art where its move ends")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    constexpr float WalkMetres = 120.0f;
    constexpr float RunMetres = 240.0f;
    constexpr float Slack = 0.01f;
    std::vector<std::pair<std::string, bool>> cases = {{"atlas", false},    {"commando", false}, {"Turkina", false},
                                                       {"atlas", true},     {"madcat", true},    {"commando", true},
                                                       {"ShadowCat", true}, {"Turkina", true}};

    // --stop-mechs walk|run: every mech of the retail PAK instead, walking or running.
    if (const std::optional<std::string_view> every = MCTest::Option("stop-mechs"); every.has_value())
    {
        cases.clear();
        std::set<std::string> names;

        for (const auto& [treeName, packet] : MCTestGame::MechTrees())
        {
            if (names.insert(treeName).second)
            {
                cases.emplace_back(treeName, *every == "run");
            }
        }
    }

    float way = 10.0f;

    for (const auto& [treeName, run] : cases)
    {
        for (const int braking : {1, 0})
        {
            MCTest::Scope scope(std::format("{} {} MechBraking {}", treeName, run ? "running" : "walking", braking));
            GMechBraking = braking;
            MCVector3D end;
            const std::vector<StopFrame> frames = MoveToHalt(treeName, run, way, run ? RunMetres : WalkMetres, end);
            way += 180.0f;
            std::cout << std::format("  {} {} braking {}:{}\n", treeName, run ? "run" : "walk", braking,
                                     GestureRuns(frames));
            const std::shared_ptr<MCSpriteTree> tree = MCTestGame::FindMechTree(treeName);
            const auto stride = [](int32_t gesture)
            { return gesture == MCStopGesture::Walk || gesture == MCStopGesture::Run; };
            const auto stopping = [](int32_t gesture)
            { return gesture == MCStopGesture::WalkToStand || gesture == MCStopGesture::RunToWalk; };
            const auto played = std::ranges::any_of(frames, [&](const StopFrame& f) { return stopping(f.Gesture); });

            if (braking == 0)
            {
                CHECK(!played);
                continue;
            }

            REQUIRE(played);

            // A torso and arms in step with the legs stay in step through a gesture, its end too.
            for (size_t i = 1; i < frames.size(); ++i)
            {
                if (frames[i].Gesture == frames[i - 1].Gesture && frames[i - 1].InStep())
                {
                    MCTest::Scope at(std::format("frame {} (gesture {}, parts {} after {})", i, frames[i].Gesture,
                                                 frames[i].Parts, frames[i - 1].Parts));
                    CHECK(frames[i].InStep());
                }
            }

            for (size_t i = 1; i < frames.size(); ++i)
            {
                CHECK(!(stride(frames[i - 1].Gesture) && frames[i].Gesture == MCStopGesture::Stand));
            }

            // Where the stride gives way: the frame after the move's last run, or its walk's before the stop.
            const int32_t strideGesture = run ? MCStopGesture::Run : MCStopGesture::Walk;
            const auto stopped = std::ranges::find_if(frames, [&](const StopFrame& f) { return stopping(f.Gesture); });
            const auto lastStride = std::find_if(std::make_reverse_iterator(stopped), frames.rend(),
                                                 [&](const StopFrame& f) { return f.Gesture == strideGesture; });
            REQUIRE(lastStride != frames.rend());
            const auto first = static_cast<size_t>(std::distance(frames.begin(), lastStride.base()));
            const MCMechSpecialInfo& info = tree->SpecialInfo;
            const auto strideFrames = static_cast<int32_t>(tree->Gestures[strideGesture].NumFrames);
            const int32_t walkGivesWay =
                info.WalkToWSFrame != NoFrame ? static_cast<int32_t>(info.WalkToWSFrame) : strideFrames - 1;
            const int32_t runGivesWay = frames[first].Gesture == MCStopGesture::RunToWalk && info.RunToRWFrame != 0
                                            ? static_cast<int32_t>(info.RunToRWFrame)
                                            : strideFrames - 1;
            CHECK_EQ(frames[first - 1].Frame, run ? runGivesWay : walkGivesWay);

            for (size_t i = first + 1; i < frames.size(); ++i)
            {
                CHECK(frames[i].Speed <= frames[i - 1].Speed + Slack);
            }

            for (const StopFrame& frame : frames)
            {
                CHECK(frame.StrideRate >= MinStrideRate - Slack);
                CHECK(frame.StrideRate <= MaxStrideRate + Slack);
            }

            // Where the original stops it: its last step's margin of error short of the end.
            const MCVector3D last = frames.back().Position;
            const float off = std::hypot(last.X - end.X, last.Y - end.Y) * MetersPerWorldUnit - MoveMarginOfError[1];
            std::cout << std::format("    stop at {} ends {:.2f} m off where the original stops\n", first, off);
            CHECK(frames.back().Speed == 0.0f);
            CHECK(std::fabs(off) <= BrakeArrivalSlack);
        }
    }

    GMechBraking = 0;
}

namespace
{
    /// <summary>
    /// A frame of a held up mech: its gesture, legs frame, and its legs as drawn (HDStops), with whether they'd settle in
    /// place from there (MCMechActor::DrawnSettlesNear).
    /// </summary>
    struct HeldFrame
    {
        int32_t Gesture = 0;
        int32_t Frame = 0;
        int32_t DrawnGesture = 0;
        int32_t DrawnFrame = 0;
        bool Held = false;
        MCDrawnMode Mode = MCDrawnMode::Held;
        bool Still = false;
        bool Near = false;
    };

    /// <summary>A held up move: how the mech goes, how long it is held up, and how it goes on.</summary>
    struct HoldUp
    {
        std::string_view Name;
        bool Run = false;
        int32_t PausedFrames = 0;
        /// <summary>How it goes on: as it was, or ordered anew, running or walking, so many metres on.</summary>
        std::optional<bool> OnRunning;
        float OnMetres = 0.0f;
        bool Braking = false;
    };

    /// <summary>
    /// Moves mission 1's player mech 120 metres along <paramref name="way"/> degrees as <paramref name="hold"/> has it,
    /// paused once it has gone a stride, in the middle of the next (as waiting on its lance's point mech pauses it,
    /// MCBattleMech's UpdateMovePath, a unit in its way much the same, CrashAvoidanceSystem), then going on, until it
    /// stands still, drawn as it plays; hands each frame, and <paramref name="paused"/> the frame it was paused at.
    /// </summary>
    std::vector<HeldFrame> MoveHeldUp(const HoldUp& hold, float way, int32_t& paused)
    {
        const auto mech = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(0x200));
        REQUIRE(mech != nullptr);
        const auto actor = std::static_pointer_cast<MCMechActor>(mech->Appearance);
        const std::shared_ptr<MCMechWarrior> pilot = mech->GetPilot();
        constexpr float FrameSeconds = 1.0f / 15.0f;
        constexpr int32_t SettleFrames = 15;
        constexpr int32_t MostFrames = 1200;
        constexpr int32_t StillFrames = 15;
        constexpr float Metres = 120.0f;
        constexpr uint32_t RunParam = 1;
        // MCMoveOrders::WaitForPointTime waiting for the point mech, and not.
        constexpr float WaitingForPoint = 999999.0f;
        constexpr float NotWaiting = -1.0f;

        for (int32_t frame = 0; frame < SettleFrames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }

        const double radians = way * MCMoverMath::DegreesToRadians;
        const auto along = [&](float metres)
        {
            const MCVector3D from = mech->GetPosition();
            const float units = metres * WorldUnitsPerMeter;
            return MCVector3D(from.X + static_cast<float>(std::cos(radians)) * units,
                              from.Y + static_cast<float>(std::sin(radians)) * units, from.Z);
        };

        pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, along(Metres), -1, hold.Run ? RunParam : 0);
        const int32_t stride = hold.Run ? MCStopGesture::Run : MCStopGesture::Walk;
        const int32_t strideFrames = static_cast<int32_t>(actor->MechTree->Gestures[stride].NumFrames);
        std::vector<HeldFrame> frames;
        int32_t strode = 0;
        int32_t still = 0;
        paused = -1;
        MCVector3D last = mech->GetPosition();

        for (int32_t frame = 0; frame < MostFrames && still < StillFrames; ++frame)
        {
            const auto now = static_cast<int32_t>(frames.size());
            const bool midStride = actor->CurrentGesture == stride &&
                                   std::abs(actor->CurrentFrame[MCMechPart::Legs] - strideFrames / 2) <= 1;
            strode += actor->CurrentGesture == stride ? 1 : 0;

            if (paused < 0 && strode > strideFrames && midStride)
            {
                paused = now;
            }

            // Kept waiting: a mech with no point mech is let go at once.
            if (paused >= 0 && now < paused + hold.PausedFrames)
            {
                pilot->PausePath();
                pilot->MoveOrders.WaitForPointTime = WaitingForPoint;
            }
            else if (paused >= 0 && now == paused + hold.PausedFrames)
            {
                pilot->MoveOrders.WaitForPointTime = NotWaiting;

                if (hold.OnRunning.has_value())
                {
                    pilot->OrderMoveToPoint(0, 1, MCOrderOrigin::Player, along(hold.OnMetres), -1,
                                            *hold.OnRunning ? RunParam : 0);
                }
                else
                {
                    pilot->ResumePath();
                }
            }

            MCTestGame::RunFrame(FrameSeconds);
            const MCVector3D at = mech->GetPosition();
            frames.push_back({actor->CurrentGesture, actor->CurrentFrame[MCMechPart::Legs], actor->DrawnGesture(),
                              actor->DrawnLegsFrame(), actor->DrawnStride.has_value(),
                              actor->DrawnStride.has_value() ? actor->DrawnStride->Mode : MCDrawnMode::Held,
                              at.X == last.X && at.Y == last.Y, actor->DrawnSettlesNear()});
            last = at;
            still = paused >= 0 && now > paused + hold.PausedFrames && actor->CurrentGesture == MCStopGesture::Stand &&
                            !actor->InTransition && !actor->DrawnStride.has_value()
                        ? still + 1
                        : 0;
        }

        REQUIRE(paused >= 0);
        REQUIRE(still == StillFrames);
        return frames;
    }
} // namespace

/// <summary>
/// With HDStops a mech whose move is held up mid-stride (its path paused, as a unit in its way pauses it) holds its
/// stride as drawn while it stands, then goes on from it: mission 1's mech walks or runs 120 metres, paused in the middle
/// of a stride for a second, or five (it settles to standing; from a run, too far to step in place, it drops to standing
/// as the original, rather than walk on the spot), then going on as it was, or ordered to run or walk on, or to walk just 15
/// metres more, braking (MechBraking). Held, its legs stay at the frame they stopped on while it stands still and go on
/// with it; they never jump: from frame to frame a frame or two on, or on to the next gesture through the art between
/// them (walk to run, run to walk, the stop), the way the mech's own go, but for that drop; they hand back to its own
/// gestures once in step; and it stands still at its move's end, drawn as it plays. Without, it drops to standing as
/// the original.
/// </summary>
TEST_CASE_ISOLATED("game: with HDStops a mech held up mid-stride holds its stride, then walks on from it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartMission(1));
    const auto mech = std::static_pointer_cast<MCBattleMech>(GetMoverFromPartId(0x200));
    REQUIRE(mech != nullptr);
    const auto actor = std::static_pointer_cast<MCMechActor>(mech->Appearance);
    MCSpriteTree& tree = *actor->MechTree;
    Eye()->ChangeTarget(mech, true);
    constexpr int32_t Second = 15;
    constexpr int32_t Settled = 75;
    constexpr float BrakingMetres = 15.0f;
    // A frame's step, and those a stride drawn on closes in by.
    constexpr int32_t MostJump = 2;
    const std::vector<HoldUp> holds = {
        {"walking, a second", false, Second},
        {"walking, five seconds", false, Settled},
        {"running, five seconds", true, Settled},
        {"running, a second, on walking", true, Second, false, 120.0f},
        {"walking, a second, on running", false, Second, true, 120.0f},
        {"walking, a second, on 15 metres braking", false, Second, false, BrakingMetres, true},
    };

    // The gestures the drawn legs go from one to the next by: the tree's art between them (settling, a start and a walk
    // speeding up back the way they came), or straight on without it.
    const auto byArt = [&](int32_t from, int32_t to)
    {
        using namespace MCStopGesture;
        static constexpr std::array<std::pair<int32_t, int32_t>, 10> Art = {{{Stand, StandToWalk},
                                                                             {StandToWalk, Walk},
                                                                             {Walk, WalkToStand},
                                                                             {WalkToStand, Stand},
                                                                             {Walk, WalkToRun},
                                                                             {WalkToRun, Run},
                                                                             {Run, RunToWalk},
                                                                             {RunToWalk, Walk},
                                                                             {StandToWalk, Stand},
                                                                             {WalkToRun, Walk}}};
        return std::ranges::find(Art, std::pair{from, to}) != Art.end() ||
               (from == Walk && to == Stand && !MCHasGestureArt(tree, WalkToStand)) ||
               (from == Run && to == Walk && !MCHasGestureArt(tree, RunToWalk)) ||
               (from == Walk && to == Run && !MCHasGestureArt(tree, WalkToRun));
    };

    float way = 10.0f;

    for (const HoldUp& hold : holds)
    {
        for (const int stops : {1, 0})
        {
            MCTest::Scope scope(std::format("{}, HDStops {}", hold.Name, stops));
            GHdStops = stops;
            GMechBraking = hold.Braking ? 1 : 0;
            int32_t paused = -1;
            const std::vector<HeldFrame> frames = MoveHeldUp(hold, way, paused);
            way += 180.0f;
            const auto pause = frames.begin() + paused;
            const auto heldFrames = std::ranges::count_if(frames, &HeldFrame::Held);
            std::string drawn;

            for (auto f = pause; f != frames.end(); ++f)
            {
                if (f == pause || f->DrawnGesture != (f - 1)->DrawnGesture || f->Held != (f - 1)->Held)
                {
                    drawn += std::format(" {}{}@{}", f->Held ? "" : "=", f->DrawnGesture, f->DrawnFrame);
                }
            }

            std::cout << std::format("  {}, HDStops {}: held {} frames, drawn{}\n", hold.Name, stops, heldFrames,
                                     drawn);

            // Stopped dead, as the original stops it.
            const auto stood =
                std::find_if(pause, frames.end(), [](const HeldFrame& f) { return f.Gesture == MCStopGesture::Stand; });
            REQUIRE(stood != frames.end());
            CHECK(std::distance(pause, stood) <= MostJump);
            CHECK(!frames.back().Held);

            if (stops == 0)
            {
                CHECK(heldFrames == 0);
                continue;
            }

            // Held at the frame its legs stopped on, and still while the mech is, but settling.
            const int32_t stride = hold.Run ? MCStopGesture::Run : MCStopGesture::Walk;
            const auto strideFrames = static_cast<int32_t>(tree.Gestures[stride].NumFrames);
            CHECK(stood->Held);
            CHECK(stood->DrawnGesture == stride);
            CHECK((stood->DrawnFrame - (pause - 1)->DrawnFrame + strideFrames) % strideFrames <= MostJump);

            for (auto f = pause + 1; f != frames.end(); ++f)
            {
                const auto prev = f - 1;

                if (!f->Held && !prev->Held)
                {
                    continue;
                }

                MCTest::Scope at(std::format("frame {}", std::distance(frames.begin(), f)));

                if (f->DrawnGesture != prev->DrawnGesture)
                {
                    // Or dropped to standing as the original stops, the mech standing still, from too far to settle.
                    const bool dropped = !f->Held && f->Gesture == MCStopGesture::Stand && f->Still && !prev->Near;
                    CHECK((byArt(prev->DrawnGesture, f->DrawnGesture) || dropped));
                    continue;
                }

                const auto count = static_cast<int32_t>(tree.Gestures[f->DrawnGesture].NumFrames);
                const int32_t step = (f->DrawnFrame - prev->DrawnFrame + count) % count;
                CHECK(std::min(step, count - step) <= MostJump);

                if (f->Still && prev->Held && f->Mode == MCDrawnMode::Held && prev->Mode == MCDrawnMode::Held)
                {
                    CHECK(step == 0);
                }
            }

            const auto played = [&](int32_t gesture)
            {
                return std::ranges::any_of(frames,
                                           [&](const HeldFrame& f) { return f.Held && f.DrawnGesture == gesture; });
            };

            if (hold.PausedFrames == Settled)
            {
                // Settled to standing while it waited, through its stop; too far from it to step there in place, dropped
                // once held as long as a stride is (from a run).
                const auto settled = std::find_if(stood, pause + Settled, [](const HeldFrame& f) { return !f.Held; });
                CHECK(settled != pause + Settled);
                CHECK(stood->Near == !hold.Run);
                CHECK(played(MCStopGesture::WalkToStand) ==
                      (MCHasGestureArt(tree, MCStopGesture::WalkToStand) && stood->Near));

                if (!stood->Near)
                {
                    CHECK(std::distance(stood, settled) >= static_cast<int32_t>(HoldMostSeconds * Second));
                }
            }

            if (hold.Run && hold.OnRunning == false)
            {
                CHECK(played(MCStopGesture::RunToWalk) == MCHasGestureArt(tree, MCStopGesture::RunToWalk));
            }

            if (!hold.Run && hold.OnRunning == true)
            {
                CHECK(played(MCStopGesture::WalkToRun) == MCHasGestureArt(tree, MCStopGesture::WalkToRun));
            }
        }
    }

    GHdStops = 0;
    GMechBraking = 0;
}
