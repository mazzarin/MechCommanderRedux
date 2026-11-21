#include "stdafx.h"
#include "MCTest.h"
#include "MCFastFileFixture.h"
#include "MCFilePatchFixture.h"
#include "MCScratchFolder.h"
#include "TestGame.h"
#include "fakes/MCMemoryFileSource.h"
#include "fixtures/MCPakBytes.h"
#include "lib/MCDice.h"
#include "lib/MCDiskFile.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFilePatch.h"
#include "lib/MCFile.h"
#include "lib/MCFilesAhead.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCIDString.h"
#include "lib/MCLz.h"
#include "lib/MCPacketFile.h"
#include "lib/MCPriorityQueue.h"
#include "lib/MCRetryDelay.h"
#include "lib/MCThreadKeeper.h"
#include "lib/MCWorkers.h"
#include "main/MCGameContext.h"
#include "main/MCSystemConfig.h"
#include "platform/MCFileSystem.h"
#include "platform/MCThreadPriority.h"

namespace
{
    /// <summary>A scratch folder of the test's own under the temp folder, deleted with it.</summary>
    class ScratchFolder : public MCScratchFolder
    {
    public:
        explicit ScratchFolder(std::string_view name) : MCScratchFolder("mc_lib_tests", name) {}

        /// <summary>The path of <paramref name="name"/> in the folder, as a string the file layer takes.</summary>
        std::string operator/(std::string_view name) const
        {
            return MCFileSystem::ToUtf8(Path() / MCFileSystem::FromUtf8(name));
        }
    };

    std::vector<uint8_t> Bytes(std::string_view text)
    {
        return std::vector<uint8_t>(text.begin(), text.end());
    }

    void WriteFile(const std::filesystem::path& path, std::span<const uint8_t> bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    std::vector<uint8_t> RoundTrip(const std::vector<uint8_t>& data)
    {
        const std::vector<uint8_t> packed = LZCompress(data);
        std::vector<uint8_t> unpacked(data.size() + 16);
        unpacked.resize(static_cast<size_t>(LZDecomp(unpacked, packed)));
        return unpacked;
    }

    int32_t Get32(const std::vector<uint8_t>& bytes, size_t at)
    {
        return std::bit_cast<int32_t>(std::array{bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]});
    }

    /// <summary>Text that packs well.</summary>
    std::vector<uint8_t> Repetitive(size_t size)
    {
        std::vector<uint8_t> data(size);

        for (size_t i = 0; i < size; ++i)
        {
            data[i] = static_cast<uint8_t>("MechCommander "[i % 14]);
        }

        return data;
    }

    /// <summary>Bytes that don't pack.</summary>
    std::vector<uint8_t> Noise(size_t size, uint32_t seed)
    {
        std::vector<uint8_t> data(size);

        for (uint8_t& b : data)
        {
            seed = seed * 1103515245 + 12345;
            b = static_cast<uint8_t>(seed >> 16);
        }

        return data;
    }

    using namespace MCFastFileFixture;
    using namespace MCFilePatchFixture;
}

TEST_CASE("lz: packing then unpacking gives the input back")
{
    CHECK(RoundTrip(Repetitive(20000)) == Repetitive(20000));
    // Random bytes fill the dictionary and force clears at every code width.
    CHECK(RoundTrip(Noise(200000, 12345)) == Noise(200000, 12345));
    const std::vector<uint8_t> one{42};
    CHECK(RoundTrip(one) == one);
    const std::vector<uint8_t> runs(5000, 7);
    CHECK(RoundTrip(runs) == runs);
    CHECK(LZCompress(Repetitive(20000)).size() < 20000u / 4);
}

/// <summary>
/// An empty input is a clear code and an end code (9 bits each: three bytes), with no padding; the decoder stops
/// three bytes before the end, so it unpacks to nothing.
/// </summary>
TEST_CASE("lz: an empty input packs to a clear and an end code")
{
    const std::vector<uint8_t> packed = LZCompress({});
    REQUIRE_EQ(packed.size(), 3u);
    // 256 then 257, 9 bits each, LSB first.
    CHECK_EQ(packed[0], 0x00);
    CHECK_EQ(packed[1], 0x03);
    CHECK_EQ(packed[2], 0x02);
    std::array<uint8_t, 8> out{};
    CHECK_EQ(LZDecomp(out, packed), 0);
}

/// <summary>Damaged data never unpacks past the output (a port fix; the original trusted the stream).</summary>
TEST_CASE("lz: damaged data never writes past the output")
{
    for (uint32_t seed = 1; seed <= 50; ++seed)
    {
        MCTest::Scope scope(std::format("seed {}", seed));
        const std::vector<uint8_t> garbage = Noise(64 + seed * 7, seed);
        std::vector<uint8_t> guarded(48, 0xcd);
        const int32_t written = LZDecomp(std::span(guarded).first(40), garbage);
        CHECK(written >= 0 && written <= 40);
        CHECK(std::ranges::all_of(std::span(guarded).subspan(40), [](uint8_t b) { return b == 0xcd; }));
    }
}

/// <summary>
/// A FastFile's entries are found by name with case ignored (the first of two equal names), read raw or unpacked,
/// and an MCFile opens a path the disk lacks from the first FastFile that has it.
/// </summary>
TEST_CASE("fastfile: entries are found by name and read raw or unpacked")
{
    ScratchFolder folder("fastfile");
    const std::vector<uint8_t> text = Bytes("FITini\r\n[Block]\r\nl Value = 7\r\n\r\nFITend\r\n");
    const std::vector<uint8_t> packed = Repetitive(5000);
    WriteFile(folder.Path() / "test.fst", BuildFastFile({{"data\\a.txt", Bytes("first"), false},
                                                         {"DATA\\B.BIN", packed, true},
                                                         {"data\\A.TXT", Bytes("second"), false},
                                                         {"data\\c.fit", text, true}}));

    auto fastFile = MCFastFile::Create(folder / "test.fst");
    REQUIRE(fastFile.has_value());
    MCFastFile& archive = **fastFile;
    CHECK_EQ(archive.GetNumFiles(), 4);
    CHECK_EQ(archive.Find("Data\\b.bin"), std::optional<int32_t>(1));
    CHECK_EQ(archive.Find("data\\a.txt"), std::optional<int32_t>(0));
    CHECK(!archive.Find("data\\d.txt").has_value());
    CHECK_EQ(archive.GetEntries()[1].GetName(), std::string_view("DATA\\B.BIN"));
    CHECK(archive.GetEntries()[1].Size < archive.GetEntries()[1].RealSize);

    std::vector<uint8_t> data(packed.size());
    CHECK_EQ(archive.ReadEntry(1, data), static_cast<int32_t>(packed.size()));
    CHECK(data == packed);
    CHECK(!MCFastFile::Create(folder / "missing.fst").has_value());

    // Through the file layer: nothing on disk or in memory has these, so they come from the FastFiles.
    MCTestContextScope scope;
    MCFastFileSet& fastFiles = scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
    REQUIRE(fastFiles.Open(folder / "test.fst").has_value());
    CHECK(!fastFiles.Open(folder / "missing.fst").has_value());
    CHECK_EQ(fastFiles.Files().size(), 1u);
    scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());

    MCFile file;
    REQUIRE_EQ(file.Open("data\\b.bin"), NO_ERR);
    CHECK_EQ(file.GetLength(), static_cast<uint32_t>(packed.size()));
    CHECK_EQ(file.ReadByte(), packed[0]);
    REQUIRE_EQ(file.Seek(-1, SEEK_END), NO_ERR);
    CHECK_EQ(file.ReadByte(), packed.back());
    CHECK(file.Eof());
    file.Close();
    CHECK(!file.IsOpen());

    REQUIRE_EQ(file.Open("data\\a.txt"), NO_ERR);
    std::string first(5, '\0');
    CHECK_EQ(file.Read(std::span(reinterpret_cast<uint8_t*>(first.data()), 5)), 5);
    CHECK_EQ(first, std::string("first"));
    file.Close();

    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\c.fit"), NO_ERR);
    REQUIRE_EQ(fit.SeekBlock("Block"), 0);
    CHECK(fit.Read<int32_t>("Value") == MCFitResult<int32_t>(7));
    fit.Close();

    CHECK_EQ(file.Open("data\\nothere.bin"), FILE_NOT_FOUND);
    CHECK(file.GetFilename().empty());
}

namespace
{
    /// <summary>Bytes no two neighbouring stretches share, so a read from the wrong place shows.</summary>
    std::vector<uint8_t> MixedBytes(size_t size)
    {
        constexpr uint32_t Mix = 2654435761u;
        std::vector<uint8_t> bytes(size);
        std::ranges::generate(bytes, [at = uint32_t{0}]() mutable { return static_cast<uint8_t>((at++ * Mix) >> 24); });
        return bytes;
    }

    /// <summary><paramref name="bytes"/>' bytes at <paramref name="offset"/>, as many of <paramref name="size"/> as it has.</summary>
    std::vector<uint8_t> BytesAt(const std::vector<uint8_t>& bytes, size_t offset, size_t size)
    {
        const size_t from = std::min(offset, bytes.size());
        return std::vector<uint8_t>(bytes.begin() + static_cast<ptrdiff_t>(from),
                                    bytes.begin() + static_cast<ptrdiff_t>(std::min(bytes.size(), from + size)));
    }

    /// <summary>What <paramref name="file"/> reads at <paramref name="offset"/>, up to <paramref name="size"/> bytes.</summary>
    std::vector<uint8_t> DiskBytes(MCDiskFile& file, size_t offset, size_t size)
    {
        std::vector<uint8_t> got(size);
        got.resize(file.ReadAt(static_cast<uint32_t>(offset), got));
        return got;
    }
}

/// <summary>
/// Port-only: a buffered disk file (the original's loose files, read a byte, word or line at a time; the music and
/// movies) reads through a buffer of its own, the same bytes as a plain read: 16 KB first, doubled at each fill that
/// carries on where the last read ended, up to 256 KB; back to 16 KB after a jump; a read across the buffer's end takes
/// what it holds and carries on from there; a read of the next fill's size or more straight in, the buffer kept; short
/// past the end.
/// </summary>
TEST_CASE("disk file: a buffered file's buffer grows while it's read in order, and starts again small after a jump")
{
    ScratchFolder folder("disk-file-buffered");
    constexpr size_t First = MCDiskFile::FirstFillBytes;
    constexpr size_t Most = MCDiskFile::MostFillBytes;
    constexpr size_t FileBytes = Most * 8;
    const std::vector<uint8_t> bytes = MixedBytes(FileBytes);
    const std::filesystem::path path = folder.Path() / "bytes.bin";
    WriteFile(path, bytes);
    std::optional<MCDiskFile> opened = MCDiskFile::Open(path, MCDiskFileMode::Read, MCDiskFileReads::Buffered);
    REQUIRE(opened.has_value());
    MCDiskFile& file = *opened;
    CHECK_EQ(file.Length(), static_cast<uint32_t>(FileBytes));
    CHECK(!MCDiskFile::Open(folder.Path() / "missing.bin", MCDiskFileMode::Read, MCDiskFileReads::Buffered));

    // Byte by byte from the start, as the original reads: fills of 16, 32, 64, 128, 256 and 256 KB.
    constexpr size_t InOrder = First * (1 + 2 + 4 + 8 + 16) + Most;
    std::vector<uint8_t> sequential;

    for (size_t at = 0; at < InOrder; ++at)
    {
        std::array<uint8_t, 1> one{};

        if (file.Read(one) == one.size())
        {
            sequential.push_back(one.front());
        }
    }

    CHECK(sequential == BytesAt(bytes, 0, InOrder));
    CHECK_EQ(file.SystemReads(), size_t{6});

    // A jump: 16 KB again, then 32 KB carrying on from there.
    const size_t jump = Most * 6;
    CHECK(DiskBytes(file, jump, 2) == BytesAt(bytes, jump, 2));
    CHECK_EQ(file.SystemReads(), size_t{7});
    CHECK(DiskBytes(file, jump + 2, First - 2) == BytesAt(bytes, jump + 2, First - 2));
    CHECK_EQ(file.SystemReads(), size_t{7});
    CHECK(DiskBytes(file, jump + First, 1) == BytesAt(bytes, jump + First, 1));
    CHECK_EQ(file.SystemReads(), size_t{8});

    // A word across the buffer's end: its first byte from the buffer, the rest from the next fill (64 KB).
    const size_t bufferEnd = jump + First * 3;
    CHECK(DiskBytes(file, bufferEnd - 1, 2) == BytesAt(bytes, bufferEnd - 1, 2));
    CHECK_EQ(file.SystemReads(), size_t{9});
    CHECK(DiskBytes(file, bufferEnd + First * 4 - 2, 2) == BytesAt(bytes, bufferEnd + First * 4 - 2, 2));
    CHECK_EQ(file.SystemReads(), size_t{9});

    // A read of the next fill's size or more: straight in, the buffer kept; a fill carrying on from it doubles.
    CHECK(DiskBytes(file, 100, First * 8) == BytesAt(bytes, 100, First * 8));
    CHECK_EQ(file.SystemReads(), size_t{10});
    CHECK(DiskBytes(file, bufferEnd + 10, 2) == BytesAt(bytes, bufferEnd + 10, 2));
    CHECK_EQ(file.SystemReads(), size_t{10});
    CHECK(DiskBytes(file, 100 + First * 8, First * 8 - 1) == BytesAt(bytes, 100 + First * 8, First * 8 - 1));
    CHECK_EQ(file.SystemReads(), size_t{11});

    // Past the end, short: across it, at it, beyond it, and a large read across it.
    CHECK(DiskBytes(file, FileBytes - 5, 20) == BytesAt(bytes, FileBytes - 5, 5));
    CHECK(DiskBytes(file, FileBytes, 4).empty());
    CHECK(DiskBytes(file, FileBytes + 1000, 4).empty());
    CHECK(DiskBytes(file, FileBytes - 100, Most * 2) == BytesAt(bytes, FileBytes - 100, 100));

    // Read goes on from where the last read ended.
    CHECK(DiskBytes(file, 10, 2) == BytesAt(bytes, 10, 2));
    std::array<uint8_t, 4> next{};
    REQUIRE_EQ(file.Read(next), next.size());
    CHECK(std::vector<uint8_t>(next.begin(), next.end()) == BytesAt(bytes, 12, 4));
}

