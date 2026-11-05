#include "stdafx.h"
#include "update/MCHttpSource.h"
#include "platform/MCFileSystem.h"

#include <condition_variable>
#include <mutex>

namespace
{
    using namespace std::chrono_literals;

    constexpr std::string_view SecureScheme = "https";
    constexpr std::string_view PlainScheme = "http";
    constexpr std::string_view SchemeEnd = "://";
    /// <summary>The names this machine goes by: plain http is allowed to them alone.</summary>
    constexpr std::array<std::string_view, 3> ThisMachine = {"127.0.0.1", "localhost", "[::1]"};
    constexpr int WholeStatus = 200;
    constexpr int PartStatus = 206;
    constexpr int PastEndStatus = 416;
    /// <summary>The answers that may pass: the host busy or failing (5xx), a request it timed out, too many at once.</summary>
    constexpr int FirstServerError = 500;
    constexpr int LastServerError = 599;
    constexpr int TimedOutStatus = 408;
    constexpr int TooManyStatus = 429;
    /// <summary>
    /// The waits before each try again after a failure that may pass (a lost connection, a busy host), resuming where
    /// the bytes stopped; a try that brought bytes starts them over, so a long download survives many cuts.
    /// </summary>
    constexpr std::array<std::chrono::milliseconds, 4> RetryWaits = {1s, 2s, 4s, 8s};
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

    /// <summary>A try's failure, and whether trying again may get past it.</summary>
    struct MCFailure
    {
        enum class MCKind : uint8_t
        {
            /// <summary>The same again (a missing file, one not as the list has it, a write that failed).</summary>
            Lasting,
            /// <summary>A lost connection, a timeout, a busy host.</summary>
            Passing,
            Stopped,
        };

        MCKind Kind = MCKind::Lasting;
        std::string Reason;
    };

    using MCKind = MCFailure::MCKind;

    /// <summary>
    /// What a source's Cancel reaches: its transfer and its waits to try again. Once cancelled, it stays so: every
    /// later fetch fails at once.
    /// </summary>
    class MCStopper
    {
    public:
        explicit MCStopper(MCHttpSource::MCGet& get) : _Get(get) {}

        void Cancel()
        {
            {
                const std::scoped_lock lock(_Lock);
                _Cancelled = true;
                _Get.Cancel();
            }

            _Woken.notify_all();
        }

        bool Cancelled()
        {
            const std::scoped_lock lock(_Lock);
            return _Cancelled;
        }

        /// <summary>Waits <paramref name="wait"/>, unless cancelled first.</summary>
        /// <returns>Whether it waited it all.</returns>
        bool Wait(std::chrono::milliseconds wait)
        {
            std::unique_lock lock(_Lock);
            return !_Woken.wait_for(lock, wait, [&] { return _Cancelled; });
        }

    private:
        std::mutex _Lock;
        std::condition_variable _Woken;
        bool _Cancelled = false;
        MCHttpSource::MCGet& _Get;
    };

    /// <summary>
    /// What every reply refuses: a status that isn't the one asked for, kept as the reply's failure with whether it
    /// may pass.
    /// </summary>
    class MCCheckedReply : public MCHttpSource::MCReply
    {
    public:
        const std::optional<MCFailure>& Failure() const { return _Failure; }

    protected:
        bool Fail(std::string error, MCKind kind = MCKind::Lasting)
        {
            _Failure = MCFailure{.Kind = kind, .Reason = std::move(error)};
            return false;
        }

        bool FailStatus(int status)
        {
            const bool passing = (status >= FirstServerError && status <= LastServerError) ||
                                 status == TimedOutStatus || status == TooManyStatus;
            return Fail(std::format("the host answered {}", status), passing ? MCKind::Passing : MCKind::Lasting);
        }

    private:
        std::optional<MCFailure> _Failure;
    };

    /// <summary>A download appended to its file: from the file's size on, up to the size the list gives.</summary>
    class MCFileReply final : public MCCheckedReply
    {
    public:
        MCFileReply(std::ofstream& out, uint64_t at, uint64_t size,
                    const std::function<bool(std::span<const char>)>& received)
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
            return _Received(bytes) || Fail(std::string(StoppedError), MCKind::Stopped);
        }

    private:
        std::ofstream& _Out;
        uint64_t _At;
        uint64_t _Size;
        uint64_t _Skip = 0;
        const std::function<bool(std::span<const char>)>& _Received;
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
            : _Address(std::move(address)), _Get(std::move(get)), _Stopper(*_Get)
        {
        }

