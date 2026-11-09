#include "stdafx.h"
#include "network/MCInternetGame.h"

#include "network/MCInternetCopy.h"
#include "platform/MCNetSwitch.h"
#include "platform/MCNeverDestroyed.h"

namespace
{
    /// <summary>How often End looks at whether the sockets' closes have gone.</summary>
    constexpr auto CloseLookEvery = std::chrono::milliseconds(10);
    /// <summary>
    /// How much longer than the transport's own wait a guest's link may take to open before the join gives up: the
    /// transport ends a link that doesn't open in its time, so this is only the backstop.
    /// </summary>
    constexpr auto ReachMargin = std::chrono::seconds(5);
    /// <summary>The log's name for a game whose code the service hasn't given yet.</summary>
    constexpr std::string_view HostingName = "(hosting)";

    std::mutex SetupLock;

    MCInternetSetup& TheSetup()
    {
        static MCNeverDestroyed<MCInternetSetup> setup;
        return *setup;
    }

    const char* StageText(MCInternetGame::Stage stage)
    {
        switch (stage)
        {
            case MCInternetGame::Stage::Service:
                return "the service answering";
            case MCInternetGame::Stage::Reaching:
                return "its link to the host opening";
            case MCInternetGame::Stage::Ready:
                return "ready";
            case MCInternetGame::Stage::Failed:
                return "failed";
        }

        return "?";
    }
}

void MCInternetGame::Setup(MCInternetSetup setup)
{
    const std::scoped_lock lock(SetupLock);
    TheSetup() = std::move(setup);
}

MCInternetSetup MCInternetGame::SetupNow()
{
    // A copy, as a game made meanwhile may outlive a change.
    const std::scoped_lock lock(SetupLock);
    return TheSetup();
}

bool MCInternetGame::Available()
{
    const std::scoped_lock lock(SetupLock);
    return static_cast<bool>(TheSetup().Open);
}

std::unique_ptr<MCInternetGame> MCInternetGame::Host(MCNetSwitch& net, bool isPublic)
{
    const MCInternetSetup setup = SetupNow();

    if (!setup.Open)
    {
        return nullptr;
    }

    auto game = std::unique_ptr<MCInternetGame>(new MCInternetGame(net, setup, true, isPublic, {}));
    game->_Signaling->Host();
    SDL_Log("Internet game: hosting a %s game", isPublic ? "public" : "private");
    return game;
}

std::unique_ptr<MCInternetGame> MCInternetGame::HostAgain(MCNetSwitch& net, bool isPublic, const MCInternetPlace& place)
{
    const MCInternetSetup setup = SetupNow();

    if (!setup.Open)
    {
        return nullptr;
    }

    auto game = std::unique_ptr<MCInternetGame>(new MCInternetGame(net, setup, true, isPublic, {}));
    game->_Signaling->HostAgain(place.Code, place.Token);
    SDL_Log("Internet game: hosting game %s again, back in its place", place.Code.c_str());
    return game;
}

std::unique_ptr<MCInternetGame> MCInternetGame::Join(MCNetSwitch& net, std::string_view code, bool relayOnly)
{
    const MCInternetSetup setup = SetupNow();

    if (!setup.Open)
    {
        return nullptr;
    }

    std::string upper = CodeOf(code);
    auto game = std::unique_ptr<MCInternetGame>(new MCInternetGame(net, setup, false, relayOnly, upper));
    game->_Signaling->Join(upper);
    SDL_Log("Internet game: joining game %s%s", upper.c_str(), relayOnly ? ", known public: relays only" : "");
    return game;
}