/// <summary>
/// Port-only: a buffered file of 256 KB or less is read whole at its first read, every read after it from memory; and
/// an archive's file (direct) reads each time exactly the bytes asked for, one call each, short past its end.
/// </summary>
TEST_CASE("disk file: a small file is read whole at its first read, and an archive's reads are the bytes asked")
{
    ScratchFolder folder("disk-file-small");
    constexpr size_t FileBytes = 100 * 1024;
    const std::vector<uint8_t> bytes = MixedBytes(FileBytes);
    const std::filesystem::path path = folder.Path() / "small.bin";
    WriteFile(path, bytes);

    std::optional<MCDiskFile> buffered = MCDiskFile::Open(path, MCDiskFileMode::Read, MCDiskFileReads::Buffered);
    REQUIRE(buffered.has_value());

    for (const auto& [offset, size] : {std::pair<size_t, size_t>{50000, 4},
                                       {0, 5},
                                       {FileBytes - 3, 3},
                                       {FileBytes - 1, 10},
                                       {FileBytes, 4},
                                       {20000, 30000},
                                       {7, 70000}})
    {
        MCTest::Scope scope(std::format("{} bytes at {}", size, offset));
        CHECK(DiskBytes(*buffered, offset, size) == BytesAt(bytes, offset, size));
    }

    CHECK_EQ(buffered->SystemReads(), size_t{1});

    std::optional<MCDiskFile> direct = MCDiskFile::Open(path, MCDiskFileMode::Read, MCDiskFileReads::Direct);
    REQUIRE(direct.has_value());
    CHECK(DiskBytes(*direct, 10, 2) == BytesAt(bytes, 10, 2));
    CHECK(DiskBytes(*direct, 12, 2) == BytesAt(bytes, 12, 2));
    CHECK_EQ(direct->SystemReads(), size_t{2});
    std::array<uint8_t, 4> next{};
    REQUIRE_EQ(direct->Read(next), next.size());
    CHECK(std::vector<uint8_t>(next.begin(), next.end()) == BytesAt(bytes, 14, 4));
    CHECK_EQ(direct->SystemReads(), size_t{3});
    CHECK(DiskBytes(*direct, FileBytes - 2, 10) == BytesAt(bytes, FileBytes - 2, 2));
    CHECK(DiskBytes(*direct, FileBytes * 2, 4).empty());

    // An empty file reads nothing.
    WriteFile(folder.Path() / "empty.bin", {});
    std::optional<MCDiskFile> empty =
        MCDiskFile::Open(folder.Path() / "empty.bin", MCDiskFileMode::Read, MCDiskFileReads::Buffered);
    REQUIRE(empty.has_value());
    CHECK(DiskBytes(*empty, 0, 4).empty());
}

/// <summary>
/// Port-only: a disk file's writes go at their offset and empty its buffer, so what it reads after is what it wrote;
/// writing past the end makes the file longer; a file created over one empties it; and another handle opens it while
/// it's open, as a stream's could.
/// </summary>
TEST_CASE("disk file: a write empties the buffer, and the bytes read back")
{
    ScratchFolder folder("disk-file-write");
    const std::filesystem::path path = folder.Path() / "written.bin";
    constexpr size_t Written = 1000;
    const std::vector<uint8_t> filler(Written, 'a');
    std::optional<MCDiskFile> file = MCDiskFile::Open(path, MCDiskFileMode::Create, MCDiskFileReads::Buffered);
    REQUIRE(file.has_value());
    CHECK_EQ(file->Write(filler), Written);
    CHECK_EQ(file->Length(), static_cast<uint32_t>(Written));

    // Read into the buffer, written over, read again: the new bytes.
    CHECK(DiskBytes(*file, 8, 7) == Bytes("aaaaaaa"));
    CHECK_EQ(file->WriteAt(10, Bytes("XYZ")), size_t{3});
    CHECK(DiskBytes(*file, 8, 7) == Bytes("aaXYZaa"));

    // Written on from the position the write left, and past the end: longer.
    CHECK_EQ(file->WriteAt(Written - 2, Bytes("yz")), size_t{2});
    CHECK_EQ(file->Write(Bytes("end")), size_t{3});
    CHECK_EQ(file->Length(), static_cast<uint32_t>(Written + 3));
    CHECK(DiskBytes(*file, Written - 4, 7) == Bytes("aayzend"));

    // Another handle opens it meanwhile and reads what was written.
    std::optional<MCDiskFile> reader = MCDiskFile::Open(path, MCDiskFileMode::Read, MCDiskFileReads::Direct);
    REQUIRE(reader.has_value());
    CHECK(DiskBytes(*reader, Written, 3) == Bytes("end"));
    reader.reset();
    file.reset();
    CHECK(ReadFile(path).size() == Written + 3);

    // Created again: empty.
    file = MCDiskFile::Open(path, MCDiskFileMode::Create, MCDiskFileReads::Buffered);
    REQUIRE(file.has_value());
    CHECK_EQ(file->Length(), uint32_t{0});
    CHECK(DiskBytes(*file, 0, 4).empty());
}

/// <summary>Seeks are checked against the length; values are little-endian; lines end at CR, CR LF or LF.</summary>
TEST_CASE("file: seek, typed reads and lines")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\values.bin", std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12, 0xff, 0xfe, 0x00, 0x00, 0x80, 0x3f});
    files.AddFile("data\\lines.txt", std::string_view("one\r\ntwo\rthree\nfour"));

    MCFile file;
    REQUIRE_EQ(file.Open("data\\values.bin"), NO_ERR);
    CHECK_EQ(file.FileSize(), 10u);
    CHECK_EQ(file.ReadLong(), 0x12345678);
    CHECK_EQ(file.ReadWord(), static_cast<int16_t>(-257));
    CHECK_EQ(file.ReadShort(), static_cast<int16_t>(0));
    CHECK_EQ(file.GetLogicalPosition(), 8u);
    CHECK_EQ(file.Seek(6), NO_ERR);
    CHECK_EQ(file.ReadFloat(), 1.0f);
    CHECK(file.Eof());
    CHECK_EQ(file.Seek(11), READ_PAST_EOF);
    CHECK_EQ(file.Seek(10), NO_ERR);
    CHECK_EQ(file.Seek(1, SEEK_END), READ_PAST_EOF);
    CHECK_EQ(file.Seek(-11, SEEK_END), READ_PAST_EOF);
    CHECK_EQ(file.Seek(-4, SEEK_END), NO_ERR);
    CHECK_EQ(file.Seek(2, SEEK_CUR), NO_ERR);
    CHECK_EQ(file.GetLogicalPosition(), 8u);
    CHECK_EQ(file.Seek(3, SEEK_CUR), READ_PAST_EOF);
    file.Skip(-8);
    CHECK_EQ(file.GetLogicalPosition(), 0u);
    std::array<uint8_t, 16> buffer{};
    CHECK_EQ(file.Read(buffer), 10);
    CHECK_EQ(buffer[9], 0x3f);
    file.Close();

    REQUIRE_EQ(file.Open("data\\lines.txt"), NO_ERR);
    CHECK_EQ(file.ReadLine(100), std::string("one"));
    CHECK_EQ(file.ReadLine(100), std::string("two"));
    CHECK_EQ(file.ReadLine(100), std::string("three"));
    uint8_t line[8] = {};
    CHECK_EQ(file.ReadLine(std::span(line), 7), 5);
    CHECK_EQ(std::string(reinterpret_cast<char*>(line)), std::string("four"));
    CHECK(file.Eof());
}

/// <summary>
/// Original behaviour (OB-135): a line longer than the reader looks at is cut there, and the byte after the cut is
/// lost: the next read starts one byte later.
/// </summary>
TEST_CASE("file: a line longer than the read is cut and loses the next byte")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\long.txt", std::string_view("abcdefghij\r\nnext"));
    MCFile file;
    REQUIRE_EQ(file.Open("data\\long.txt"), NO_ERR);
    CHECK_EQ(file.ReadLine(4), std::string("abcd"));
    CHECK_EQ(file.ReadLine(100), std::string("fghij"));
    CHECK_EQ(file.ReadLine(100), std::string("next"));
}

/// <summary>
/// A child file is a window into its parent from the parent's read position. A parent read whole into memory (a
/// FastFile entry, a test's memory file) takes no window children (TOO_MANY_CHILDREN, as the original), and closing
/// a parent closes its children.
/// </summary>
TEST_CASE("file: a child window reads its part of the parent")
{
    ScratchFolder folder("child");
    WriteFile(folder.Path() / "parent.bin", Bytes("0123456789abcdef"));

    MCFile parent;
    REQUIRE_EQ(parent.Open(folder / "parent.bin"), NO_ERR);
    REQUIRE_EQ(parent.Seek(4), NO_ERR);
    MCFile child;
    REQUIRE_EQ(child.Open(parent, 6), NO_ERR);
    CHECK_EQ(child.GetLength(), 6u);
    CHECK(child.IsChild());
    CHECK(!parent.IsChild());
    CHECK_EQ(child.GetFilename(), parent.GetFilename());
    std::string text(8, '\0');
    CHECK_EQ(child.Read(std::span(reinterpret_cast<uint8_t*>(text.data()), 8)), 8);
    CHECK_EQ(text.substr(0, 6), std::string("456789"));
    CHECK_EQ(child.Seek(7), READ_PAST_EOF);
    CHECK_EQ(child.WriteByte(1), 0);

    // A window takes windows of its own, and closes them with it.
    REQUIRE_EQ(child.Seek(2), NO_ERR);
    MCFile grandchild;
    REQUIRE_EQ(grandchild.Open(child, 3), NO_ERR);
    CHECK_EQ(grandchild.GetLength(), 3u);

    parent.Close();
    CHECK(!child.IsOpen());
    CHECK(!grandchild.IsOpen());
    CHECK(!child.IsChild());
    CHECK(child.GetFilename().empty());
    CHECK_EQ(child.GetLength(), 0u);
    CHECK_EQ(child.Read(std::span(reinterpret_cast<uint8_t*>(text.data()), 8)), 0);
    CHECK_EQ(grandchild.GetLength(), 0u);
    MCFile orphan;
    CHECK_EQ(orphan.Open(child, 1), TOO_MANY_CHILDREN);

    // A child that outlives its parent is closed too.
    MCFile survivor;
    {
        MCFile shortLived;
        REQUIRE_EQ(shortLived.Open(folder / "parent.bin"), NO_ERR);
        REQUIRE_EQ(survivor.Open(shortLived, 4), NO_ERR);
        CHECK(survivor.IsOpen());
    }

    CHECK(!survivor.IsOpen());
    CHECK_EQ(survivor.ReadByte(), 0);

    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\memory.bin", Bytes("0123456789"));
    MCFile memoryParent;
    REQUIRE_EQ(memoryParent.Open("data\\memory.bin"), NO_ERR);
    MCFile memoryChild;
    CHECK_EQ(memoryChild.Open(memoryParent, 4), TOO_MANY_CHILDREN);
}

