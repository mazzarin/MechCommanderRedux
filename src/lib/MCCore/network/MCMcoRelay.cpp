#include "stdafx.h"
#include "network/MCMcoRelay.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace
{
    using Json = nlohmann::json;

    /// <summary>The kinds of the game's datagrams, after the magic (MCDirectPlay's).</summary>
    constexpr uint8_t QueryKind = 1;
    constexpr uint8_t AnswerKind = 2;
    /// <summary>Where a datagram's kind is, and where an answer's port is (little-endian, as the game writes it).</summary>
    constexpr size_t KindAt = 4;
    constexpr size_t PortAt = 5;

    constexpr uint8_t ByteBits = 8;
    constexpr uint8_t ByteMask = 0xff;

    /// <summary>Whether <paramref name="datagram"/> starts with the game's magic and has <paramref name="kind"/>.</summary>
    bool IsKind(std::span<const uint8_t> datagram, uint8_t kind)
    {
        return datagram.size() > KindAt && std::equal(MCMcoRelay::GameMagic.begin(), MCMcoRelay::GameMagic.end(),
                                                      datagram.begin()) &&
               datagram[KindAt] == kind;
    }

    /// <summary>The string <paramref name="name"/> of <paramref name="object"/>, or why it isn't one.</summary>
    std::expected<std::string, std::string> Field(const Json& object, std::string_view name, bool required)
    {
        const auto it = object.find(name);

        if (it == object.end() || it->is_null())
        {
            if (required)
            {
                return std::unexpected(std::format("the relay's answer has no {}", name));
            }

            return std::string();
        }

        if (!it->is_string())
        {
            return std::unexpected(std::format("the relay's {} isn't text", name));
        }

        return it->get<std::string>();
    }
}

namespace MCMcoRelay
{
    bool SizeAllowed(uint8_t type, size_t size)
    {
        switch (static_cast<MCFrameType>(type))
        {
        case MCFrameType::Control:
            return size >= 1 && size <= ControlMost;
        case MCFrameType::Datagram:
            return size >= 1 && size <= DatagramMost;
        case MCFrameType::StreamData:
            return size >= 1 && size <= StreamMost;
        case MCFrameType::StreamOpen:
        case MCFrameType::StreamClose:
        case MCFrameType::KeepAlive:
            return size == 0;
        }

        return false;
    }

    std::vector<uint8_t> Frame(MCFrameType type, std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> frame;
        frame.reserve(HeaderBytes + payload.size());
        frame.push_back(static_cast<uint8_t>(type));
        frame.push_back(static_cast<uint8_t>((payload.size() >> ByteBits) & ByteMask));
        frame.push_back(static_cast<uint8_t>(payload.size() & ByteMask));
        frame.insert(frame.end(), payload.begin(), payload.end());
        return frame;
    }

    void MCFrameReader::Feed(std::span<const uint8_t> bytes)
    {
        // What was read is dropped once it's most of what's held, so the buffer stays about a frame's size.
        if (_Read > 0 && _Read * 2 >= _Bytes.size())
        {
            _Bytes.erase(_Bytes.begin(), _Bytes.begin() + static_cast<ptrdiff_t>(_Read));
            _Read = 0;
        }

        _Bytes.insert(_Bytes.end(), bytes.begin(), bytes.end());
    }

    std::optional<MCFrame> MCFrameReader::Next()
    {
        if (_Bad || _Bytes.size() - _Read < HeaderBytes)
        {
            return std::nullopt;
        }

        const uint8_t type = _Bytes[_Read];
        const size_t size = (static_cast<size_t>(_Bytes[_Read + 1]) << ByteBits) | _Bytes[_Read + 2];

        if (!SizeAllowed(type, size))
        {
            _Bad = true;
            return std::nullopt;
        }

        if (_Bytes.size() - _Read - HeaderBytes < size)
        {
            return std::nullopt;
        }

        const auto start = _Bytes.begin() + static_cast<ptrdiff_t>(_Read + HeaderBytes);
        MCFrame frame{static_cast<MCFrameType>(type), std::vector<uint8_t>(start, start + static_cast<ptrdiff_t>(size))};
        _Read += HeaderBytes + size;
        return frame;
    }

    bool MCFrameReader::Bad() const
    {
        return _Bad;
    }

    std::string_view ModeName(MCMode mode)
    {
        switch (mode)
        {
        case MCMode::Status:
            return "status";
        case MCMode::Host:
            return "host";
        case MCMode::Attach:
            return "attach";
        case MCMode::Join:
            return "join";
        }

        return "status";
    }

    std::string Hello(MCMode mode, std::string_view credential)
    {
        // The helper's hello (mode, then ticket or grant, each left out when empty), in its order.
        Json hello = Json::object();
        hello["mode"] = std::string(ModeName(mode));

        if (!credential.empty())
        {
            hello[mode == MCMode::Attach ? "grant" : "ticket"] = std::string(credential);
        }

        return hello.dump();
    }

    std::expected<MCReply, std::string> ReadReply(std::span<const uint8_t> payload)
    {
        const Json reply = Json::parse(payload.begin(), payload.end(), nullptr, false);

        if (!reply.is_object())
        {
            return std::unexpected(std::string("the relay's answer isn't a JSON object"));
        }

        auto status = Field(reply, "status", true);
        auto epoch = Field(reply, "epoch", false);
        auto grant = Field(reply, "grant", false);

        if (!status || !epoch || !grant)
        {
            return std::unexpected(!status ? status.error() : !epoch ? epoch.error() : grant.error());
        }

        return MCReply{std::move(*status), std::move(*epoch), std::move(*grant)};
    }

    bool IsQuery(std::span<const uint8_t> datagram)
    {
        return datagram.size() == QueryBytes && IsKind(datagram, QueryKind);
    }

    bool IsAnswer(std::span<const uint8_t> datagram)
    {
        return datagram.size() >= AnswerLeast && datagram.size() <= DatagramMost && IsKind(datagram, AnswerKind);
    }

    void SetAnswerPort(std::span<uint8_t> answer, uint16_t port)
    {
        answer[PortAt] = static_cast<uint8_t>(port & ByteMask);
        answer[PortAt + 1] = static_cast<uint8_t>(port >> ByteBits);
    }

    uint16_t AnswerPort(std::span<const uint8_t> answer)
    {
        return static_cast<uint16_t>(answer[PortAt] | (answer[PortAt + 1] << ByteBits));
    }

    std::array<uint8_t, QueryBytes> Query(const _GUID& application)
    {
        // As MCDirectPlay's Writer lays it: the magic, the kind, then the GUID's three numbers little-endian and its
        // eight bytes.
        std::array<uint8_t, QueryBytes> query{};
        size_t at = 0;
        const auto put = [&](uint64_t value, size_t bytes)
        {
            for (size_t i = 0; i < bytes; i++)
            {
                query[at++] = static_cast<uint8_t>((value >> (ByteBits * i)) & ByteMask);
            }
        };

        for (const uint8_t b : GameMagic)
        {
            query[at++] = b;
        }

        query[at++] = QueryKind;
        put(application.Data1, sizeof(uint32_t));
        put(application.Data2, sizeof(uint16_t));
        put(application.Data3, sizeof(uint16_t));

        for (const uint8_t b : application.Data4)
        {
            query[at++] = b;
        }

        return query;
    }
}
