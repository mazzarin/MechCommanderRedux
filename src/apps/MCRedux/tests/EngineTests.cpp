#include "stdafx.h"
#include "MCTest.h"
#include "engine/MCBitFlag.h"
#include "engine/MCByteFlag.h"
#include "engine/MCCraterManager.h"
#include "engine/MCElementBuffer.h"
#include "engine/MCFont.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "main/MCGameContext.h"

namespace
{
    /// <summary>What the recording elements of a test saw.</summary>
    struct MCDrawRecord
    {
        /// <summary>The depth of each element drawn, in drawing order.</summary>
        std::vector<int32_t> Draws;
        /// <summary>How many elements were destroyed.</summary>
        int32_t Deaths = 0;
    };

    /// <summary>A draw element that records its draws and its destruction.</summary>
    class MCRecordingElement : public MCElement
    {
    public:
        MCRecordingElement(int32_t depth, std::shared_ptr<MCDrawRecord> record)
            : MCElement(depth), _Record(std::move(record))
        {
        }

        ~MCRecordingElement() override { ++_Record->Deaths; }

        void Draw() override { _Record->Draws.push_back(static_cast<int32_t>(Depth)); }

    private:
        /// <summary>The test's record, kept until the element dies (its destructor counts in it).</summary>
        std::shared_ptr<MCDrawRecord> _Record;
    };

    /// <summary>A 1-row stroke-font letter: its header and a type-0 end.</summary>
    std::vector<uint8_t> Letter(uint8_t letter, uint8_t width, uint8_t height, std::vector<uint8_t> strokes = {})
    {
        std::vector<uint8_t> data = {letter, width, height};
        data.insert(data.end(), strokes.begin(), strokes.end());
        data.push_back(0);
        return data;
    }
}

TEST_CASE("elements: the draw list owns the frame's elements until it is reset")
{
    MCElementBuffer list;
    const auto record = std::make_shared<MCDrawRecord>();

    // More elements than any scenario's ElementHeapSize or MaxElements allowed: the list grows instead.
    for (int32_t i = 0; i < 5000; ++i)
    {
        list.Add(list.Make<MCRecordingElement>(i, record));
    }

    CHECK_EQ(list.MadeCount(), 5000u);
    CHECK_EQ(list.ElementCount(), 5000u);
    // An element made but not added lives until the reset too.
    list.Make<MCRecordingElement>(1, record);
    CHECK_EQ(record->Deaths, 0);
    list.Reset();
    CHECK_EQ(record->Deaths, 5001);
    CHECK_EQ(list.MadeCount(), 0u);
    CHECK_EQ(list.ElementCount(), 0u);
    CHECK_EQ(list.GroupCount(), 1u);
}

TEST_CASE("elements: groups draw deepest first, their elements sorted when asked")
{
    const auto record = std::make_shared<MCDrawRecord>();
    MCElementBuffer list;

    // Group 1 (opened by the reset, depth 0, sorted): elements 5, 9, 7.
    list.Add(list.Make<MCRecordingElement>(5, record));
    list.Add(list.Make<MCRecordingElement>(9, record));
    list.Add(list.Make<MCRecordingElement>(7, record));
    // Group 2 at depth 100, unsorted: 1, 3.
    list.OpenGroup(100, false);
    list.Add(list.Make<MCRecordingElement>(1, record));
    list.Add(list.Make<MCRecordingElement>(3, record));
    // Group 3 at depth 50, sorted: 2, 4.
    list.OpenGroup(50, true);
    list.Add(list.Make<MCRecordingElement>(2, record));
    list.Add(list.Make<MCRecordingElement>(4, record));
    // Group 4 at depth 100 like group 2: groups of equal depth keep the order they were opened in.
    list.OpenGroup(100, true);
    list.Add(list.Make<MCRecordingElement>(6, record));
    CHECK_EQ(list.GroupCount(), 4u);
    CHECK_EQ(list.ElementCount(), 8u);

    list.Sort();
    list.Draw();
    // Groups by depth, deepest first (100, 100, 50, 0); inside a sorted group deepest first too.
    const std::vector<int32_t> expected = {1, 3, 6, 4, 2, 9, 7, 5};
    CHECK(record->Draws == expected);
}

