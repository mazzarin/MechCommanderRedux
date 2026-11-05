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
    constexpr auto CutWait = 300ms;

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
    /// may, and cut the next answer off after some bytes, as a lost connection does.
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

        /// <summary>The next answer's body stops after <paramref name="bytes"/> bytes.</summary>
        void CutAfter(int64_t bytes) { _CutAfter = bytes; }

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
            const size_t sent = cut >= 0 ? static_cast<size_t>(cut) : bytes.size() - first;
            answer.append(bytes, static_cast<size_t>(first), sent);
            Send(client, answer);

            // A connection lost midway has been passing bytes on all along; NSURLSession drops what it holds unpassed
            // when the connection goes, so the cut waits for what was sent to be passed on.
            if (cut >= 0)
            {
                std::this_thread::sleep_for(CutWait);
            }
        }

        void Send(MCSocket::Handle client, std::string_view data)
        {
            while (!data.empty() && !_Stop)
            {
                const int sent = _Net.Send(client, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});

                if (sent < 0)
                {
                    return;
                }

                if (sent == 0)
                {
                    std::this_thread::sleep_for(PollTime);
                }

                data.remove_prefix(static_cast<size_t>(sent));
            }
        }

        MCSocketTransport _Net;
        std::map<std::string, std::string, std::less<>> _Files;
        MCSocket::Handle _Listener = MCSocket::InvalidHandle;
        uint16_t _Port = 0;
        std::thread _Thread;
        std::atomic<bool> _Stop = false;
        std::atomic<bool> _IgnoreRanges = false;
        std::atomic<int64_t> _CutAfter = -1;
        std::mutex _Lock;
        std::vector<std::string> _Ranges;
    };

    std::unique_ptr<MCContentStage::MCSource> SourceAt(const MCTestHost& host)
    {
        auto source = MCHttpSource::Make(host.Address(), MCMakeHttpGet());
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
                            [&](uint64_t count)
                            {
                                received += count;
                                return true;
                            });
    }
}

TEST_CASE("content source: a hosted file comes whole, and a cut-off one resumes from where it stopped")
{
    const std::string zip = Bytes(ZipBytes);
    MCTestHost host({{std::string(ZipName), zip}});
    const auto source = SourceAt(host);
    const MCScratchFolder folder(TestGroup, "resume");
    const std::filesystem::path to = folder.Path() / "release-1-1.zip";
    constexpr int64_t CutBytes = 100'000;
    host.CutAfter(CutBytes);
    uint64_t received = 0;
    const auto cut = Fetch(*source, ZipName, to, zip.size(), received);
    REQUIRE(!cut.has_value());
    const uint64_t kept = std::filesystem::file_size(to);
    CHECK(kept > 0);
    CHECK(kept <= static_cast<uint64_t>(CutBytes));
    CHECK_EQ(received, kept);

    const auto resumed = Fetch(*source, ZipName, to, zip.size(), received);
    CHECK_EQ(resumed.error_or(""), std::string());
    CHECK(FileText(to) == zip);
    CHECK_EQ(received, uint64_t{ZipBytes});
    const std::vector<std::string> ranges = host.Ranges();
    REQUIRE_EQ(ranges.size(), size_t{2});
    CHECK_EQ(ranges[0], std::string());
    CHECK_EQ(ranges[1], std::format("bytes={}-", kept));

    // Whole, it needs no range; once all here, nothing is asked.
    const std::filesystem::path whole = folder.Path() / "whole.zip";
    uint64_t wholeReceived = 0;
    CHECK(Fetch(*source, ZipName, whole, zip.size(), wholeReceived).has_value());
    CHECK(FileText(whole) == zip);
    CHECK(Fetch(*source, ZipName, whole, zip.size(), wholeReceived).has_value());
    CHECK_EQ(host.Ranges().size(), size_t{3});
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

    constexpr uint64_t Extra = 10;
    const auto shortWhole = Fetch(*source, ZipName, folder.Path() / "short.zip", zip.size() + Extra, received);
    CHECK(shortWhole.error_or("").find("stopped short") != std::string::npos);

    // All the hosted bytes here and more asked: the host has nothing from there on.
    const auto pastEnd = Fetch(*source, ZipName, folder.Path() / "short.zip", zip.size() + Extra, received);
    CHECK(pastEnd.error_or("").find("shorter than the list says") != std::string::npos);

    const auto longer = Fetch(*source, ZipName, folder.Path() / "long.zip", zip.size() - Extra, received);
    CHECK(longer.error_or("").find("longer than the list says") != std::string::npos);

    const auto stopped =
        source->Fetch(ZipName, folder.Path() / "stopped.zip", zip.size(), [](uint64_t) { return false; });
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

    const auto makes = [](std::string_view address)
    { return MCHttpSource::Make(address, MCMakeHttpGet()).has_value(); };
    CHECK(makes("https://mechcommander-redux-downloads.mazzarin.workers.dev/content"));
    CHECK(makes("HTTPS://example.com"));
    CHECK(makes("http://127.0.0.1:8080/content/"));
    CHECK(makes("http://localhost/content"));
    CHECK(makes("http://[::1]:8080/content"));
    CHECK(!makes("http://example.com/content"));
    CHECK(!makes("http://127.0.0.1.example.com/content"));
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
