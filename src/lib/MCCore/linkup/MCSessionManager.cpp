#include "stdafx.h"
#include "linkup/MCSessionManager.h"
#include "linkup/MCFidpGroup.h"
#include "linkup/MCFidpMessage.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCFidpSession.h"
#include "lib/MCFatal.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCSessionScreen.h"
#include "main/MCGameContext.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "platform/MCDirectPlay.h"
#include "platform/MCInput.h"
#include "platform/MCNetSwitch.h"
#include "platform/MCSocket.h"
#include "platform/MCWin32Defs.h"

// The session manager's life, its connections, and the sessions, players and groups DirectPlay lists. The receive
// pass and guaranteed delivery are in MCSessionManagerReceive.cpp, the sends in MCSessionManagerSend.cpp, the players
// coming and going and the message handlers in MCSessionManagerPlayers.cpp.

bool LaunchedFromLobby = false;
MCLobbyLaunchArgs LobbyLaunchArgs;

namespace
{
    using namespace MCDirectPlayGuids;

    /// <summary>How often, and how many times, a lobby's start tries to join its room.</summary>
    constexpr uint32_t LobbyJoinEveryMs = 500;
    constexpr int32_t LobbyJoinTries = 30;

    /// <summary>How often a lobby's internet game is moved along while it starts (the service, the link).</summary>
    constexpr uint32_t LobbyInternetEveryMs = 50;

    /// <summary>Port (Redux HD): the room's chat when the service plays a public room by its code alone.</summary>
    constexpr std::string_view UnlistedFormat =
        "YOUR NETWORK ALREADY HAS {} PUBLIC GAMES, SO THIS ONE ISN'T LISTED. PLAYERS CAN JOIN WITH ITS CODE.";

    /// <summary>The enumeration flags GetSessions and Dial ask DirectPlay for.</summary>
    constexpr uint32_t EnumSessionFlags =
        DPENUMSESSIONS_AVAILABLE | DPENUMSESSIONS_ASYNC | DPENUMSESSIONS_PASSWORDREQUIRED | DPENUMSESSIONS_RETURNSTATUS;

    /// <summary>
    /// Waits up to <paramref name="ms"/> with the window kept responsive: false when the player gave up (Escape, or
    /// closing the window).
    /// </summary>
    bool KeepWaiting(uint32_t ms)
    {
        const bool open = MCInput::WaitMessage(ms);
        return open && (MCInput::GetAsyncKeyState(VK_ESCAPE) & 0x8001) == 0;
    }

    /// <summary>Why a lobby's room can't be joined, for an internet game the service refused or couldn't reach.</summary>
    MCRoomRefusal RoomRefusalOf(std::optional<MCServiceRefusal> refusal)
    {
        switch (refusal.value_or(MCServiceRefusal::Unreachable))
        {
            case MCServiceRefusal::Full:
                return MCRoomRefusal::Full;
            case MCServiceRefusal::InBattle:
                return MCRoomRefusal::Started;
            default:
                return MCRoomRefusal::Gone;
        }
    }
}

uint32_t MCSessionManager::TicksPerMs()
{
    return static_cast<uint32_t>(MCPort::PerformanceFrequency()) / 1000;
}

// ---- lifetime ------------------------------------------------------------------------------------------------------

MCSessionManager::MCSessionManager(const _GUID& appGUID) : _AppGuid(appGUID)
{
    CreateDirectPlayInterface();
    _ServerMessage.Header = LinkupHeader(MCLinkupMessageType::NewServer, FIMSG_GUARANTEED);
    _NextPingTime = static_cast<uint32_t>(MCPort::PerformanceCounter());
}

MCSessionManager::~MCSessionManager()
{
    std::scoped_lock lock(CriticalSection);
    // Port (Redux HD): an internet game ends here, while the network it's on is sure to be there.
    EndInternet();
    DestroyDirectPlayInterface();
}

void MCSessionManager::CreateDirectPlayInterface()
{
    // Port: CoCreateInstance(CLSID_DirectPlay, IID_IDirectPlay3A) becomes the port's stand-in, which can't fail.
    if (DirectPlay == nullptr)
    {
        DirectPlay = std::make_unique<MCDirectPlay>();
    }

    EnumerateConnections();
}

void MCSessionManager::DestroyDirectPlayInterface()
{
    LeaveSession();
    DirectPlay.reset();
    CurrentConnection = MCNetProtocol::None;
}

// ---- message queues ------------------------------------------------------------------------------------------------

bool MCSessionManager::IsMyPlayer(const MCFidpPlayer& player) const
{
    return MyPlayer().get() == &player;
}

void MCSessionManager::AddMessageToEmptyQueue(std::shared_ptr<MCFidpMessage> msg)
{
    std::scoped_lock lock(_EmptyQueueLock);
    msg->Clear();
    _EmptyMessages.push_back(std::move(msg));
}