/// <summary>
/// What the game writes goes to the user folder: an install file of the same name stays as it was, and reads find
/// the user's copy first. The same holds for a memory source's scratch folder.
/// </summary>
TEST_CASE("file: writes go to the user folder and never touch the install")
{
    ScratchFolder folder("overlay");
    std::filesystem::create_directories(folder.Path() / "install/data");
    const std::vector<uint8_t> original = Bytes("FITini\r\n[Prefs]\r\nl Volume = 3\r\n\r\nFITend\r\n");
    WriteFile(folder.Path() / "install/data/prefs.cfg", original);

    {
        MCTestContextScope scope;
        scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
        MCFileSystem::SetGameRoot(folder.Path() / "install");
        MCFileSystem::SetUserRoot(folder.Path() / "user");

        MCFitIniFile fit;
        REQUIRE_EQ(fit.Open("DATA\\PREFS.CFG"), 0);
        REQUIRE_EQ(fit.SeekBlock("Prefs"), 0);
        CHECK(fit.Read<int32_t>("Volume") == MCFitResult<int32_t>(3));
        fit.Close();

        MCFitIniFile out;
        REQUIRE_EQ(out.Create("data\\prefs.cfg"), 0);
        out.WriteBlock("Prefs");
        out.WriteIdLong("Volume", 9);
        out.Close();

        CHECK(std::filesystem::exists(folder.Path() / "user/data/prefs.cfg"));
        REQUIRE_EQ(fit.Open("data\\prefs.cfg"), 0);
        REQUIRE_EQ(fit.SeekBlock("Prefs"), 0);
        CHECK(fit.Read<int32_t>("Volume") == MCFitResult<int32_t>(9));
    }

    CHECK(ReadFile(folder.Path() / "install/data/prefs.cfg") == original);

    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\prefs.cfg", original);
    {
        MCFitIniFile out;
        REQUIRE_EQ(out.Create("data\\prefs.cfg"), 0);
        out.WriteBlock("Prefs");
        out.WriteIdLong("Volume", 5);
    }

    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\prefs.cfg"), 0);
    REQUIRE_EQ(fit.SeekBlock("Prefs"), 0);
    CHECK(fit.Read<int32_t>("Volume") == MCFitResult<int32_t>(5));
    fit.Close();
    REQUIRE(MCFileSystem::RemoveFile("data\\prefs.cfg"));
    const auto image = MCFileSystem::FindImage("data\\prefs.cfg");
    REQUIRE(image.has_value());
    CHECK(std::ranges::equal(*image, original));
}

/// <summary>
/// A launcher's match keeps the player's own settings: they're read from the player's folder once it holds them (until
/// then, from the launcher's user folder over the install, as before), and always saved there, never in the launcher's
/// folder or the install. Without a settings folder, they're the user folder's.
/// </summary>
TEST_CASE("file: the player's own settings are read and saved in their folder when a launcher gives its own")
{
    ScratchFolder folder("settings");
    std::filesystem::create_directories(folder.Path() / "install");
    const std::vector<uint8_t> original = Bytes("FITini\r\n[Prefs]\r\nl Volume = 3\r\n\r\nFITend\r\n");
    WriteFile(folder.Path() / "install/prefs.cfg", original);
    const std::filesystem::path player = folder.Path() / "player's own";
    const std::filesystem::path launcher = folder.Path() / "launcher";

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(folder.Path() / "install");
    MCFileSystem::SetUserRoot(launcher);
    CHECK_EQ(MCFileSystem::SettingsToRead("prefs.cfg"), "prefs.cfg");
    CHECK_EQ(MCFileSystem::SettingsToWrite("prefs.cfg"), "prefs.cfg");
    REQUIRE(MCFileSystem::SetSettingsRoot(player));

    // The settings folder is the process's: none once the test ends, however it ends.
    struct MCSettingsReset
    {
        ~MCSettingsReset() { MCFileSystem::SetSettingsRoot({}); }
    } reset;

    // The player's folder holds none yet: the install's, through the launcher's folder.
    CHECK_EQ(MCFileSystem::SettingsToRead("prefs.cfg"), "prefs.cfg");
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open(MCFileSystem::SettingsToRead("prefs.cfg")), 0);
    REQUIRE_EQ(fit.SeekBlock("Prefs"), 0);
    CHECK(fit.Read<int32_t>("Volume") == MCFitResult<int32_t>(3));
    fit.Close();

    {
        MCFitIniFile out;
        REQUIRE_EQ(out.Create(MCFileSystem::SettingsToWrite("prefs.cfg")), 0);
        out.WriteBlock("Prefs");
        out.WriteIdLong("Volume", 9);
    }

    CHECK(std::filesystem::exists(player / "prefs.cfg"));
    CHECK(!std::filesystem::exists(launcher / "prefs.cfg"));
    CHECK(ReadFile(folder.Path() / "install/prefs.cfg") == original);
    CHECK_EQ(MCFileSystem::SettingsToRead("prefs.cfg"), MCFileSystem::ToUtf8(player / "prefs.cfg"));
    REQUIRE_EQ(fit.Open(MCFileSystem::SettingsToRead("prefs.cfg")), 0);
    REQUIRE_EQ(fit.SeekBlock("Prefs"), 0);
    CHECK(fit.Read<int32_t>("Volume") == MCFitResult<int32_t>(9));
    fit.Close();

#ifdef _WIN32
    // A folder the game's paths don't reach (a network share's \\server\share, here the same folder by its \\?\
    // name): the user folder keeps them, as before.
    CHECK(!MCFileSystem::SetSettingsRoot(std::filesystem::path(L"\\\\?\\") += player));
    CHECK_EQ(MCFileSystem::SettingsToRead("prefs.cfg"), "prefs.cfg");
    CHECK_EQ(MCFileSystem::SettingsToWrite("prefs.cfg"), "prefs.cfg");
#endif
}

/// <summary>
/// The release doesn't list PREFS.CFG, so an update never replaces a taken-over folder's own: a copy with none anywhere
/// gets one where the game saves it, the folder's own that an earlier update set aside in Redux backup, else every
/// setting at its default; one the game can already read (the user folder's, the install's) is left as it is.
/// </summary>
TEST_CASE("file: a copy without prefs.cfg gets one where it's saved: the folder's own an update set aside, else the "
          "defaults")
{
    ScratchFolder folder("ensure-prefs");
    const std::filesystem::path install = folder.Path() / "install";
    const std::filesystem::path user = folder.Path() / "user";
    std::filesystem::create_directories(install);
    std::filesystem::create_directories(user);

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(install);
    MCFileSystem::SetUserRoot(user);

    // None anywhere (a new install): the defaults, in the user folder; the install untouched.
    REQUIRE(EnsurePrefs());
    REQUIRE(std::filesystem::is_regular_file(user / "prefs.cfg"));
    CHECK(!std::filesystem::exists(install / "prefs.cfg"));
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open(MCFileSystem::SettingsToRead("prefs.cfg")), 0);
    REQUIRE_EQ(fit.SeekBlock("MechCommander"), 0);
    CHECK(fit.Read<int32_t>("PrefsVersion") == MCFitResult<int32_t>(PrefsVersion));
    CHECK(!fit.Read<int32_t>("Difficulty").has_value());
    fit.Close();

    // The user folder's, once there, is the player's: left as it is, a set-aside one notwithstanding.
    const std::vector<uint8_t> players = Bytes("FITini\r\n[MechCommander]\r\nl Difficulty = 2\r\n\r\nFITend\r\n");
    WriteFile(user / "prefs.cfg", players);
    const std::vector<uint8_t> setAside =
        Bytes("FITini\r\n[MechCommander]\r\nl Difficulty = 1\r\nl MusicVolume = 40\r\n\r\nFITend\r\n");
    std::filesystem::create_directories(install / "Redux backup");
    WriteFile(install / "Redux backup/prefs.cfg", setAside);
    REQUIRE(EnsurePrefs());
    CHECK(ReadFile(user / "prefs.cfg") == players);

    // A taken-over folder's own in the install, none in the user folder: read from there as ever, nothing made.
    std::filesystem::remove(user / "prefs.cfg");
    const std::vector<uint8_t> folders = Bytes("FITini\r\n[MechCommander]\r\nl Difficulty = 3\r\n\r\nFITend\r\n");
    WriteFile(install / "prefs.cfg", folders);
    REQUIRE(EnsurePrefs());
    CHECK(!std::filesystem::exists(user / "prefs.cfg"));
    CHECK(ReadFile(install / "prefs.cfg") == folders);

    // None but the one an update set aside (release 8 replaced a taken-over folder's, and release 9 drops the default
    // it put there): carried over to the user folder as it was, and kept in Redux backup for the uninstall.
    std::filesystem::remove(install / "prefs.cfg");
    REQUIRE(EnsurePrefs());
    CHECK(ReadFile(user / "prefs.cfg") == setAside);
    CHECK(ReadFile(install / "Redux backup/prefs.cfg") == setAside);

    // A launcher's match with the player's own folder and none anywhere: made there, never in the launcher's.
    std::filesystem::remove(user / "prefs.cfg");
    std::filesystem::remove(install / "Redux backup/prefs.cfg");
    const std::filesystem::path player = folder.Path() / "player's own";
    REQUIRE(MCFileSystem::SetSettingsRoot(player));

    struct MCSettingsReset
    {
        ~MCSettingsReset() { MCFileSystem::SetSettingsRoot({}); }
    } reset;

    REQUIRE(EnsurePrefs());
    CHECK(std::filesystem::is_regular_file(player / "prefs.cfg"));
    CHECK(!std::filesystem::exists(user / "prefs.cfg"));
    CHECK_EQ(MCFileSystem::SettingsToRead("prefs.cfg"), MCFileSystem::ToUtf8(player / "prefs.cfg"));
}

/// <summary>
/// Saves are the player's files: deleting one removes it wherever it is, the install's save folder included (the
/// retail game ships saves there), and the user folder's copy with it. Other files of the install stay.
/// </summary>
TEST_CASE("file: deleting a save removes the install's copy too")
{
    ScratchFolder folder("delete-save");
    std::filesystem::create_directories(folder.Path() / "install/data/savegame");
    std::filesystem::create_directories(folder.Path() / "user/data/savegame");
    WriteFile(folder.Path() / "install/data/savegame/shipped.sav", Bytes("install"));
    WriteFile(folder.Path() / "install/data/savegame/both.sav", Bytes("install"));
    WriteFile(folder.Path() / "user/data/savegame/both.sav", Bytes("user"));
    WriteFile(folder.Path() / "install/data/savegame/other.sav", Bytes("install"));

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(folder.Path() / "install");
    MCFileSystem::SetUserRoot(folder.Path() / "user");

    // The listing tells which folders have each save.
    const std::vector<MCFoundFile> listed = MCFileSystem::FindFileSources("data\\savegame\\*.sav");
    REQUIRE_EQ(listed.size(), size_t{3});

    const auto sources = [&](std::string_view name)
    {
        const auto file =
            std::ranges::find_if(listed, [&](const MCFoundFile& f) { return MCPort::StrICmp(f.Name, name) == 0; });
        return file == listed.end() ? std::pair(false, false) : std::pair(file->InUserFolder, file->InGameFolder);
    };

    CHECK(sources("shipped.sav") == std::pair(false, true));
    CHECK(sources("both.sav") == std::pair(true, true));
    CHECK(sources("other.sav") == std::pair(false, true));

    // The plain delete leaves the install alone.
    CHECK(!MCFileSystem::RemoveFile("data\\savegame\\shipped.sav"));
    CHECK(MCFileSystem::Exists("data\\savegame\\shipped.sav"));

    CHECK(MCFileSystem::RemoveSave("DATA\\SAVEGAME\\SHIPPED.SAV"));
    CHECK(!std::filesystem::exists(folder.Path() / "install/data/savegame/shipped.sav"));
    CHECK(MCFileSystem::RemoveSave("data\\savegame\\both.sav"));
    CHECK(!std::filesystem::exists(folder.Path() / "install/data/savegame/both.sav"));
    CHECK(!std::filesystem::exists(folder.Path() / "user/data/savegame/both.sav"));
    CHECK(MCFileSystem::RemoveSave("data\\savegame\\missing.sav"));

    const std::vector<std::string> left = MCFileSystem::FindFiles("data\\savegame\\*.sav");
    REQUIRE_EQ(left.size(), size_t{1});
    CHECK_EQ(left[0], std::string("other.sav"));
}

