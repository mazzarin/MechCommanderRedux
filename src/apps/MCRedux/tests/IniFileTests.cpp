#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "platform/MCUtf8Path.h"

namespace
{
    /// <summary>A scratch path outside the game install.</summary>
    std::string ScratchPath(std::string_view name)
    {
        return MCFileSystem::ToUtf8(std::filesystem::temp_directory_path() / name);
    }

    /// <summary>Opens a FIT file and requires success.</summary>
    struct OpenFit
    {
        explicit OpenFit(std::string_view name) { Result = File.Open(name); }

        MCFitIniFile File;
        int32_t Result = 0;
    };
}

TEST_CASE("inifile: a written FIT file reads back")
{
    const std::string path = ScratchPath("mc_inifile_test.fit");
    {
        MCFitIniFile out;
        REQUIRE_EQ(out.Create(path.c_str()), 0);
        out.WriteBlock("First");
        out.WriteIdLong("Count", -42);
        out.WriteIdULong("Big", 4000000000u);
        out.WriteIdFloat("Scale", 2.5f);
        out.WriteIdBoolean("On", true);
        out.WriteIdBoolean("Off", false);
        out.WriteIdString("Name", "Jade Falcon");
        out.WriteIdShort("Short", -7);
        out.WriteIdUShort("UShort", 65000);
        out.WriteIdChar("Char", -3);
        out.WriteIdUChar("UChar", 200);
        out.WriteBlock("Arrays");
        const int32_t longs[] = {1, -2, 3, 40000};
        out.WriteIdLongArray("Longs", longs);
        const float floats[] = {0.25f, -1.5f, 100.0f};
        out.WriteIdFloatArray("Floats", floats);
        const uint8_t bytes[] = {0, 7, 255};
        out.WriteIdUCharArray("Bytes", bytes);
        const uint16_t shorts[] = {9, 65535};
        out.WriteIdUShortArray("Shorts", shorts);
        out.Close();
    }

    MCFitIniFile in;
    REQUIRE_EQ(in.Open(path.c_str()), 0);
    CHECK_EQ(in.GetNumBlocks(), 2);
    CHECK_EQ(in.SeekBlock("Missing"), BLOCK_NOT_FOUND);
    CHECK_EQ(in.SeekBlock("first"), BLOCK_NOT_FOUND);
    REQUIRE_EQ(in.SeekBlock("First"), 0);

    int32_t count = 0;
    CHECK_EQ(in.ReadIdLong("Count", count), 0);
    CHECK_EQ(count, -42);
    CHECK_EQ(in.ReadIdLong("count", count), 0); // names ignore case
    uint32_t big = 0;
    CHECK_EQ(in.ReadIdULong("Big", big), 0);
    CHECK_EQ(big, 4000000000u);
    float scale = 0.0f;
    CHECK_EQ(in.ReadIdFloat("Scale", scale), 0);
    CHECK_EQ(scale, 2.5f);
    int on = 0;
    int off = 1;
    CHECK_EQ(in.ReadIdBoolean("On", on), 0);
    CHECK_EQ(in.ReadIdBoolean("Off", off), 0);
    CHECK_EQ(on, 1);
    CHECK_EQ(off, 0);
    char name[32] = {};
    CHECK_EQ(in.ReadIdString("Name", name), 0);
    CHECK(std::strcmp(name, "Jade Falcon") == 0);
    CHECK_EQ(in.GetIdStringLength("Name"), 12);
    char tiny[4];
    CHECK_EQ(in.ReadIdString("Name", tiny), BUFFER_TOO_SMALL);
    int16_t s = 0;
    CHECK_EQ(in.ReadIdShort("Short", s), 0);
    CHECK_EQ(s, -7);
    uint16_t us = 0;
    CHECK_EQ(in.ReadIdUShort("UShort", us), 0);
    CHECK_EQ(us, 65000);
    char c = 0;
    CHECK_EQ(in.ReadIdChar("Char", c), 0);
    CHECK_EQ(c, -3);
    uint8_t uc = 0;
    CHECK_EQ(in.ReadIdUChar("UChar", uc), 0);
    CHECK_EQ(uc, 200);
    count = 5;
    CHECK_EQ(in.ReadIdLong("Scale", count), VARIABLE_NOT_FOUND); // "f Scale" isn't an "l" entry
    CHECK_EQ(count, 0);

    REQUIRE_EQ(in.SeekBlock("Arrays"), 0);
    CHECK_EQ(in.GetIdLongArrayElements("Longs"), 4u);
    int32_t readLongs[4] = {};
    CHECK_EQ(in.ReadIdLongArray("Longs", readLongs), 0);
    CHECK_EQ(readLongs[1], -2);
    CHECK_EQ(readLongs[3], 40000);
    int32_t small[2];
    CHECK_EQ(in.ReadIdLongArray("Longs", small), USER_ARRAY_TOO_SMALL);
    float readFloats[3] = {};
    CHECK_EQ(in.ReadIdFloatArray("Floats", readFloats), 0);
    CHECK_EQ(readFloats[0], 0.25f);
    CHECK_EQ(readFloats[1], -1.5f);
    CHECK_EQ(readFloats[2], 100.0f);
    uint8_t readBytes[3] = {};
    CHECK_EQ(in.ReadIdUCharArray("Bytes", readBytes), 0);
    CHECK_EQ(readBytes[2], 255);
    uint16_t readShorts[2] = {};
    CHECK_EQ(in.ReadIdUShortArray("Shorts", readShorts), 0);
    CHECK_EQ(readShorts[1], 65535);
    CHECK_EQ(in.GetIdFloatArrayElements("Nothing"), static_cast<uint32_t>(VARIABLE_NOT_FOUND));
    in.Close();

    // The file is laid out as the game writes it.
    MCFile raw;
    REQUIRE_EQ(raw.Open(path.c_str()), 0);
    std::string text(raw.GetLength(), '\0');
    raw.Read(std::span(reinterpret_cast<uint8_t*>(text.data()), text.size()));
    raw.Close();
    CHECK(text.starts_with("FITini \r\n\r\n[First]\r\nl Count=-42\r\n"));
    CHECK(text.find("ul Big=-294967296\r\n") != std::string::npos);
    CHECK(text.find("l[4] Longs=1,-2,3,40000,\r\n") != std::string::npos);
    CHECK(text.find("f[3] Floats=0.25, -1.50, 100.00,\r\n") != std::string::npos);
    CHECK(text.find("uc[3] Bytes=0, 7, 255,\r\n") != std::string::npos);
    CHECK(text.ends_with("FITend \r\n"));
    std::filesystem::remove(path);
}