std::shared_ptr<MCFidpMessage> MCSessionManager::GetMessageFromEmptyQueue()
{
    std::scoped_lock lock(_EmptyQueueLock);

    // Port fix: a new message when the free queue is empty (the original's senders went on with the null they got).
    if (_EmptyMessages.empty())
    {
        return std::make_shared<MCFidpMessage>(MyPlayerID, 0x200);
    }

    std::shared_ptr<MCFidpMessage> msg = std::move(_EmptyMessages.front());
    _EmptyMessages.pop_front();
    return msg;
}

std::shared_ptr<MCFidpMessage> MCSessionManager::CopyToFreeMessage(std::span<const uint8_t> message, uint32_t toID)
{
    std::shared_ptr<MCFidpMessage> msg = GetMessageFromEmptyQueue();
    msg->SetMessageBuffer(message);
    msg->ToID = toID;
    return msg;
}

namespace
{
    /// <summary>The bytes of <paramref name="value"/>.</summary>
    template <class T> std::vector<uint8_t> ValueBytes(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        std::vector<uint8_t> bytes(sizeof(T));
        std::memcpy(bytes.data(), &value, sizeof(T));
        return bytes;
    }

    /// <summary>An address element holding a GUID.</summary>
    DPCOMPOUNDADDRESSELEMENT AddressGuid(const _GUID& type, const _GUID& value)
    {
        return DPCOMPOUNDADDRESSELEMENT{type, ValueBytes(value)};
    }

    /// <summary>An address element holding a string, null-terminated as DirectPlay's were.</summary>
    DPCOMPOUNDADDRESSELEMENT AddressText(const _GUID& type, std::string_view text)
    {
        std::vector<uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        return DPCOMPOUNDADDRESSELEMENT{type, std::move(bytes)};
    }
}

// ---- connections ---------------------------------------------------------------------------------------------------

void MCSessionManager::EnumerateConnections()
{
    Assert(DirectPlay != nullptr, 0);
    Connections.clear();
    MCDirectPlay::EnumConnections(
        [this](const _GUID& serviceProvider, std::span<const uint8_t> connection, const DPNAME& name, uint32_t)
        {
            AddConnection(serviceProvider, connection, name);
            return true;
        },
        0);
}

MCNetProtocol MCSessionManager::ConnectionType(const _GUID& guid)
{
    constexpr std::array<std::pair<_GUID, MCNetProtocol>, 5> known = {{{DPSPGUID_TCPIP, MCNetProtocol::TcpIp},
                                                                       {DPSPGUID_IPX, MCNetProtocol::Ipx},
                                                                       {DPSPGUID_MODEM, MCNetProtocol::Modem},
                                                                       {DPSPGUID_SERIAL, MCNetProtocol::Serial},
                                                                       {DPSPGUID_LOBBY, MCNetProtocol::Lobby}}};

    for (const auto& [provider, protocol] : known)
    {
        if (MCSameGuid(guid, provider))
        {
            AvailableProtocols |= ProtocolBit(protocol);
            return protocol;
        }
    }

    return MCNetProtocol::None;
}

void MCSessionManager::AddConnection(const _GUID& serviceProvider, std::span<const uint8_t> connection,
                                     const DPNAME& name)
{
    auto protocol = std::make_unique<MCFidpNetworkProtocol>(name, connection);
    protocol->ProtocolType = ConnectionType(serviceProvider);
    Connections.push_back(std::move(protocol));
}

int32_t MCSessionManager::SetCurrentConnection(MCNetProtocol type)
{
    const auto found =
        std::ranges::find_if(Connections, [&](const auto& protocol) { return protocol->ProtocolType == type; });
    Assert(found != Connections.end(), 0);

    if (found == Connections.end())
    {
        return -1;
    }

    // Port fix: recreating the DirectPlay object below re-enumerates the connections, which frees the data just found
    // (the original went on using it); a copy is kept.
    std::vector<uint8_t> copy = (*found)->ConnectionBuffer;
    copy.resize(std::max<size_t>(copy.size(), MCDirectPlay::ConnectionDataSize));

    if (CurrentConnection != MCNetProtocol::None)
    {
        DestroyDirectPlayInterface();
        CreateDirectPlayInterface();
    }

    const int32_t result = static_cast<int32_t>(DirectPlay->InitializeConnection(copy, 0));

    if (type != MCNetProtocol::Modem && result == 0)
    {
        GetSessions();
    }

    CurrentConnection = type;
    return result;
}

void MCSessionManager::ConnectIpx()
{
    SetCurrentConnection(MCNetProtocol::Ipx);
}

void MCSessionManager::ConnectTcp(std::string_view ipAddress)
{
    if (CurrentConnection == MCNetProtocol::TcpIp)
    {
        DestroyDirectPlayInterface();
        CreateDirectPlayInterface();
    }

    const std::array<DPCOMPOUNDADDRESSELEMENT, 2> elements = {AddressGuid(DPAID_ServiceProvider, DPSPGUID_TCPIP),
                                                              AddressText(DPAID_INet, ipAddress)};

    if (InitializeConnection(elements) == 0)
    {
        CurrentConnection = MCNetProtocol::TcpIp;
        _TcpAddress = std::string(ipAddress);
    }
}