/// <summary>
/// A relative game path stays inside its root (issue #25): ".." at the root goes nowhere, as at a drive's root, and a
/// part naming a drive is skipped, so a name from a save, a mission or the network can't reach other files. ".." below
/// the root still climbs.
/// </summary>
TEST_CASE("file: a relative path never leaves the user or game folder")
{
    ScratchFolder folder("contain");
    std::filesystem::create_directories(folder.Path() / "install/data");
    WriteFile(folder.Path() / "install/data/shared.cfg", Bytes("install"));
    WriteFile(folder.Path() / "secret.txt", Bytes("outside"));

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(folder.Path() / "install");
    MCFileSystem::SetUserRoot(folder.Path() / "user");

    CHECK(MCFileSystem::ResolveWrite("..\\..\\evil.txt") == MCFileSystem::ResolveWrite("evil.txt"));
    CHECK(MCFileSystem::ResolveWrite("data\\..\\..\\evil.txt") == MCFileSystem::ResolveWrite("evil.txt"));
    CHECK(MCFileSystem::ResolveWrite("data\\..\\evil.txt") == MCFileSystem::ResolveWrite("evil.txt"));
    CHECK(MCFileSystem::ResolveWrite("data/C:/Windows/evil.txt") ==
          MCFileSystem::ResolveWrite("data\\Windows\\evil.txt"));
    CHECK(!MCFileSystem::Exists("..\\secret.txt"));
    CHECK(MCFileSystem::Exists("data\\..\\data\\shared.cfg"));

    // A name from another machine is checked before anything opens it: relative, no "..", drive or stream.
    CHECK(MCFileSystem::IsContainedPath("data\\savegame\\duel.mpk"));
    CHECK(MCFileSystem::IsContainedPath("duel.mpk"));
    CHECK(MCFileSystem::IsContainedPath("data/..duel../x.mpk"));
    CHECK(!MCFileSystem::IsContainedPath(""));
    CHECK(!MCFileSystem::IsContainedPath("..\\duel.mpk"));
    CHECK(!MCFileSystem::IsContainedPath("data\\..\\..\\duel.mpk"));
    CHECK(!MCFileSystem::IsContainedPath("data/../duel.mpk"));
    CHECK(!MCFileSystem::IsContainedPath("C:\\Windows\\win.ini"));
    CHECK(!MCFileSystem::IsContainedPath("data\\C:duel.mpk"));
    CHECK(!MCFileSystem::IsContainedPath("\\\\server\\share\\duel.mpk"));
    CHECK(!MCFileSystem::IsContainedPath("/etc/passwd"));
    CHECK(!MCFileSystem::IsContainedPath("duel.mpk:stream"));

    // A write that tries to climb out of the user folder lands inside it; the install and the folder above stay.
    REQUIRE(MCFileSystem::CopyFile("data\\shared.cfg", "..\\..\\secret.txt"));
    CHECK(ReadFile(folder.Path() / "secret.txt") == Bytes("outside"));
    CHECK(ReadFile(folder.Path() / "user/secret.txt") == Bytes("install"));
}

/// <summary>
/// Folders list in name order (MCFileSystem::NameLess: ASCII letters upper-cased, then the bytes; NTFS's order, '_'
/// after the letters), and FastFiles are searched in that order however they were opened: of archives holding one
/// name, the first by name gives it, on every system. A loose file still comes first.
/// </summary>
TEST_CASE("fastfile: archives are searched in name order, whatever order they're listed or opened in")
{
    CHECK(MCFileSystem::NameLess("A.FST", "a2.fst"));
    CHECK(MCFileSystem::NameLess("b.fst", "_z.fst"));
    CHECK(MCFileSystem::NameLess("A.FST", "a.fst"));
    CHECK(!MCFileSystem::NameLess("a.fst", "A.FST"));
    CHECK(!MCFileSystem::NameLess("A.FST", "A.FST"));
    CHECK(MCFileSystem::NameLess("z.fst", "\xC3\xA9.fst"));

    ScratchFolder folder("fastfile-order");
    const std::vector<std::string> sorted = {"A.FST", "a2.fst", "b.fst", "_z.fst"};
    constexpr std::string_view SharedName = "data\\same.txt";

    for (const std::string& name : sorted | std::views::reverse)
    {
        WriteFile(folder.Path() / name, BuildFastFile({{std::string(SharedName), Bytes(name), false}}));
    }

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(folder.Path());
    CHECK(MCFileSystem::FindFiles("*.fst") == sorted);

    for (const bool backwards : {false, true})
    {
        MCTest::Scope opened(backwards ? "opened backwards" : "opened in name order");
        MCFastFileSet& fastFiles = scope.Context().SetFastFiles(std::make_unique<MCFastFileSet>());
        std::vector<std::string> order = sorted;

        if (backwards)
        {
            std::ranges::reverse(order);
        }

        for (const std::string& name : order)
        {
            REQUIRE(fastFiles.Open(folder / name).has_value());
        }

        CHECK(fastFiles.Read(SharedName) == std::optional(Bytes("A.FST")));
    }

    std::filesystem::create_directories(folder.Path() / "data");
    WriteFile(folder.Path() / "data" / "same.txt", Bytes("loose"));
    MCFile file;
    REQUIRE_EQ(file.Open(SharedName), NO_ERR);
    std::vector<uint8_t> read(file.GetLength());
    file.Read(read);
    CHECK(read == Bytes("loose"));
}

/// <summary>
/// Game paths are UTF-8, so names outside the ANSI code page (Windows') resolve, list and open, in an install whose
/// own folder has such a name too. A missing CD movie path once listed a drive's root holding one and threw.
/// </summary>
TEST_CASE("file: names outside the ANSI code page resolve, list and open")
{
    ScratchFolder folder("unicode-names");
    const std::filesystem::path install = folder.Path() / u8"游戏 install";
    std::filesystem::create_directories(install / "data");
    WriteFile(install / "data/plain.smk", Bytes("plain"));
    WriteFile(install / u8"中文.txt", Bytes("root"));
    WriteFile(install / u8"data/中文.smk", Bytes("data"));

    MCTestContextScope scope;
    scope.Context().SetFiles(std::make_unique<MCDiskFileSource>());
    MCFileSystem::SetGameRoot(install);

    // A part that isn't there as spelled lists its folder for a match in another case.
    CHECK(!MCFileSystem::Exists("movies\\logos1.smk"));
    CHECK(MCFileSystem::Exists("DATA\\PLAIN.SMK"));
    CHECK(MCFileSystem::Exists("DATA\\中文.SMK"));

    const std::vector<std::string> listed = MCFileSystem::FindFiles("data\\*.smk");
    CHECK(std::ranges::contains(listed, std::string("plain.smk")));
    CHECK(std::ranges::contains(listed, std::string("中文.smk")));

    MCFile file;
    REQUIRE_EQ(file.Open("data\\中文.smk"), NO_ERR);
    std::vector<uint8_t> read(file.GetLength());
    file.Read(read);
    CHECK(read == Bytes("data"));
}

/// <summary>
/// The packet file format: a magic (or checksum) word, the offset of the first packet (so the packet count is
/// offset / 4 - 2), a table of offsets whose top 3 bits are the storage, then the packets; an LZ packet starts with
/// its unpacked size. A packet never written reads as an empty NUL packet.
/// </summary>
TEST_CASE("packet file: written packets read back raw and packed")
{
    ScratchFolder folder("packet");
    const std::vector<uint8_t> repetitive = Repetitive(3000);
    const std::vector<uint8_t> noise = Noise(500, 77);
    const std::vector<uint8_t> small = Bytes("abc");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "test.pak"), NO_ERR);
        out.Reserve(4);
        CHECK_EQ(out.WritePacket(0, noise, MCPacketStorage::Nul), 500);
        CHECK(out.GetStorageType() == MCPacketStorage::Raw);
        out.WritePacket(1, repetitive, MCPacketStorage::Nul);
        CHECK(out.GetStorageType() == MCPacketStorage::Lzd);
        out.WritePacket(2, small, MCPacketStorage::Lzd);
        CHECK(out.GetStorageType() == MCPacketStorage::Lzd);
        CHECK_EQ(out.WritePacket(4, small, MCPacketStorage::Raw), 0);
    }

    const std::vector<uint8_t> raw = ReadFile(folder.Path() / "test.pak");
    REQUIRE(raw.size() > 24);
    CHECK_EQ(Get32(raw, 0), MCPacketFile::PacketFileMagic);
    CHECK_EQ(Get32(raw, 4), 4 * 4 + 8);
    // Packet 0 is stored raw right after the table; packet 1 is LZD (storage 2 in the top bits).
    CHECK_EQ(static_cast<uint32_t>(Get32(raw, 8)), 24u);
    CHECK_EQ(static_cast<uint32_t>(Get32(raw, 12)) >> 29, 2u);
    CHECK_EQ(static_cast<uint32_t>(Get32(raw, 12)) & 0x1fffffff, 524u);
    CHECK_EQ(Get32(raw, 524), 3000);

    MCPacketFile pak;
    REQUIRE_EQ(pak.Open(folder / "test.pak"), NO_ERR);
    REQUIRE_EQ(pak.GetNumPackets(), 4);

    REQUIRE_EQ(pak.SeekPacket(0), NO_ERR);
    CHECK(pak.GetStorageType() == MCPacketStorage::Raw);
    CHECK_EQ(pak.GetPacketSize(), 500);
    std::vector<uint8_t> buffer(500);
    CHECK_EQ(pak.ReadPacket(0, buffer), 500);
    CHECK(buffer == noise);

    REQUIRE_EQ(pak.SeekPacket(1), NO_ERR);
    CHECK(pak.GetStorageType() == MCPacketStorage::Lzd);
    CHECK_EQ(pak.GetPacketSize(), 3000);
    CHECK(pak.GetPackedPacketSize() < 3000);
    buffer.assign(3000, 0);
    CHECK_EQ(pak.ReadPacket(1, buffer), 3000);
    CHECK(buffer == repetitive);

    buffer.assign(3, 0);
    CHECK_EQ(pak.ReadPacket(2, buffer), 3);
    CHECK(buffer == small);

    REQUIRE_EQ(pak.SeekPacket(3), NO_ERR);
    CHECK(pak.GetStorageType() == MCPacketStorage::Nul);
    CHECK_EQ(pak.GetPacketSize(), 0);
    CHECK_EQ(pak.ReadPacket(3, buffer), 0);
    CHECK(pak.SeekPacket(-1) != NO_ERR);
}

/// <summary>A checksummed packet file holds the sum of every byte after the first word, and fails to open when one changes.</summary>
TEST_CASE("packet file: a checksum guards the contents")
{
    ScratchFolder folder("checksum");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "sum.pak"), NO_ERR);
        out.Reserve(2, true);
        out.WritePacket(0, Bytes("hello"), MCPacketStorage::Raw);
        out.WritePacket(1, Bytes("world!"), MCPacketStorage::Raw);
    }

    std::vector<uint8_t> raw = ReadFile(folder.Path() / "sum.pak");
    REQUIRE_EQ(raw.size(), 8u + 2 * 4 + 11);
    CHECK_EQ(Get32(raw, 0), std::accumulate(raw.begin() + 4, raw.end(), 0));

    {
        MCPacketFile pak;
        REQUIRE_EQ(pak.Open(folder / "sum.pak"), NO_ERR);
        CHECK_EQ(pak.GetNumPackets(), 2);
        std::vector<uint8_t> buffer(6);
        REQUIRE_EQ(pak.SeekPacket(1), NO_ERR);
        CHECK_EQ(pak.ReadPacket(-1, buffer), 6);
        CHECK(buffer == Bytes("world!"));
    }

    raw.back() ^= 1;
    WriteFile(folder.Path() / "sum.pak", raw);
    MCPacketFile bad;
    CHECK_EQ(bad.Open(folder / "sum.pak"), static_cast<int32_t>(0xBADF0004));
}

