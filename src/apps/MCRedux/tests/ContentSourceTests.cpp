#include "stdafx.h"
#include "../MCHttpGet.h"
#include "MCScratchFolder.h"
#include "MCTest.h"
#include "platform/MCSocket.h"
#include "update/MCHttpSource.h"

#include <thread>

namespace
{
    using namespace std::chrono_literals;

    constexpr std::string_view TestGroup = "mc_content_source";
    constexpr std::string_view HostFolder = "/content/";
    constexpr std::string_view ZipName = "zips/release-1-1.zip";
    constexpr size_t ZipBytes = 300'000;
    /// <summary>A request's head must arrive within this, or the test host drops it.</summary>
    constexpr auto RequestTime = 5s;
    constexpr auto PollTime = 1ms;
    /// <summary>A cancel ends a wait to try again (a second, the first) well before it would end.</summary>
    constexpr auto CancelledWithin = 800ms;
    /// <summary>The longest a stalled answer holds its connection, and how soon a cancel ends a transfer stalled.</summary>
    constexpr auto HoldTime = 20s;
    constexpr auto StalledCancelledWithin = 5s;

    /// <summary>Bytes no two runs differ in, and no two places in them repeat soon.</summary>
    std::string Bytes(size_t count)
    {
        std::string bytes(count, '\0');
        uint32_t state = 20261105;

        for (char& byte : bytes)
        {
            state = state * 1664525u + 1013904223u;
            byte = static_cast<char>(state >> 24);
        }

        return bytes;
    }