int32_t MCSessionManager::ConnectModem(std::string_view phoneNumber, std::string_view modemName)
{
    std::vector<DPCOMPOUNDADDRESSELEMENT> elements{AddressGuid(DPAID_ServiceProvider, DPSPGUID_MODEM)};

    if (modemName.size() > 1)
    {
        elements.push_back(AddressText(DPAID_Modem, modemName));
    }

    elements.push_back(AddressText(DPAID_Phone, phoneNumber));
    const int32_t result = InitializeConnection(elements);

    if (result == 0)
    {
        CurrentConnection = MCNetProtocol::Modem;
    }

    return result;
}

int32_t MCSessionManager::ConnectComPort(uint32_t port, uint32_t baudRate, uint32_t stopBits, uint32_t parity,
                                         uint32_t flowControl)
{
    const std::array<uint32_t, 5> settings = {port, baudRate, stopBits, parity, flowControl};
    const std::array<DPCOMPOUNDADDRESSELEMENT, 2> elements = {
        AddressGuid(DPAID_ServiceProvider, DPSPGUID_SERIAL),
        DPCOMPOUNDADDRESSELEMENT{DPAID_ComPort, ValueBytes(settings)}};
    const int32_t result = InitializeConnection(elements);

    if (result == 0)
    {
        CurrentConnection = MCNetProtocol::Serial;
        GetSessions();
    }

    return result;
}

int32_t MCSessionManager::InitializeConnection(std::span<const DPCOMPOUNDADDRESSELEMENT> elements)
{
    if (CurrentConnection != MCNetProtocol::None)
    {
        DestroyDirectPlayInterface();
        CreateDirectPlayInterface();
    }

    // Port: the original made a DirectPlay lobby object only to build the address, which the stand-in does itself.
    const std::vector<uint8_t> address = MCDirectPlay::CreateCompoundAddress(elements);
    return static_cast<int32_t>(DirectPlay->InitializeConnection(address, 0));
}

bool MCSessionManager::FindModems()
{
    if (CurrentSession() != nullptr || ConnectModem("", "") != 0)
    {
        return false;
    }

    // Port: not reached. MCDirectPlay has no modem service provider, so ConnectModem always fails above. The original
    // went on to read the modem connection's address and walk it with the lobby's EnumAddress, collecting the names.
    _ModemNames.clear();
    return false;
}

std::optional<std::string> MCSessionManager::GetModemName(int32_t index) const
{
    if (index < 0 || static_cast<size_t>(index) >= _ModemNames.size())
    {
        return std::nullopt;
    }

    return _ModemNames[static_cast<size_t>(index)];
}

bool MCSessionManager::WasLaunchedFromLobby()
{
    return LobbyLaunchArgs.Active;
}

bool MCSessionManager::JoinsThroughThisComputer(std::string_view address)
{
    constexpr std::string_view launcherDefault = "127.0.0.1";
    const std::string_view host = address.substr(0, address.find(':'));
    const std::optional<uint32_t> ip = MCSocket::Resolve(host.empty() ? launcherDefault : host);
    return ip.has_value() && MCSocket::IsLoopback(*ip);
}