/// <summary>
/// Port-only: a file held to a most unpacked size (as a save is, MCPacketFile::SetMostUnpacked) refuses an LZ packet
/// claiming more, though its LZ data could make it; held to more it opens, and its hold ends as it closes.
/// </summary>
TEST_CASE("packet file: a file held to a most unpacked size refuses an LZ packet claiming more")
{
    constexpr int32_t Unpacked = 10000;
    ScratchFolder folder("mostunpacked");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "held.pak"), NO_ERR);
        out.Reserve(1);
        out.WritePacket(0, Repetitive(Unpacked), MCPacketStorage::Lzd);
    }

    MCPacketFile pak;
    REQUIRE_EQ(pak.Open(folder / "held.pak"), NO_ERR);
    pak.SetMostUnpacked(Unpacked - 1);
    CHECK(pak.SeekPacket(0) != NO_ERR);
    CHECK_EQ(pak.GetPacketSize(), 0);
    pak.SetMostUnpacked(Unpacked);
    REQUIRE_EQ(pak.SeekPacket(0), NO_ERR);
    CHECK_EQ(pak.GetPacketSize(), Unpacked);

    pak.SetMostUnpacked(Unpacked - 1);
    pak.Close();
    REQUIRE_EQ(pak.Open(folder / "held.pak"), NO_ERR);
    CHECK_EQ(pak.SeekPacket(0), NO_ERR);
}

namespace
{
    /// <summary>
    /// Runs <paramref name="check"/> on the packet file <paramref name="bytes"/> opened from a file on disk (in the
    /// scratch folder <paramref name="name"/>), then from a test's file in memory (read whole).
    /// </summary>
    void OnDiskAndInMemory(std::string_view name, const std::vector<uint8_t>& bytes,
                           const std::function<void(MCPacketFile&)>& check)
    {
        {
            MCTest::Scope place("on disk");
            ScratchFolder folder(name);
            WriteFile(folder.Path() / "test.pak", bytes);
            MCPacketFile pak;
            REQUIRE_EQ(pak.Open(folder / "test.pak"), NO_ERR);
            check(pak);
        }

        MCTest::Scope place("in memory");
        MCTestContextScope scope;
        MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
        files.AddFile("data\\test.pak", bytes);
        MCPacketFile pak;
        REQUIRE_EQ(pak.Open("data\\test.pak"), NO_ERR);
        check(pak);
    }

    /// <summary>Packets <paramref name="first"/> to <paramref name="last"/>.</summary>
    std::vector<int32_t> PacketsFrom(int32_t first, int32_t last)
    {
        std::vector<int32_t> packets(static_cast<size_t>(last - first + 1));
        std::iota(packets.begin(), packets.end(), first);
        return packets;
    }
}

/// <summary>
/// Port-only: packets read together (MCPacketFile::ReadPackets) are what ReadPacket reads of each, in few reads: in the
/// order they lie in the file, neighbours within 64 KB of each other in one read whatever the order asked, before the
/// first asked or after it; a gap past 64 KB two reads; no read past 1 MB, a bigger packet read alone; nothing read for
/// a Nul or empty packet; a packet asked twice read once. On disk, each thread its own handle, and in memory.
/// </summary>
TEST_CASE("packet file: packets read together come in few reads, neighbours within 64 KB in one, none past 1 MB")
{
    using namespace MCPakBytes;
    constexpr size_t Gap = MCDiskFile::RunGapBytes;
    constexpr size_t Most = MCDiskFile::RunMostBytes;
    constexpr size_t Small = 3000;
    constexpr size_t Wide = 60u * 1024u;
    constexpr int32_t WideCount = 20;
    std::vector<MCStored> stored;

    // 0-7: small packets back to back, raw and LZ in turn.
    for (uint32_t i = 0; i < 8; ++i)
    {
        stored.push_back(i % 2 == 0 ? Raw(Filled(Small, i)) : Lz(Filled(Small, i)));
    }

    // 8: a byte more than a read crosses; 9 past it. 10-29: 1.2 MB of neighbours.
    stored.push_back(Raw(Filled(Gap + 1, 8)));
    stored.push_back(Raw(Filled(1000, 9)));

    for (int32_t i = 0; i < WideCount; ++i)
    {
        stored.push_back(Raw(Filled(Wide, static_cast<uint32_t>(10 + i))));
    }

    // 30: bigger than a read. 31: Nul, 32: empty. 33, then just what a read crosses (34), then 35.
    stored.push_back(Raw(Filled(Most + 1, 30)));
    stored.push_back(As(MCPacketStorage::Nul, {}));
    stored.push_back(Raw({}));
    stored.push_back(Raw(Filled(100, 33)));
    stored.push_back(Raw(Filled(Gap, 34)));
    stored.push_back(Raw(Filled(100, 35)));

    OnDiskAndInMemory("packets-together", Pak(stored),
                      [&](MCPacketFile& pak)
                      {
                          // Read together, each as ReadPacket reads it alone.
                          const auto together = [&](const std::vector<int32_t>& packets)
                          {
                              MCPacketBatch batch = pak.ReadPackets(packets);
                              REQUIRE_EQ(batch.Packets.size(), packets.size());

                              for (size_t i = 0; i < packets.size(); ++i)
                              {
                                  MCTest::Scope packet(std::format("packet {}", packets[i]));
                                  REQUIRE(batch.Packets[i].has_value());
                                  CHECK(*batch.Packets[i] == ReadAlone(pak, packets[i]));
                              }

                              return batch;
                          };

                          CHECK_EQ(together({5, 2, 7, 0, 3, 6, 1, 4}).Reads, 1u);
                          CHECK_EQ(together({4, 0, 7}).Reads, 1u);
                          CHECK_EQ(together({7, 9}).Reads, 2u);
                          CHECK_EQ(together({35, 33}).Reads, 1u);
                          const MCPacketBatch wide = together(PacketsFrom(10, 10 + WideCount - 1));
                          CHECK_EQ(wide.Reads, 2u);
                          CHECK_EQ(wide.MostReadBytes, Most / Wide * Wide);
                          const MCPacketBatch alone = together({29, 30, 33});
                          CHECK_EQ(alone.Reads, 3u);
                          CHECK_EQ(alone.MostReadBytes, Most + 1);
                          const MCPacketBatch empty = together({31, 32});
                          CHECK_EQ(empty.Reads, 0u);
                          CHECK(empty.Packets[0]->empty());
                          CHECK(empty.Packets[1]->empty());
                          CHECK_EQ(together({3, 5, 3}).Reads, 1u);
                          // 0-24 (to 1 MB), 25-29, 30 alone, 32-35.
                          CHECK_EQ(together(PacketsFrom(0, 35)).Reads, 4u);
                      });
}

/// <summary>
/// Port-only: runs of a file's packets wanted as stored (MCPacketsWanted::Stored: a mech's art, for its stops' hash)
/// come with the packets read together, each as ReadStoredPackets gives it (a Nul packet's bytes in it), in the same
/// reads: a packet both wanted and in a run, or in two runs, read once, and one bigger than a read in one read however
/// it's wanted. A run of none, or one past the table's end, comes as nothing, the caller's to read.
/// </summary>
TEST_CASE("packet file: runs wanted as stored come in the packets' reads, as ReadStoredPackets gives them")
{
    using namespace MCPakBytes;
    constexpr size_t Most = MCDiskFile::RunMostBytes;
    constexpr size_t Small = 3000;
    constexpr size_t NulBytes = 100;
    constexpr size_t Last = 100;
    // The small packets (0-5), then the Nul one (6), the one bigger than a read (7) and the last (8).
    constexpr int32_t SmallCount = 6;
    constexpr int32_t Nul = SmallCount;
    constexpr int32_t Big = Nul + 1;
    std::vector<MCStored> stored;

    for (uint32_t i = 0; i < static_cast<uint32_t>(SmallCount); ++i)
    {
        stored.push_back(i % 2 == 0 ? Raw(Filled(Small, i)) : Lz(Filled(Small, i)));
    }

    stored.push_back(As(MCPacketStorage::Nul, Filled(NulBytes, Nul)));
    stored.push_back(Raw(Filled(Most + 1, Big)));
    stored.push_back(Raw(Filled(Last, Big + 1)));
    const auto count = static_cast<int32_t>(stored.size());

    OnDiskAndInMemory(
        "packets-stored", Pak(stored),
        [&](MCPacketFile& pak)
        {
            const std::vector<int32_t> packets{1, 3, Big, Big + 1};
            // Two runs sharing 2 and 3, one from the Nul packet to the end, one of none, one past the end.
            const std::vector<MCStoredRun> runs{
                MCStoredRun{.First = 0, .Count = 4}, MCStoredRun{.First = 2, .Count = 5},
                MCStoredRun{.First = Nul, .Count = 3}, MCStoredRun{.First = 0, .Count = 0},
                MCStoredRun{.First = count - 2, .Count = 3}};
            const std::set<size_t> none{3, 4};
            const std::array<MCPacketsWanted, 1> withPackets{MCPacketsWanted{pak, packets, runs}};
            const std::array<MCPacketsWanted, 1> runsAlone{MCPacketsWanted{pak, {}, runs}};

            for (const auto& [wanted, name] : {std::pair{withPackets, "with packets"}, std::pair{runsAlone, "alone"}})
            {
                MCTest::Scope scope(name);
                const std::vector<MCPacketBatch> batches = MCPacketFile::ReadPackets(wanted);
                REQUIRE_EQ(batches.size(), 1u);
                const MCPacketBatch& batch = batches[0];
                // 0-6 in one read, 7 alone, 8 alone (7 fills a read).
                CHECK_EQ(batch.Reads, 3u);
                REQUIRE_EQ(batch.Packets.size(), wanted[0].Packets.size());

                for (size_t i = 0; i < batch.Packets.size(); ++i)
                {
                    MCTest::Scope packet(std::format("packet {}", packets[i]));
                    REQUIRE(batch.Packets[i].has_value());
                    CHECK(*batch.Packets[i] == ReadAlone(pak, packets[i]));
                }

                REQUIRE_EQ(batch.Stored.size(), runs.size());

                for (size_t i = 0; i < runs.size(); ++i)
                {
                    MCTest::Scope run(std::format("run from {}, {} of them", runs[i].First, runs[i].Count));
                    const std::vector<uint8_t> alone = pak.ReadStoredPackets(runs[i].First, runs[i].Count);
                    REQUIRE_EQ(batch.Stored[i].has_value(), !none.contains(i));
                    CHECK_EQ(alone.empty(), none.contains(i));

                    if (batch.Stored[i].has_value())
                    {
                        CHECK(*batch.Stored[i] == alone);
                    }
                }

                // The Nul packet's stored bytes are in its runs: the table, then 6's, 7's and 8's bytes.
                REQUIRE(batch.Stored[2].has_value());
                CHECK_EQ(batch.Stored[2]->size(), 3 * sizeof(uint32_t) + NulBytes + Most + 1 + Last);
            }
        });
}

/// <summary>
/// Port-only: packets read together leave the caller what ReadPacket doesn't read plainly, to read itself: a packet
/// stored other than raw, LZ or Nul (SeekPacket keeps the size the packet before gave), an LZ packet claiming more than
/// its data could make or the file is held to (issue #25), one that doesn't unpack to its size or is too short to hold
/// it, a packet at the header, one ending past the file's end or starting past it (a file cut short of its table), a
/// number outside the table. The rest still come; an LZ packet of nothing comes empty.
/// </summary>
TEST_CASE("packet file: packets read together leave the caller what ReadPacket doesn't read plainly")
{
    using namespace MCPakBytes;
    constexpr uint32_t Fwf = 1;
    constexpr uint32_t Huffman = 3;
    constexpr uint32_t Zlib = 4;
    constexpr std::array<uint32_t, 2> Unnamed{5, 6};
    const std::vector<uint8_t> data = Filled(4000, 6);
    const auto lzMost = static_cast<int32_t>((Lz(data).Bytes.size() - sizeof(int32_t)) * 4096);
    // The packet at the header first: the one before a packet at 0 would end before it starts.
    const std::vector<MCStored> stored{
        MCStored{.Bytes = Filled(100, 0), .Offset = 0},
        Lz(Filled(5000, 1)),
        MCStored{.Storage = Fwf, .Bytes = Filled(300, 2)},
        MCStored{.Storage = Huffman, .Bytes = Filled(300, 3)},
        MCStored{.Storage = Zlib, .Bytes = Filled(300, 4)},
        MCStored{.Storage = Unnamed[0], .Bytes = Filled(300, 5)},
        MCStored{.Storage = Unnamed[1], .Bytes = Filled(300, 6)},
        Lz(data, lzMost + 1),
        Lz(data, static_cast<int32_t>(data.size()) + 50),
        As(MCPacketStorage::Lzd, {0, 0}),
        Raw(Filled(100, 10)),
        MCStored{.Offset = 0x100000},
        Raw(Filled(100, 12)),
        Lz({}),
    };

    const std::set<int32_t> plain{1, 12, 13};

    OnDiskAndInMemory("packets-plainly", Pak(stored),
                      [&](MCPacketFile& pak)
                      {
                          REQUIRE_EQ(pak.GetNumPackets(), static_cast<int32_t>(stored.size()));
                          std::vector<int32_t> packets = PacketsFrom(-1, pak.GetNumPackets());
                          const MCPacketBatch batch = pak.ReadPackets(packets);

                          for (size_t i = 0; i < packets.size(); ++i)
                          {
                              MCTest::Scope packet(std::format("packet {}", packets[i]));
                              REQUIRE_EQ(batch.Packets[i].has_value(), plain.contains(packets[i]));

                              if (batch.Packets[i].has_value())
                              {
                                  CHECK(*batch.Packets[i] == ReadAlone(pak, packets[i]));
                              }
                          }

                          // Packets from -1: packet 13, an LZ packet of nothing, comes empty.
                          CHECK(batch.Packets[13 + 1]->empty());
                          CHECK_EQ(batch.Packets[1 + 1]->size(), 5000u);
                          // Held to less than packet 1 unpacks to.
                          pak.SetMostUnpacked(4999);
                          CHECK(!pak.ReadPackets(std::vector{1}).Packets[0].has_value());
                          CHECK(ReadAlone(pak, 1).empty());
                      });
}