TEST_CASE("inifile: hex values and malformed files")
{
    const std::string path = ScratchPath("mc_inifile_hex.fit");
    {
        std::ofstream out(path, std::ios::binary);
        out << "FITini\r\n[Hex]\r\nl Mask = 0x1F  // comment\r\nul Color=0xffAA00\r\nl Word = Null\r\n"
               "l[3] Split = 1, 2\r\n  3\r\nl NoEquals 5\r\n\r\n[Next]\r\nFITend\r\n";
    }

    MCFitIniFile in;
    REQUIRE_EQ(in.Open(path.c_str()), 0);
    REQUIRE_EQ(in.SeekBlock("Hex"), 0);
    int32_t mask = 0;
    CHECK_EQ(in.ReadIdLong("Mask", mask), 0);
    CHECK_EQ(mask, 0x1f);
    uint32_t color = 0;
    CHECK_EQ(in.ReadIdULong("Color", color), 0);
    CHECK_EQ(color, 0xffaa00u);
    int32_t word = 7;
    CHECK_EQ(in.ReadIdLong("Word", word), 0);
    CHECK_EQ(word, 0);
    int32_t split[3] = {};
    CHECK_EQ(in.ReadIdLongArray("Split", split), 0);
    CHECK_EQ(split[2], 3);
    int32_t noEquals = 0;
    CHECK_EQ(in.ReadIdLong("NoEquals", noEquals), VARIABLE_NOT_FOUND);
    in.Close();

    {
        std::ofstream out(path, std::ios::binary);
        out << "Not a fit file\r\n[Block]\r\n";
    }

    MCFitIniFile bad;
    CHECK_EQ(bad.Open(path.c_str()), NOT_A_FITINIFILE);
    bad.Close();
    std::filesystem::remove(path);
}