uint32_t MCSessionManager::SetupLobbyConnection(const std::function<void()>& showStatus,
                                                const std::function<void()>& hideStatus)
{
    if (!LobbyLaunchArgs.Active)
    {
        if (DirectPlay != nullptr)
        {
            DirectPlay->Close();
        }

        DirectPlay = std::make_unique<MCDirectPlay>();
        return DPERR_NOTLOBBIED;
    }

    if (DirectPlay != nullptr)
    {
        DirectPlay->Close();
    }

    DirectPlay = std::make_unique<MCDirectPlay>();

    std::string playerName =
        !LobbyLaunchArgs.PlayerName.empty() ? LobbyLaunchArgs.PlayerName : MyGetUserName().value_or("Player");
    SaveUserName(playerName);

    if (LobbyLaunchArgs.IsHost)
    {
        std::string sessionName =
            !LobbyLaunchArgs.SessionName.empty() ? LobbyLaunchArgs.SessionName : (playerName + "'s game");

        // Port (Redux HD): an internet game's made on the service once the connection is chosen (choosing it again
        // would leave a session, and the game with it).
        ConnectTcp(LobbyLaunchArgs.Address);

        if (LobbyLaunchArgs.Internet)
        {
            if (const uint32_t started = StartLobbyInternetGame(); started != DP_OK)
            {
                return started;
            }
        }

        MCFidpSession session(_AppGuid);
        session.SetName(sessionName);
        session.SessionDesc.dwMaxPlayers = static_cast<uint32_t>(std::clamp(LobbyLaunchArgs.MaxPlayers, 2, 6));

        const int32_t result = HostSession(session, playerName);

        if (result != 0)
        {
            SDL_Log("SetupLobbyConnection: failed to host session (result %d)", result);
            EndInternet();
            return DPERR_CANTCREATESESSION;
        }

        ServerID = MyPlayerID;
        IsHost = true;
        SDL_Log("SetupLobbyConnection: hosting \"%s\" as \"%s\"", sessionName.c_str(), playerName.c_str());
        return DP_OK;
    }
    else
    {
        // Port (Redux HD): an internet game's code is its address, as the service and its links name it.
        std::string targetAddr = !LobbyLaunchArgs.Address.empty() ? LobbyLaunchArgs.Address : "127.0.0.1";

        if (LobbyLaunchArgs.Internet)
        {
            targetAddr = MCInternetGame::CodeOf(targetAddr);
        }

        ConnectTcp(targetAddr);

        // Port-only (Redux HD): a launcher's game joined at this computer's own address reaches its host through the
        // launcher's relay here (MechCommander Online's), which the session's other machines can't reach it through: it
        // never takes the session over, so its host's leaving ends the session at once, as before the rollover.
        if (!LobbyLaunchArgs.Internet && DirectPlay != nullptr && JoinsThroughThisComputer(targetAddr))
        {
            SDL_Log("SetupLobbyConnection: joining through this computer (%s): this game never takes the session over",
                    targetAddr.c_str());
            DirectPlay->NeverTakeOver();
        }

        if (showStatus)
        {
            showStatus();
        }

        // A press from before the wait doesn't cancel it.
        MCInput::GetAsyncKeyState(VK_ESCAPE);

        // Port (Redux HD): an internet game's link to its host opens before the search.
        if (LobbyLaunchArgs.Internet)
        {
            if (const uint32_t started = StartLobbyInternetGame(); started != DP_OK)
            {
                if (hideStatus)
                {
                    hideStatus();
                }

                return started;
            }
        }

        SDL_Log("SetupLobbyConnection: connecting to %s as \"%s\"...", targetAddr.c_str(), playerName.c_str());

        for (int32_t attempt = 0; attempt < LobbyJoinTries; ++attempt)
        {
            MCLobbyJoin tried = TryLobbyJoin(playerName);

            // Port-only (the personal build): a rejoin finding its room there but full or started (the list of rooms
            // to join leaves those out) stops at once, saying so.
            if (tried == MCLobbyJoin::NoRoom && LobbyLaunchArgs.Rejoin)
            {
                if (const MCRoomRefusal refusal = LobbyRoomRefusal(); refusal != MCRoomRefusal::Gone)
                {
                    SDL_Log("SetupLobbyConnection: the room at %s takes no one now", targetAddr.c_str());
                    LastRoomRefusal = refusal;
                    tried = MCLobbyJoin::Refused;
                }
            }

            if (tried != MCLobbyJoin::NoRoom)
            {
                if (hideStatus)
                {
                    hideStatus();
                }

                // A session that refuses the join (full, or gone) is not tried again.
                if (tried != MCLobbyJoin::Joined)
                {
                    EndInternet();
                    return DPERR_NOSESSIONS;
                }

                return DP_OK;
            }

            // The window stays responsive while it waits, and Escape (or closing it) gives up.
            if (!KeepWaiting(LobbyJoinEveryMs))
            {
                if (hideStatus)
                {
                    hideStatus();
                }

                EndInternet();
                return DPERR_USERCANCEL;
            }
        }

        if (hideStatus)
        {
            hideStatus();
        }

        SDL_Log("SetupLobbyConnection: timed out connecting to %s", targetAddr.c_str());
        LastRoomRefusal = LobbyRoomRefusal();
        EndInternet();
        return DPERR_NOSESSIONS;
    }
}

bool MCSessionManager::BeginInternet(bool host, std::string_view code, bool isPublic,
                                     std::optional<MCInternetPlace> place)
{
    const MCOptionalRef<MCNetSwitch> net = MCNetSwitch::Of(MCGameContext::Current().Net());

    if (!net || !MCInternetGame::Available())
    {
        SDL_Log("MCSessionManager: this game has no internet games");
        return false;
    }

    EndInternet();
    Internet = !host   ? MCInternetGame::Join(net->get(), code, isPublic)
               : place ? MCInternetGame::HostAgain(net->get(), isPublic, *place)
                       : MCInternetGame::Host(net->get(), isPublic);

    // Port-only (Redux HD): a session rolling over reaches its new host once the service says the game is its.
    if (Internet != nullptr && DirectPlay != nullptr)
    {
        DirectPlay->SetRolloverReach([this] { return Internet != nullptr && Internet->HostReached(); },
                                     InternetRolloverWait);
    }

    return Internet != nullptr;
}

uint32_t MCSessionManager::StartLobbyInternetGame()
{
    // The code alone: a port after it is DirectPlay's (the game's port on its host), not the service's.
    const std::string_view address = LobbyLaunchArgs.Address;

    if (!BeginInternet(LobbyLaunchArgs.IsHost, address.substr(0, address.find(':')), LobbyLaunchArgs.Public,
                       LobbyLaunchArgs.Place))
    {
        LastRoomRefusal = MCRoomRefusal::Gone;
        return LobbyLaunchArgs.IsHost ? DPERR_CANTCREATESESSION : DPERR_NOSESSIONS;
    }

    for (;;)
    {
        const MCInternetGame::Stage stage = Internet->Update();

        if (stage == MCInternetGame::Stage::Ready)
        {
            return DP_OK;
        }

        if (stage == MCInternetGame::Stage::Failed)
        {
            LastRoomRefusal = RoomRefusalOf(Internet->Refusal());
            EndInternet();
            return LobbyLaunchArgs.IsHost ? DPERR_CANTCREATESESSION : DPERR_NOSESSIONS;
        }

        if (!KeepWaiting(LobbyInternetEveryMs))
        {
            EndInternet();
            return DPERR_USERCANCEL;
        }
    }
}

