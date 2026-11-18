#pragma once

#include "lib/MCOptionalRef.h"

struct ZSTD_DDict_s;

/// <summary>
/// Port-only: Zstandard decoding for the version 2 HD art pack's streams (the workspace's SPEC-pack-v2.md, which packs
/// them with zstd at level 19, small pieces against a trained dictionary from the pack's <c>dicts.hdd</c>): Zstandard's
/// decompressor (src/lib/zstd-1.5.7), on one frame that carries its content checksum.
/// </summary>
namespace MCZstd
{
    /// <summary>One trained Zstandard dictionary, digested once for every unpack against it, on any thread.</summary>
    class Dictionary
    {
        struct Key
        {
        };

    public:
        /// <summary>
        /// The dictionary <paramref name="bytes"/> holds, or what is wrong with it: it isn't a trained dictionary (one
        /// with Zstandard's magic and an ID other than 0), or there was no memory to digest it.
        /// </summary>
        static std::expected<std::shared_ptr<const Dictionary>, std::string> Make(std::span<const uint8_t> bytes);

        /// <summary>For <see cref="Make"/> alone.</summary>
        Dictionary(Key, ZSTD_DDict_s& made, uint32_t id);

        Dictionary(const Dictionary&) = delete;
        Dictionary& operator=(const Dictionary&) = delete;

        /// <summary>The dictionary's ID, which every frame packed against it carries.</summary>
        uint32_t Id() const { return _Id; }

        /// <summary>The digested dictionary, for <see cref="Unpack"/>.</summary>
        const ZSTD_DDict_s& Made() const { return *_Made; }

    private:
        struct Free
        {
            void operator()(ZSTD_DDict_s* made) const;
        };

        std::unique_ptr<ZSTD_DDict_s, Free> _Made;
        uint32_t _Id = 0;
    };

    /// <summary>
    /// A pack's dictionaries, by the number its streams name them by (<c>dicts.hdd</c>: an 8-byte head, <c>MCD2</c>,
    /// format 2, the count; then each dictionary's number, its length and its bytes, to the file's end).
    /// </summary>
    class Dictionaries
    {
    public:
        /// <summary>The longest dictionary read (the pack's are 112,640 bytes).</summary>
        static constexpr uint32_t MostBytes = 1u << 20;

        /// <summary>
        /// The dictionaries <paramref name="file"/> (a whole <c>dicts.hdd</c>) holds, or what is wrong with it: another
        /// kind or format of file, a number 0 or given twice, a dictionary longer than <see cref="MostBytes"/>, cut short
        /// or not a trained one, other than the count the head gives, or bytes past the last.
        /// </summary>
        static std::expected<std::shared_ptr<const Dictionaries>, std::string> Read(std::span<const uint8_t> file);

        /// <summary>Dictionary <paramref name="number"/>, if the file holds it.</summary>
        MCOptionalRef<const Dictionary> Find(uint16_t number) const;

        /// <summary>How many dictionaries the file holds.</summary>
        size_t Count() const { return _ByNumber.size(); }

    private:
        std::unordered_map<uint16_t, std::shared_ptr<const Dictionary>> _ByNumber;
    };

    /// <summary>
    /// The <paramref name="size"/> bytes <paramref name="packed"/> unpacks to, against <paramref name="dictionary"/>
    /// (none: a frame packed without one), or what is wrong with it: it isn't one whole frame, carries no checksum, was
    /// packed against another dictionary or none, is damaged (its checksum fails), or unpacks to another size. Never
    /// writes past <paramref name="size"/>.
    /// </summary>
    std::expected<std::vector<uint8_t>, std::string> Unpack(std::span<const uint8_t> packed, size_t size,
                                                            MCOptionalRef<const Dictionary> dictionary = std::nullopt);
}