namespace
{
    /// <summary>A FIT file held by a test's memory source, opened.</summary>
    struct MemoryFit
    {
        explicit MemoryFit(std::string_view text)
        {
            MCMemoryFileSource& files = Scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
            files.AddFile("data\\test.fit", text);
            Result = File.Open("data\\test.fit");
        }

        MCTestContextScope Scope;
        MCFitIniFile File;
        int32_t Result = 0;
    };

    /// <summary>Whether <paramref name="result"/> failed with <paramref name="error"/>.</summary>
    template <typename T> bool FailedWith(const MCFitResult<T>& result, MCFitError error)
    {
        return !result.has_value() && result.error() == error;
    }
}

/// <summary>
/// Read and ReadArray give each entry as its type tag says (f l ul s us c uc b st), numbers as atol/atof read them
/// (hex after "0x", 32-bit wrap), and say why an entry is missing.
/// </summary>
TEST_CASE("inifile: Read gives every entry type, or why it is missing")
{
    MemoryFit fit("FITini\r\n[All]\r\nf Float = -2.5\r\nl Long = -123456\r\nul ULong = 0xFFFFFFFF\r\n"
                  "s Short = -300\r\nus UShort = 65535\r\nc Char = -5\r\nuc UChar = 0x80\r\nb True = TRUE\r\n"
                  "b Number = 2\r\nb False = 0\r\nst Text = \"Hello, world\" // comment\r\n"
                  "st Open = \"no closing quote\r\nst NoQuote = plain\r\nf NegZero = -0.0\r\nl Wrap = 4294967297\r\n"
                  "f[3] Floats = 1.5, -2, 3e2\r\nl[4] Longs = 1, 0x10,\r\n  -3\t4\r\nuc[2] Bytes = 255, 256\r\n"
                  "ul[2] ULongs = 7, 8\r\n\r\n[Second]\r\nl Long = 99\r\nFITend\r\n");
    REQUIRE_EQ(fit.Result, 0);
    MCFitIniFile& file = fit.File;
    CHECK_EQ(file.GetNumBlocks(), 2);
    CHECK_EQ(file.GetBlockName(1), std::string_view("Second"));
    CHECK(file.GetBlockName(2).empty());
    CHECK_EQ(file.SeekBlock("second"), BLOCK_NOT_FOUND);
    REQUIRE_EQ(file.SeekBlock("All"), 0);

    CHECK(file.Read<float>("Float") == MCFitResult<float>(-2.5f));
    CHECK(file.Read<int32_t>("Long") == MCFitResult<int32_t>(-123456));
    CHECK(file.Read<int32_t>("LONG") == MCFitResult<int32_t>(-123456));
    CHECK(file.Read<uint32_t>("ULong") == MCFitResult<uint32_t>(0xffffffffu));
    CHECK(file.Read<int16_t>("Short") == MCFitResult<int16_t>(-300));
    CHECK(file.Read<uint16_t>("UShort") == MCFitResult<uint16_t>(65535));
    CHECK(file.Read<char>("Char") == MCFitResult<char>(-5));
    CHECK(file.Read<uint8_t>("UChar") == MCFitResult<uint8_t>(0x80));
    CHECK(file.Read<bool>("True") == MCFitResult<bool>(true));
    CHECK(file.Read<bool>("Number") == MCFitResult<bool>(true));
    CHECK(file.Read<bool>("False") == MCFitResult<bool>(false));
    int number = 0;
    CHECK_EQ(file.ReadIdBoolean("Number", number), 0);
    CHECK_EQ(number, 2);
    CHECK(file.Read<std::string>("Text") == MCFitResult<std::string>("Hello, world"));
    CHECK(file.Read<std::string>("Open") == MCFitResult<std::string>("no closing quote"));
    CHECK(FailedWith(file.Read<std::string>("NoQuote"), MCFitError::SyntaxError));
    CHECK_EQ(file.GetIdStringLength("Text"), 13);
    CHECK_EQ(file.GetIdStringLength("Open"), SYNTAX_ERROR);

    // -0 reads as 0 (the original's never-written expression reader gave 0 for any zero).
    const MCFitResult<float> negZero = file.Read<float>("NegZero");
    REQUIRE(negZero.has_value());
    CHECK(!std::signbit(*negZero));
    CHECK(file.Read<int32_t>("Wrap") == MCFitResult<int32_t>(1));

    CHECK(FailedWith(file.Read<int32_t>("Nothing"), MCFitError::VariableNotFound));
    CHECK(FailedWith(file.Read<int32_t>("Float"), MCFitError::VariableNotFound));
    int32_t legacy = 5;
    CHECK_EQ(file.ReadIdLong("Nothing", legacy), VARIABLE_NOT_FOUND);
    CHECK_EQ(legacy, 0);

    CHECK(file.ReadArray<float>("Floats") == MCFitResult<std::vector<float>>(std::vector<float>{1.5f, -2.0f, 300.0f}));
    CHECK(file.ArraySize<int32_t>("Longs") == MCFitResult<uint32_t>(4u));
    CHECK(file.ReadArray<int32_t>("Longs") == MCFitResult<std::vector<int32_t>>(std::vector<int32_t>{1, 16, -3, 4}));
    CHECK(file.ReadArray<uint8_t>("Bytes") == MCFitResult<std::vector<uint8_t>>(std::vector<uint8_t>{255, 0}));
    std::array<int32_t, 3> three{};
    CHECK(FailedWith(file.ReadArray<int32_t>("Longs", std::span(three)), MCFitError::UserArrayTooSmall));
    CHECK(FailedWith(file.ReadArray<int32_t>("Nothing"), MCFitError::VariableNotFound));
    CHECK(FailedWith(file.ArraySize<float>("Longs"), MCFitError::VariableNotFound));
    // The type is matched anywhere in the line, so "l[" also finds a "ul[" array.
    CHECK(file.ReadArray<int32_t>("ULongs") == MCFitResult<std::vector<int32_t>>(std::vector<int32_t>{7, 8}));

    REQUIRE_EQ(file.SeekBlock("Second"), 0);
    CHECK(file.Read<int32_t>("Long") == MCFitResult<int32_t>(99));
    CHECK(FailedWith(file.Read<float>("Float"), MCFitError::VariableNotFound));
}

