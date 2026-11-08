#include "stdafx.h"
#include "MCIceTrial.h"

#include "MCConsole.h"
#include "MCVersion.h"
#include "MCWebSocketOpen.h"
#include "lib/MCRandomId.h"
#include "lib/MCThreadKeeper.h"
#include "main/MCTelemetry.h"
#include "network/MCInternetGame.h"
#include "platform/MCFileSystem.h"
#include "platform/MCIceTransport.h"
#include "platform/MCServiceSignaling.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    /// <summary>The trial's room: private, so the service lists nothing and its guests take relays only when told to.</summary>
    constexpr std::string_view TrialRoomName = "ICE TRIAL";
    constexpr int32_t TrialPlayers = 2;
    constexpr std::string_view HostWord = "host";
    constexpr std::string_view JoinWord = "join";
    constexpr std::string_view RelayOnlyOption = "--relay-only";
    constexpr std::string_view SecondsOption = "--seconds";
    constexpr std::string_view ServiceOption = "--service";
    constexpr std::string_view OutOption = "--out";
    /// <summary>The game's port (MCDirectPlay's), on the host's side of the link.</summary>
    constexpr uint16_t GamePort = 28800;
    /// <summary>How long the service gets to answer a host's or guest's socket.</summary>
    constexpr auto ServiceWait = std::chrono::seconds(15);
    /// <summary>How long the guest's connection waits for its link: ICE's paths, then the service's.</summary>
    constexpr int ConnectMs = 30000;
    /// <summary>The guest's messages: one every 100 ms, about a game message's size (a lobby's, a battle's orders).</summary>
    constexpr auto MessageEvery = std::chrono::milliseconds(100);
    constexpr size_t MessageBytes = 32;
    constexpr size_t SequenceBytes = 4;
    constexpr int DefaultGuestSeconds = 30;
    /// <summary>A host waits this long for a guest, then for its guests' links to end.</summary>
    constexpr int DefaultHostSeconds = 600;
    constexpr auto PumpStep = std::chrono::milliseconds(5);
    /// <summary>How long an end waits for its links to end (and report) once it's done.</summary>
    constexpr auto EndWait = std::chrono::seconds(5);
    /// <summary>How long the trial waits for its sockets' closes to go to the service (its leaving) before it ends.</summary>
    constexpr auto CloseWait = std::chrono::milliseconds(500);
    constexpr double Median = 0.5;
    constexpr double Tail = 0.95;

    /// <summary>The trial's lines: to the terminal and to --out's file, each flushed as it goes.</summary>
    class MCTrialOut
    {
    public:
        explicit MCTrialOut(const std::string& file)
        {
            if (!file.empty())
            {
                _File.open(MCFileSystem::FromUtf8(file), std::ios::binary | std::ios::app);
            }
        }

        void Line(const std::string& line)
        {
            const std::scoped_lock lock(_Lock);
            SDL_Log("Trial: %s", line.c_str());

            if (MCConsole::HasOutput())
            {
                std::cout << line << std::endl;
            }

            if (_File.is_open())
            {
                _File << line << std::endl;
            }
        }

    private:
        std::mutex _Lock;
        std::ofstream _File;
    };

    struct MCTrialOptions
    {
        bool Host = false;
        std::string Code;
        bool RelayOnly = false;
        std::optional<int> Seconds;
        std::string Service = std::string(MCInternetService);
        std::string Out;
    };

    std::optional<MCTrialOptions> Parse(std::span<const std::string_view> arguments, std::string& error)
    {
        MCTrialOptions options;
        size_t at = 0;

        if (at < arguments.size() && arguments[at] == HostWord)
        {
            options.Host = true;
            at++;
        }
        else if (at + 1 < arguments.size() && arguments[at] == JoinWord)
        {
            options.Code = std::string(arguments[at + 1]);
            std::ranges::transform(options.Code, options.Code.begin(), [](char c)
                                   { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
            at += 2;
        }
        else
        {
            error = "the trial is \"host\" or \"join CODE\"";
            return std::nullopt;
        }

        for (; at < arguments.size(); at++)
        {
            const std::string_view arg = arguments[at];
            const bool hasValue = at + 1 < arguments.size();

            if (arg == RelayOnlyOption)
            {
                options.RelayOnly = true;
            }
            else if (arg == SecondsOption && hasValue)
            {
                const std::string_view given = arguments[++at];
                int seconds = 0;
                const auto [end, failed] = std::from_chars(given.data(), given.data() + given.size(), seconds);

                if (failed != std::errc() || end != given.data() + given.size() || seconds < 1)
                {
                    error = std::format("{} takes a number of seconds, not \"{}\"", SecondsOption, given);
                    return std::nullopt;
                }

                options.Seconds = seconds;
            }
            else if (arg == ServiceOption && hasValue)
            {
                options.Service = std::string(arguments[++at]);
            }
            else if (arg == OutOption && hasValue)
            {
                options.Out = std::string(arguments[++at]);
            }
            else
            {
                error = std::format("the trial doesn't take \"{}\"", arg);
                return std::nullopt;
            }
        }

        return options;
    }

    /// <summary>Waits up to <paramref name="wait"/> for <paramref name="done"/>, pumping as it goes.</summary>
    template <typename Done> bool Until(Clock::duration wait, Done done)
    {
        const auto deadline = Clock::now() + wait;

        while (Clock::now() < deadline)
        {
            if (done())
            {
                return true;
            }

            std::this_thread::sleep_for(PumpStep);
        }

        return done();
    }

    /// <summary>Moves a transport's signals and links along (an accept on no listener does).</summary>
    void Pump(MCIceTransport& transport)
    {
        MCSocket::Address from;
        transport.AcceptTcp(MCSocket::InvalidHandle, from);
    }

    /// <summary>The share <paramref name="share"/> of the sorted round trips, in ms.</summary>
    double At(const std::vector<double>& sorted, double share)
    {
        return sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, static_cast<size_t>(share * sorted.size()))];
    }

    /// <summary>The host: makes the game, prints its code, and echoes each guest's messages until the time is up.</summary>
    int RunHost(const MCTrialOptions& options, MCServiceSignaling& signaling, MCIceTransport& transport,
                MCTrialOut& out)
    {
        signaling.Host();
        std::optional<std::string> code;

        // The service's answer comes in as the transport takes what the service said.
        if (!Until(ServiceWait,
                   [&]
                   {
                       Pump(transport);
                       return (code = signaling.HostedCode()).has_value() || signaling.WhyClosed();
                   }) ||
            !code)
        {
            out.Line(std::format("FAILED: the service didn't make a game: {}",
                                 signaling.WhyClosed().value_or("no answer in time")));
            return 1;
        }

        // A game the service isn't told is private is public, and its guests' links take relays only.
        signaling.SetRoom({.Name = std::string(TrialRoomName), .Public = false, .Max = TrialPlayers});
        const MCSocket::Handle listener = transport.ListenTcp(GamePort);

        if (listener == MCSocket::InvalidHandle)
        {
            out.Line("FAILED: the game's port can't be listened on");
            return 1;
        }

        out.Line(std::format("CODE {} (join it with --ice-trial join {}{})", *code, *code,
                             options.RelayOnly ? " --relay-only" : ""));
        const auto deadline = Clock::now() + std::chrono::seconds(options.Seconds.value_or(DefaultHostSeconds));
        std::vector<MCSocket::Handle> guests;
        std::array<uint8_t, MessageBytes * 16> buffer{};
        bool served = false;

        while (Clock::now() < deadline)
        {
            MCSocket::Address from;

            if (const MCSocket::Handle accepted = transport.AcceptTcp(listener, from);
                accepted != MCSocket::InvalidHandle)
            {
                out.Line("a guest connected");
                guests.push_back(accepted);
                served = true;
            }

            for (auto guest = guests.begin(); guest != guests.end();)
            {
                int got = 0;
                bool closed = false;

                while ((got = transport.Receive(*guest, buffer)) != 0)
                {
                    if (got < 0)
                    {
                        closed = true;
                        break;
                    }

                    if (transport.Send(*guest, std::span(buffer).first(static_cast<size_t>(got))) != got)
                    {
                        out.Line("an echo didn't all go: the guest's round trips will say so");
                    }
                }

                if (closed)
                {
                    out.Line("a guest's connection ended");
                    transport.Close(*guest);
                    guest = guests.erase(guest);
                }
                else
                {
                    ++guest;
                }
            }

            // One guest at a time: once it's gone and its link has ended (and reported), the trial is done.
            if (served && guests.empty() && transport.Links() == 0)
            {
                return 0;
            }

            std::this_thread::sleep_for(PumpStep);
        }

        out.Line(served ? "the time is up" : "FAILED: no guest came in time");
        return served ? 0 : 1;
    }

    /// <summary>The guest: joins the game, connects, and times its messages' round trips.</summary>
    int RunGuest(const MCTrialOptions& options, MCServiceSignaling& signaling, MCIceTransport& transport,
                 MCTrialOut& out)
    {
        signaling.Join(options.Code);

        if (!Until(ServiceWait,
                   [&]
                   {
                       Pump(transport);
                       return signaling.Joined(options.Code) || signaling.WhyClosed(options.Code);
                   }) ||
            !signaling.Joined(options.Code))
        {
            out.Line(std::format("FAILED: game {} wasn't joined: {}", options.Code,
                                 signaling.WhyClosed(options.Code).value_or("no answer in time")));
            return 1;
        }

        const std::optional<uint32_t> game = transport.Resolve(options.Code);
        const auto asked = Clock::now();
        const MCSocket::Handle stream =
            game.has_value() ? transport.ConnectTcp({*game, GamePort}, ConnectMs) : MCSocket::InvalidHandle;

        if (stream == MCSocket::InvalidHandle)
        {
            out.Line("FAILED: the connection to the game didn't open");
            Until(EndWait,
                  [&]
                  {
                      Pump(transport);
                      return transport.Links() == 0;
                  });
            return 1;
        }

        out.Line(std::format("connected in {} ms",
                             std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - asked).count()));
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::seconds(options.Seconds.value_or(DefaultGuestSeconds));
        std::vector<Clock::time_point> sentAt;
        std::vector<double> roundTrips;
        std::vector<uint8_t> received;
        std::array<uint8_t, MessageBytes * 16> buffer{};
        auto next = start;
        bool failed = false;

        while (!failed && (Clock::now() < deadline || roundTrips.size() < sentAt.size()) &&
               Clock::now() < deadline + EndWait)
        {
            if (Clock::now() >= next && Clock::now() < deadline)
            {
                std::array<uint8_t, MessageBytes> message{};
                const auto sequence = static_cast<uint32_t>(sentAt.size());

                for (size_t i = 0; i < SequenceBytes; i++)
                {
                    message[i] = static_cast<uint8_t>(sequence >> (8 * i));
                }

                sentAt.push_back(Clock::now());
                failed = transport.Send(stream, message) != static_cast<int>(message.size());
                next += MessageEvery;
            }

            for (int got = 0; !failed && (got = transport.Receive(stream, buffer)) != 0;)
            {
                failed = got < 0;
                received.insert(received.end(), buffer.begin(), buffer.begin() + std::max(got, 0));
            }

            // Each echo whole, in order: the round trip of the message it answers.
            for (; received.size() >= MessageBytes; received.erase(received.begin(), received.begin() + MessageBytes))
            {
                uint32_t sequence = 0;

                for (size_t i = 0; i < SequenceBytes; i++)
                {
                    sequence |= static_cast<uint32_t>(received[i]) << (8 * i);
                }

                if (sequence != roundTrips.size() || sequence >= sentAt.size())
                {
                    out.Line(std::format("FAILED: echo {} came where {} was due", sequence, roundTrips.size()));
                    failed = true;
                    break;
                }

                roundTrips.push_back(
                    std::chrono::duration<double, std::milli>(Clock::now() - sentAt[sequence]).count());
            }

            std::this_thread::sleep_for(PumpStep);
        }

        std::vector<double> sorted = roundTrips;
        std::ranges::sort(sorted);
        out.Line(std::format("messages: {} sent, {} echoed; round trip p50 {:.1f} ms, p95 {:.1f} ms, worst {:.1f} ms",
                             sentAt.size(), roundTrips.size(), At(sorted, Median), At(sorted, Tail),
                             sorted.empty() ? 0.0 : sorted.back()));
        transport.Close(stream);
        Until(EndWait,
              [&]
              {
                  Pump(transport);
                  return transport.Links() == 0;
              });
        return !failed && !sentAt.empty() && roundTrips.size() == sentAt.size() ? 0 : 1;
    }
}

