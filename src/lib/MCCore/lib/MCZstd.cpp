#include "stdafx.h"
#include "lib/MCZstd.h"

// Zstandard (src/lib/zstd-1.5.7, BSD): its decompressor alone, linked statically, so its frame header reader too.
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

namespace
{
    struct FreeContext
    {
        void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); }
    };

    /// <summary>This thread's decompression context, made the first time it is needed (null: no memory for one).</summary>
    ZSTD_DCtx* Context()
    {
        thread_local const std::unique_ptr<ZSTD_DCtx, FreeContext> context(ZSTD_createDCtx());
        return context.get();
    }

    /// <summary><c>dicts.hdd</c>'s magic, its first 4 bytes (SPEC-pack-v2.md's dictionaries).</summary>
    constexpr std::array<uint8_t, 4> DictionariesMagic = {'M', 'C', 'D', '2'};
    constexpr uint16_t DictionariesFormat = 2;
    /// <summary>The head: the magic, the format (u16) and the count (u16).</summary>
    constexpr size_t DictionariesHeadBytes = 8;
    constexpr size_t DictionariesFormatAt = 4;
    constexpr size_t DictionariesCountAt = 6;
    /// <summary>Each dictionary's own head: its number (u16) and its length (u32).</summary>
    constexpr size_t DictionaryHeadBytes = 6;
    constexpr size_t DictionaryBytesAt = 2;

    /// <summary>A little-endian value at <paramref name="at"/> of <paramref name="bytes"/>, which must hold it.</summary>
    template <typename T> T ValueAt(std::span<const uint8_t> bytes, size_t at)
    {
        T value{};
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        return value;
    }
}

void MCZstd::Dictionary::Free::operator()(ZSTD_DDict_s* made) const
{
    ZSTD_freeDDict(made);
}

MCZstd::Dictionary::Dictionary(Key, ZSTD_DDict_s& made, uint32_t id) : _Made(&made), _Id(id)
{
}

auto MCZstd::Dictionary::Make(std::span<const uint8_t> bytes)
    -> std::expected<std::shared_ptr<const Dictionary>, std::string>
{
    // A trained dictionary alone: its magic and an ID the frames packed against it carry (0 is no dictionary's).
    const uint32_t id = ZSTD_getDictID_fromDict(bytes.data(), bytes.size());

    if (id == 0)
    {
        return std::unexpected(std::format("{} bytes aren't a trained Zstandard dictionary", bytes.size()));
    }

    // Copied, so the file's bytes needn't outlive it; its tables are checked as they're digested.
    ZSTD_DDict* made =
        ZSTD_createDDict_advanced(bytes.data(), bytes.size(), ZSTD_dlm_byCopy, ZSTD_dct_fullDict, ZSTD_defaultCMem);

    if (made == nullptr)
    {
        return std::unexpected(std::format(
            "the Zstandard dictionary {} ({} bytes) is damaged, or there's no memory for it", id, bytes.size()));
    }

    return std::make_shared<const Dictionary>(Key{}, *made, id);
}

auto MCZstd::Dictionaries::Read(std::span<const uint8_t> file)
    -> std::expected<std::shared_ptr<const Dictionaries>, std::string>
{
    if (file.size() < DictionariesHeadBytes ||
        !std::ranges::equal(file.first(DictionariesMagic.size()), DictionariesMagic))
    {
        return std::unexpected("it isn't a pack's dictionaries (no MCD2 head)");
    }

    if (const auto format = ValueAt<uint16_t>(file, DictionariesFormatAt); format != DictionariesFormat)
    {
        return std::unexpected(std::format("its format is {}, not {}", format, DictionariesFormat));
    }

    const auto count = ValueAt<uint16_t>(file, DictionariesCountAt);
    auto read = std::make_shared<Dictionaries>();
    size_t at = DictionariesHeadBytes;

    while (at < file.size())
    {
        if (file.size() - at < DictionaryHeadBytes)
        {
            return std::unexpected(std::format("it's cut short at byte {}, inside a dictionary's head", at));
        }

        const auto number = ValueAt<uint16_t>(file, at);
        const auto bytes = ValueAt<uint32_t>(file, at + DictionaryBytesAt);
        at += DictionaryHeadBytes;

        if (number == 0 || read->_ByNumber.contains(number))
        {
            return std::unexpected(
                std::format("dictionary {} at byte {} is numbered 0 or twice", number, at - DictionaryHeadBytes));
        }

        if (bytes == 0 || bytes > MostBytes || bytes > file.size() - at)
        {
            return std::unexpected(std::format("dictionary {}'s {} bytes are none, more than {} or past the file's end",
                                               number, bytes, MostBytes));
        }

        std::expected<std::shared_ptr<const Dictionary>, std::string> made = Dictionary::Make(file.subspan(at, bytes));

        if (!made)
        {
            return std::unexpected(std::format("dictionary {}: {}", number, made.error()));
        }

        read->_ByNumber.emplace(number, std::move(*made));
        at += bytes;
    }

    if (read->_ByNumber.size() != count)
    {
        return std::unexpected(
            std::format("it holds {} dictionaries, not the {} its head gives", read->_ByNumber.size(), count));
    }

    return read;
}

auto MCZstd::Dictionaries::Find(uint16_t number) const -> MCOptionalRef<const Dictionary>
{
    const auto found = _ByNumber.find(number);
    return found != _ByNumber.end() ? MCOptionalRef<const Dictionary>(*found->second) : std::nullopt;
}

auto MCZstd::Unpack(std::span<const uint8_t> packed, size_t size, MCOptionalRef<const Dictionary> dictionary)
    -> std::expected<std::vector<uint8_t>, std::string>
{
    // One whole frame and nothing past it: a stream cut short, or with bytes after its frame, is refused here.
    const size_t frameBytes = ZSTD_findFrameCompressedSize(packed.data(), packed.size());

    if (ZSTD_isError(frameBytes) != 0 || frameBytes != packed.size())
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes isn't one whole frame", packed.size()));
    }

    ZSTD_FrameHeader header{};

    if (ZSTD_getFrameHeader(&header, packed.data(), packed.size()) != 0 || header.frameType != ZSTD_frame ||
        header.checksumFlag == 0)
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes carries no checksum", packed.size()));
    }

    // The frame names the dictionary it was packed against (0: none); unpacked against another, it would come out
    // garbled with a checksum that fails, so a mismatch is refused by name first.
    const uint32_t expected = dictionary ? dictionary->get().Id() : 0;

    if (header.dictID != expected)
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes was packed against dictionary {}, not {}",
                                           packed.size(), header.dictID, expected));
    }

    ZSTD_DCtx* context = Context();

    if (context == nullptr)
    {
        return std::unexpected(std::format("no memory to unpack a Zstandard stream of {} bytes", packed.size()));
    }

    std::vector<uint8_t> out(size);

    // The frame's checksum is checked as it unpacks; a frame that would unpack to more than the buffer fails instead of
    // writing past it.
    const size_t written = dictionary
                               ? ZSTD_decompress_usingDDict(context, out.data(), out.size(), packed.data(),
                                                            packed.size(), &dictionary->get().Made())
                               : ZSTD_decompressDCtx(context, out.data(), out.size(), packed.data(), packed.size());

    if (ZSTD_isError(written) != 0)
    {
        return std::unexpected(std::format("a Zstandard stream of {} bytes is damaged or unpacks past {} bytes ({})",
                                           packed.size(), size, ZSTD_getErrorName(written)));
    }

    if (written != size)
    {
        return std::unexpected(std::format("a Zstandard stream unpacks to {} bytes, not {}", written, size));
    }

    return out;
}