    std::string Lower(std::string text)
    {
        std::ranges::transform(text, text.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        return text;
    }

    /// <summary>
    /// A host on this machine for the tests: serves files from memory under /content/, whole or from a byte on
    /// ("Range: bytes=N-"), one connection at a time, each closed after its answer. It can ignore ranges, as a host
    /// may, cut the next answer off after some bytes, as a lost connection does, answer the next requests with a
    /// failure, as a busy host does, stall the next answer after some bytes, and send its answers slowly.
    /// </summary>
    class MCTestHost
    {
    public:
        explicit MCTestHost(std::map<std::string, std::string, std::less<>> files) : _Files(std::move(files))
        {
            REQUIRE(_Net.Startup());
            _Listener = _Net.ListenTcp(0);
            REQUIRE(_Listener != MCSocket::InvalidHandle);
            _Port = _Net.LocalPort(_Listener);
            _Thread = std::thread([this] { Serve(); });
        }

        MCTestHost(const MCTestHost&) = delete;
        MCTestHost& operator=(const MCTestHost&) = delete;

        ~MCTestHost()
        {
            _Stop = true;
            _Thread.join();
            _Net.Close(_Listener);
        }

        std::string Address() const { return std::format("http://127.0.0.1:{}{}", _Port, HostFolder); }

        void IgnoreRanges() { _IgnoreRanges = true; }

        /// <summary>
        /// The next answer's body stops after <paramref name="bytes"/> bytes; the connection closes once the client
        /// has passed them on (<see cref="Passed"/>), as a connection lost midway has been passing bytes on all along.
        /// </summary>
        void CutAfter(int64_t bytes)
        {
            _Passed = 0;
            _CutAfter = bytes;
        }

        /// <summary>The client passed on <paramref name="bytes"/> more bytes (its fetch's received).</summary>
        void Passed(uint64_t bytes) { _Passed += bytes; }

        /// <summary>
        /// The next answer sends <paramref name="bytes"/> bytes of its body, then nothing more, the connection held
        /// open until the client closes it.
        /// </summary>
        void StallAfter(int64_t bytes) { _StallAfter = bytes; }

        /// <summary>Answers send their body <paramref name="bytes"/> at a time, waiting <paramref name="wait"/> after each.</summary>
        void Pace(size_t bytes, std::chrono::milliseconds wait)
        {
            _PaceBytes = bytes;
            _PaceWait = wait;
        }

        /// <summary>The next <paramref name="count"/> requests are answered <paramref name="status"/>, with no body.</summary>
        void FailNext(int status, int count)
        {
            _FailStatus = status;
            _FailCount = count;
        }

        /// <summary>Each request's Range header, in order (empty: none).</summary>
        std::vector<std::string> Ranges()
        {
            const std::scoped_lock lock(_Lock);
            return _Ranges;
        }

    private:
        void Serve()
        {
            while (!_Stop)
            {
                MCSocket::Address from;
                const MCSocket::Handle client = _Net.AcceptTcp(_Listener, from);

                if (client == MCSocket::InvalidHandle)
                {
                    std::this_thread::sleep_for(PollTime);
                    continue;
                }

                Answer(client);
                _Net.Close(client);
            }
        }

        /// <summary>The request's head, or empty when it doesn't come.</summary>
        std::string Head(MCSocket::Handle client)
        {
            std::string head;
            const auto deadline = std::chrono::steady_clock::now() + RequestTime;

            while (head.find("\r\n\r\n") == std::string::npos)
            {
                std::array<uint8_t, 4096> buffer{};
                const int got = _Net.Receive(client, buffer);

                if (got < 0 || _Stop || std::chrono::steady_clock::now() > deadline)
                {
                    return {};
                }

                if (got == 0)
                {
                    std::this_thread::sleep_for(PollTime);
                    continue;
                }

                head.append(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(got));
            }

            return head;
        }

        void Answer(MCSocket::Handle client)
        {
            const std::string head = Head(client);

            if (head.empty())
            {
                return;
            }

            const size_t pathStart = head.find(' ') + 1;
            const std::string path = head.substr(pathStart, head.find(' ', pathStart) - pathStart);
            const std::string lower = Lower(head);
            constexpr std::string_view RangeField = "\r\nrange: ";
            std::string range;

            if (const size_t at = lower.find(RangeField); at != std::string::npos)
            {
                const size_t start = at + RangeField.size();
                range = head.substr(start, head.find("\r\n", start) - start);
            }

            {
                const std::scoped_lock lock(_Lock);
                _Ranges.push_back(range);
            }

            if (_FailCount.fetch_sub(1) > 0)
            {
                Send(client, std::format("HTTP/1.1 {} Failed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                                         _FailStatus.load()));
                return;
            }

            const auto file = path.starts_with(HostFolder) ? _Files.find(path.substr(HostFolder.size())) : _Files.end();

            if (file == _Files.end())
            {
                Send(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                return;
            }

            const std::string& bytes = file->second;
            constexpr std::string_view RangeUnit = "bytes=";
            const bool ranged = range.starts_with(RangeUnit) && !_IgnoreRanges;
            const uint64_t first = ranged ? std::stoull(range.substr(RangeUnit.size())) : 0;

            if (ranged && first >= bytes.size())
            {
                Send(client, std::format("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */{}\r\n"
                                         "Content-Length: 0\r\nConnection: close\r\n\r\n",
                                         bytes.size()));
                return;
            }

            std::string answer = ranged
                                     ? std::format("HTTP/1.1 206 Partial Content\r\nContent-Range: bytes {}-{}/{}\r\n",
                                                   first, bytes.size() - 1, bytes.size())
                                     : std::string("HTTP/1.1 200 OK\r\n");
            answer += std::format("Content-Length: {}\r\nConnection: close\r\n\r\n", bytes.size() - first);
            const int64_t cut = _CutAfter.exchange(-1);
            const int64_t stall = _StallAfter.exchange(-1);
            const int64_t shortened = cut >= 0 ? cut : stall;
            const size_t sent = shortened >= 0 ? static_cast<size_t>(shortened) : bytes.size() - first;
            const std::string_view body = std::string_view(bytes).substr(static_cast<size_t>(first), sent);
            Send(client, answer);

            for (size_t at = 0; at < body.size() && !_Stop;)
            {
                const size_t piece = _PaceBytes > 0 ? std::min<size_t>(_PaceBytes, body.size() - at) : body.size();

                if (!Send(client, body.substr(at, piece)))
                {
                    return;
                }

                at += piece;

                if (_PaceBytes > 0)
                {
                    std::this_thread::sleep_for(_PaceWait.load());
                }
            }

            if (stall >= 0)
            {
                Hold(client);
            }

            // NSURLSession drops what it holds unpassed when the connection goes, so the cut waits for what was sent to
            // be passed on (or a long while).
            if (cut >= 0)
            {
                const auto deadline = std::chrono::steady_clock::now() + RequestTime;

                while (_Passed < body.size() && !_Stop && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(PollTime);
                }
            }
        }

        /// <summary>Sends nothing more, until the client closes the connection or the host stops (or a long while).</summary>
        void Hold(MCSocket::Handle client)
        {
            const auto deadline = std::chrono::steady_clock::now() + HoldTime;

            while (!_Stop && std::chrono::steady_clock::now() < deadline)
            {
                std::array<uint8_t, 256> buffer{};

                if (_Net.Receive(client, buffer) < 0)
                {
                    return;
                }

                std::this_thread::sleep_for(PollTime);
            }
        }

        /// <summary>Sends all of <paramref name="data"/>: false when the connection failed (or the host stops).</summary>
        bool Send(MCSocket::Handle client, std::string_view data)
        {
            while (!data.empty() && !_Stop)
            {
                const int sent = _Net.Send(client, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});

                if (sent < 0)
                {
                    return false;
                }

                if (sent == 0)
                {
                    std::this_thread::sleep_for(PollTime);
                }

                data.remove_prefix(static_cast<size_t>(sent));
            }

            return data.empty();
        }

        MCSocketTransport _Net;
        std::map<std::string, std::string, std::less<>> _Files;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        std::thread _Thread;
        std::atomic<bool> _Stop = false;
        std::atomic<bool> _IgnoreRanges = false;
        std::atomic<int64_t> _CutAfter = -1;
        std::atomic<uint64_t> _Passed = 0;
        std::atomic<int64_t> _StallAfter = -1;
        std::atomic<size_t> _PaceBytes = 0;
        std::atomic<std::chrono::milliseconds> _PaceWait{};
        std::atomic<int> _FailStatus = 0;
        std::atomic<int> _FailCount = 0;
        std::mutex _Lock;
        std::vector<std::string> _Ranges;
    };

    std::unique_ptr<MCContentStage::MCSource> SourceAt(const MCTestHost& host)
    {
        auto source = MCHttpSource::Make(host.Address(), MCMakeHttpGet);
        REQUIRE(source.has_value());
        return std::move(*source);
    }

    std::string FileText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    /// <summary>Fetches <paramref name="name"/> into <paramref name="to"/>, adding what arrives to <paramref name="received"/>.</summary>
    std::expected<void, std::string> Fetch(MCContentStage::MCSource& source, std::string_view name,
                                           const std::filesystem::path& to, uint64_t size, uint64_t& received)
    {
        return source.Fetch(name, to, size,
                            [&](std::span<const char> bytes)
                            {
                                received += bytes.size();
                                return true;
                            });
    }
}

TEST_CASE("content source: a hosted file comes whole; one cut off is tried again from where it stopped")
{
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "resume");
    const std::filesystem::path to = folder.Path() / "release-1-1.zip";
    constexpr int64_t CutBytes = 100'000;
    host.CutAfter(CutBytes);
    uint64_t received = 0;
    const auto passed = source->Fetch(ZipName, to, zip.size(),
                                      [&](std::span<const char> bytes)
                                      {
                                          received += bytes.size();
                                          host.Passed(bytes.size());
                                          return true;
                                      });
    CHECK_EQ(passed.error_or(""), std::string());
    CHECK(FileText(to) == zip);
    CHECK_EQ(received, uint64_t{ZipBytes});
    const std::vector<std::string> ranges = host.Ranges();
    REQUIRE_EQ(ranges.size(), size_t{2});
    CHECK_EQ(ranges[0], std::string());
    constexpr std::string_view RangeStart = "bytes=";
    REQUIRE(ranges[1].starts_with(RangeStart));
    const uint64_t kept = std::stoull(ranges[1].substr(RangeStart.size()));
    CHECK(kept > 0);
    CHECK(kept <= static_cast<uint64_t>(CutBytes));

    // Whole, it needs no range; once all here, nothing is asked.
    const std::filesystem::path whole = folder.Path() / "whole.zip";
    uint64_t wholeReceived = 0;
    CHECK(Fetch(*source, ZipName, whole, zip.size(), wholeReceived).has_value());
    CHECK(FileText(whole) == zip);
    CHECK(Fetch(*source, ZipName, whole, zip.size(), wholeReceived).has_value());
    CHECK_EQ(host.Ranges().size(), size_t{3});
}

TEST_CASE("content source: a busy host is tried again, a missing file isn't; a cancel ends the wait to try again")
{
    const std::string zip = Bytes(ZipBytes);
    const std::string release = R"({"payloadType": "x"})";
    MCTestHost host({{std::string(ZipName), zip}, {"release.json", release}});
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "retried");
    constexpr int Unavailable = 503;
    constexpr int TooMany = 429;
    uint64_t received = 0;
    host.FailNext(Unavailable, 1);
    CHECK_EQ(Fetch(*source, ZipName, folder.Path() / "busy.zip", zip.size(), received).error_or(""), std::string());
    CHECK(FileText(folder.Path() / "busy.zip") == zip);
    CHECK_EQ(host.Ranges().size(), size_t{2});
    host.FailNext(TooMany, 1);
    CHECK_EQ(source->Read("release.json", release.size()).value_or(""), release);
    CHECK_EQ(host.Ranges().size(), size_t{4});

    const auto missing = Fetch(*source, "zips/none.zip", folder.Path() / "none.zip", zip.size(), received);
    CHECK(missing.error_or("").find("404") != std::string::npos);
    CHECK_EQ(host.Ranges().size(), size_t{5});
    constexpr int Forbidden = 403;
    host.FailNext(Forbidden, 1);
    CHECK(source->Read("release.json", release.size()).error_or("").find("403") != std::string::npos);
    CHECK_EQ(host.Ranges().size(), size_t{6});

    // Busy for good: cancelled while it waits to try again, it ends at once, and so does every later fetch.
    constexpr int Always = 1000;
    host.FailNext(Unavailable, Always);
    std::thread canceller(
        [&]
        {
            while (host.Ranges().size() < 7)
            {
                std::this_thread::sleep_for(PollTime);
            }

            source->Cancel();
        });
    const auto start = std::chrono::steady_clock::now();
    const auto cancelled = Fetch(*source, ZipName, folder.Path() / "cancelled.zip", zip.size(), received);
    const auto took = std::chrono::steady_clock::now() - start;
    canceller.join();
    CHECK_EQ(cancelled.error_or(""), std::string("stopped"));
    CHECK(took < CancelledWithin);
    CHECK_EQ(Fetch(*source, ZipName, folder.Path() / "later.zip", zip.size(), received).error_or(""),
             std::string("stopped"));
    CHECK_EQ(host.Ranges().size(), size_t{7});

    // A source made from one cancelled is too, and one cancelled after it's made: neither asks the host.
    const auto cancelledAnother = source->Another();
    REQUIRE(cancelledAnother != nullptr);
    CHECK_EQ(Fetch(*cancelledAnother, ZipName, folder.Path() / "another.zip", zip.size(), received).error_or(""),
             std::string("stopped"));
    const auto fresh = SourceAt(host);
    const auto another = fresh->Another();
    REQUIRE(another != nullptr);
    fresh->Cancel();
    CHECK_EQ(Fetch(*another, ZipName, folder.Path() / "another.zip", zip.size(), received).error_or(""),
             std::string("stopped"));
    CHECK_EQ(host.Ranges().size(), size_t{7});
}

TEST_CASE("content source: the waits to try again grow to a cap and ride out two minutes of outage; a cancel ends a "
          "long wait at once")
{
    // The game's own: doubling from a second, never over half a minute, adding up to two minutes.
    const std::vector<std::chrono::milliseconds> waits = MCHttpSource::RetryWaits();
    REQUIRE(!waits.empty());
    CHECK(waits.front() == MCHttpSource::FirstRetryWait);
    std::chrono::milliseconds waited{0};

    for (size_t i = 0; i < waits.size(); ++i)
    {
        CHECK(waits[i] <= MCHttpSource::LongestRetryWait);
        CHECK((i == 0 || waits[i] == std::min(waits[i - 1] * 2, MCHttpSource::LongestRetryWait)));
        waited += waits[i];
    }

    CHECK(waited >= std::chrono::minutes(2));
    CHECK(waited - waits.back() < std::chrono::minutes(2));
    CHECK(waits.back() == MCHttpSource::LongestRetryWait);

    // A source follows its schedule: busy through every wait, it gets the file; busy past them, it gives up.
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    const MCHttpSource::MCRetries quick{.First = 10ms, .Longest = 40ms, .RideOut = 300ms};
    const size_t tries = MCHttpSource::RetryWaits(quick).size() + 1;
    auto source = MCHttpSource::Make(host.Address(), MCMakeHttpGet, quick);
    REQUIRE(source.has_value());
    const MCScratchFolder folder(TestGroup, "schedule");
    constexpr int Unavailable = 503;
    uint64_t received = 0;
    host.FailNext(Unavailable, static_cast<int>(tries) - 1);
    CHECK_EQ(Fetch(**source, ZipName, folder.Path() / "rode-out.zip", zip.size(), received).error_or(""),
             std::string());
    CHECK_EQ(host.Ranges().size(), tries);
    host.FailNext(Unavailable, static_cast<int>(tries));
    CHECK(Fetch(**source, ZipName, folder.Path() / "gave-up.zip", zip.size(), received).error_or("").find("503") !=
          std::string::npos);
    CHECK_EQ(host.Ranges().size(), 2 * tries);

    // Waiting half a minute to try again, a cancel ends the wait at once.
    const MCHttpSource::MCRetries slow{.First = MCHttpSource::LongestRetryWait};
    auto waiting = MCHttpSource::Make(host.Address(), MCMakeHttpGet, slow);
    REQUIRE(waiting.has_value());
    constexpr int Always = 1000;
    host.FailNext(Unavailable, Always);
    const size_t before = host.Ranges().size();
    std::thread canceller(
        [&]
        {
            while (host.Ranges().size() == before)
            {
                std::this_thread::sleep_for(PollTime);
            }

            (*waiting)->Cancel();
        });
    const auto start = std::chrono::steady_clock::now();
    const auto cancelled = Fetch(**waiting, ZipName, folder.Path() / "cancelled.zip", zip.size(), received);
    const auto took = std::chrono::steady_clock::now() - start;
    canceller.join();
    CHECK_EQ(cancelled.error_or(""), std::string("stopped"));
    CHECK(took < CancelledWithin);
}

TEST_CASE("content source: a host that can't be reached is told apart once the tries run out; a lost connection "
          "tried again past, a busy host and a missing file aren't")
{
    const MCHttpSource::MCRetries quick{.First = 10ms, .Longest = 20ms, .RideOut = 50ms};
    const MCScratchFolder folder(TestGroup, "unreachable");
    uint64_t received = 0;

    // Nothing listens on the port: the connection is refused.
    uint16_t closedPort = 0;

    {
        MCSocketTransport net;
        REQUIRE(net.Startup());
        const MCSocket::Handle listener = net.ListenTcp(0);
        REQUIRE(listener != MCSocket::InvalidHandle);
        closedPort = net.LocalPort(listener);
        net.Close(listener);
    }

    auto nowhere =
        MCHttpSource::Make(std::format("http://127.0.0.1:{}{}", closedPort, HostFolder), MCMakeHttpGet, quick);
    REQUIRE(nowhere.has_value());
    CHECK(!(*nowhere)->HostUnreachable());
    CHECK(!(*nowhere)->Read("release.json", ZipBytes).has_value());
    CHECK((*nowhere)->HostUnreachable());

    // A lost connection tried again and got past clears it; a busy host or a missing file isn't the host out of reach.
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    auto source = MCHttpSource::Make(host.Address(), MCMakeHttpGet, quick);
    REQUIRE(source.has_value());
    constexpr int64_t CutBytes = 100'000;
    host.CutAfter(CutBytes);
    const auto resumed = (*source)->Fetch(ZipName, folder.Path() / "resumed.zip", zip.size(),
                                          [&](std::span<const char> bytes)
                                          {
                                              host.Passed(bytes.size());
                                              return true;
                                          });
    CHECK_EQ(resumed.error_or(""), std::string());
    CHECK(!(*source)->HostUnreachable());
    constexpr int Unavailable = 503;
    constexpr int Always = 1000;
    host.FailNext(Unavailable, Always);
    CHECK(!Fetch(**source, ZipName, folder.Path() / "busy.zip", zip.size(), received).has_value());
    CHECK(!(*source)->HostUnreachable());
    host.FailNext(Unavailable, 0);
    CHECK(!Fetch(**source, "zips/none.zip", folder.Path() / "none.zip", zip.size(), received).has_value());
    CHECK(!(*source)->HostUnreachable());

    // Made from one that can't reach the host, a source is told apart on its own.
    const auto another = (*nowhere)->Another();
    REQUIRE(another != nullptr);
    CHECK(!another->HostUnreachable());
    CHECK(!Fetch(*another, ZipName, folder.Path() / "another.zip", zip.size(), received).has_value());
    CHECK(another->HostUnreachable());
}

TEST_CASE("content source: sources made from one fetch files at the same time, each over a transfer of its own")
{
    constexpr int Files = 4;
    std::map<std::string, std::string, std::less<>> hosted;

    for (int i = 0; i < Files; ++i)
    {
        hosted[std::format("patches/{}.hdiff", i)] = Bytes(ZipBytes + static_cast<size_t>(i));
    }

    MCTestHost host(hosted);
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "another");
    std::vector<std::unique_ptr<MCContentStage::MCSource>> others;
    std::vector<std::expected<void, std::string>> fetched(Files);
    std::vector<std::thread> threads;

