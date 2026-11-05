#include "stdafx.h"
#include "update/MCHttpSource.h"
#include "platform/MCFileSystem.h"

namespace
{
    constexpr std::string_view SecureScheme = "https";
    constexpr std::string_view PlainScheme = "http";
    constexpr std::string_view SchemeEnd = "://";
    /// <summary>The names this machine goes by: plain http is allowed to them alone.</summary>
    constexpr std::array<std::string_view, 3> ThisMachine = {"127.0.0.1", "localhost", "[::1]"};
    constexpr int WholeStatus = 200;
    constexpr int PartStatus = 206;
    constexpr int PastEndStatus = 416;
    constexpr std::string_view StoppedError = "stopped";
    constexpr std::string_view ContentRangeUnit = "bytes ";

    std::string Lower(std::string_view text)
    {
        std::string lower(text);
        std::ranges::transform(lower, lower.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        return lower;
    }

    /// <summary>The name as an address's path: '/' kept, every byte but the unreserved ones percent-encoded.</summary>
    std::string Escaped(std::string_view name)
    {
        std::string escaped;

        for (const char c : name)
        {
            const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                    c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
            escaped += unreserved ? std::string(1, c) : std::format("%{:02X}", static_cast<unsigned char>(c));
        }

        return escaped;
    }

    /// <summary>What every reply refuses: a status that isn't the one asked for, kept as the reply's error.</summary>
    class MCCheckedReply : public MCHttpSource::MCReply
    {
    public:
        const std::string& Error() const { return _Error; }

    protected:
        bool Fail(std::string error)
        {
            _Error = std::move(error);
            return false;
        }

        bool FailStatus(int status) { return Fail(std::format("the host answered {}", status)); }

    private:
        std::string _Error;
    };

    /// <summary>A download appended to its file: from the file's size on, up to the size the list gives.</summary>
    class MCFileReply final : public MCCheckedReply
    {
    public:
        MCFileReply(std::ofstream& out, uint64_t at, uint64_t size, const std::function<bool(uint64_t)>& received)
            : _Out(out), _At(at), _Size(size), _Received(received)
        {
        }

        uint64_t At() const { return _At; }

        bool Status(int status, std::string_view contentRange) override
        {
            if (status == PartStatus)
            {
                return MCHttpSource::RangeFirst(contentRange) == _At ||
                       Fail("the host sent another part of the file than the one asked for");
            }

            if (status == WholeStatus)
            {
                // The host ignored the range: the bytes already here are skipped.
                _Skip = _At;
                return true;
            }

            return status == PastEndStatus ? Fail("the hosted file is shorter than the list says") : FailStatus(status);
        }

        bool Body(std::span<const char> bytes) override
        {
            const size_t skipped = static_cast<size_t>(std::min<uint64_t>(_Skip, bytes.size()));
            _Skip -= skipped;
            bytes = bytes.subspan(skipped);

            if (bytes.empty())
            {
                return true;
            }

            if (bytes.size() > _Size - _At)
            {
                return Fail("the hosted file is longer than the list says");
            }

            _Out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

            if (!_Out)
            {
                return Fail("the download can't be written");
            }

            _At += bytes.size();
            return _Received(bytes.size()) || Fail(std::string(StoppedError));
        }

    private:
        std::ofstream& _Out;
        uint64_t _At;
        uint64_t _Size;
        uint64_t _Skip = 0;
        const std::function<bool(uint64_t)>& _Received;
    };

    /// <summary>A small file read whole, up to a most.</summary>
    class MCTextReply final : public MCCheckedReply
    {
    public:
        explicit MCTextReply(size_t most) : _Most(most) {}

        std::string& Text() { return _Text; }

        bool Status(int status, std::string_view) override { return status == WholeStatus || FailStatus(status); }

        bool Body(std::span<const char> bytes) override
        {
            if (bytes.size() > _Most - _Text.size())
            {
                return Fail(std::format("it's bigger than {} bytes", _Most));
            }

            _Text.append(bytes.data(), bytes.size());
            return true;
        }

    private:
        size_t _Most;
        std::string _Text;
    };

    class MCSource final : public MCContentStage::MCSource
    {
    public:
        MCSource(std::string address, std::unique_ptr<MCHttpSource::MCGet> get)
            : _Address(std::move(address)), _Get(std::move(get))
        {
        }

        std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                               const std::function<bool(uint64_t)>& received) override
        {
            std::error_code error;
            const uint64_t at = std::filesystem::exists(to, error) ? std::filesystem::file_size(to, error) : 0;

            if (error)
            {
                return std::unexpected(std::format("{} can't be read ({})", MCFileSystem::ToUtf8(to), error.message()));
            }

            if (at >= size)
            {
                return {};
            }

            std::ofstream out(to, std::ios::binary | std::ios::app);

            if (!out)
            {
                return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(to)));
            }