TEST_CASE("elements: before the sort the groups draw in the order they were opened")
{
    const auto record = std::make_shared<MCDrawRecord>();
    MCElementBuffer list;
    list.Add(list.Make<MCRecordingElement>(1, record));
    list.OpenGroup(100, true);
    list.Add(list.Make<MCRecordingElement>(2, record));
    list.Add(list.Make<MCRecordingElement>(3, record));
    // Opening a group sorts the one it closes.
    list.OpenGroup();
    list.Add(list.Make<MCRecordingElement>(4, record));
    list.Draw();
    const std::vector<int32_t> expected = {1, 3, 2, 4};
    CHECK(record->Draws == expected);
}

TEST_CASE("elements: there is no group limit (the original stopped drawing objects when MaxGroups ran out)")
{
    const auto record = std::make_shared<MCDrawRecord>();
    MCElementBuffer list;

    for (int32_t i = 1; i <= 3000; ++i)
    {
        list.OpenGroup(i, true);
        list.Add(list.Make<MCRecordingElement>(i, record));
    }

    list.Sort();
    list.Draw();
    REQUIRE_EQ(record->Draws.size(), 3000u);
    CHECK_EQ(record->Draws.front(), 3000);
    CHECK_EQ(record->Draws.back(), 1);
}

TEST_CASE("elements: an element's float depth is rounded down to a whole number")
{
    const auto record = std::make_shared<MCDrawRecord>();
    CHECK_EQ(MCRecordingElement(-3, record).Depth, -3.0f);
    // Through the float constructor: floor, as the original's __ftol after a floor.
    struct FloatElement : MCElement
    {
        explicit FloatElement(float depth) : MCElement(depth) {}

        void Draw() override {}
    };

    CHECK_EQ(FloatElement(2.75f).Depth, 2.0f);
    CHECK_EQ(FloatElement(-2.25f).Depth, -3.0f);
}

TEST_CASE("bitflag: flags address rows of whole bytes, column == columns reaching the next row")
{
    MCBitFlag flags(4, 16, false);
    CHECK_EQ(flags.ByteCount(), 8u);
    CHECK_EQ(flags.Data().size(), 9u);
    CHECK(!flags.GetFlag(1, 0));

    // Column 16 of row 0 is bit 0 of row 1's first byte.
    flags.SetFlag(0, 16);
    CHECK(flags.GetFlag(1, 0));
    CHECK(flags.GetFlag(0, 16));

    // On the last row it is the byte after the grid, which the original's heap slack held: set and read back.
    flags.SetFlag(3, 16);
    CHECK(flags.GetFlag(3, 16));

    // Rows past the grid read false and aren't written.
    flags.SetFlag(4, 0);
    CHECK(!flags.GetFlag(4, 0));

    flags.SetFlag(2, 13);
    CHECK(flags.GetFlag(2, 13));
    CHECK(!flags.GetFlag(2, 12));
    flags.ResetAll(true);
    CHECK(flags.GetFlag(0, 3));
    flags.ResetAll(false);
    CHECK(!flags.GetFlag(2, 13));
    CHECK(MCBitFlag(2, 8, true).GetFlag(1, 7));
}

TEST_CASE("bitflag: a group sets a run of a row's flags (OB-069: whole bytes start one byte late)")
{
    // A run inside one byte.
    MCBitFlag flags(4, 32, false);
    flags.SetGroup(1, 2, 3);

    for (uint32_t c = 0; c < 8; ++c)
    {
        MCTest::Scope scope(std::format("column {}", c));
        CHECK_EQ(flags.GetFlag(1, c), c >= 2 && c < 5);
    }

    // A run starting mid-byte: its first byte's bits, then the whole bytes from the byte after it.
    MCBitFlag unaligned(4, 32, false);
    unaligned.SetGroup(1, 4, 12);

    for (uint32_t c = 0; c < 32; ++c)
    {
        MCTest::Scope scope(std::format("column {}", c));
        CHECK_EQ(unaligned.GetFlag(1, c), c >= 4 && c < 16);
    }

    // OB-069: a byte-aligned run of 16 skips its own first byte and sets the two after it.
    MCBitFlag aligned(4, 32, false);
    aligned.SetGroup(1, 8, 16);

    for (uint32_t c = 0; c < 32; ++c)
    {
        MCTest::Scope scope(std::format("column {}", c));
        CHECK_EQ(aligned.GetFlag(1, c), c >= 16 && c < 32);
    }

    // Nothing happens for an empty run or one starting outside the grid.
    MCBitFlag untouched(4, 32, false);
    untouched.SetGroup(1, 0, 0);
    untouched.SetGroup(4, 0, 3);
    CHECK(std::ranges::all_of(untouched.Data(), [](uint8_t b) { return b == 0; }));
}

