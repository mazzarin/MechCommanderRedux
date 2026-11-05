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

struct MCSha256::MCHasher::MCState
{
    crypto_hash_sha256_state Sha256{};
};

MCSha256::MCHasher::MCHasher() : _State(std::make_unique<MCState>())
{
    crypto_hash_sha256_init(&_State->Sha256);
}

MCSha256::MCHasher::~MCHasher() = default;

void MCSha256::MCHasher::Add(std::span<const char> bytes)
{
    crypto_hash_sha256_update(&_State->Sha256, reinterpret_cast<const unsigned char*>(bytes.data()),
                              static_cast<unsigned long long>(bytes.size()));
}

std::expected<uint64_t, std::string> MCSha256::MCHasher::AddFile(const std::filesystem::path& path, uint64_t count,
                                                                 const std::function<bool()>& goOn)
{
    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected(std::format("{} can't be opened", MCFileSystem::ToUtf8(path)));
    }

    std::vector<char> buffer(static_cast<size_t>(std::min<uint64_t>(ReadBytes, count)));
    uint64_t added = 0;

    while (added < count && file)
    {
        if (goOn && !goOn())
        {
            return std::unexpected(std::string(StoppedError));
        }

        file.read(buffer.data(), static_cast<std::streamsize>(std::min<uint64_t>(buffer.size(), count - added)));
        const std::streamsize read = file.gcount();

        if (read > 0)
        {
            Add({buffer.data(), static_cast<size_t>(read)});
            added += static_cast<uint64_t>(read);
        }
    }

    if (added < count && !file.eof())
    {
        return std::unexpected(std::format("{} can't be read", MCFileSystem::ToUtf8(path)));
    }

    return added;
}

MCSha256::MCDigest MCSha256::MCHasher::Finish()
{
    MCDigest digest{};
    crypto_hash_sha256_final(&_State->Sha256, digest.data());
    return digest;
}

std::expected<MCSha256::MCDigest, std::string> MCSha256::OfFile(const std::filesystem::path& path,
                                                                const std::function<bool()>& goOn)
{
    MCHasher hasher;

    if (const auto added = hasher.AddFile(path, std::numeric_limits<uint64_t>::max(), goOn); !added)
    {
        return std::unexpected(added.error());
    }

    return hasher.Finish();
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