/// <summary>
/// Port-only: a packet file that can't hand out readers of its own (a file written, a file closed, a file on disk
/// changed since it opened) leaves every packet read together to the caller.
/// </summary>
TEST_CASE("packet file: packets read together are the caller's from a file written, closed or changed")
{
    using namespace MCPakBytes;
    ScratchFolder folder("packets-readers");
    const std::vector<uint8_t> bytes = Pak({Raw(Filled(100, 0)), Raw(Filled(100, 1))});
    WriteFile(folder.Path() / "test.pak", bytes);
    const std::vector<int32_t> both{0, 1};
    const auto noneRead = [&](const MCPacketFile& pak)
    {
        const MCPacketBatch batch = pak.ReadPackets(both);
        return !pak.OpenReader().has_value() && !batch.Packets[0].has_value() && !batch.Packets[1].has_value() &&
               batch.Reads == 0;
    };

    MCPacketFile written;
    REQUIRE_EQ(written.Create(folder / "written.pak"), NO_ERR);
    written.Reserve(2);
    written.WritePacket(0, Filled(10, 2), MCPacketStorage::Raw);
    CHECK(noneRead(written));
    CHECK(noneRead(MCPacketFile()));

    MCPacketFile changed;
    REQUIRE_EQ(changed.Open(folder / "test.pak"), NO_ERR);
    REQUIRE(changed.OpenReader().has_value());
    CHECK_EQ(changed.ReadPackets(both).Reads, 1u);
    std::vector<uint8_t> longer = bytes;
    longer.push_back(0);
    WriteFile(folder.Path() / "test.pak", longer);
    CHECK(noneRead(changed));
}

/// <summary>
/// Port-only: a child window into a packet file on disk (a mech's PAK inside a part PAK's packet) hands out readers of
/// its own bytes, read from its parent's file at its place there and no further, so its packets read together are
/// ReadPacket's; and two windows into one file read together share reads, their neighbours joined across them.
/// </summary>
TEST_CASE("packet file: a child window's packets read together come from its parent's file, two windows' in one read")
{
    using namespace MCPakBytes;
    const std::vector<uint8_t> first = Pak({Raw(Filled(100, 0)), Lz(Filled(3000, 1)), Raw(Filled(200, 2))});
    const std::vector<uint8_t> second = Pak({Raw(Filled(150, 3)), Lz(Filled(2000, 4))});
    ScratchFolder folder("packets-children");
    WriteFile(folder.Path() / "parts.pak", Pak({Raw(Filled(500, 5)), Raw(first), Raw(second)}));
    MCPacketFile parts;
    REQUIRE_EQ(parts.Open(folder / "parts.pak"), NO_ERR);
    std::array<MCPacketFile, 2> children;

    for (int32_t k = 0; k < 2; ++k)
    {
        REQUIRE_EQ(parts.SeekPacket(k + 1), NO_ERR);
        REQUIRE_EQ(children[static_cast<size_t>(k)].Open(parts, static_cast<uint32_t>(parts.GetPacketSize())), NO_ERR);
    }

    // A reader of the first child reads its bytes, and none past them.
    std::optional<MCFileReader> reader = children[0].OpenReader();
    REQUIRE(reader.has_value());
    std::vector<uint8_t> read(first.size() + 10, 0);
    CHECK_EQ(reader->ReadAt(0, read), first.size());
    CHECK(std::equal(first.begin(), first.end(), read.begin()));
    CHECK_EQ(reader->ReadAt(static_cast<uint32_t>(first.size()), read), 0u);

    const std::vector<int32_t> ofFirst = PacketsFrom(0, 2);
    const std::vector<int32_t> ofSecond = PacketsFrom(0, 1);
    const MCPacketBatch alone = children[0].ReadPackets(ofFirst);
    CHECK_EQ(alone.Reads, 1u);

    for (size_t i = 0; i < ofFirst.size(); ++i)
    {
        REQUIRE(alone.Packets[i].has_value());
        CHECK(*alone.Packets[i] == ReadAlone(children[0], ofFirst[i]));
    }

    const std::array<MCPacketsWanted, 2> both{MCPacketsWanted{children[0], ofFirst},
                                              MCPacketsWanted{children[1], ofSecond}};
    const std::vector<MCPacketBatch> together = MCPacketFile::ReadPackets(both);
    REQUIRE_EQ(together.size(), 2u);
    CHECK_EQ(together[0].Reads + together[1].Reads, 1u);

    for (size_t i = 0; i < ofSecond.size(); ++i)
    {
        REQUIRE(together[1].Packets[i].has_value());
        CHECK(*together[1].Packets[i] == ReadAlone(children[1], ofSecond[i]));
    }

    CHECK(together[0].Packets == alone.Packets);
}

/// <summary>
/// Port-only: packets held (MCPacketFile::HoldPackets: a mission's load's object types) seek and read as SeekPacket and
/// ReadPacket leave and give them, the file's position and the packet's size included; let go, they're read as ever.
/// A FIT file inside one opens from what's held.
/// </summary>
TEST_CASE("packet file: packets held seek and read as SeekPacket and ReadPacket do")
{
    using namespace MCPakBytes;
    const std::vector<uint8_t> fit = Bytes("FITini\r\n[ObjectClass]\r\nl ObjectTypeNum = 3\r\n\r\nFITend\r\n");
    const std::vector<MCStored> stored{Raw(Filled(300, 0)), Lz(Filled(4000, 1)), As(MCPacketStorage::Nul, {}), Lz(fit),
                                       Raw(Filled(100, 2))};

    OnDiskAndInMemory("packets-held", Pak(stored),
                      [&](MCPacketFile& pak)
                      {
                          MCPacketFile plain;
                          REQUIRE_EQ(plain.Open(pak.GetFilename()), NO_ERR);
                          const std::vector<int32_t> held = PacketsFrom(0, 3);
                          const MCPacketsHeld kept = pak.HoldPackets(held);
                          CHECK_EQ(kept.Packets, held.size());
                          CHECK_EQ(kept.Bytes, 300u + 4000u + fit.size());

                          for (const int32_t packet : PacketsFrom(-1, 5))
                          {
                              MCTest::Scope scope(std::format("packet {}", packet));
                              CHECK_EQ(pak.Holds(packet), packet >= 0 && packet <= 3);
                              CHECK_EQ(pak.SeekPacket(packet), plain.SeekPacket(packet));
                              CHECK_EQ(pak.GetPacketSize(), plain.GetPacketSize());
                              CHECK_EQ(pak.GetLogicalPosition(), plain.GetLogicalPosition());
                              std::vector<uint8_t> ours(static_cast<size_t>(std::max(pak.GetPacketSize(), 0)) + 5,
                                                        0xcc);
                              std::vector<uint8_t> theirs = ours;
                              CHECK_EQ(pak.ReadPacket(packet, ours), plain.ReadPacket(packet, theirs));
                              CHECK(ours == theirs);
                              CHECK_EQ(pak.GetLogicalPosition(), plain.GetLogicalPosition());
                          }

                          pak.ReleaseHeld();
                          CHECK(!pak.Holds(0));
                          CHECK(ReadAlone(pak, 1) == ReadAlone(plain, 1));
                      });

    // A FIT file in a held packet opens from what's held, as MCObjectTypeManager::Load opens a type's (a child window
    // reads only from a file on disk).
    ScratchFolder folder("packets-held-fit");
    WriteFile(folder.Path() / "test.pak", Pak(stored));
    MCPacketFile pak;
    REQUIRE_EQ(pak.Open(folder / "test.pak"), NO_ERR);
    CHECK_EQ(pak.HoldPackets(std::vector{3}).Packets, 1u);
    REQUIRE_EQ(pak.SeekPacket(3), NO_ERR);
    MCFitIniFile classFile;
    REQUIRE_EQ(classFile.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())), 0);
    REQUIRE_EQ(classFile.SeekBlock("ObjectClass"), 0);
    CHECK(classFile.Read<int32_t>("ObjectTypeNum") == MCFitResult<int32_t>(3));
}

/// <summary>A FIT file stored as a packet opens straight from the packet file (read into memory).</summary>
TEST_CASE("packet file: a FIT file inside a packet opens as a child")
{
    ScratchFolder folder("fitpacket");
    const std::vector<uint8_t> fit =
        Bytes("FITini\r\n[Mech]\r\nl Tonnage = 35\r\nst Name = \"Uller\"\r\n\r\nFITend\r\n");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "fits.pak"), NO_ERR);
        out.Reserve(2);
        out.WritePacket(0, Bytes("not a fit"), MCPacketStorage::Raw);
        out.WritePacket(1, fit, MCPacketStorage::Lzd);
    }

    MCPacketFile pak;
    REQUIRE_EQ(pak.Open(folder / "fits.pak"), NO_ERR);
    REQUIRE_EQ(pak.SeekPacket(1), NO_ERR);
    MCFitIniFile profile;
    REQUIRE_EQ(profile.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())), 0);
    REQUIRE_EQ(profile.SeekBlock("Mech"), 0);
    CHECK(profile.Read<int32_t>("Tonnage") == MCFitResult<int32_t>(35));
    CHECK(profile.Read<std::string>("Name") == MCFitResult<std::string>("Uller"));

    REQUIRE_EQ(pak.SeekPacket(0), NO_ERR);
    MCFitIniFile notFit;
    CHECK_EQ(notFit.Open(pak, static_cast<uint32_t>(pak.GetPacketSize())), NOT_A_FITINIFILE);

    // The FIT closes with its packet file, though its bytes are in memory (as the original's parent closed it).
    pak.Close();
    CHECK(!profile.IsOpen());
    CHECK_EQ(profile.SeekBlock("Mech"), BLOCK_NOT_FOUND);
    CHECK_EQ(profile.GetNumBlocks(), 0);
}

/// <summary>A packet file inside a packet file closes with it: its packets read as missing.</summary>
TEST_CASE("packet file: a packet file inside one closes with its parent")
{
    ScratchFolder folder("nestedpak");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "inner.pak"), NO_ERR);
        out.Reserve(1);
        out.WritePacket(0, Bytes("shape"), MCPacketStorage::Raw);
    }

    const std::vector<uint8_t> inner = ReadFile(folder.Path() / "inner.pak");
    {
        MCPacketFile out;
        REQUIRE_EQ(out.Create(folder / "outer.pak"), NO_ERR);
        out.Reserve(1);
        out.WritePacket(0, inner, MCPacketStorage::Raw);
    }

    auto outer = std::make_unique<MCPacketFile>();
    REQUIRE_EQ(outer->Open(folder / "outer.pak"), NO_ERR);
    REQUIRE_EQ(outer->SeekPacket(0), NO_ERR);
    MCPacketFile child;
    REQUIRE_EQ(child.Open(*outer, static_cast<uint32_t>(outer->GetPacketSize())), NO_ERR);
    REQUIRE_EQ(child.GetNumPackets(), 1);
    std::vector<uint8_t> buffer(5);
    CHECK_EQ(child.ReadPacket(0, buffer), 5);
    CHECK(buffer == Bytes("shape"));

    outer.reset();
    CHECK_EQ(child.SeekPacket(0), static_cast<int32_t>(0xBADF0004));
    CHECK_EQ(child.GetNumPackets(), 0);
    CHECK_EQ(child.ReadPacket(0, buffer), 0);
}