/// <summary>
/// Block names sit between brackets at the start of a line; comments start with "/" after a value; a text entry is
/// cut to the caller's buffer as the original's readIdString cut it.
/// </summary>
TEST_CASE("inifile: blocks, comments and string buffers")
{
    MemoryFit fit("FITini\r\n// [Commented]\r\n [Indented]\r\n[Name] // a comment\r\nl A = 5 // five\r\n"
                  "// l B = 6\r\nl[3] C = 1, 2 // two\r\n 3\r\nst S = \"Jade\"\r\n[ ]\r\nl D = 4\r\n\r\nFITend\r\n");
    REQUIRE_EQ(fit.Result, 0);
    MCFitIniFile& file = fit.File;
    REQUIRE_EQ(file.GetNumBlocks(), 2);
    CHECK_EQ(file.GetBlockName(0), std::string_view("Name"));
    CHECK_EQ(file.GetBlockName(1), std::string_view(" "));
    REQUIRE_EQ(file.SeekBlock("Name"), 0);
    CHECK(file.Read<int32_t>("A") == MCFitResult<int32_t>(5));
    CHECK(FailedWith(file.Read<int32_t>("B"), MCFitError::VariableNotFound));
    CHECK(file.ReadArray<int32_t>("C") == MCFitResult<std::vector<int32_t>>(std::vector<int32_t>{1, 2, 3}));

    std::array<char, 6> text{'x', 'x', 'x', 'x', 'x', 'x'};
    CHECK_EQ(file.ReadIdString("S", std::span(text).first(5)), 0);
    CHECK_EQ(std::string_view(text.data()), std::string_view("Jade"));
    text.fill('x');
    CHECK_EQ(file.ReadIdString("S", std::span(text).first(4)), BUFFER_TOO_SMALL);
    CHECK_EQ(std::string_view(text.data(), 5), std::string_view("Jadex"));
    CHECK_EQ(file.ReadIdString("Missing", std::span(text).first(4)), VARIABLE_NOT_FOUND);

    REQUIRE_EQ(file.SeekBlock(" "), 0);
    CHECK(file.Read<int32_t>("D") == MCFitResult<int32_t>(4));

    MemoryFit unclosed("FITini\r\n[Unclosed\r\nl A = 1\r\n\r\nFITend\r\n");
    CHECK_EQ(unclosed.Result, SYNTAX_ERROR);
}