    for (int i = 0; i < Files; ++i)
    {
        others.push_back(source->Another());
        REQUIRE(others.back() != nullptr);
    }

    for (int i = 0; i < Files; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                uint64_t received = 0;
                const std::string name = std::format("patches/{}.hdiff", i);
                fetched[static_cast<size_t>(i)] =
                    Fetch(*others[static_cast<size_t>(i)], name, folder.Path() / std::format("{}.hdiff", i),
                          hosted.at(name).size(), received);
            });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    for (int i = 0; i < Files; ++i)
    {
        CHECK_EQ(fetched[static_cast<size_t>(i)].error_or(""), std::string());
        CHECK(FileText(folder.Path() / std::format("{}.hdiff", i)) == hosted[std::format("patches/{}.hdiff", i)]);
    }
}

TEST_CASE("content source: a cancel from another thread ends a transfer at once, stalled or streaming")
{
    const std::string zip = Bytes(ZipBytes);
    const MCScratchFolder folder(TestGroup, "cancelled");

    // Fetches with the host as set, cancelling from another thread once bytes have come: how long it took.
    const auto cancelledFetch = [&](MCTestHost& host, std::string_view name)
    {
        const auto source = SourceAt(host);
        std::atomic<uint64_t> received = 0;
        std::thread canceller(
            [&]
            {
                const auto deadline = std::chrono::steady_clock::now() + RequestTime;

                while (received == 0 && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::sleep_for(PollTime);
                }

                source->Cancel();
            });
        const auto start = std::chrono::steady_clock::now();
        const auto fetched = source->Fetch(ZipName, folder.Path() / name, zip.size(),
                                           [&](std::span<const char> bytes)
                                           {
                                               received += bytes.size();
                                               return true;
                                           });
        const auto took = std::chrono::steady_clock::now() - start;
        canceller.join();
        CHECK_EQ(fetched.error_or(""), std::string("stopped"));
        CHECK(received > 0);
        CHECK(std::filesystem::file_size(folder.Path() / name) < zip.size());
        return took;
    };

    // The host sends a part, then nothing: the cancel doesn't wait for the idle timeout.
    {
        MCTestHost host({{std::string(ZipName), zip}});
        constexpr int64_t StallBytes = 20'000;
        host.StallAfter(StallBytes);
        CHECK(cancelledFetch(host, "stalled.zip") < StalledCancelledWithin);
    }

    // Streaming slowly: it ends at once, not at the end of the file.
    MCTestHost host({{std::string(ZipName), zip}});
    constexpr size_t PaceBytes = 4096;
    constexpr auto PaceWait = 20ms;
    host.Pace(PaceBytes, PaceWait);
    CHECK(cancelledFetch(host, "streaming.zip") < StalledCancelledWithin);
}