/// <summary>
/// Port-only: low-priority workers' helpers (the sharp pictures' maker's) run a step below the thread they were made
/// for, as HD art's readers do (MCSetCurrentThreadPriority's step below), not at SDL's low priority (Windows' lowest).
/// </summary>
TEST_CASE("workers: low-priority helpers run a step below the thread they were made for, as HD art's readers")
{
    constexpr size_t Helpers = 2;
    constexpr size_t Parts = 8;
    constexpr auto PartTime = std::chrono::milliseconds(20);
    constexpr bool Below = true;
    const MCThreadPriority like = MCCurrentThreadPriority();
    // What a thread a step below it gets by the readers' rule.
    MCThreadPriority below;
    std::thread(
        [&]
        {
            MCSetCurrentThreadPriority(like, Below);
            below = MCCurrentThreadPriority();
        })
        .join();
    MCWorkers workers(Helpers, MCWorkers::Priority::Low, like);
    const std::thread::id caller = std::this_thread::get_id();
    std::vector<std::optional<MCThreadPriority>> onHelpers(Parts);
    workers.Run(Parts,
                [&](size_t part)
                {
                    std::this_thread::sleep_for(PartTime);

                    if (std::this_thread::get_id() != caller)
                    {
                        onHelpers[part] = MCCurrentThreadPriority();
                    }
                });

    REQUIRE(std::ranges::any_of(onHelpers, [](const auto& priority) { return priority.has_value(); }));

    for (const std::optional<MCThreadPriority>& priority : onHelpers)
    {
        if (priority.has_value())
        {
            CHECK(*priority == below);
        }
    }
}

/// <summary>
/// The open list pops the smallest key first. Equal keys come out last in, first out (an insert climbs past equal
/// parents), which decides between equally good path steps (rule R5).
/// </summary>
TEST_CASE("pqueue: smallest key first, ties last in first out")
{
    MCPriorityQueue queue(16, -1000000);
    const int32_t keys[] = {50, 10, 70, 30, 20, 60, 40};

    for (int32_t i = 0; i < 7; ++i)
    {
        CHECK(queue.Insert({keys[i], 100 + i, i, i}));
    }

    CHECK(queue.Find(102) > 0);
    CHECK_EQ(queue.GetItem(queue.Find(102)).Key, 70);
    CHECK_EQ(queue.Find(999), 0);

    // Lower 70 to 5: it comes out first; raise 10 to 65: it comes out after 60.
    queue.Change(queue.Find(102), 5);
    queue.Change(queue.Find(101), 65);
    std::vector<int32_t> ids;

    while (!queue.IsEmpty())
    {
        ids.push_back(queue.Pop().Id);
    }

    CHECK((ids == std::vector<int32_t>{102, 104, 103, 106, 100, 105, 101}));

    for (int32_t id = 1; id <= 3; ++id)
    {
        CHECK(queue.Insert({5, id, 0, 0}));
    }

    CHECK_EQ(queue.Pop().Id, 3);
    CHECK_EQ(queue.Pop().Id, 2);
    CHECK_EQ(queue.Pop().Id, 1);
}

/// <summary>The queue holds two items more than its size (the original counted its spare slots), and refuses more.</summary>
TEST_CASE("pqueue: a full queue refuses an insert")
{
    MCPriorityQueue queue(2, -1000);

    for (int32_t i = 0; i < 4; ++i)
    {
        CHECK(queue.Insert({10 - i, i, 0, 0}));
    }

    CHECK(!queue.Insert({0, 9, 0, 0}));
    CHECK_EQ(queue.Size(), 4);
    queue.Clear();
    CHECK(queue.IsEmpty());
    CHECK(queue.Insert({0, 9, 0, 0}));
}

TEST_CASE("cident: ids keep seven characters, and game paths join")
{
    MCIDString id;
    id.Init("ABCDEFGHIJ");
    CHECK_EQ(id.Id, std::string("ABCDEFG"));
    CHECK(id == "ABCDEFG");
    CHECK(!(id == "ABCDEFGH"));
    CHECK(!(id == "ABCDEFX"));
    CHECK(!(id == "ABC"));
    id.Init("TBlk12");
    CHECK(id == "TBlk12");

    CHECK_EQ(GamePath("data\\missions\\", "mcx0101", ".fit"), std::string("data\\missions\\mcx0101.fit"));
    CHECK_EQ(GamePath("data\\", "x"), std::string("data\\x"));
}

TEST_CASE("cvmath: vector products and random ranges")
{
    const MCVector3D x(1.0f, 0.0f, 0.0f);
    const MCVector3D y(0.0f, 1.0f, 0.0f);
    const MCVector3D z = x & y;
    CHECK_EQ(z.X, 0.0f);
    CHECK_EQ(z.Y, 0.0f);
    CHECK_EQ(z.Z, 1.0f);
    CHECK_EQ(x | y, 0.0);
    MCVector3D v(3.0f, 4.0f, 0.0f);
    CHECK_EQ(v.Magnitude(), 5.0);
    v.Normalize();
    CHECK_EQ(v.X, 0.6f);
    MCFrameOfRef frame;
    frame.ResetToWorldFrame();
    CHECK_EQ(frame.K.Z, 1.0f);
    CHECK_EQ(NullFrameOfRef.J.Y, 1.0f);
    CHECK_EQ(frame.MyAcos(2.0f), 0.0);
    CHECK_EQ(AcosMatherr(-2.0), 3.14159265359);

    for (int i = 0; i < 1000; ++i)
    {
        const int32_t r = RandomNumber(10);
        CHECK(r >= 0 && r < 10);
        const int32_t s = SignedRandomNumber(5);
        CHECK(s >= -5 && s < 5);
    }

    CHECK(!RollDice(0));
    CHECK(RollDice(101));
}

TEST_CASE("game: every FastFile entry unpacks to its recorded size")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    int checked = 0;
    int packed = 0;

    for (const std::string_view name : {"ART.FST", "MISSION.FST", "MISC.FST", "SHAPES.FST", "TERRAIN.FST"})
    {
        MCTest::Scope scope(name);
        auto fastFile = MCFastFile::Create(name);
        REQUIRE(fastFile.has_value());
        MCFastFile& archive = **fastFile;
        CHECK(archive.GetNumFiles() > 0);
        std::vector<uint8_t> buffer;

        for (int32_t i = 0; i < archive.GetNumFiles(); ++i)
        {
            const MCFileEntry& entry = archive.GetEntries()[static_cast<size_t>(i)];
            MCTest::Scope entryScope(std::string(entry.GetName()));
            REQUIRE(archive.Find(entry.GetName()).has_value());
            buffer.assign(static_cast<size_t>(entry.RealSize), 0);
            CHECK_EQ(archive.ReadEntry(i, buffer), entry.RealSize);
            packed += entry.Size != entry.RealSize ? 1 : 0;
            ++checked;
        }
    }

    CHECK(checked > 1000);
    CHECK(packed > 100);
}

TEST_CASE("game: File falls back to the FastFiles")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    MCFile file;
    REQUIRE_EQ(file.Open("data\\art\\access00.tga"), 0);
    CHECK(file.GetLength() > 18);
    // A TGA header: no image id, no colour map or a colour map, type 1/2/9/10.
    static_cast<void>(file.ReadByte());
    const uint8_t mapType = file.ReadByte();
    const uint8_t imageType = file.ReadByte();
    CHECK(mapType <= 1);
    CHECK(imageType == 1 || imageType == 2 || imageType == 9 || imageType == 10);
    file.Close();
    CHECK(!file.IsOpen());
}

TEST_CASE(
    "game: files read ahead open as the archives and the folders give them until let go, and one written is looked for "
    "again")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    MCFastFileSet& archives = MCGameContext::Current().FastFiles();
    REQUIRE(!archives.Files().empty());

    // An entry stored packed and one stored as is, a name no archive or folder holds, a loose file, one dropped.
    std::optional<std::string> packed;
    std::optional<std::string> raw;

    for (const std::unique_ptr<MCFastFile>& archive : archives.Files())
    {
        for (const MCFileEntry& entry : archive->GetEntries())
        {
            std::optional<std::string>& kind = entry.Size != entry.RealSize ? packed : raw;

            if (!kind.has_value() && entry.RealSize > 0 && !MCFileSystem::Exists(entry.GetName()))
            {
                kind = std::string(entry.GetName());
            }
        }
    }

    REQUIRE(packed.has_value());
    REQUIRE(raw.has_value());
    const std::string missing = "data\\art\\no such picture.tga";
    const std::string loose = "system.cfg";
    REQUIRE(MCFileSystem::Exists(loose));
    const std::string dropped = *raw;
    const auto read = [](const std::string& name)
    {
        MCFile file;
        const int32_t opened = file.Open(name);
        std::vector<uint8_t> bytes(file.IsOpen() ? file.GetLength() : 0);
        file.Read(bytes);
        return std::pair(opened, bytes);
    };

    const auto [packedOpened, packedBytes] = read(*packed);
    REQUIRE_EQ(packedOpened, 0);
    const size_t taken = MCFilesAhead::TakenCount();
    MCFilesAhead::ReadAhead({*packed, missing, loose, dropped}, archives);
    MCFilesAhead::Drop(dropped);
    const auto [aheadOpened, aheadBytes] = read(*packed);
    CHECK_EQ(aheadOpened, 0);
    CHECK(aheadBytes == packedBytes);
    CHECK(read(*packed).second == packedBytes);
    CHECK_EQ(read(missing).first, FILE_NOT_FOUND);
    CHECK_EQ(MCFilesAhead::TakenCount() - taken, 3u);

    // The loose file and the one dropped were left to their opens.
    CHECK(!MCFilesAhead::Take(loose).has_value());
    CHECK(!MCFilesAhead::Take(dropped).has_value());
    CHECK_EQ(read(loose).first, 0);

    // A write to any spelling of a path drops it; a spelling the archives don't hold (they match slashes as spelled) is
    // not found as spelled, not in another's place.
    std::string slashed = *packed;
    std::ranges::replace(slashed, '\\', '/');
    REQUIRE(slashed != *packed);
    MCFilesAhead::Drop(slashed);
    CHECK(!MCFilesAhead::Take(*packed).has_value());
    MCFilesAhead::ReadAhead({slashed}, archives);
    CHECK_EQ(read(slashed).first, FILE_NOT_FOUND);
    CHECK(read(*packed).second == packedBytes);
    MCFilesAhead::Forget();
    CHECK_EQ(MCFilesAhead::HeldCount(), 0u);
    CHECK(!MCFilesAhead::Take(slashed).has_value());

    // What's noted: the files looked for in the archives, found or not; not a loose file.
    MCFilesAhead::StartNoting();
    std::ignore = read(*raw);
    std::ignore = read(missing);
    std::ignore = read(loose);
    MCFilesAhead::StopNoting();
    const std::vector<std::string> noted = MCFilesAhead::Noted();
    CHECK(std::ranges::find(noted, *raw) != noted.end());
    CHECK(std::ranges::find(noted, missing) != noted.end());
    CHECK(std::ranges::find(noted, loose) == noted.end());
}

TEST_CASE("game: packet files open and read")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestGame::OpenFastFiles();
    MCPacketFile pak;
    REQUIRE_EQ(pak.Open("data\\art\\art.pak"), 0);
    CHECK(pak.GetNumPackets() > 0);
    std::vector<uint8_t> buffer;

    for (int32_t i = 0; i < pak.GetNumPackets(); ++i)
    {
        MCTest::Scope scope(std::format("packet {}", i));

        if (pak.SeekPacket(i) != 0)
        {
            continue;
        }

        buffer.assign(static_cast<size_t>(pak.GetPacketSize()) + 1, 0);
        CHECK_EQ(pak.ReadPacket(i, std::span(buffer).first(static_cast<size_t>(pak.GetPacketSize()))),
                 pak.GetPacketSize());
    }
}