TEST_CASE("byteflag: flags are bytes of 0xFF, drawn into through the renderer")
{
    MCByteFlag flags(4, 6, false);
    CHECK_EQ(flags.Rows(), 4u);
    CHECK_EQ(flags.Columns(), 6u);
    CHECK_EQ(flags.Window().Pixels.get(), flags.Data().data());
    CHECK(!flags.GetFlag(2, 3));

    flags.SetFlag(2, 3);
    CHECK(flags.GetFlag(2, 3));
    CHECK_EQ(flags.Data()[2 * 6 + 3], 0xff);

    // Column == columns is the next row's first byte.
    flags.SetFlag(0, 6);
    CHECK(flags.GetFlag(1, 0));

    // A group runs on into the next row.
    flags.SetGroup(1, 4, 4);
    CHECK(flags.GetFlag(1, 4));
    CHECK(flags.GetFlag(1, 5));
    CHECK(flags.GetFlag(2, 0));
    CHECK(flags.GetFlag(2, 1));
    CHECK(!flags.GetFlag(2, 2));

    // Outside the grid reads false.
    CHECK(!flags.GetFlag(4, 0));

    flags.ResetAll(true);
    CHECK(flags.GetFlag(3, 5));
    flags.ResetAll(false);
    CHECK(!flags.GetFlag(3, 5));

    // A circle sets the flags inside it (x is the column, y the row).
    flags.SetCircle(3, 2, 2);

    for (const auto& [row, column] :
         std::initializer_list<std::pair<uint32_t, uint32_t>>{{2, 3}, {1, 3}, {3, 3}, {2, 2}, {2, 4}})
    {
        MCTest::Scope scope(std::format("row {} column {}", row, column));
        CHECK(flags.GetFlag(row, column));
    }

    CHECK(!flags.GetFlag(0, 0));
}

TEST_CASE("craters: the slots are a ring, the oldest crater gives way to the newest")
{
    MCCraterManager craters(3, {});
    CHECK_EQ(craters.Craters().size(), 3u);
    CHECK(std::ranges::all_of(craters.Craters(), [](const MCCraterData& c) { return c.CraterShapeId == -1; }));

    craters.Place(6, MCVector3D(1.0f, 0.0f, 0.0f), 1);
    craters.Place(7, MCVector3D(2.0f, 0.0f, 0.0f), 2);
    CHECK_EQ(craters.NextSlot(), 2);
    CHECK_EQ(craters.Craters()[2].CraterShapeId, -1);
    craters.Place(6, MCVector3D(3.0f, 0.0f, 0.0f), 3);
    CHECK_EQ(craters.NextSlot(), 0);

    // The fourth takes the first's slot.
    craters.Place(7, MCVector3D(4.0f, 0.0f, 0.0f), 4);
    CHECK_EQ(craters.NextSlot(), 1);
    CHECK_EQ(craters.Craters()[0].CraterShapeId, 7);
    CHECK_EQ(craters.Craters()[0].Position.X, 4.0f);
    CHECK_EQ(craters.Craters()[0].Rotation, 4);
    CHECK_EQ(craters.Craters()[1].Position.X, 2.0f);
    CHECK_EQ(craters.Craters()[2].Position.X, 3.0f);

    // A manager without slots keeps nothing.
    MCCraterManager none(0, {});
    none.Place(6, MCVector3D(1.0f, 0.0f, 0.0f), 1);
    CHECK(none.Craters().empty());
}