TEST_CASE("content source: a reader slower than the host gets every byte in order (the transfer paused meanwhile)")
{
    // More than the transfer holds unread before it pauses (16 MB), read slowly at first.
    constexpr size_t BigBytes = size_t{40} << 20;
    const std::string big = Bytes(BigBytes);
    MCTestHost host({{std::string(ZipName), big}});
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "slow-reader");
    constexpr auto SlowStart = 500ms;
    bool first = true;
    const auto fetched = source->Fetch(ZipName, folder.Path() / "big.zip", big.size(),
                                       [&](std::span<const char>)
                                       {
                                           if (std::exchange(first, false))
                                           {
                                               std::this_thread::sleep_for(SlowStart);
                                           }

                                           return true;
                                       });
    CHECK_EQ(fetched.error_or(""), std::string());
    CHECK(FileText(folder.Path() / "big.zip") == big);
}

TEST_CASE("content source: from a host that ignores the range, the bytes already here are skipped")
{
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    host.IgnoreRanges();
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "ignored-range");
    const std::filesystem::path to = folder.Path() / "release-1-1.zip";
    constexpr size_t Here = 12'345;
    std::ofstream(to, std::ios::binary).write(zip.data(), Here);
    uint64_t received = 0;
    CHECK_EQ(Fetch(*source, ZipName, to, zip.size(), received).error_or(""), std::string());
    CHECK(FileText(to) == zip);
    CHECK_EQ(received, uint64_t{ZipBytes - Here});
}