/// <summary>The original reader's quirks that a data file can meet (OB-135, OB-136, OB-137, OB-138, OB-139).</summary>
TEST_CASE("inifile: the original reader's quirks are kept")
{
    // OB-136: an entry on the file's last line (no FITend after it) is reported missing.
    MemoryFit last("FITini\r\n[Block]\r\nl First = 1\r\nl Last = 3");
    REQUIRE_EQ(last.Result, 0);
    REQUIRE_EQ(last.File.SeekBlock("Block"), 0);
    CHECK(last.File.Read<int32_t>("First") == MCFitResult<int32_t>(1));
    CHECK(FailedWith(last.File.Read<int32_t>("Last"), MCFitError::VariableNotFound));

    // OB-137: array elements on the file's last line aren't read.
    MemoryFit split("FITini\r\n[Block]\r\nl[3] Split = 5, 6\r\n7");
    REQUIRE_EQ(split.Result, 0);
    REQUIRE_EQ(split.File.SeekBlock("Block"), 0);
    CHECK(split.File.ArraySize<int32_t>("Split") == MCFitResult<uint32_t>(3u));
    CHECK(FailedWith(split.File.ReadArray<int32_t>("Split"), MCFitError::NotEnoughElementsForArray));

    // OB-138: a file without blocks doesn't open.
    MemoryFit empty("FITini\r\nl X = 1\r\nFITend\r\n");
    CHECK_EQ(empty.Result, std::to_underlying(MCFitError::NoBlocks));

    // OB-139: an array element longer than nine characters doesn't fit the original's word buffer.
    MemoryFit words("FITini\r\n[Block]\r\nl[2] Nine = 123456789, 2\r\nl[2] Ten = 1234567890, 2\r\n\r\nFITend\r\n");
    REQUIRE_EQ(words.Result, 0);
    REQUIRE_EQ(words.File.SeekBlock("Block"), 0);
    CHECK(words.File.ReadArray<int32_t>("Nine") ==
          MCFitResult<std::vector<int32_t>>(std::vector<int32_t>{123456789, 2}));
    CHECK(FailedWith(words.File.ReadArray<int32_t>("Ten"), MCFitError::BufferTooSmall));

    // OB-135: a line over 254 characters is cut there and loses the next character, so a value past the cut reads
    // from the rest of the line as if it were a line of its own.
    const std::string longLine = "l Long = 1 //" + std::string(254 - 13, '-') + "Xl Cut = 8";
    MemoryFit cut("FITini\r\n[Block]\r\n" + longLine + "\r\n\r\nFITend\r\n");
    REQUIRE_EQ(cut.Result, 0);
    REQUIRE_EQ(cut.File.SeekBlock("Block"), 0);
    CHECK(cut.File.Read<int32_t>("Long") == MCFitResult<int32_t>(1));
    CHECK(cut.File.Read<int32_t>("Cut") == MCFitResult<int32_t>(8));
}