TEST_CASE("craters: the shapes come from the crater PAK, full size first, zoomed out second")
{
    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    const std::string pakName = GamePath(SpritePath, "testcrtr", ".pak");
    {
        MCPacketFile pak;
        REQUIRE_EQ(pak.Create(pakName), NO_ERR);
        pak.Reserve(4);

        for (int32_t i = 0; i < 4; ++i)
        {
            const std::vector<uint8_t> shape(static_cast<size_t>(16 + i), static_cast<uint8_t>(i));
            pak.WritePacket(i, shape, MCPacketStorage::Raw);
        }
    }

    std::expected<std::unique_ptr<MCCraterManager>, std::string> craters = MCCraterManager::Create(5, "testcrtr");
    REQUIRE(craters.has_value());
    CHECK_EQ((*craters)->ShapeCount(), 4);
    CHECK_EQ((*craters)->Craters().size(), 5u);

    // Port: HDFootprints makes the ring longer.
    GHdFootprints = 1;
    const std::expected<std::unique_ptr<MCCraterManager>, std::string> longer = MCCraterManager::Create(5, "testcrtr");
    GHdFootprints = 0;
    REQUIRE(longer.has_value());
    CHECK_EQ((*longer)->Craters().size(), static_cast<size_t>(5 * HdFootprintsScale));

    CHECK(!MCCraterManager::Create(5, "nocrater").has_value());
}

TEST_CASE("font: letters are found, scaled and measured as the stroke font's rules say")
{
    // 'A' 4 wide, 6 high with one pixel stroke; 'b' 3 x 8; 0xFF (not counted in the height) 2 x 20.
    std::vector<uint8_t> data;

    for (std::vector<uint8_t> letter : {Letter('A', 4, 6, {1, 0, 0, 3, 0}), Letter('b', 3, 8), Letter(0xff, 2, 20)})
    {
        data.insert(data.end(), letter.begin(), letter.end());
    }

    data.push_back(0);
    MCFont font(data);
    CHECK_EQ(font.FontHeight, 8);

    // Scaled by 2 by default.
    CHECK_EQ(font.PrintWidth('A'), 8);
    // A lower-case letter the font lacks falls back to upper case; an upper-case one doesn't fall back.
    CHECK_EQ(font.PrintWidth('a'), 8);
    CHECK_EQ(font.PrintWidth('B'), 0);
    CHECK_EQ(font.PrintWidth('b'), 6);
    font.Scaled = false;
    CHECK_EQ(font.PrintWidth('A'), 4);
    font.Scale = 1.5f;
    font.Scaled = true;
    // floor(3 * 1.5) = 4.
    CHECK_EQ(font.PrintWidth('b'), 4);

    font.Scaled = false;
    CHECK_EQ(font.PrintWidth("AbA", false), 11);
    // Multi-line: the widest line.
    CHECK_EQ(font.PrintWidth("A\nAbA\nb", true), 11);
    CHECK_EQ(font.PrintWidth("", true), 0);

    // An empty font has no letters.
    MCFont empty;
    CHECK_EQ(empty.PrintWidth('A'), 0);
    CHECK_EQ(empty.FontHeight, 0);
}

TEST_CASE("font: a pixel stroke draws a line at the cursor and the cursor moves on")
{
    std::vector<uint8_t> data = Letter('A', 4, 6, {1, 0, 0, 3, 0});
    data.push_back(0);
    MCFont font(data);
    font.Scaled = false;
    const std::shared_ptr<MCWindow> window = MCWindow::Make(16, 4);
    const std::span<uint8_t> pixels = window->Bytes();
    MCPane pane = MCPane::Over(window);

    font.Print(2, 1, "AA", 9, pane);
    CHECK_EQ(font.CurX, 2 + 4 + 4);
    CHECK_EQ(font.Color, 9);

    // Each 'A' is a line from its cell's (0, 0) to (3, 0): row 1, columns 2..5 and 6..9.
    for (int32_t x = 0; x < 16; ++x)
    {
        MCTest::Scope scope(std::format("x {}", x));
        CHECK_EQ(pixels[static_cast<size_t>(16 + x)], x >= 2 && x <= 9 ? 9 : 0);
    }

    // To the newline only.
    CHECK_EQ(font.PrintToNewline(0, 3, "A\nA", -1, pane), 1);
    CHECK_EQ(font.CurX, 4);
}