/// <summary>
/// An update's patch (HDiffPatch's window diff, with xxh128 checksums) turns its old file into the new one byte for
/// byte, with the patch as hdiffz wrote it.
/// </summary>
TEST_CASE("file patch: an update's patch turns its old file into the new one, byte for byte")
{
    ScratchFolder folder("file-patch");
    WriteFile(folder.Path() / "old.bin", PatchOld());
    WriteFile(folder.Path() / "patch.hdiff", CheckedPatch);

    const auto applied =
        MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff", folder.Path() / "new.bin");
    REQUIRE(applied.has_value());
    CHECK(ReadFile(folder.Path() / "new.bin") == PatchNew());

    // Held to the size wanted (the list's): another is refused before anything is written.
    const auto sized = MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff",
                                          folder.Path() / "sized.bin", {}, PatchNew().size());
    REQUIRE(sized.has_value());
    CHECK(ReadFile(folder.Path() / "sized.bin") == PatchNew());
    const auto longer = MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff",
                                           folder.Path() / "longer.bin", {}, PatchNew().size() + 1);
    CHECK(longer.error_or("").find("makes a file of") != std::string::npos);
    CHECK(!std::filesystem::exists(folder.Path() / "longer.bin"));

#ifndef _WIN32
    // A link where the new file goes is replaced, never written through.
    WriteFile(folder.Path() / "elsewhere.bin", Bytes("not the patch's"));
    std::filesystem::create_symlink(folder.Path() / "elsewhere.bin", folder.Path() / "linked.bin");
    REQUIRE(MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff", folder.Path() / "linked.bin")
                .has_value());
    CHECK(!std::filesystem::is_symlink(folder.Path() / "linked.bin"));
    CHECK(ReadFile(folder.Path() / "linked.bin") == PatchNew());
    CHECK(ReadFile(folder.Path() / "elsewhere.bin") == Bytes("not the patch's"));
#endif
}

/// <summary>
/// A patch applied to another old file, a damaged or cut short one, one without checksums, a file that isn't a patch,
/// or a patch written over its own old file writes nothing, and says why; a file an earlier attempt left where it
/// would write goes too.
/// </summary>
TEST_CASE("file patch: one for another file, damaged, without checksums or no patch at all writes nothing")
{
    ScratchFolder folder("file-patch-refused");
    const std::filesystem::path old = folder.Path() / "old.bin";
    const std::filesystem::path patch = folder.Path() / "patch.hdiff";
    const std::filesystem::path out = folder.Path() / "new.bin";
    const auto refused = [&](std::string_view expected)
    {
        WriteFile(out, Bytes("an earlier attempt's"));
        const auto applied = MCFilePatch::Apply(old, patch, out);
        CHECK(!applied.has_value());
        CHECK(!std::filesystem::exists(out));

        if (!applied.has_value())
        {
            CHECK(applied.error().find(expected) != std::string::npos);
        }
    };

    std::vector<uint8_t> other = PatchOld();
    other[PatchReferencedByte] ^= 1;
    WriteFile(old, other);
    WriteFile(patch, CheckedPatch);
    refused("another file");

    WriteFile(old, PatchOld());
    std::vector<uint8_t> damaged(CheckedPatch.begin(), CheckedPatch.end());
    damaged[damaged.size() - PatchDataFromEnd] ^= 1;
    WriteFile(patch, damaged);
    refused("damaged");

    WriteFile(patch, std::span(CheckedPatch).first(PatchCutShort));
    refused("patch.hdiff");
    WriteFile(patch, UncheckedPatch);
    refused("no xxh128 checksums");
    WriteFile(patch, PatchOld());
    refused("isn't a patch");

    WriteFile(patch, CheckedPatch);
    CHECK(!MCFilePatch::Apply(old, patch, old).has_value());
    CHECK(ReadFile(old) == PatchOld());
}

/// <summary>
/// A patch asked to stop as it reads (the old file's checksum first) or writes ends at once, as stopped, and leaves
/// nothing, wherever it's asked.
/// </summary>
TEST_CASE("file patch: a patch stopped as it reads or writes ends at once and writes nothing")
{
    ScratchFolder folder("file-patch-stopped");
    WriteFile(folder.Path() / "old.bin", PatchOld());
    WriteFile(folder.Path() / "patch.hdiff", CheckedPatch);
    int asks = 0;
    REQUIRE(MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff", folder.Path() / "new.bin",
                               [&]
                               {
                                   ++asks;
                                   return true;
                               })
                .has_value());
    REQUIRE(asks > 1);

    for (int stopAt = 1; stopAt <= asks; ++stopAt)
    {
        int asked = 0;
        const auto stopped = MCFilePatch::Apply(folder.Path() / "old.bin", folder.Path() / "patch.hdiff",
                                                folder.Path() / "new.bin", [&] { return ++asked < stopAt; });
        CHECK_EQ(stopped.error_or(""), std::string(MCFilePatch::StoppedError));
        CHECK_EQ(asked, stopAt);
        CHECK(!std::filesystem::exists(folder.Path() / "new.bin"));
    }
}

/// <summary>
/// Tool: <c>--file-patch &lt;out&gt; --patch-old &lt;file&gt; --patch-file &lt;patch&gt;</c> applies an update's patch
/// with the game's code (MCFilePatch::Apply), printing the new file's size and the time taken: to check the release
/// script's patches (made with hdiffz) on real files.
/// </summary>
TEST_CASE_ISOLATED("tool: --file-patch applies an update's patch to a file")
{
    const std::optional<std::string_view> out = MCTest::Option("file-patch");

    if (!out.has_value())
    {
        return;
    }

    const std::optional<std::string_view> old = MCTest::Option("patch-old");
    const std::optional<std::string_view> patch = MCTest::Option("patch-file");
    REQUIRE(old.has_value());
    REQUIRE(patch.has_value());
    const auto started = std::chrono::steady_clock::now();
    const auto applied =
        MCFilePatch::Apply(MCFileSystem::FromUtf8(*old), MCFileSystem::FromUtf8(*patch), MCFileSystem::FromUtf8(*out));

    if (!applied)
    {
        std::cout << std::format("  {}\n", applied.error());
    }

    REQUIRE(applied.has_value());
    std::cout << std::format("  {}: {} bytes, {:.1f} s\n", *out,
                             std::filesystem::file_size(MCFileSystem::FromUtf8(*out)),
                             std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
}

TEST_CASE(
    "retry delay: waits double to their cap, each drawn from its last share at random, the first spread when asked")
{
    using std::chrono::milliseconds;
    // Random fractions from the draw's ends: none (each wait its longest), and all but nothing (its least).
    const MCRetryDelay::MCRandom none = [] { return 0.0; };
    const MCRetryDelay::MCRandom all = [] { return std::nextafter(1.0, 0.0); };
    const MCRetryDelay::MCRandom half = [] { return 0.5; };

    // The progression: from the first, doubling, held at the most from there on (however many tries).
    const MCRetryDelay delay{.First = milliseconds(1000), .Most = milliseconds(30000), .Jitter = 0.5};
    const std::array<int64_t, 8> longest = {1000, 2000, 4000, 8000, 16000, 30000, 30000, 30000};

    for (size_t tries = 0; tries < longest.size(); tries++)
    {
        const auto at = static_cast<int32_t>(tries);
        CHECK_EQ(delay.Longest(at).count(), longest[tries]);
        CHECK_EQ(delay.Wait(at, none).count(), longest[tries]);
        CHECK_EQ(delay.Wait(at, half).count(), longest[tries] * 3 / 4);
        CHECK_EQ(delay.Wait(at, all).count(), longest[tries] / 2);
    }

    CHECK_EQ(delay.Longest(MCRetryDelay::MostDoublings).count(), 30000);
    CHECK_EQ(delay.Longest(std::numeric_limits<int32_t>::max()).count(), 30000);
    CHECK_EQ(delay.Longest(-1).count(), 1000);

    // The jitter's bounds: drawn from the thread's own source, every wait from its least to its longest, and spread.
    std::set<int64_t> drawn;

    for (int draw = 0; draw < 200; draw++)
    {
        const milliseconds wait = delay.Wait(2);
        CHECK(wait >= milliseconds(2000));
        CHECK(wait <= milliseconds(4000));
        drawn.insert(wait.count());
    }

    CHECK(drawn.size() > 10);

    // No jitter: each wait its longest. All of it: from nothing to its longest.
    const MCRetryDelay exact{.First = milliseconds(10), .Most = milliseconds(40), .Jitter = 0};
    CHECK_EQ(exact.Wait(1, all).count(), 20);
    const MCRetryDelay full{.First = milliseconds(10), .Most = milliseconds(40), .Jitter = 1};
    CHECK_EQ(full.Wait(1, all).count(), 0);
    CHECK_EQ(full.Wait(1, none).count(), 20);

    // The first try spread: anywhere from none to the spread, the tries after it as ever.
    const MCRetryDelay spread{
        .First = milliseconds(1000), .Most = milliseconds(30000), .FirstSpread = milliseconds(3000)};
    CHECK_EQ(spread.Wait(0, none).count(), 0);
    CHECK_EQ(spread.Wait(0, half).count(), 1500);
    CHECK_EQ(spread.Wait(0, all).count(), 3000);
    CHECK_EQ(spread.Wait(1, none).count(), 2000);
    CHECK_EQ(spread.Wait(1, all).count(), 1000);

    // Waits of nothing stay nothing (the tests' services come back at once).
    const MCRetryDelay atOnce{};
    CHECK_EQ(atOnce.Wait(0, none).count(), 0);
    CHECK_EQ(atOnce.Wait(MCRetryDelay::MostDoublings, all).count(), 0);
}

TEST_CASE("thread keeper: a thread handed over is joined off the caller's thread, and JoinAll joins what's left")
{
    constexpr auto Patience = std::chrono::seconds(5);
    constexpr auto Step = std::chrono::milliseconds(1);
    constexpr auto Released = std::chrono::milliseconds(50);
    MCThreadKeeper keeper;

    // A thread that ends at once: joined soon after, with no one waiting for it.
    std::atomic<bool> ran = false;
    keeper.Keep(std::thread([&] { ran = true; }));
    const auto deadline = std::chrono::steady_clock::now() + Patience;

    while (keeper.Kept() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(Step);
    }

    CHECK_EQ(keeper.Kept(), size_t{0});
    CHECK(ran);

    // A thread still running (until another lets it go): Keep returns at once, and JoinAll waits for its end.
    std::mutex lock;
    std::condition_variable changed;
    bool release = false;
    std::atomic<bool> ended = false;
    keeper.Keep(std::thread(
        [&]
        {
            std::unique_lock held(lock);
            changed.wait(held, [&] { return release; });
            ended = true;
        }));
    CHECK(!ended);
    CHECK_EQ(keeper.Kept(), size_t{1});
    std::thread releaser(
        [&]
        {
            std::this_thread::sleep_for(Released);

            {
                const std::scoped_lock released(lock);
                release = true;
            }

            changed.notify_all();
        });
    keeper.JoinAll();
    CHECK(ended);
    CHECK_EQ(keeper.Kept(), size_t{0});
    releaser.join();

    // It keeps threads after a JoinAll as before; one not running isn't kept.
    std::atomic<int> later = 0;
    keeper.Keep(std::thread([&] { later++; }));
    keeper.Keep(std::thread([&] { later++; }));
    keeper.Keep(std::thread());
    keeper.JoinAll();
    CHECK_EQ(later.load(), 2);
    CHECK_EQ(keeper.Kept(), size_t{0});
}

TEST_CASE("thread keeper: a thread handed over while JoinAll waits is waited for too, and one kept before never missed")
{
    constexpr auto Settling = std::chrono::milliseconds(50);
    MCThreadKeeper keeper;
    std::mutex lock;
    std::condition_variable changed;
    int released = 0;
    std::atomic<int> ended = 0;
    // Each ends once `released` reaches its turn.
    const auto waiter = [&](int turn)
    {
        return std::thread(
            [&, turn]
            {
                std::unique_lock held(lock);
                changed.wait(held, [&] { return released >= turn; });
                ended++;
            });
    };

    const auto release = [&](int turn)
    {
        {
            const std::scoped_lock held(lock);
            released = turn;
        }

        changed.notify_all();
    };

    // The reaper joins the first while the second waits queued; JoinAll starts; a third is kept meanwhile.
    keeper.Keep(waiter(1));
    keeper.Keep(waiter(3));
    std::atomic<bool> joined = false;
    std::thread joiner(
        [&]
        {
            keeper.JoinAll();
            joined = true;
        });
    std::this_thread::sleep_for(Settling);
    keeper.Keep(waiter(2));
    release(2);
    std::this_thread::sleep_for(Settling);
    CHECK_EQ(ended.load(), 2);
    CHECK(!joined);
    release(3);
    joiner.join();
    CHECK(joined);
    CHECK_EQ(ended.load(), 3);
    CHECK_EQ(keeper.Kept(), size_t{0});
}
