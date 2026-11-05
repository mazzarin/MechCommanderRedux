#include "stdafx.h"
#include "update/MCSignedFile.h"
#include "update/MCUpdateJson.h"

#ifndef SODIUM_STATIC
#define SODIUM_STATIC 1
#endif
#include <sodium/crypto_sign_ed25519.h>

namespace
{
    using namespace MCUpdateJson;

    /// <summary>The envelope's fields, as DSSE names them.</summary>
    constexpr std::string_view PayloadKey = "payload";
    constexpr std::string_view PayloadTypeKey = "payloadType";
    constexpr std::string_view SignaturesKey = "signatures";
    constexpr std::string_view SignatureKey = "sig";
    constexpr std::string_view PaeHeader = "DSSEv1";
    /// <summary>The most signatures an envelope may carry: each costs a check against every key.</summary>
    constexpr size_t MaxSignatures = 8;
}

std::string MCSignedFile::SignedBytes(std::string_view type, std::string_view payload)
{
    return std::format("{} {} {} {} {}", PaeHeader, type.size(), type, payload.size(), payload);
}

std::expected<std::string, std::string> MCSignedFile::Open(std::string_view envelope, std::string_view type,
                                                           std::span<const MCPublicKey> keys)
{
    static_assert(std::tuple_size_v<MCPublicKey> == crypto_sign_ed25519_PUBLICKEYBYTES);

    return Read("signed file",
                [&]()
                {
                    const MCJson json = Object(envelope);

                    if (const std::string found = Text(json, PayloadTypeKey); found != type)
                    {
                        Fail("holds a \"{}\", not a \"{}\"", found, type);
                    }

                    const std::vector<uint8_t> payload = Base64(Text(json, PayloadKey), PayloadKey);
                    const std::string text(payload.begin(), payload.end());
                    const std::string signedBytes = SignedBytes(type, text);
                    const MCJson& signatures = Array(json, SignaturesKey);

                    if (signatures.size() > MaxSignatures)
                    {
                        Fail("carries {} signatures (at most {})", signatures.size(), MaxSignatures);
                    }

                    for (const MCJson& signature : signatures)
                    {
                        const std::vector<uint8_t> bytes = Base64(Text(signature, SignatureKey), SignatureKey);

                        if (bytes.size() != crypto_sign_ed25519_BYTES)
                        {
                            continue;
                        }

                        for (const MCPublicKey& key : keys)
                        {
                            if (crypto_sign_ed25519_verify_detached(
                                    bytes.data(), reinterpret_cast<const unsigned char*>(signedBytes.data()),
                                    signedBytes.size(), key.data()) == 0)
                            {
                                return text;
                            }
                        }
                    }

                    Fail("isn't signed by a key this game trusts");
                });
}

std::string MCSignedFile::Envelope(std::string_view type, std::string_view payload,
                                   std::span<const std::vector<uint8_t>> signatures)
{
    MCJson json = MCJson::object();
    json[std::string(PayloadTypeKey)] = type;
    json[std::string(PayloadKey)] =
        ToBase64(std::span(reinterpret_cast<const uint8_t*>(payload.data()), payload.size()));
    MCJson list = MCJson::array();

    for (const std::vector<uint8_t>& signature : signatures)
    {
        list.push_back(MCJson{{std::string(SignatureKey), ToBase64(signature)}});
    }

    json[std::string(SignaturesKey)] = std::move(list);
    return json.dump(2);
}

std::optional<MCSignedFile::MCPublicKey> MCSignedFile::KeyFromBase64(std::string_view text)
{
    const auto bytes = Read("key", [&]() { return Base64(text, "key"); });

    if (!bytes || bytes->size() != std::tuple_size_v<MCPublicKey>)
    {
        return std::nullopt;
    }

    MCPublicKey key{};
    std::ranges::copy(*bytes, key.begin());
    return key;
}