std::string MCInternetGame::CodeOf(std::string_view typed)
{
    std::string code(typed);
    std::ranges::transform(code, code.begin(),
                           [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
    return code;
}

MCInternetGame::MCInternetGame(MCNetSwitch& net, const MCInternetSetup& setup, bool hosting, bool isPublic,
                               std::string code)
    : _Net(net)
    , _Signaling(std::make_shared<MCServiceSignaling>(
          setup.Service, setup.Open,
          MCServiceIdentity{.Copy = setup.Copy.empty() ? MCInternetCopyId() : setup.Copy, .Version = setup.Version},
          isPublic))
    , _Hosting(hosting)
    , _Public(isPublic)
    , _Code(std::move(code))
    , _AskedAt(Clock::now())
    , _ServiceWait(setup.ServiceWait)
    , _ReachWait(std::chrono::duration_cast<std::chrono::milliseconds>(setup.Deadlines.Open + ReachMargin))
{
    if (!setup.BindAddress.empty())
    {
        _Signaling->BindLinksTo(setup.BindAddress);
    }

    _Transport = std::make_shared<MCIceTransport>(_Signaling, setup.Deadlines);
    _Net.BeginInternet(_Transport);
}

MCInternetGame::~MCInternetGame()
{
    End();
}

MCInternetGame::Stage MCInternetGame::Update()
{
    if (_Stage == Stage::Failed || _Transport == nullptr)
    {
        _Stage = Stage::Failed;
        return _Stage;
    }

    _Transport->Tend();
    const std::string_view socket = _Hosting ? std::string_view() : std::string_view(_Code);

    // The service ended the game's socket (the game gone, full, banned...): nothing comes back after it.
    if (const std::optional<MCServiceRefusal> refusal = _Signaling->Refusal(socket))
    {
        return Fail(*refusal, "the service refused it");
    }

    // A join whose host went away from the service before its link opened can't finish (the host's game restarting
    // into an update, say): as the service's own answer to a join then, the guest tries again.
    if (!_Hosting && _Stage != Stage::Ready && _Signaling->HostAway(_Code))
    {
        return Fail(MCServiceRefusal::HostAway, "its host went away from the service");
    }

    // What the service says of a joined game only ever adds to what this game knew (its socket back in its place too).
    if (!_Hosting && !_Public && _Signaling->Joined(_Code))
    {
        _Public = _Signaling->GamePublic(_Code);
    }

    switch (_Stage)
    {
        case Stage::Service:
        {
            if (_Hosting)
            {
                if (const std::optional<std::string> code = _Signaling->HostedCode())
                {
                    _Code = *code;
                    _Stage = Stage::Ready;
                    SDL_Log("Internet game: hosting game %s", _Code.c_str());
                    break;
                }
            }
            else if (_Signaling->Joined(_Code))
            {
                _Transport->Reach(_Code);
                _Stage = Stage::Reaching;
                _ReachingSince = Clock::now();
                break;
            }

            if (const std::optional<std::string> why = _Signaling->WhyClosed(socket))
            {
                return Fail(MCServiceRefusal::Unreachable, *why);
            }

            if (Clock::now() - _AskedAt > _ServiceWait)
            {
                return Fail(MCServiceRefusal::Unreachable, "no answer in time");
            }

            break;
        }

        case Stage::Reaching:
        {
            switch (_Transport->Reached(_Code))
            {
                case MCIceTransport::Reaching::Open:
                {
                    _Stage = Stage::Ready;
                    SDL_Log("Internet game: the link to game %s is open", _Code.c_str());
                    break;
                }
                case MCIceTransport::Reaching::Failed:
                    return Fail(MCServiceRefusal::Unreachable, "no link to its host");
                case MCIceTransport::Reaching::Opening:
                {
                    if (Clock::now() - _ReachingSince > _ReachWait)
                    {
                        return Fail(MCServiceRefusal::Unreachable, "its link to the host didn't open in time");
                    }

                    break;
                }
            }

            break;
        }

        case Stage::Ready:
        case Stage::Failed:
            break;
    }

    return _Stage;
}

MCInternetGame::Stage MCInternetGame::Fail(MCServiceRefusal refusal, std::string_view why)
{
    if (_Stage != Stage::Failed)
    {
        SDL_Log("Internet game %s: %s, while %s", std::string(LogName()).c_str(), std::string(why).c_str(),
                StageText(_Stage));
    }

    _Stage = Stage::Failed;
    _Refusal = refusal;
    return _Stage;
}

void MCInternetGame::SetRoom(MCServiceRoom room)
{
    if (_Hosting && _Signaling != nullptr)
    {
        room.Public = _Public;
        _Signaling->SetRoom(room);
    }
}

std::optional<MCInternetPlace> MCInternetGame::Place()
{
    if (!_Hosting || _Signaling == nullptr)
    {
        return std::nullopt;
    }

    std::optional<std::string> code = _Signaling->HostedCode();
    std::optional<std::string> token = _Signaling->HostedToken();
    return code && token ? std::optional(MCInternetPlace{.Code = std::move(*code), .Token = std::move(*token)})
                         : std::nullopt;
}

bool MCInternetGame::NameRefused()
{
    return _Signaling != nullptr && _Signaling->NameRefused();
}

bool MCInternetGame::TakeUnlistedNotice()
{
    if (!_Hosting || _UnlistedTold || _Signaling == nullptr || !_Signaling->Unlisted())
    {
        return false;
    }

    _UnlistedTold = true;
    return true;
}

void MCInternetGame::TakeOver(std::chrono::milliseconds wait)
{
    if (_Hosting || _Signaling == nullptr || _Stage != Stage::Ready)
    {
        return;
    }

    // A public game's guest taking it over stays public: its links relayed, its room listed as the old host's was.
    _Public = _Public || _Signaling->GamePublic(_Code);

    if (_Signaling->TakeOver(_Code, wait))
    {
        _Hosting = true;
        SDL_Log("Internet game %s: taking it over as its host", _Code.c_str());
    }
}

bool MCInternetGame::HostReached()
{
    if (_Hosting || _Signaling == nullptr || _Transport == nullptr)
    {
        return false;
    }

    // Not until the service has said the game has a new host: a link from before goes to the host that's gone. Each new
    // host gets a link of its own.
    const int32_t moves = _Signaling->HostMoves(_Code);

    if (moves == 0 || _Signaling->HostAway(_Code))
    {
        return false;
    }

    const MCIceTransport::Reaching reached =
        _ReachedFor == moves ? _Transport->Reached(_Code) : MCIceTransport::Reaching::Failed;

    if (reached == MCIceTransport::Reaching::Failed)
    {
        _ReachedFor = moves;
        _Transport->Reach(_Code);
        return false;
    }

    return reached == MCIceTransport::Reaching::Open;
}

void MCInternetGame::End(std::chrono::milliseconds wait)
{
    if (_Signaling == nullptr)
    {
        return;
    }

    // The links first (they say their closes through the service), then the sockets.
    if (_Transport != nullptr)
    {
        _Net.EndInternet(*_Transport);
        _Transport.reset();
    }

    if (_KeepPlace)
    {
        _Signaling->LeaveAway();
    }
    else
    {
        _Signaling->Leave();
    }

    const auto deadline = Clock::now() + wait;

    while (!_Signaling->AllClosed() && Clock::now() < deadline)
    {
        std::this_thread::sleep_for(CloseLookEvery);
    }

    _Signaling.reset();

    if (_Stage != Stage::Failed)
    {
        _Stage = Stage::Failed;
        _Refusal = MCServiceRefusal::GameOver;
    }

    SDL_Log("Internet game %s: ended", std::string(LogName()).c_str());
}

std::string_view MCInternetGame::LogName() const
{
    return _Code.empty() ? HostingName : std::string_view(_Code);
}