TEST_CASE("content source: a missing file, or one shorter or longer than the list says, fails; so does a stop")
{
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "refused");
    uint64_t received = 0;
    const auto missing = Fetch(*source, "zips/none.zip", folder.Path() / "none.zip", zip.size(), received);
    CHECK(missing.error_or("").find("404") != std::string::npos);

    // Ending short, it's asked for again from there, as a lost connection's would be: the host has nothing more.
    constexpr uint64_t Extra = 10;
    const auto shortWhole = Fetch(*source, ZipName, folder.Path() / "short.zip", zip.size() + Extra, received);
    CHECK(shortWhole.error_or("").find("shorter than the list says") != std::string::npos);

    // All the hosted bytes here and more asked: the host has nothing from there on.
    const auto pastEnd = Fetch(*source, ZipName, folder.Path() / "short.zip", zip.size() + Extra, received);
    CHECK(pastEnd.error_or("").find("shorter than the list says") != std::string::npos);

    const auto longer = Fetch(*source, ZipName, folder.Path() / "long.zip", zip.size() - Extra, received);
    CHECK(longer.error_or("").find("longer than the list says") != std::string::npos);

    const auto stopped =
        source->Fetch(ZipName, folder.Path() / "stopped.zip", zip.size(), [](std::span<const char>) { return false; });
    CHECK_EQ(stopped.error_or(""), std::string("stopped"));
}

