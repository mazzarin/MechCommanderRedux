#include "stdafx.h"
#include "lib/MCSha256.h"
#include "platform/MCFileSystem.h"

// libsodium is built as a static library (MCCore.vcxproj, src/mac/CMakeLists.txt); its headers are read the same.
#ifndef SODIUM_STATIC
#define SODIUM_STATIC 1
#endif
#include <sodium/crypto_hash_sha256.h>

namespace
{
    /// <summary>How much of a file is read at once.</summary>
    constexpr size_t ReadBytes = size_t{1} << 20;
    constexpr std::string_view HexDigits = "0123456789abcdef";
    constexpr int BitsPerHexDigit = 4;
    constexpr uint8_t LowHexDigit = 0x0f;

    /// <summary>The value of the hex digit <paramref name="digit"/> (either case), or none.</summary>
    std::optional<uint8_t> HexValue(char digit)
    {
        constexpr int TenAsHex = 10;

        if (digit >= '0' && digit <= '9')
        {
            return static_cast<uint8_t>(digit - '0');
        }

        if (digit >= 'a' && digit <= 'f')
        {
            return static_cast<uint8_t>(digit - 'a' + TenAsHex);
        }

        if (digit >= 'A' && digit <= 'F')
        {
            return static_cast<uint8_t>(digit - 'A' + TenAsHex);
        }

        return std::nullopt;
    }
}

MCSha256::MCDigest MCSha256::Of(std::span<const uint8_t> bytes)
{
    static_assert(std::tuple_size_v<MCDigest> == crypto_hash_sha256_BYTES);
    MCDigest digest{};
    crypto_hash_sha256(digest.data(), bytes.data(), bytes.size());
    return digest;
}

MCSha256::MCDigest MCSha256::Of(std::string_view text)
{
    return Of(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

std::expected<MCSha256::MCDigest, std::string> MCSha256::OfFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected(std::format("{} can't be opened", MCFileSystem::ToUtf8(path)));
    }

    crypto_hash_sha256_state state;
    crypto_hash_sha256_init(&state);
    std::vector<char> buffer(ReadBytes);

    while (file)
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();

        if (count > 0)
        {
            crypto_hash_sha256_update(&state, reinterpret_cast<const unsigned char*>(buffer.data()),
                                      static_cast<unsigned long long>(count));
        }
    }

    if (!file.eof())
    {
        return std::unexpected(std::format("{} can't be read", MCFileSystem::ToUtf8(path)));
    }

    MCDigest digest{};
    crypto_hash_sha256_final(&state, digest.data());
    return digest;
}

std::string MCSha256::Hex(const MCDigest& digest)
{
    std::string hex;
    hex.reserve(digest.size() * 2);

    for (const uint8_t byte : digest)
    {
        hex += HexDigits[byte >> BitsPerHexDigit];
        hex += HexDigits[byte & LowHexDigit];
    }

    return hex;
}

std::optional<MCSha256::MCDigest> MCSha256::FromHex(std::string_view hex)
{
    MCDigest digest{};

    if (hex.size() != digest.size() * 2)
    {
        return std::nullopt;
    }

    for (size_t i = 0; i < digest.size(); ++i)
    {
        const std::optional<uint8_t> high = HexValue(hex[2 * i]);
        const std::optional<uint8_t> low = HexValue(hex[2 * i + 1]);

        if (!high || !low)
        {
            return std::nullopt;
        }

        digest[i] = static_cast<uint8_t>(*high << BitsPerHexDigit | *low);
    }

    return digest;
}
