#include "stdafx.h"
#include "update/MCUpdateJson.h"

#ifndef SODIUM_STATIC
#define SODIUM_STATIC 1
#endif
#include <sodium/utils.h>

namespace
{
    using MCUpdateJson::Fail;
    using MCUpdateJson::MCJson;

    /// <summary>The member <paramref name="key"/>, or none.</summary>
    const MCJson* Find(const MCJson& object, std::string_view key)
    {
        const auto found = object.find(key);
        return found == object.end() ? nullptr : &*found;
    }
}

MCUpdateJson::MCJson MCUpdateJson::Object(std::string_view text)
{
    MCJson json = MCJson::parse(text, nullptr, false);

    if (json.is_discarded())
    {
        Fail("isn't JSON");
    }

    if (!json.is_object())
    {
        Fail("isn't a JSON object");
    }

    return json;
}

const MCUpdateJson::MCJson& MCUpdateJson::Member(const MCJson& object, std::string_view key)
{
    if (!object.is_object())
    {
        Fail("\"{}\" is in something that isn't an object", key);
    }

    const MCJson* const member = Find(object, key);

    if (member == nullptr)
    {
        Fail("\"{}\" is missing", key);
    }

    return *member;
}

std::string MCUpdateJson::Text(const MCJson& object, std::string_view key)
{
    const MCJson& member = Member(object, key);

    if (!member.is_string())
    {
        Fail("\"{}\" isn't a string", key);
    }

    return member.get<std::string>();
}

std::string MCUpdateJson::TextOr(const MCJson& object, std::string_view key, std::string_view otherwise)
{
    return Find(object, key) == nullptr ? std::string(otherwise) : Text(object, key);
}

uint64_t MCUpdateJson::Count(const MCJson& object, std::string_view key)
{
    const MCJson& member = Member(object, key);

    if (!member.is_number_unsigned())
    {
        Fail("\"{}\" isn't a whole number from 0", key);
    }

    return member.get<uint64_t>();
}

double MCUpdateJson::Number(const MCJson& object, std::string_view key)
{
    const MCJson& member = Member(object, key);

    if (!member.is_number())
    {
        Fail("\"{}\" isn't a number", key);
    }

    return member.get<double>();
}

MCSha256::MCDigest MCUpdateJson::Digest(const MCJson& object, std::string_view key)
{
    return DigestOf(Member(object, key), key);
}

MCSha256::MCDigest MCUpdateJson::DigestOf(const MCJson& value, std::string_view key)
{
    const std::optional<MCSha256::MCDigest> digest =
        value.is_string() ? MCSha256::FromHex(value.get_ref<const std::string&>()) : std::nullopt;

    if (!digest)
    {
        Fail("\"{}\" isn't a SHA-256 hash (64 hex digits)", key);
    }

    return *digest;
}

const MCUpdateJson::MCJson& MCUpdateJson::Array(const MCJson& object, std::string_view key, bool required)
{
    static const MCJson Empty = MCJson::array();

    if (!required && Find(object, key) == nullptr)
    {
        return Empty;
    }

    const MCJson& member = Member(object, key);

    if (!member.is_array())
    {
        Fail("\"{}\" isn't an array", key);
    }

    return member;
}

const MCUpdateJson::MCJson& MCUpdateJson::Child(const MCJson& object, std::string_view key)
{
    const MCJson& member = Member(object, key);

    if (!member.is_object())
    {
        Fail("\"{}\" isn't an object", key);
    }

    return member;
}

std::vector<uint8_t> MCUpdateJson::Base64(std::string_view text, std::string_view key)
{
    std::vector<uint8_t> bytes(text.size() / 4 * 3 + 3);
    size_t length = 0;
    const char* end = nullptr;

    if (sodium_base642bin(bytes.data(), bytes.size(), text.data(), text.size(), nullptr, &length, &end,
                          sodium_base64_VARIANT_ORIGINAL) != 0 ||
        end != text.data() + text.size())
    {
        Fail("\"{}\" isn't base64", key);
    }

    bytes.resize(length);
    return bytes;
}

std::string MCUpdateJson::ToBase64(std::span<const uint8_t> bytes)
{
    std::string text(sodium_base64_ENCODED_LEN(bytes.size(), sodium_base64_VARIANT_ORIGINAL), '\0');
    sodium_bin2base64(text.data(), text.size(), bytes.data(), bytes.size(), sodium_base64_VARIANT_ORIGINAL);
    // The length counts the terminating zero, which std::string holds on its own.
    text.pop_back();
    return text;
}