void MCSessionManager::EndInternet(std::chrono::milliseconds wait)
{
    if (Internet == nullptr)
    {
        return;
    }

    // DirectPlay lets go of its sockets first: they're on the game's links. (Port-only, Redux HD: a host's session rolls
    // over to another machine, unless its game restarts into an update, its place kept: its guests wait for it.)
    if (DirectPlay != nullptr)
    {
        if (Internet->KeepsPlace() || !HandsOn)
        {
            DirectPlay->Close();
        }
        else
        {
            DirectPlay->Leave();
        }
    }

    Internet->End(wait);
    Internet.reset();
}

std::optional<std::string> MCSessionManager::HostedInternetCode() const
{
    return Internet != nullptr && Internet->Hosting() && !Internet->Code().empty() ? std::optional(Internet->Code())
                                                                                   : std::nullopt;
}

void MCSessionManager::TendInternet()
{
    if (Internet == nullptr)
    {
        return;
    }

    // A game whose service socket ended (the service refused it, or it can't come back) plays on over its links: only
    // its place on the service is gone (Update says why in the log).
    Internet->Update();
    const std::shared_ptr<MCFidpSession> session = CurrentSession();

    if (!IsHost || session == nullptr)
    {
        return;
    }

    if (Internet->TakeUnlistedNotice() && GlobalLog() != nullptr && GlobalLog()->Chat != nullptr)
    {
        GlobalLog()->Chat->AddLine(std::format(UnlistedFormat, MCServiceListedPerNetwork));
    }

    // The room as the service's list shows it, with the mission the host picked on its room's screen.
    MCServiceRoom room{.Name = session->Name,
                       .Max = static_cast<int32_t>(session->SessionDesc.dwMaxPlayers),
                       .Battle = GameStarted || _SessionLocked};

    if (GlobalLog() != nullptr && GlobalLog()->SessionScreen != nullptr)
    {
        room.Mission = GlobalLog()->SessionScreen->MissionName;
    }

    for (const std::shared_ptr<MCFidpPlayer>& player : Players)
    {
        room.Players.push_back(player->Name);
    }

    Internet->SetRoom(std::move(room));
}

MCLobbyJoin MCSessionManager::TryLobbyJoin(std::string_view playerName)
{
    const auto sessions = GetSessions();

    if (!sessions.has_value() || sessions->empty())
    {
        return MCLobbyJoin::NoRoom;
    }

    std::shared_ptr<MCFidpSession> targetSession;

    if (!LobbyLaunchArgs.SessionName.empty())
    {
        const auto named = std::ranges::find_if(*sessions, [](const std::shared_ptr<MCFidpSession>& session)
                                                { return session->Name == LobbyLaunchArgs.SessionName; });
        targetSession = named != sessions->end() ? *named : nullptr;
    }
    else
    {
        targetSession = sessions->front();
    }

    if (targetSession == nullptr)
    {
        return MCLobbyJoin::NoRoom;
    }

    const int32_t joinResult = JoinSession(targetSession->SessionDesc.guidInstance, playerName);

    if (joinResult != 0)
    {
        SDL_Log("SetupLobbyConnection: could not join \"%s\" (result %d)", targetSession->Name.c_str(), joinResult);
        LastRoomRefusal = !RefusalReason().empty() ? MCRoomRefusal::Lacks : LobbyRoomRefusal();
        return MCLobbyJoin::Refused;
    }

    IsHost = false;
    SDL_Log("SetupLobbyConnection: joined session \"%s\"", targetSession->Name.c_str());
    return MCLobbyJoin::Joined;
}

std::string MCSessionManager::RefusalReason() const
{
    return _Refusal;
}

MCRoomRefusal MCSessionManager::LobbyRoomRefusal()
{
    if (DirectPlay == nullptr)
    {
        return MCRoomRefusal::Gone;
    }

    // Every session there, whatever its state: the available ones alone are what GetSessions lists.
    std::optional<DPSESSIONDESC2> room;
    DPSESSIONDESC2 desc{};
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.guidApplication = _AppGuid;
    DirectPlay->EnumSessions(
        desc, 0,
        [&](const DPSESSIONDESC2& session, uint32_t flags)
        {
            if ((flags & DPESC_TIMEDOUT) != 0)
            {
                return false;
            }

            if (LobbyLaunchArgs.SessionName.empty() || session.SessionName == LobbyLaunchArgs.SessionName)
            {
                room = session;
                return false;
            }

            return true;
        },
        EnumSessionFlags & ~DPENUMSESSIONS_AVAILABLE);

    if (!room.has_value())
    {
        return MCRoomRefusal::Gone;
    }

    if ((room->dwFlags & (DPSESSION_NEWPLAYERSDISABLED | DPSESSION_JOINDISABLED)) != 0)
    {
        return MCRoomRefusal::Started;
    }

    return room->dwCurrentPlayers >= room->dwMaxPlayers ? MCRoomRefusal::Full : MCRoomRefusal::Gone;
}