TEST_CASE("content source: a small file is read whole, one too big refused; only https, or http to this machine")
{
    const std::string release = R"({"payloadType": "x"})";
    MCTestHost host({{"release.json", release}});
    const auto source = SourceAt(host);
    CHECK_EQ(source->Read("release.json", release.size()).value_or(""), release);
    CHECK(source->Read("release.json", release.size() - 1).error_or("").find("bigger than") != std::string::npos);
    CHECK(source->Read("keys.json", release.size()).error_or("").find("404") != std::string::npos);

    const auto makes = [](std::string_view address) { return MCHttpSource::Make(address, MCMakeHttpGet).has_value(); };
    CHECK(makes("https://mechcommander-redux-downloads.mazzarin.workers.dev/content"));
    CHECK(makes("HTTPS://example.com"));
    CHECK(makes("http://127.0.0.1:8080/content/"));
    CHECK(makes("http://localhost/content"));
    CHECK(makes("http://[::1]:8080/content"));
    CHECK(!makes("http://example.com/content"));
    CHECK(!makes("http://127.0.0.1.example.com/content"));
    CHECK(!makes("http://127.0.0.1:80@evil.example/content"));
    CHECK(!makes("http://localhost@evil.example"));
    CHECK(!makes("http://127.0.0.1\\@evil.example/content"));
    CHECK(!makes("http://localhost\\evil.example/content"));
    CHECK(!makes("http://127.0.0.1 .evil.example/content"));
    CHECK(!makes("http://127.0.0.1\t/content"));
    CHECK(makes("https://example.com/content/a@b"));
    CHECK(!makes("ftp://example.com/content"));
    CHECK(!makes("https://example.com/content?release=1"));
    CHECK(!makes("example.com/content"));
    CHECK(!makes("https://"));

    CHECK(MCHttpSource::RangeFirst("bytes 100-199/1000") == uint64_t{100});
    CHECK(MCHttpSource::RangeFirst("bytes 0-0/1") == uint64_t{0});
    CHECK(!MCHttpSource::RangeFirst("bytes */1000").has_value());
    CHECK(!MCHttpSource::RangeFirst("items 1-2/3").has_value());
    CHECK(!MCHttpSource::RangeFirst("").has_value());
}