TEST_CASE("game: SYSTEM.CFG and PREFS.CFG read as the game reads them")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    OpenFit system("system.cfg");
    REQUIRE_EQ(system.Result, 0);
    // The heaps' sizes are each install's own (the CD installer writes a system heap of 4095999, a tuned SYSTEM.CFG
    // bigger ones): each reads, and is set.
    REQUIRE_EQ(system.File.SeekBlock("systemHeap"), 0);
    uint32_t size = 0;
    CHECK_EQ(system.File.ReadIdULong("systemHeapSize", size), 0);
    CHECK(size > 0);
    CHECK_EQ(system.File.ReadIdULong("logisticsHeapSize", size), 0);
    CHECK(size > 0);

    REQUIRE_EQ(system.File.SeekBlock("ABL"), 0);
    CHECK_EQ(system.File.ReadIdULong("SymbolTableHeapSize", size), 0);
    CHECK(size > 0);
    int32_t watches = 0;
    CHECK_EQ(system.File.ReadIdLong("MaxWatchesPerModule", watches), 0);
    CHECK_EQ(watches, 20);

    REQUIRE_EQ(system.File.SeekBlock("FastFiles"), 0);
    int32_t numFastFiles = 0;
    CHECK_EQ(system.File.ReadIdLong("NumFastFiles", numFastFiles), 0);
    CHECK_EQ(numFastFiles, 5);
    char name[80] = {};
    CHECK_EQ(system.File.ReadIdString("File0", name), 0);
    CHECK(std::strcmp(name, "art.fst") == 0);
    CHECK_EQ(system.File.ReadIdString("File4", name), 0); // spelled "FIle4" in the file
    CHECK(std::strcmp(name, "shapes.fst") == 0);

    REQUIRE_EQ(system.File.SeekBlock("systemPaths"), 0);
    CHECK_EQ(system.File.ReadIdString("missionPath", name), 0);
    CHECK(std::strcmp(name, "data\\missions\\") == 0);
    CHECK_EQ(system.File.SeekBlock("UseSound"), 0);

    // Every PREFS.CFG has DirectDraw; the rest are there once the game has saved its preferences (the installer's has
    // four lines). Each that is there reads, in range.
    OpenFit prefs("prefs.cfg");
    REQUIRE_EQ(prefs.Result, 0);
    REQUIRE_EQ(prefs.File.SeekBlock("MechCommander"), 0);
    const auto there = [](int32_t result)
    {
        CHECK((result == 0 || result == std::to_underlying(MCFitError::VariableNotFound)));
        return result == 0;
    };

    int flag = 0;
    CHECK_EQ(prefs.File.ReadIdBoolean("DirectDraw", flag), 0);
    CHECK_EQ(flag, 1);
    there(prefs.File.ReadIdBoolean("Force16Mb", flag));

    if (int32_t difficulty = -1; there(prefs.File.ReadIdLong("Difficulty", difficulty)))
    {
        CHECK((difficulty >= 0 && difficulty <= 3));
    }

    if (int32_t volume = -1; there(prefs.File.ReadIdLong("SFXVolume", volume)))
    {
        CHECK(volume >= 0);
    }
}

