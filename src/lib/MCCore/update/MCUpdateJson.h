#pragma once

#include "lib/MCSha256.h"

#include <nlohmann/json.hpp>

/// <summary>
/// Port-only: reading the JSON of updates' files (the signed envelope, the key list, <c>release.json</c>, the content
/// list), each field checked for its type. A file that breaks a rule throws <see cref="MCBadFile"/> inside the readers,
/// and <see cref="Read"/> turns that into the reader's error: callers outside update/ never see an exception.
/// </summary>
namespace MCUpdateJson
{
    using MCJson = nlohmann::json;

    /// <summary>What's wrong with a file being read, said as the player would read it in a log.</summary>
    struct MCBadFile
    {
        std::string Reason;
    };

    /// <summary>Throws <see cref="MCBadFile"/>, the reason formatted from <paramref name="format"/>.</summary>
    template <typename... Args> [[noreturn]] void Fail(std::format_string<Args...> format, Args&&... args)
    {
        throw MCBadFile{std::format(format, std::forward<Args>(args)...)};
    }

    /// <summary>
    /// Runs <paramref name="reader"/>, returning what it returns, or the reason it gave up prefixed by
    /// <paramref name="what"/> ("release.json: ...").
    /// </summary>
    template <typename Reader>
    auto Read(std::string_view what, Reader&& reader) -> std::expected<decltype(reader()), std::string>
    {
        try
        {
            return reader();
        }
        catch (const MCBadFile& bad)
        {
            return std::unexpected(std::format("{}: {}", what, bad.Reason));
        }
    }

    /// <summary>The JSON object <paramref name="text"/> holds.</summary>
    MCJson Object(std::string_view text);

    /// <summary>The member <paramref name="key"/> of <paramref name="object"/>, which must be there.</summary>
    const MCJson& Member(const MCJson& object, std::string_view key);

    /// <summary>The member, a string.</summary>
    std::string Text(const MCJson& object, std::string_view key);

    /// <summary>The member, a string, or <paramref name="otherwise"/> without one.</summary>
    std::string TextOr(const MCJson& object, std::string_view key, std::string_view otherwise);

    /// <summary>The member, a whole number from 0.</summary>
    uint64_t Count(const MCJson& object, std::string_view key);

    /// <summary>The member, a number.</summary>
    double Number(const MCJson& object, std::string_view key);

    /// <summary>The member, a SHA-256 hash as 64 hex digits.</summary>
    MCSha256::MCDigest Digest(const MCJson& object, std::string_view key);

    /// <summary><paramref name="value"/>, a SHA-256 hash as 64 hex digits (<paramref name="key"/> names it).</summary>
    MCSha256::MCDigest DigestOf(const MCJson& value, std::string_view key);

    /// <summary>The member, an array (empty without one when <paramref name="required"/> is false).</summary>
    const MCJson& Array(const MCJson& object, std::string_view key, bool required = true);

    /// <summary>The member, an object.</summary>
    const MCJson& Child(const MCJson& object, std::string_view key);

    /// <summary>The bytes the base64 <paramref name="text"/> spells (standard alphabet, padded).</summary>
    std::vector<uint8_t> Base64(std::string_view text, std::string_view key);

    /// <summary><paramref name="bytes"/> in base64 (standard alphabet, padded).</summary>
    std::string ToBase64(std::span<const uint8_t> bytes);
}
