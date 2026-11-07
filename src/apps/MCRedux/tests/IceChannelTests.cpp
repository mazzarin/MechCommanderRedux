// Built without the precompiled header (mc_tests.vcxproj), as MCIceLink.cpp is: libdatachannel's headers include the
// system's socket headers (winsock2.h drags in windows.h), which must not meet the game's Win32 stand-ins.

#include <rtc/rtc.hpp>

#include "MCTest.h"
#include "platform/MCIceLink.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// The internet games' links (platform/MCIceLink) against an end that isn't a game's: libdatachannel's own peer, which
// opens a channel the game never opens. A link has one channel, the offer's: any other means the far end isn't this
// game, and the link fails (a channel nobody takes would keep what it's sent, outside the link's limit). On the
// loopback, as IceLinkTests.cpp's links are.

namespace
{
    const MCIceLink::Settings OnThisMachine{.BindAddress = "127.0.0.1"};
    constexpr auto LinkDeadline = std::chrono::seconds(15);
    constexpr auto PumpStep = std::chrono::milliseconds(2);
    /// <summary>The game's channel, as MCIceLink names it.</summary>
    constexpr std::string_view GameChannel = "mc";
    constexpr std::string_view StrayChannel = "stray";

    /// <summary>libdatachannel's peer as the far end of a game's link: its signals queued for the game to hear.</summary>
    struct Far
    {
        std::shared_ptr<rtc::PeerConnection> Peer;
        std::mutex Lock;
        std::vector<MCIceLink::Signal> ToGame;
        std::vector<std::shared_ptr<rtc::DataChannel>> Channels;

        Far()
        {
            rtc::Configuration configuration;
            configuration.bindAddress = OnThisMachine.BindAddress;
            Peer = std::make_shared<rtc::PeerConnection>(configuration);
            Peer->onLocalDescription(
                [this](const rtc::Description& description)
                {
                    const std::scoped_lock lock(Lock);
                    ToGame.push_back({description.typeString(), std::string(description), {}});
                });
            Peer->onLocalCandidate(
                [this](const rtc::Candidate& candidate)
                {
                    const std::scoped_lock lock(Lock);
                    ToGame.push_back({"candidate", std::string(candidate), candidate.mid()});
                });
            Peer->onDataChannel(
                [this](std::shared_ptr<rtc::DataChannel> channel)
                {
                    const std::scoped_lock lock(Lock);
                    Channels.push_back(std::move(channel));
                });
        }

        ~Far() { Peer->close(); }
        Far(const Far&) = delete;
        Far& operator=(const Far&) = delete;

        /// <summary>Hears the game's signals and passes it this end's.</summary>
        void Trade(MCIceLink& game)
        {
            for (const MCIceLink::Signal& signal : game.TakeSignals())
            {
                if (signal.Kind == "candidate")
                {
                    Peer->addRemoteCandidate(rtc::Candidate(signal.Text, signal.Mid));
                }
                else
                {
                    Peer->setRemoteDescription(rtc::Description(signal.Text, signal.Kind));
                }
            }

            std::vector<MCIceLink::Signal> signals;

            {
                const std::scoped_lock lock(Lock);
                signals.swap(ToGame);
            }

            for (const MCIceLink::Signal& signal : signals)
            {
                game.Heard(signal);
            }
        }

        /// <summary>Trades signals until <paramref name="done"/> or the deadline.</summary>
        template <typename Done> bool TradeUntil(MCIceLink& game, Done done)
        {
            const auto deadline = std::chrono::steady_clock::now() + LinkDeadline;

            while (std::chrono::steady_clock::now() < deadline)
            {
                Trade(game);

                if (done())
                {
                    return true;
                }

                std::this_thread::sleep_for(PumpStep);
            }

            return false;
        }
    };
}

TEST_CASE("ice: a guest's link fails when the host opens a channel of its own")
{
    const std::unique_ptr<MCIceLink> guest = MCIceLink::Offer(OnThisMachine);
    REQUIRE(guest != nullptr);
    Far host;
    REQUIRE(host.TradeUntil(*guest, [&] { return guest->GetState() == MCIceLink::State::Open; }));

    const std::shared_ptr<rtc::DataChannel> stray = host.Peer->createDataChannel(std::string(StrayChannel));
    CHECK(host.TradeUntil(*guest, [&] { return guest->GetState() != MCIceLink::State::Open; }));
    CHECK(guest->GetState() == MCIceLink::State::Failed);
    CHECK(guest->CutOff());
}

TEST_CASE("ice: a host's link fails when the guest opens a second channel")
{
    const std::unique_ptr<MCIceLink> host = MCIceLink::Answer(OnThisMachine);
    REQUIRE(host != nullptr);
    Far guest;
    const std::shared_ptr<rtc::DataChannel> game = guest.Peer->createDataChannel(std::string(GameChannel));
    REQUIRE(guest.TradeUntil(*host, [&] { return host->GetState() == MCIceLink::State::Open; }));

    const std::shared_ptr<rtc::DataChannel> stray = guest.Peer->createDataChannel(std::string(StrayChannel));
    CHECK(guest.TradeUntil(*host, [&] { return host->GetState() != MCIceLink::State::Open; }));
    CHECK(host->GetState() == MCIceLink::State::Failed);
    CHECK(host->CutOff());
}
