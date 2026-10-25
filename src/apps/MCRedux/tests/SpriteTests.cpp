#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "fixtures/MCRegisteredData.h"
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
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "sprite/MCGVAppearance.h"
#include "sprite/MCMechActor.h"
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

    // A gesture without frames has no shapes; the frame rate is made positive.
    CHECK(!tree->GesturePacket(3, 0, 0.0f).has_value());
    int32_t reverse = 0;
    float frameRate = 0.0f;
    tree->SetGesture(14, 1, 10.0f, reverse, frameRate);
    CHECK_EQ(frameRate, 15.0f);

    // The shape is the mech's packet of that part PAK.
    const std::shared_ptr<MCShape> shape = tree->GetGesture(2, 0, 90.0f, reverse, frameRate, false);
    REQUIRE(shape != nullptr);
    CHECK_EQ(Tag(shape), 8000 + 2 * 9 + 2);
    CHECK(tree->GetGesture(2, 0, 90.0f, reverse, frameRate, false) == shape);
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