int MCIceTrial::Run(std::span<const std::string_view> arguments)
{
    std::string error;
    const std::optional<MCTrialOptions> options = Parse(arguments, error);

    if (!options)
    {
        std::cerr << "MechCommander Redux --ice-trial: " << error << "\n"
                  << "  --ice-trial host | join CODE [--relay-only] [--seconds N] [--service wss://...] [--out FILE]\n";
        return 1;
    }

    MCTrialOut out(options->Out);
    out.Line(std::format("{} {} through {}{}", options->Host ? "hosting" : "joining",
                         options->Host ? "a game" : options->Code, options->Service,
                         options->RelayOnly ? ", relays only" : ""));
    // A copy's ID of its own each trial: the trial runs before the game has a user folder to keep one in.
    auto signaling = std::make_shared<MCServiceSignaling>(
        options->Service, MCOpenWebSocket, MCServiceIdentity{.Copy = MCRandomId(), .Version = MC_VERSION_SHOWN},
        options->RelayOnly);
    const auto report = [&](const MCTelemetry::MCLink& link) { out.Line("link " + MCTelemetry::LinkJson(link)); };
    int result = 1;

    {
        MCIceTransport transport(signaling, {}, report);
        result = options->Host ? RunHost(*options, *signaling, transport, out)
                               : RunGuest(*options, *signaling, transport, out);
    }

    // The sockets close as their player leaving, so the service frees the places at once: the closes go before the
    // process ends (Windows' on the threads the keeper joins).
    signaling.reset();
    MCThreadKeeper::Process().JoinAll();
    std::this_thread::sleep_for(CloseWait);
    out.Line(result == 0 ? "done" : "done, FAILED");
    return result;
}