            MCFileReply reply(out, at, size, received);
            const auto got = _Get->Get(Url(name), at, reply);
            out.close();

            if (!reply.Error().empty())
            {
                return std::unexpected(reply.Error());
            }

            if (!got)
            {
                return std::unexpected(got.error());
            }

            if (!out)
            {
                return std::unexpected(std::format("{} can't be written", MCFileSystem::ToUtf8(to)));
            }

            if (reply.At() < size)
            {
                return std::unexpected("the download stopped short");
            }

            return {};
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            MCTextReply reply(most);
            const auto got = _Get->Get(Url(name), 0, reply);

            if (!reply.Error().empty())
            {
                return std::unexpected(std::format("{}: {}", name, reply.Error()));
            }

            if (!got)
            {
                return std::unexpected(std::format("{}: {}", name, got.error()));
            }

            return std::move(reply.Text());
        }

    private:
        std::string Url(std::string_view name) const { return std::format("{}/{}", _Address, Escaped(name)); }

        std::string _Address;
        std::unique_ptr<MCHttpSource::MCGet> _Get;
    };
}

std::optional<uint64_t> MCHttpSource::RangeFirst(std::string_view contentRange)
{
    if (!contentRange.starts_with(ContentRangeUnit))
    {
        return std::nullopt;
    }

    const std::string_view rest = contentRange.substr(ContentRangeUnit.size());
    uint64_t first = 0;
    const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), first);

    if (error != std::errc() || end == rest.data() || end == rest.data() + rest.size() || *end != '-')
    {
        return std::nullopt;
    }

    return first;
}

std::optional<std::pair<std::string, std::string>> MCHttpSource::SchemeAndHost(std::string_view url)
{
    const size_t schemeEnd = url.find(SchemeEnd);

    if (schemeEnd == std::string_view::npos || schemeEnd == 0)
    {
        return std::nullopt;
    }

    const std::string_view rest = url.substr(schemeEnd + SchemeEnd.size());
    // A bracketed address ([::1]) keeps its colons; otherwise the host ends at its port or path.
    const size_t hostEnd = rest.starts_with('[') ? rest.find(']') + 1 : rest.find_first_of(":/");
    const std::string_view host = rest.substr(0, std::min(hostEnd, rest.size()));

    if (host.empty() || (rest.starts_with('[') && hostEnd == 0))
    {
        return std::nullopt;
    }

    return std::pair{Lower(url.substr(0, schemeEnd)), Lower(host)};
}

std::expected<std::unique_ptr<MCContentStage::MCSource>, std::string> MCHttpSource::Make(std::string_view address,
                                                                                         std::unique_ptr<MCGet> get)
{
    const auto parts = SchemeAndHost(address);

    if (!parts || address.find_first_of("?#") != std::string_view::npos)
    {
        return std::unexpected(std::format("\"{}\" isn't the address of a folder", address));
    }

    const auto& [scheme, host] = *parts;

    if (scheme != SecureScheme && !(scheme == PlainScheme && std::ranges::contains(ThisMachine, host)))
    {
        return std::unexpected(std::format("\"{}\" isn't an https address", address));
    }

    while (address.ends_with('/'))
    {
        address.remove_suffix(1);
    }

    return std::make_unique<MCSource>(std::string(address), std::move(get));
}