void MCSessionManager::AdmitNewPlayers(bool admit)
{
    const std::shared_ptr<MCFidpSession> session = CurrentSession();

    if (session == nullptr || !IsHost || DirectPlay == nullptr)
    {
        return;
    }

    // The game's own lock (_SessionLocked: how its messages go out once the room is closed) stays as it is.
    if (admit)
    {
        session->SessionDesc.dwFlags &= ~DPSESSION_NEWPLAYERSDISABLED;
    }
    else
    {
        session->SessionDesc.dwFlags |= DPSESSION_NEWPLAYERSDISABLED;
    }

    DirectPlay->SetSessionDesc(session->SessionDesc, 0);
}

int32_t MCSessionManager::Dial()
{
    if (CurrentConnection != MCNetProtocol::Modem)
    {
        return 1;
    }

    ClearSessionList();
    DPSESSIONDESC2 desc{};
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.guidApplication = _AppGuid;
    return static_cast<int32_t>(DirectPlay->EnumSessions(desc, 0, SessionAdder(), EnumSessionFlags));
}

void MCSessionManager::CancelDialing()
{
    DestroyDirectPlayInterface();
    CreateDirectPlayInterface();
}

bool MCSessionManager::IsTcpAvailable()
{
    // Port: the original looked for the TCP/IP stack in the registry. The port's transport is TCP/IP: available when
    // the sockets library starts.
    if (!_TcpChecked)
    {
        _TcpAvailable = MCSocket::Startup();
        _TcpChecked = true;
    }

    return _TcpAvailable;
}

bool MCSessionManager::IsIpxAvailable()
{
    // Port: the original looked for the IPX stack in the registry. The port's "IPX" connection is its LAN search over
    // TCP/IP (see MCDirectPlay), available with TCP/IP.
    if (!_IpxChecked)
    {
        _IpxAvailable = MCSocket::Startup();
        _IpxChecked = true;
    }

    return _IpxAvailable;
}

bool MCSessionManager::IsModemAvailable()
{
    // Original behaviour: the "checked" flag is never set, so every call looks for modems again.
    return FindModems() && GetModemName(0).has_value();
}

// ---- sessions ------------------------------------------------------------------------------------------------------

void MCSessionManager::AddSession(const DPSESSIONDESC2& desc)
{
    if (const std::shared_ptr<MCFidpSession> current = CurrentSession();
        current != nullptr && MCSameGuid(current->SessionDesc.guidInstance, desc.guidInstance))
    {
        return;
    }

    Sessions.push_back(std::make_shared<MCFidpSession>(desc));
}

MCEnumSessionsCallback MCSessionManager::SessionAdder()
{
    return [this](const DPSESSIONDESC2& session, uint32_t flags)
    {
        if ((flags & DPESC_TIMEDOUT) != 0)
        {
            return false;
        }

        AddSession(session);
        return true;
    };
}

void MCSessionManager::ClearSessionList()
{
    const std::shared_ptr<MCFidpSession> current = CurrentSession();
    std::erase_if(Sessions, [&](const auto& session) { return session != current; });
}

std::optional<std::vector<std::shared_ptr<MCFidpSession>>> MCSessionManager::GetSessions()
{
    if (DirectPlay == nullptr)
    {
        return std::nullopt;
    }

    ClearSessionList();
    DPSESSIONDESC2 desc{};
    desc.dwSize = sizeof(DPSESSIONDESC2);
    desc.guidApplication = _AppGuid;
    DirectPlay->EnumSessions(desc, 0, SessionAdder(), EnumSessionFlags);
    return Sessions;
}

std::shared_ptr<MCFidpSession> MCSessionManager::FindMatchingSession(const _GUID& sessionGUID)
{
    const auto found = std::ranges::find_if(Sessions, [&](const auto& session)
                                            { return MCSameGuid(session->SessionDesc.guidInstance, sessionGUID); });
    return found != Sessions.end() ? *found : nullptr;
}

int32_t MCSessionManager::HostSession(const MCFidpSession& session, std::string_view playerName)
{
    Assert(DirectPlay != nullptr, 0, "Can't host session.  No DirectPlayObject");
    IsHost = true;

    if (DirectPlay->Open(session.SessionDesc, DPOPEN_CREATE | DPOPEN_RETURNSTATUS) != DP_OK)
    {
        return -1;
    }

    // Port: the game's say on each player who joins, at the door.
    DirectPlay->SetAdmission(Admission);

    _CurrentSession = Sessions.emplace_back(std::make_shared<MCFidpSession>(session));

    if (CreatePlayer(playerName) != 0)
    {
        return -2;
    }

    SendSystemInformation();
    return 0;
}