TEST_CASE("game: FIT files from the FastFiles read")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();

    {
        OpenFit mission("data\\missions\\MCX0101.FIT");
        REQUIRE_EQ(mission.Result, 0);
        MCFitIniFile& fit = mission.File;
        REQUIRE_EQ(fit.SeekBlock("GameScale"), 0);
        float unitsPerMeter = 0.0f;
        CHECK_EQ(fit.ReadIdFloat("WorldUnitsPerMeter", unitsPerMeter), 0);
        CHECK_EQ(unitsPerMeter, 5.01f);
        uint32_t duration = 0;
        CHECK_EQ(fit.ReadIdULong("Duration", duration), 0);
        CHECK_EQ(duration, 60u);

        REQUIRE_EQ(fit.SeekBlock("Artillery"), 0);
        int32_t strikes = -1;
        CHECK_EQ(fit.ReadIdLong("NumLargeStrikes", strikes), 0); // "Null"
        CHECK_EQ(strikes, 0);
        CHECK_EQ(fit.ReadIdLong("NumSmallStrikes", strikes), 0);
        CHECK_EQ(strikes, 4);

        REQUIRE_EQ(fit.SeekBlock("Part1"), 0);
        uint32_t objectNumber = 0;
        CHECK_EQ(fit.ReadIdULong("ObjectNumber", objectNumber), 0);
        CHECK_EQ(objectNumber, 15u);
        int playerPart = 1;
        CHECK_EQ(fit.ReadIdBoolean("PlayerPart", playerPart), 0);
        CHECK_EQ(playerPart, 0);
        char team = 0;
        CHECK_EQ(fit.ReadIdChar("TeamID", team), 0);
        CHECK_EQ(team, 1);
        char profile[16] = {};
        CHECK_EQ(fit.ReadIdString("ObjectProfile", profile), 0);
        CHECK(std::strcmp(profile, "PM200300") == 0);
        float x = 0.0f;
        float y = 0.0f;
        CHECK_EQ(fit.ReadIdFloat("PositionX", x), 0);
        CHECK_EQ(fit.ReadIdFloat("PositionY", y), 0);
        CHECK_EQ(x, 1530.0f);
        CHECK_EQ(y, -1233.0f);

        REQUIRE_EQ(fit.SeekBlock("Commander1Group:0"), 0);
        CHECK_EQ(fit.GetIdLongArrayElements("Mates"), 12u);
        int32_t mates[12] = {};
        CHECK_EQ(fit.ReadIdLongArray("Mates", mates), 0);
        CHECK_EQ(mates[0], 1);
        CHECK_EQ(mates[4], 5);
        CHECK_EQ(mates[5], 0);
    }

    {
        OpenFit clan("data\\missions\\E3_0101.FIT");
        REQUIRE_EQ(clan.Result, 0);
        REQUIRE_EQ(clan.File.SeekBlock("ClanStar:0"), 0);
        float withdraw[7] = {};
        CHECK_EQ(clan.File.ReadIdFloatArray("WithdrawThreshold", withdraw), 0);
        CHECK_EQ(withdraw[0], 0.25f);
        CHECK_EQ(withdraw[4], 500.0f);
        CHECK_EQ(withdraw[5], -2000.0f);
        CHECK_EQ(withdraw[6], -1.0f);
        float zone[4] = {};
        CHECK_EQ(clan.File.ReadIdFloatArray("Zone1", zone), 0); // ends in a // comment
        CHECK_EQ(zone[0], -135.0f);
        CHECK_EQ(zone[3], 90.0f);
        int32_t roles[5] = {};
        CHECK_EQ(clan.File.ReadIdLongArray("Roles", roles), 0);
        CHECK_EQ(roles[0], 0);
        CHECK_EQ(roles[4], 1);
    }

    {
        OpenFit screen("data\\art\\lanscreen.fit");
        REQUIRE_EQ(screen.Result, 0);
        REQUIRE_EQ(screen.File.SeekBlock("Element1"), 0);
        int32_t left = 0;
        CHECK_EQ(screen.File.ReadIdLong("Left", left), 0);
        CHECK_EQ(left, 549);
        char art[32] = {};
        CHECK_EQ(screen.File.ReadIdString("GreyArt", art), 0);
        CHECK(std::strcmp(art, "bn_cancl.tga") == 0);
        REQUIRE_EQ(screen.File.SeekBlock("Element0"), 0);
        int back = 0;
        CHECK_EQ(screen.File.ReadIdBoolean("UseBackPalette", back), 0); // "T"
        CHECK_EQ(back, 1);
        REQUIRE_EQ(screen.File.SeekBlock("Blocks"), 0);
        uint8_t block1[12] = {};
        CHECK_EQ(screen.File.GetIdUCharArrayElements("Block1"), 12u);
        CHECK_EQ(screen.File.ReadIdUCharArray("Block1", block1), 0); // "8, 9,10,11"
        CHECK_EQ(block1[7], 7);
        CHECK_EQ(block1[9], 9);
        CHECK_EQ(block1[11], 11);
    }

    {
        OpenFit palette("data\\palette\\palette.fit");
        REQUIRE_EQ(palette.Result, 0);
        REQUIRE_EQ(palette.File.SeekBlock("Range2"), 0);
        uint8_t base = 0;
        CHECK_EQ(palette.File.ReadIdUChar("BaseColorIndex", base), 0);
        CHECK_EQ(base, 32);
        REQUIRE_EQ(palette.File.SeekBlock("Ranges"), 0);
        int32_t ranges = 0;
        CHECK_EQ(palette.File.ReadIdLong("NumColorRanges", ranges), 0);
        CHECK_EQ(ranges, 9);
    }
}

TEST_CASE("game: every FIT file in the FastFiles opens and every block seeks")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();

    int opened = 0;
    int blocks = 0;

    for (const std::unique_ptr<MCFastFile>& fastFile : MCGameContext::Current().FastFiles().Files())
    {
        for (int32_t i = 0; i < fastFile->GetNumFiles(); ++i)
        {
            const std::string_view name = fastFile->GetEntries()[static_cast<size_t>(i)].GetName();

            if (name.size() < 4 || !MCIEquals(name.substr(name.size() - 4), ".fit"))
            {
                continue;
            }

            MCTest::Scope scope{std::string(name)};
            MCFitIniFile fit;
            CHECK_EQ(fit.Open(name), 0);

            for (int32_t b = 0; b < fit.GetNumBlocks(); ++b)
            {
                MCTest::Scope blockScope{std::string(fit.GetBlockName(b))};
                // A block named twice is found at its first copy; seeking still succeeds.
                CHECK_EQ(fit.SeekBlock(fit.GetBlockName(b)), 0);
                ++blocks;
            }

            fit.Close();
            ++opened;
        }
    }

    CHECK(opened > 700);
    CHECK(blocks > 10000);
}