        std::expected<void, std::string> Fetch(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                               const std::function<bool(std::span<const char>)>& received) override
        {
            return Tried(name, [&](bool& progressed) { return FetchOnce(name, to, size, received, progressed); });
        }

        std::expected<std::string, std::string> Read(std::string_view name, size_t most) override
        {
            std::string text;
            const auto read = Tried(name,
                                    [&](bool&) -> std::expected<void, MCFailure>
                                    {
                                        MCTextReply reply(most);
                                        const auto got = _Get->Get(Url(name), 0, reply);

                                        if (reply.Failure())
                                        {
                                            return std::unexpected(*reply.Failure());
                                        }

                                        if (!got)
                                        {
                                            return std::unexpected(MCFailure{MCKind::Passing, got.error()});
                                        }

                                        text = std::move(reply.Text());
                                        return {};
                                    });

            if (!read)
            {
                return std::unexpected(std::format("{}: {}", name, read.error()));
            }

            return text;
        }

        void Cancel() override { _Stopper.Cancel(); }

    private:
        std::string Url(std::string_view name) const { return std::format("{}/{}", _Address, Escaped(name)); }

        /// <summary>
        /// Runs <paramref name="attempt"/> (given whether it brought bytes) until it succeeds, tries again after a
        /// failure that may pass, waiting longer each time, and gives up on any other, or once the waits run out.
        /// </summary>
        template <typename MCAttempt>
        std::expected<void, std::string> Tried(std::string_view name, const MCAttempt& attempt)
        {
            size_t failed = 0;

            for (;;)
            {
                if (_Stopper.Cancelled())
                {
                    return std::unexpected(std::string(StoppedError));
                }

                bool progressed = false;
                const std::expected<void, MCFailure> done = attempt(progressed);

                if (done)
                {
                    return {};
                }

                if (done.error().Kind == MCKind::Stopped || _Stopper.Cancelled())
                {
                    return std::unexpected(std::string(StoppedError));
                }

                if (progressed)
                {
                    failed = 0;
                }

                if (done.error().Kind == MCKind::Lasting || failed == RetryWaits.size())
                {
                    return std::unexpected(done.error().Reason);
                }

                const std::chrono::milliseconds wait = RetryWaits[failed++];
                SDL_Log("Updates: %.*s: %s; tried again in %lld ms", static_cast<int>(name.size()), name.data(),
                        done.error().Reason.c_str(), static_cast<long long>(wait.count()));

                if (!_Stopper.Wait(wait))
                {
                    return std::unexpected(std::string(StoppedError));
                }
            }
        }

        /// <summary>One try at a download, from the bytes already in its file on.</summary>
        std::expected<void, MCFailure> FetchOnce(std::string_view name, const std::filesystem::path& to, uint64_t size,
                                                 const std::function<bool(std::span<const char>)>& received,
                                                 bool& progressed)
        {
            std::error_code error;
            const uint64_t at = std::filesystem::exists(to, error) ? std::filesystem::file_size(to, error) : 0;

            if (error)
            {
                return std::unexpected(MCFailure{
                    MCKind::Lasting, std::format("{} can't be read ({})", MCFileSystem::ToUtf8(to), error.message())});
            }

            if (at >= size)
            {
                return {};
            }

            std::ofstream out(to, std::ios::binary | std::ios::app);

            if (!out)
            {
                return std::unexpected(
                    MCFailure{MCKind::Lasting, std::format("{} can't be written", MCFileSystem::ToUtf8(to))});
            }

            MCFileReply reply(out, at, size, received);
            const auto got = _Get->Get(Url(name), at, reply);
            out.close();
            progressed = reply.At() > at;

            if (reply.Failure())
            {
                return std::unexpected(*reply.Failure());
            }

            if (!got)
            {
                return std::unexpected(MCFailure{MCKind::Passing, got.error()});
            }

            if (!out)
            {
                return std::unexpected(
                    MCFailure{MCKind::Lasting, std::format("{} can't be written", MCFileSystem::ToUtf8(to))});
            }

            // The answer ended early without the transfer saying so: as a lost connection, the rest is asked for.
            if (reply.At() < size)
            {
                return std::unexpected(MCFailure{MCKind::Passing, "the download stopped short"});
            }

            return {};
        }

        std::string _Address;
        std::unique_ptr<MCHttpSource::MCGet> _Get;
        MCStopper _Stopper;
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