int32_t MCSessionManager::JoinSession(const _GUID& sessionGUID, std::string_view playerName)
{
    Assert(DirectPlay != nullptr, 0);
    // An earlier join's refusal says nothing of this one.
    _Refusal.clear();
    const std::shared_ptr<MCFidpSession> session = FindMatchingSession(sessionGUID);

    if (session == nullptr)
    {
        return -1;
    }

    _CurrentSession = session;
    // Port-only (Redux HD): the game's say at the door too, should the session roll over to this machine.
    DirectPlay->SetAdmission(Admission);
    int32_t result = static_cast<int32_t>(DirectPlay->Open(session->SessionDesc, DPOPEN_JOIN | DPOPEN_RETURNSTATUS));

    if (result == 0)
    {
        result = CreatePlayer(playerName);

        if (result == 0)
        {
            SendSystemInformation();
        }
        else
        {
            // Port: refused at the door (or the room filled meanwhile): out of the session, free to join another, the
            // host's reason kept.
            _Refusal = DirectPlay->LastRefusal();
            SDL_Log("MCSessionManager: the room refused this player (%s)", _Refusal.c_str());
            DirectPlay->Close();
            _CurrentSession.reset();
        }
    }

    return result;
}

bool MCSessionManager::LockSession()
{
    const std::shared_ptr<MCFidpSession> session = CurrentSession();

    if (session == nullptr || !IsHost)
    {
        return false;
    }

    session->SessionDesc.dwFlags |= DPSESSION_NEWPLAYERSDISABLED;
    DirectPlay->SetSessionDesc(session->SessionDesc, 0);
    _SessionLocked = true;
    GameStarted = true;
    return true;
}

bool MCSessionManager::LeaveHandsOn() const
{
    return HandsOn && DirectPlay != nullptr && DirectPlay->HasSuccessor();
}

bool MCSessionManager::LeaveSession()
{
    const bool handOn = std::exchange(HandsOn, true);

    if (CurrentSession() == nullptr || MyPlayer() == nullptr)
    {
        return false;
    }

    MyPlayerID = 0;
    IsHost = false;
    ServerID = 0;
    HasPlayerNumber = false;
    _CurrentSession.reset();
    _MyPlayer.reset();

    // Port-only (Redux HD): a host leaving hands its session on to another machine, as DirectPlay migrated a host
    // (MCDirectPlay::Leave), unless its battle is over (HandsOn).
    if (handOn)
    {
        DirectPlay->Leave();
    }
    else
    {
        DirectPlay->Close();
    }

    DestroyDirectPlayInterface();
    // Port (Redux HD): an internet game ends with its session (its links, its place on the service).
    EndInternet();
    CreateDirectPlayInterface();
    PlayersByLatency = {0, 1, 2, 3, 4, 5};
    return true;
}

// ---- players and groups --------------------------------------------------------------------------------------------

int32_t MCSessionManager::CreatePlayer(std::string_view playerName)
{
    _SessionLocked = false;
    _NewPlayerNumbers.fill(0);
    const DPNAME dpName{std::string(playerName), std::nullopt};
    // Port: the original made two Win32 events here (DirectPlay signalled one on every arrival; nothing waited on
    // either).
    const uint32_t result = DirectPlay->CreatePlayer(MyPlayerID, dpName, PlayerData, 0);

    if (result != DP_OK)
    {
        MyPlayerID = 0;
        return static_cast<int32_t>(result);
    }

    {
        std::scoped_lock lock(_EmptyQueueLock);

        for (int i = 0; i < 900; i++)
        {
            _EmptyMessages.push_back(std::make_shared<MCFidpMessage>(MyPlayerID, 0x200));
        }
    }

    if (IsHost)
    {
        ServerID = MyPlayerID;
        _ServerMessage.Value = MyPlayerID;
        HasPlayerNumber = true;
    }

    EnumeratePlayers(nullptr);
    GetGroups(nullptr);
    return 0;
}

void MCSessionManager::NewPlayerEnumeration(uint32_t playerID, const DPNAME& name, uint32_t flags)
{
    std::shared_ptr<MCFidpPlayer> player;

    {
        std::scoped_lock lock(CriticalSection);

        if (GetPlayer(playerID) != nullptr)
        {
            return;
        }

        player = Players.emplace_back(std::make_shared<MCFidpPlayer>(playerID, name, flags));
    }

    if (player->Id == MyPlayerID)
    {
        _MyPlayer = player;

        if (IsHost)
        {
            player->PlayerNumber = 0;
        }
    }
    else if (!LaunchedFromLobby)
    {
        if (!IsHost && HasPlayerNumber)
        {
            GivePlayerAnID(*player);
        }
    }
    else
    {
        for (int32_t i = 0; i < MaxLinkupPlayers; i++)
        {
            if (_NewPlayerNumbers[i] == playerID)
            {
                player->PlayerNumber = i;
                player->HasPlayerNumber = true;
            }
        }
    }
}

void MCSessionManager::NewGroupEnumeration(uint32_t groupID, const DPNAME& name, uint32_t flags)
{
    Groups.push_back(std::make_shared<MCFidpGroup>(groupID, name, flags));
}

std::shared_ptr<MCFidpGroup> MCSessionManager::GetGroup(uint32_t groupID)
{
    const auto found = std::ranges::find_if(Groups, [&](const auto& group) { return group->Id == groupID; });
    return found != Groups.end() ? *found : nullptr;
}

std::shared_ptr<MCFidpPlayer> MCSessionManager::GetPlayer(uint32_t playerID)
{
    const auto found = std::ranges::find_if(Players, [&](const auto& player) { return player->Id == playerID; });
    return found != Players.end() ? *found : nullptr;
}

std::shared_ptr<MCFidpPlayer> MCSessionManager::GetPlayerNumber(int32_t playerNumber)
{
    const auto found =
        std::ranges::find_if(Players, [&](const auto& player) { return player->PlayerNumber == playerNumber; });
    return found != Players.end() ? *found : nullptr;
}

const std::vector<std::shared_ptr<MCFidpPlayer>>& MCSessionManager::GetPlayers(std::shared_ptr<MCFidpSession> session)
{
    if (MyPlayerID == 0)
    {
        EnumeratePlayers(std::move(session));
    }

    return Players;
}

void MCSessionManager::EnumeratePlayers(std::shared_ptr<MCFidpSession> session)
{
    // A list made again drops this machine's player too: the enumeration finds it again.
    _MyPlayer.reset();
    Players.clear();
    Assert(DirectPlay != nullptr, 0);
    Assert(CurrentConnection != MCNetProtocol::None, 0);
    const MCEnumPlayersCallback callback = [this](uint32_t id, uint32_t, const DPNAME& name, uint32_t flags)
    {
        NewPlayerEnumeration(id, name, flags);
        return true;
    };

    if (session == nullptr)
    {
        DirectPlay->EnumPlayers(callback, 0);
    }
    else
    {
        DirectPlay->EnumPlayers(session->SessionDesc.guidInstance, callback, DPENUMPLAYERS_SESSION);
    }

    if (const std::shared_ptr<MCFidpSession> current = CurrentSession(); current != nullptr)
    {
        current->SessionDesc.dwCurrentPlayers = static_cast<uint32_t>(Players.size());
    }
}

const std::vector<std::shared_ptr<MCFidpGroup>>& MCSessionManager::GetGroups(std::shared_ptr<MCFidpSession> session)
{
    Groups.clear();
    Assert(DirectPlay != nullptr, 0);
    Assert(CurrentConnection != MCNetProtocol::None, 0);
    const MCEnumPlayersCallback callback = [this](uint32_t id, uint32_t, const DPNAME& name, uint32_t flags)
    {
        NewGroupEnumeration(id, name, flags);
        return true;
    };

    if (session == nullptr)
    {
        DirectPlay->EnumGroups(callback, 0);
    }
    else
    {
        // Original behaviour: a listed session's groups are asked for with EnumPlayers (its players come back).
        DirectPlay->EnumPlayers(session->SessionDesc.guidInstance, callback, DPENUMPLAYERS_SESSION);
    }

    return Groups;
}

void MCSessionManager::CreateGroup(uint32_t& groupID, std::string_view groupName, std::span<const uint8_t> data,
                                   uint32_t flags)
{
    const DPNAME dpName{std::string(groupName), std::nullopt};
    DirectPlay->CreateGroup(groupID, dpName, {}, flags);
    auto group = std::make_shared<MCFidpGroup>(groupID, dpName, flags);

    if (!data.empty())
    {
        group->SetGroupData(data);
    }

    Groups.push_back(std::move(group));
    SetGroupData(groupID, data, 0);
}

bool MCSessionManager::AddPlayerToGroup(uint32_t groupID, uint32_t playerID)
{
    if (playerID == 0)
    {
        playerID = MyPlayerID;
    }

    const std::shared_ptr<MCFidpGroup> group = GetGroup(groupID);

    if (group == nullptr)
    {
        return false;
    }

    if (group->AddPlayer(playerID))
    {
        const std::shared_ptr<MCFidpPlayer> player = GetPlayer(playerID);

        if (player == nullptr)
        {
            return false;
        }

        player->JoinGroup(groupID);
    }

    DirectPlay->AddPlayerToGroup(groupID, playerID);
    return true;
}

bool MCSessionManager::RemovePlayerFromGroup(uint32_t groupID, uint32_t playerID)
{
    if (playerID == 0)
    {
        playerID = MyPlayerID;
    }

    const std::shared_ptr<MCFidpGroup> group = GetGroup(groupID);

    if (group == nullptr || !group->RemovePlayer(playerID))
    {
        return false;
    }

    const std::shared_ptr<MCFidpPlayer> player = GetPlayer(playerID);

    if (player == nullptr)
    {
        return false;
    }

    player->LeaveGroup(groupID);
    DirectPlay->DeletePlayerFromGroup(groupID, playerID);
    return true;
}

void MCSessionManager::SetGroupData(uint32_t groupID, std::span<const uint8_t> data, uint32_t flags) const
{
    if (!data.empty())
    {
        DirectPlay->SetGroupData(groupID, data, flags);
    }
}
