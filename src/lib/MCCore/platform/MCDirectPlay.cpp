#include "stdafx.h"
#include "platform/MCDirectPlay.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCSha256.h"
#include "platform/MCSocket.h"

#include <random>

namespace
{
    using namespace MCDirectPlayGuids;

    /// <summary>DirectPlay's "object not initialized" (no connection selected yet).</summary>
    constexpr uint32_t DPERR_UNINITIALIZED = 0x88770140;
    /// <summary>DirectPlay's "access denied" (a joiner changing the host's session description).</summary>
    constexpr uint32_t DPERR_ACCESSDENIED = 0x8877000a;

    /// <summary>The first word of every UDP datagram and of the join request: "MCG1".</summary>
    constexpr uint32_t ProtocolMagic = 0x3147434d;
    /// <summary>A UDP session query ("are you hosting this game?").</summary>
    constexpr uint8_t DatagramQuery = 1;
    /// <summary>A UDP answer to a session query: the session, its TCP port and its players.</summary>
    constexpr uint8_t DatagramReply = 2;

    /// <summary>A frame on a TCP connection: a 32-bit length (of the type and payload), the type, the payload.</summary>
    enum class FrameType : uint8_t
    {
        /// <summary>Joiner to host: magic, application GUID, session GUID, password.</summary>
        JoinRequest = 1,
        /// <summary>Host to joiner: result; when DP_OK the session and every player and group.</summary>
        JoinReply,
        /// <summary>Joiner to host: token, the new player (names, flags, data).</summary>
        CreatePlayer,
        /// <summary>Joiner to host: token, the new group.</summary>
        CreateGroup,
        /// <summary>Host to joiner: token, result, the new id.</summary>
        CreateReply,
        /// <summary>Either way: group, player.</summary>
        AddToGroup,
        /// <summary>Either way: group, player.</summary>
        DeleteFromGroup,
        /// <summary>Either way: group, data.</summary>
        SetGroupData,
        /// <summary>Host to joiner: the session.</summary>
        SessionDesc,
        /// <summary>Either way: sender, receiver (player, group or everyone), bytes.</summary>
        Data,
        /// <summary>Host to joiner: a player or group was created (the entity).</summary>
        Created,
        /// <summary>Host to joiner: a player or group was destroyed (its id).</summary>
        Destroyed,
        /// <summary>Port-only (Redux HD): host to joiner: the hub's machine and the session's machines (the rollover).</summary>
        Machines,
        /// <summary>Port-only (Redux HD): host to joiner: the session ends with the host (Close): it doesn't roll over.</summary>
        Ending,
        /// <summary>
        /// Port-only (Redux HD): a joiner to the machine taking over from its hub: magic, application GUID, session
        /// GUID, its machine, its key and the hash of its next one, its rollover version and listening port.
        /// </summary>
        Resume,
        /// <summary>Port-only (Redux HD): the new hub to a joiner resuming: as JoinReply.</summary>
        ResumeReply
    };

    /// <summary>Largest frame a peer may announce (anything bigger means the stream is garbage).</summary>
    constexpr uint32_t MaxFrameSize = 1u << 20;
    /// <summary>The groups one joiner may create (the game's three are the host's).</summary>
    constexpr std::ptrdiff_t MaxJoinerGroups = 8;
    /// <summary>Host: connections accepted but not yet joined at once (more are closed at once).</summary>
    constexpr std::ptrdiff_t MaxPendingJoiners = 8;
    /// <summary>Host: how long an accepted connection has to ask to join.</summary>
    constexpr uint64_t JoinTimeoutMs = 10000;
    /// <summary>Unsent bytes a connection may pile up before it is dropped (its peer stopped reading).</summary>
    constexpr size_t MaxUnsentBytes = 8u << 20;
    /// <summary>Host: session queries answered per second at most (the answers are bigger than the questions).</summary>
    constexpr uint32_t MaxAnswersPerSecond = 20;
    /// <summary>How long a join or a create waits for the host's answer.</summary>
    constexpr uint64_t ReplyTimeoutMs = 5000;
    /// <summary>How long connecting to a host may take.</summary>
    constexpr int ConnectTimeoutMs = 3000;
    /// <summary>How long EnumSessions listens for answers after asking.</summary>
    constexpr uint64_t EnumListenMs = 60;
    /// <summary>How long an answering session stays listed without answering again.</summary>
    constexpr uint64_t SessionExpiryMs = 4000;
    /// <summary>
    /// Port-only (Redux HD): the rollover this build plays, which its join and its join's answer say (an older build
    /// says none, and never takes a session over).
    /// </summary>
    constexpr uint32_t RolloverVersion = 1;
    /// <summary>
    /// Port-only (Redux HD): how long a rollover waits: a joiner for the machine taking over to answer it, the new hub
    /// for the session's other machines to come back, and a joiner keeps a connection to it before it takes over.
    /// </summary>
    constexpr uint64_t RolloverWaitMs = 10000;
    /// <summary>Port-only (Redux HD): how long each try at connecting to the machine taking over may take.</summary>
    constexpr int RolloverConnectMs = 1000;
    /// <summary>Port-only (Redux HD): the bytes of the key a machine proves itself with as it resumes.</summary>
    constexpr size_t MachineKeyBytes = 32;
    /// <summary>
    /// Port-only (Redux HD): the addresses of an internet game's links (0.0.0.0/8, platform/MCIceTransport): a machine
    /// seen at one is reached by the game's own name, where its hub was.
    /// </summary>
    constexpr uint32_t LinkAddressMask = 0xff000000u;

    using MachineKey = std::array<uint8_t, MachineKeyBytes>;

    /// <summary>The connection data EnumConnections hands out and InitializeConnection takes (the port's own layout).</summary>
    struct ConnectionData
    {
        _GUID serviceProvider;
        /// <summary>The host to look for ("name" or "name:port"); empty = search the LAN.</summary>
        std::array<char, 256> address; // Fixed layout: the connection data's host, null-terminated
    };

    static_assert(sizeof(ConnectionData) == MCDirectPlay::ConnectionDataSize);

    /// <summary>Appends little-endian values to a byte buffer.</summary>
    class Writer
    {
    public:
        void U8(uint8_t value) { Bytes.push_back(value); }

        void U16(uint16_t value)
        {
            U8(static_cast<uint8_t>(value));
            U8(static_cast<uint8_t>(value >> 8));
        }

        void U32(uint32_t value)
        {
            U16(static_cast<uint16_t>(value));
            U16(static_cast<uint16_t>(value >> 16));
        }

        void Guid(const _GUID& value)
        {
            U32(value.Data1);
            U16(value.Data2);
            U16(value.Data3);

            for (uint8_t b : value.Data4)
            {
                U8(b);
            }
        }

        void Blob(std::span<const uint8_t> data)
        {
            U32(static_cast<uint32_t>(data.size()));
            Bytes.insert(Bytes.end(), data.begin(), data.end());
        }

        void String(std::string_view text)
        {
            Blob(std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
        }

        std::vector<uint8_t> Bytes;
    };

    /// <summary>Reads what <see cref="Writer"/> wrote; any overrun clears <see cref="Ok"/> and yields zeros.</summary>
    class Reader
    {
    public:
        explicit Reader(std::span<const uint8_t> data) : _Data(data) {}

        uint8_t U8()
        {
            if (_Position >= _Data.size())
            {
                Ok = false;
                return 0;
            }

            return _Data[_Position++];
        }

        uint16_t U16()
        {
            const uint16_t low = U8();
            return static_cast<uint16_t>(low | (U8() << 8));
        }

        uint32_t U32()
        {
            const uint32_t low = U16();
            return low | (static_cast<uint32_t>(U16()) << 16);
        }

        _GUID Guid()
        {
            _GUID value{};
            value.Data1 = U32();
            value.Data2 = U16();
            value.Data3 = U16();

            for (uint8_t& b : value.Data4)
            {
                b = U8();
            }

            return value;
        }

        std::vector<uint8_t> Blob()
        {
            const uint32_t size = U32();

            if (!Ok || size > _Data.size() - _Position)
            {
                Ok = false;
                return {};
            }

            std::vector<uint8_t> bytes(_Data.begin() + _Position, _Data.begin() + _Position + size);
            _Position += size;
            return bytes;
        }

        std::string String()
        {
            const std::vector<uint8_t> bytes = Blob();
            return std::string(bytes.begin(), bytes.end());
        }

        bool Ok = true;

    private:
        std::span<const uint8_t> _Data;
        size_t _Position = 0;
    };

    /// <summary>A session's description with its own copies of the strings.</summary>
    struct SessionInfo
    {
        DPSESSIONDESC2 Desc{};
        std::string Name;
        std::string Password;

        void Write(Writer& out, bool withPassword) const
        {
            out.U32(Desc.dwFlags);
            out.Guid(Desc.guidInstance);
            out.Guid(Desc.guidApplication);
            out.U32(Desc.dwMaxPlayers);
            out.U32(Desc.dwCurrentPlayers);
            out.U32(Desc.dwReserved1);
            out.U32(Desc.dwReserved2);
            out.U32(Desc.dwUser1);
            out.U32(Desc.dwUser2);
            out.U32(Desc.dwUser3);
            out.U32(Desc.dwUser4);
            out.String(Name);
            out.String(withPassword ? Password : std::string());
        }

        void Read(Reader& in)
        {
            Desc = {};
            Desc.dwSize = sizeof(DPSESSIONDESC2);
            Desc.dwFlags = in.U32();
            Desc.guidInstance = in.Guid();
            Desc.guidApplication = in.Guid();
            Desc.dwMaxPlayers = in.U32();
            Desc.dwCurrentPlayers = in.U32();
            Desc.dwReserved1 = in.U32();
            Desc.dwReserved2 = in.U32();
            Desc.dwUser1 = in.U32();
            Desc.dwUser2 = in.U32();
            Desc.dwUser3 = in.U32();
            Desc.dwUser4 = in.U32();
            Name = in.String();
            Password = in.String();
        }

        /// <summary>Copies a caller's description (and its strings).</summary>
        void Set(const DPSESSIONDESC2& desc)
        {
            Desc = desc;
            Desc.dwSize = sizeof(DPSESSIONDESC2);
            Name = desc.SessionName.value_or("");
            Password = desc.Password.value_or("");
        }

        /// <summary>
        /// The description with its name. The password is never handed out (an enumerated session's is none, as
        /// DirectPlay's were null).
        /// </summary>
        DPSESSIONDESC2 View() const
        {
            DPSESSIONDESC2 view = Desc;
            view.SessionName = Name;
            view.Password.reset();
            return view;
        }
    };

    /// <summary>A player or group of the session.</summary>
    struct Entity
    {
        uint32_t Id = 0;
        /// <summary>DPPLAYERTYPE_PLAYER or DPPLAYERTYPE_GROUP.</summary>
        uint32_t Type = DPPLAYERTYPE_PLAYER;
        uint32_t Flags = 0;
        uint32_t Parent = 0;
        std::string ShortName;
        std::string LongName;
        std::vector<uint8_t> Data;
        /// <summary>A group's players.</summary>
        std::vector<uint32_t> Members;
        /// <summary>Host only: the index of the joiner connection that owns this player, -1 for the host's own.</summary>
        int Owner = -1;
        /// <summary>Port-only (Redux HD): the machine it is of (every machine's mirror: the rollover's).</summary>
        uint32_t Machine = 0;

        void Write(Writer& out) const
        {
            out.U32(Id);
            out.U32(Type);
            out.U32(Flags);
            out.U32(Parent);
            out.String(ShortName);
            out.String(LongName);
            out.Blob(Data);
            out.U32(static_cast<uint32_t>(Members.size()));

            for (uint32_t member : Members)
            {
                out.U32(member);
            }
        }

        void Read(Reader& in)
        {
            Id = in.U32();
            Type = in.U32();
            Flags = in.U32();
            Parent = in.U32();
            ShortName = in.String();
            LongName = in.String();
            Data = in.Blob();
            const uint32_t count = in.U32();
            Members.clear();

            for (uint32_t i = 0; i < count && in.Ok; i++)
            {
                Members.push_back(in.U32());
            }
        }

        void SetName(const DPNAME& name)
        {
            ShortName = name.ShortName.value_or("");
            LongName = name.LongName.value_or("");
        }

        /// <summary>The names as DirectPlay hands them out (never null).</summary>
        DPNAME Name() const { return DPNAME{ShortName, LongName}; }
    };

    /// <summary>
    /// Port-only (Redux HD): a machine of the session, as its hub shares them for a rollover (see the header's top).
    /// </summary>
    struct Machine
    {
        /// <summary>The hub numbers machines as they join (its own the first): the earliest joined takes over.</summary>
        uint32_t Id = 0;
        /// <summary>The rollover its join said it plays (0: an older build, which never takes over or comes back).</summary>
        uint32_t Version = 0;
        /// <summary>Where it takes connections: its listener's port, at the address its hub saw it at (port 0: none).</summary>
        MCSocket::Address Address;
        /// <summary>The hash of the key it proves itself with as it comes back.</summary>
        MCSha256::MCDigest KeyHash{};

        /// <summary>Whether it can take the session over: a build that rolls over, which takes connections.</summary>
        bool TakesOver() const { return Version >= RolloverVersion && Address.Port != 0; }

        void Write(Writer& out) const
        {
            out.U32(Id);
            out.U32(Version);
            out.U32(Address.Ip);
            out.U16(Address.Port);
            out.Blob(KeyHash);
        }

        void Read(Reader& in)
        {
            Id = in.U32();
            Version = in.U32();
            Address.Ip = in.U32();
            Address.Port = in.U16();
            const std::vector<uint8_t> hash = in.Blob();

            if (hash.size() == KeyHash.size())
            {
                std::ranges::copy(hash, KeyHash.begin());
            }
            else
            {
                in.Ok = false;
            }
        }
    };

    /// <summary>A new key for a machine to prove itself with (the system's random source).</summary>
    MachineKey NewKey()
    {
        std::random_device device;
        MachineKey key{};

        for (size_t i = 0; i < key.size(); i += sizeof(uint32_t))
        {
            const uint32_t word = device();
            std::memcpy(key.data() + i, &word, sizeof(word));
        }

        return key;
    }

    /// <summary>A message waiting in <see cref="MCDirectPlay::Receive"/>'s queue.</summary>
    struct Incoming
    {
        uint32_t From = DPID_SYSMSG;
        uint32_t To = 0;
        /// <summary>A player's message: its bytes. A system message: unused.</summary>
        std::vector<uint8_t> Payload;
        /// <summary>A system message's DPSYS_* type (0 for a player's message).</summary>
        uint32_t SystemType = 0;
        /// <summary>The player or group a create/destroy/set-data message is about.</summary>
        Entity Subject;
        /// <summary>Add/delete-player-to-group: the group and the player.</summary>
        uint32_t Group = 0;
        uint32_t Player = 0;
        /// <summary>DPSYS_SETSESSIONDESC: the new description.</summary>
        SessionInfo Session;
    };

    /// <summary>One TCP connection with its unparsed input and unsent output.</summary>
    struct Link
    {
        MCSocket::Handle Socket = MCSocket::InvalidHandle;
        std::vector<uint8_t> Input;
        std::vector<uint8_t> Output;
        /// <summary>Host side: the join was accepted.</summary>
        bool Joined = false;
        /// <summary>Host side: when the connection was accepted (it must join within <see cref="JoinTimeoutMs"/>).</summary>
        uint64_t AcceptedAt = 0;
        /// <summary>Close once <see cref="Output"/> is written (a refused join).</summary>
        bool CloseWhenFlushed = false;
        bool Dead = false;
        /// <summary>Port-only (Redux HD), host side: the machine on it, and the address it was accepted from.</summary>
        uint32_t Machine = 0;
        MCSocket::Address From;
        /// <summary>
        /// Port-only (Redux HD), a new hub's: a slot with no connection yet, kept until then for a machine of the
        /// session to come back to (its players' messages held in <see cref="Output"/> meanwhile).
        /// </summary>
        std::optional<uint64_t> ResumeBy;
    };

    /// <summary>A session that answered a query.</summary>
    struct FoundSession
    {
        SessionInfo Info;
        MCSocket::Address Address;
        uint64_t SeenAt = 0;
        std::vector<Entity> Players;
    };

    uint64_t NowMs()
    {
        return SDL_GetTicks();
    }

    _GUID NewGuid()
    {
        std::random_device device;
        std::mt19937 random(device());
        _GUID guid{};
        guid.Data1 = random();
        guid.Data2 = static_cast<uint16_t>(random());
        guid.Data3 = static_cast<uint16_t>((random() & 0x0fff) | 0x4000);

        for (uint8_t& b : guid.Data4)
        {
            b = static_cast<uint8_t>(random());
        }

        return guid;
    }

    /// <summary>Splits "host:port" (the port is optional).</summary>
    std::pair<std::string, uint16_t> SplitAddress(std::string_view text)
    {
        const size_t colon = text.rfind(':');

        if (colon != std::string_view::npos)
        {
            int port = 0;
            const std::string_view digits = text.substr(colon + 1);
            const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), port);

            if (error == std::errc() && end == digits.data() + digits.size() && port > 0 && port < 65536)
            {
                return {std::string(text.substr(0, colon)), static_cast<uint16_t>(port)};
            }
        }

        return {std::string(text), MCDirectPlay::DefaultPort};
    }
}

/// <summary>Everything an MCDirectPlay holds (kept out of the header so it needs no socket types).</summary>
struct MCDirectPlay::Impl
{
    enum class Mode
    {
        None,
        Host,
        Joiner
    };

    /// <summary>Whether InitializeConnection was called.</summary>
    bool Initialized = false;
    /// <summary>The host searched for: empty = the LAN.</summary>
    std::string HostName;
    uint16_t Port = DefaultPort;
    /// <summary>The address <see cref="HostName"/> resolved to when sessions were last asked for.</summary>
    std::optional<uint32_t> QueriedIp;

    Mode State = Mode::None;
    /// <summary>A joiner whose host went away: everything fails until Close.</summary>
    bool Lost = false;
    SessionInfo Session;
    std::map<uint32_t, Entity> Entities;
    std::set<uint32_t> LocalPlayers;
    std::deque<Incoming> Inbox;
    /// <summary>Host: the next id to give a player or group.</summary>
    uint32_t NextId = 0;

    /// <summary>Host: the listening TCP socket. Host and searcher: the UDP socket.</summary>
    MCSocket::Handle Listener = MCSocket::InvalidHandle;
    MCSocket::Handle Udp = MCSocket::InvalidHandle;
    /// <summary>Host: one per joiner (slots are never reused while hosting, so owners stay valid).</summary>
    std::vector<Link> Joiners;
    /// <summary>Joiner: the connection to the host, and (Port-only, Redux HD) the address it was made to.</summary>
    Link Server;
    MCSocket::Address ServerAddress;

    /// <summary>Sessions that answered queries, by instance GUID.</summary>
    std::vector<FoundSession> Found;

    /// <summary>Host: when the current second of query answers began, and how many went out in it.</summary>
    uint64_t AnswerWindowStart = 0;
    uint32_t AnswersInWindow = 0;

    /// <summary>Joiner: the answers the host sent to blocking requests (JoinReply, CreateReply) not yet taken.</summary>
    std::deque<std::pair<FrameType, std::vector<uint8_t>>> Replies;
    uint32_t NextToken = 1;
    /// <summary>Host: its say on a joiner's players (MCDirectPlay::SetAdmission).</summary>
    MCDirectPlay::Admission Admit;
    /// <summary>Joiner: why the host refused its last player (MCDirectPlay::LastRefusal).</summary>
    std::string Refusal;

    // Port-only (Redux HD): the rollover (see the header's top).

    /// <summary>The session's machines as its hub shares them, by id (the hub's own the first).</summary>
    std::vector<Machine> Machines;
    /// <summary>This machine's id in the session, and the hub's.</summary>
    uint32_t SelfMachine = 0;
    uint32_t HubMachine = 0;
    /// <summary>Host: the id the next machine to join gets.</summary>
    uint32_t NextMachine = 1;
    /// <summary>The key this machine proves itself with, should it come back to a new hub.</summary>
    MachineKey Key{};
    /// <summary>Joiner: its hub plays the rollover (its join's answer named this machine).</summary>
    bool RollsOver = false;
    /// <summary>Joiner: its hub said the session ends with it (MCDirectPlay::Close).</summary>
    bool Ending = false;
    /// <summary>The port this machine takes connections on: a host's session port, a joiner's own (for a rollover).</summary>
    uint16_t ListenPort = 0;

    /// <summary>A joiner whose hub has gone, coming back to the machine taking over.</summary>
    struct Rolling
    {
        uint32_t Successor = 0;
        /// <summary>Where it is reached: its own listener, at its address or (an internet game) the game's.</summary>
        MCSocket::Address Address;
        uint64_t Until = 0;
        /// <summary>The key this machine comes back with next time, once this one is taken.</summary>
        MachineKey NextKey{};
    };

    std::optional<Rolling> Roll;

    ~Impl()
    {
        Shutdown();
        MCSocket::Close(Udp);
    }

    // ---- framing -------------------------------------------------------------------------------------------------

    /// <summary>
    /// Has the operating system watch a link to a session that asked DirectPlay to drop players who vanish
    /// (DPSESSION_KEEPALIVE, as the game's sessions do): it then fails within half a minute of the machine at its
    /// other end going, as a closed one does, where a quiet link to it waited for good.
    /// </summary>
    static void KeepAliveIfAsked(const DPSESSIONDESC2& desc, MCSocket::Handle socket)
    {
        if ((desc.dwFlags & DPSESSION_KEEPALIVE) != 0)
        {
            MCSocket::KeepAlive(socket);
        }
    }

    static void Queue(Link& link, FrameType type, const Writer& payload)
    {
        const uint32_t size = static_cast<uint32_t>(payload.Bytes.size() + 1);
        Writer header;
        header.U32(size);
        header.U8(static_cast<uint8_t>(type));
        link.Output.insert(link.Output.end(), header.Bytes.begin(), header.Bytes.end());
        link.Output.insert(link.Output.end(), payload.Bytes.begin(), payload.Bytes.end());
        Flush(link);

        // A peer that stopped reading would make this grow without end (issue #25): it is dropped instead.
        if (link.Output.size() > MaxUnsentBytes)
        {
            link.Dead = true;
        }
    }

    /// <summary>
    /// Port-only (Redux HD): queues a frame on a link that has sent nothing yet, ahead of <paramref name="held"/> (frames
    /// kept for it while it had no connection), which go after it.
    /// </summary>
    static void QueueAhead(Link& link, FrameType type, const Writer& payload, std::vector<uint8_t> held)
    {
        Queue(link, type, payload);
        link.Output.insert(link.Output.end(), held.begin(), held.end());
        Flush(link);
    }

    static void Flush(Link& link)
    {
        // Port-only (Redux HD): a link with no connection yet (a rollover's) keeps what it is given until it has one.
        if (link.Socket == MCSocket::InvalidHandle)
        {
            return;
        }

        while (!link.Dead && !link.Output.empty())
        {
            const int sent = MCSocket::Send(link.Socket, link.Output);

            if (sent < 0)
            {
                link.Dead = true;
                return;
            }

            if (sent == 0)
            {
                return;
            }

            link.Output.erase(link.Output.begin(), link.Output.begin() + sent);
        }

        if (link.Output.empty() && link.CloseWhenFlushed)
        {
            link.Dead = true;
        }
    }

    /// <summary>Reads what arrived on <paramref name="link"/> and hands each whole frame to <paramref name="handle"/>.</summary>
    template <class Handler> static void ReadFrames(Link& link, Handler&& handle)
    {
        std::array<uint8_t, 8192> chunk{};

        while (!link.Dead)
        {
            const int received = MCSocket::Receive(link.Socket, chunk);

            if (received < 0)
            {
                link.Dead = true;
                break;
            }

            if (received == 0)
            {
                break;
            }

            link.Input.insert(link.Input.end(), chunk.begin(), chunk.begin() + received);
        }

        size_t position = 0;

        while (link.Input.size() - position >= 5)
        {
            const std::span<const uint8_t> p = std::span<const uint8_t>(link.Input).subspan(position);
            const uint32_t size = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);

            if (size == 0 || size > MaxFrameSize)
            {
                link.Dead = true;
                break;
            }

            if (link.Input.size() - position - 4 < size)
            {
                break;
            }

            const FrameType type = static_cast<FrameType>(p[4]);
            handle(type, p.subspan(5, size - 1));
            position += 4 + size;
        }

        link.Input.erase(link.Input.begin(), link.Input.begin() + position);
    }

    // ---- shared state --------------------------------------------------------------------------------------------

    uint32_t PlayerCount() const
    {
        uint32_t count = 0;

        for (const auto& [id, entity] : Entities)
        {
            if (entity.Type == DPPLAYERTYPE_PLAYER)
            {
                count++;
            }
        }

        return count;
    }

    /// <summary>System messages only reach machines that have a player (DirectPlay delivers them to players).</summary>
    void QueueSystem(Incoming message)
    {
        if (!LocalPlayers.empty())
        {
            message.From = DPID_SYSMSG;
            message.To = *LocalPlayers.begin();
            Inbox.push_back(std::move(message));
        }
    }

    void QueueCreated(const Entity& entity)
    {
        Incoming message;
        message.SystemType = DPSYS_CREATEPLAYERORGROUP;
        message.Subject = entity;
        QueueSystem(std::move(message));
    }

    void QueueDestroyed(const Entity& entity)
    {
        Incoming message;
        message.SystemType = DPSYS_DESTROYPLAYERORGROUP;
        message.Subject = entity;
        QueueSystem(std::move(message));
    }

    void QueueMembership(uint32_t type, uint32_t group, uint32_t player)
    {
        Incoming message;
        message.SystemType = type;
        message.Group = group;
        message.Player = player;
        QueueSystem(std::move(message));
    }

    /// <summary>Adds or removes a group member in the local state.</summary>
    /// <returns>Whether the membership changed.</returns>
    bool ApplyMembership(bool add, uint32_t group, uint32_t player)
    {
        const auto found = Entities.find(group);

        if (found == Entities.end() || found->second.Type != DPPLAYERTYPE_GROUP || !Entities.contains(player))
        {
            return false;
        }

        std::vector<uint32_t>& members = found->second.Members;
        const auto member = std::ranges::find(members, player);

        if (add && member == members.end())
        {
            members.push_back(player);
            return true;
        }

        if (!add && member != members.end())
        {
            members.erase(member);
            return true;
        }

        return false;
    }

    /// <summary>
    /// Writes the session as a join's (or a rollover's resume's) answer gives it: its description, then every player and
    /// group, then (Port-only, Redux HD: after what an older joiner reads) the rollover this hub plays, the joiner's
    /// machine <paramref name="machine"/>, and each player's and group's machine in the same order.
    /// </summary>
    void WriteSessionState(Writer& out, uint32_t machine)
    {
        Session.Desc.dwCurrentPlayers = PlayerCount();
        Session.Write(out, false);
        out.U32(static_cast<uint32_t>(Entities.size()));

        for (const auto& [id, entity] : Entities)
        {
            entity.Write(out);
        }

        out.U32(RolloverVersion);
        out.U32(machine);

        for (const auto& [id, entity] : Entities)
        {
            out.U32(entity.Machine);
        }
    }

    /// <summary>The session as <see cref="WriteSessionState"/> wrote it (an older host's says no rollover).</summary>
    struct SessionState
    {
        SessionInfo Session;
        std::map<uint32_t, Entity> Entities;
        bool RollsOver = false;
        /// <summary>The joiner's machine, when the hub rolls over.</summary>
        uint32_t Machine = 0;
    };

    static std::optional<SessionState> ReadSessionState(Reader& in)
    {
        SessionState state;
        state.Session.Read(in);
        const uint32_t count = in.U32();
        std::vector<uint32_t> order;

        for (uint32_t i = 0; i < count && in.Ok; i++)
        {
            Entity entity;
            entity.Read(in);
            entity.Owner = -1;
            order.push_back(entity.Id);
            state.Entities[entity.Id] = std::move(entity);
        }

        if (!in.Ok)
        {
            return std::nullopt;
        }

        // An older host's answer ends here: its session never rolls over.
        const uint32_t version = in.U32();
        const uint32_t machine = in.U32();
        std::vector<uint32_t> machines;

        for (size_t i = 0; i < order.size() && in.Ok; i++)
        {
            machines.push_back(in.U32());
        }

        if (in.Ok && version >= RolloverVersion)
        {
            state.RollsOver = true;
            state.Machine = machine;

            for (size_t i = 0; i < order.size(); i++)
            {
                state.Entities[order[i]].Machine = machines[i];
            }
        }

        return state;
    }

    /// <summary>Port-only (Redux HD): the machine <paramref name="id"/> of the session's, if it has it.</summary>
    MCOptionalRef<Machine> MachineOf(uint32_t id)
    {
        const auto found = std::ranges::find(Machines, id, &Machine::Id);
        return found != Machines.end() ? MCOptionalRef<Machine>(*found) : std::nullopt;
    }

    /// <summary>Removes <paramref name="id"/> and, for a player, its group memberships (in the local state).</summary>
    std::optional<Entity> RemoveEntity(uint32_t id)
    {
        const auto found = Entities.find(id);

        if (found == Entities.end())
        {
            return std::nullopt;
        }

        Entity removed = std::move(found->second);
        Entities.erase(found);
        LocalPlayers.erase(id);
        return removed;
    }

    // ---- host ----------------------------------------------------------------------------------------------------

    /// <summary>Host: sends a frame to every joined connection except <paramref name="except"/>.</summary>
    void Broadcast(FrameType type, const Writer& payload, int except)
    {
        for (size_t i = 0; i < Joiners.size(); i++)
        {
            if (static_cast<int>(i) != except && Joiners[i].Joined && !Joiners[i].Dead)
            {
                Queue(Joiners[i], type, payload);
            }
        }
    }

    /// <summary>Host: numbers and records a new player or group and tells everyone else.</summary>
    uint32_t HostCreate(Entity entity, int origin)
    {
        entity.Id = NextId++;
        entity.Owner = origin;
        entity.Machine = origin < 0 ? SelfMachine : Joiners[origin].Machine;

        // Only the host's own players are marked as the host's; a joiner can't claim it (issue #25).
        if (entity.Type == DPPLAYERTYPE_PLAYER && origin < 0)
        {
            entity.Flags |= DPPLAYER_SERVERPLAYER;
        }
        else
        {
            entity.Flags &= ~static_cast<uint32_t>(DPPLAYER_SERVERPLAYER);
        }

        Writer note;
        entity.Write(note);
        // Port-only (Redux HD): its machine after it (an older joiner reads the entity alone).
        note.U32(entity.Machine);
        Broadcast(FrameType::Created, note, origin);

        if (origin >= 0)
        {
            QueueCreated(entity);
        }

        const uint32_t id = entity.Id;
        Entities[id] = std::move(entity);
        return id;
    }

    /// <summary>Host: whether <paramref name="id"/> is a player or group (<paramref name="type"/>) of joiner <paramref name="owner"/>.</summary>
    bool OwnedBy(uint32_t id, uint32_t type, int owner) const
    {
        const auto found = Entities.find(id);
        return found != Entities.end() && found->second.Type == type && found->second.Owner == owner;
    }

    /// <summary>Host: changes a membership and tells everyone else.</summary>
    void HostMembership(bool add, uint32_t group, uint32_t player, int origin)
    {
        if (!ApplyMembership(add, group, player))
        {
            return;
        }

        Writer note;
        note.U32(group);
        note.U32(player);
        Broadcast(add ? FrameType::AddToGroup : FrameType::DeleteFromGroup, note, origin);

        if (origin >= 0)
        {
            QueueMembership(add ? DPSYS_ADDPLAYERTOGROUP : DPSYS_DELETEPLAYERFROMGROUP, group, player);
        }
    }

    void HostSetGroupData(uint32_t group, std::vector<uint8_t> data, int origin)
    {
        const auto found = Entities.find(group);

        if (found == Entities.end())
        {
            return;
        }

        found->second.Data = std::move(data);
        Writer note;
        note.U32(group);
        note.Blob(found->second.Data);
        Broadcast(FrameType::SetGroupData, note, origin);

        if (origin >= 0)
        {
            Incoming message;
            message.SystemType = DPSYS_SETPLAYERORGROUPDATA;
            message.Subject = found->second;
            QueueSystem(std::move(message));
        }
    }

    /// <summary>Host: removes a player (its memberships first) and tells everyone.</summary>
    void HostDestroyPlayer(uint32_t id)
    {
        for (auto& [groupID, group] : Entities)
        {
            if (group.Type == DPPLAYERTYPE_GROUP && std::ranges::find(group.Members, id) != group.Members.end())
            {
                HostMembership(false, groupID, id, -2);
                QueueMembership(DPSYS_DELETEPLAYERFROMGROUP, groupID, id);
            }
        }

        std::optional<Entity> removed = RemoveEntity(id);

        if (removed)
        {
            Writer note;
            note.U32(id);
            Broadcast(FrameType::Destroyed, note, -1);
            QueueDestroyed(*removed);
        }
    }

    /// <summary>Host: delivers a player's message to its receivers (never back to the sender).</summary>
    void Route(uint32_t from, uint32_t to, std::span<const uint8_t> payload)
    {
        std::vector<uint32_t> receivers;

        if (to == DPID_ALLPLAYERS)
        {
            for (const auto& [id, entity] : Entities)
            {
                if (entity.Type == DPPLAYERTYPE_PLAYER)
                {
                    receivers.push_back(id);
                }
            }
        }
        else if (const auto found = Entities.find(to); found != Entities.end())
        {
            if (found->second.Type == DPPLAYERTYPE_GROUP)
            {
                receivers = found->second.Members;
            }
            else
            {
                receivers.push_back(to);
            }
        }

        for (uint32_t receiver : receivers)
        {
            if (receiver == from)
            {
                continue;
            }

            if (LocalPlayers.contains(receiver))
            {
                Incoming message;
                message.From = from;
                message.To = receiver;
                message.Payload.assign(payload.begin(), payload.end());
                Inbox.push_back(std::move(message));
                continue;
            }

            const auto found = Entities.find(receiver);

            if (found != Entities.end() && found->second.Owner >= 0)
            {
                Writer frame;
                frame.U32(from);
                frame.U32(receiver);
                frame.Blob(payload);
                Queue(Joiners[found->second.Owner], FrameType::Data, frame);
            }
        }
    }

    /// <summary>
    /// Port-only (Redux HD), the hub: tells the joiners that roll over the session's machines (each joining, coming
    /// back or leaving), for a rollover. An older joiner is told nothing.
    /// </summary>
    void ShareMachines()
    {
        Writer note;
        note.U32(HubMachine);
        note.U32(static_cast<uint32_t>(Machines.size()));

        for (const Machine& machine : Machines)
        {
            machine.Write(note);
        }

        for (Link& joiner : Joiners)
        {
            const MCOptionalRef<Machine> machine = MachineOf(joiner.Machine);

            if (joiner.Joined && !joiner.Dead && machine && machine->get().Version >= RolloverVersion)
            {
                Queue(joiner, FrameType::Machines, note);
            }
        }
    }

    /// <summary>
    /// Port-only (Redux HD), a new hub: the connection <paramref name="index"/> asks to come back as a machine of the
    /// session (<see cref="FrameType::Resume"/>). It takes the slot kept for that machine, its players and groups and
    /// the messages held for them, once its key's hash is the one its old hub shared; anything else is refused.
    /// </summary>
    void HostResume(int index, Reader& in)
    {
        Link& link = Joiners[index];
        const uint32_t magic = in.U32();
        const _GUID application = in.Guid();
        const _GUID instance = in.Guid();
        const uint32_t machineId = in.U32();
        const std::vector<uint8_t> key = in.Blob();
        const std::vector<uint8_t> nextHash = in.Blob();
        const uint32_t version = in.U32();
        const uint16_t port = in.U16();
        const MCOptionalRef<Machine> machine = MachineOf(machineId);
        const auto slot = std::ranges::find_if(Joiners, [&](const Link& kept)
                                               { return kept.ResumeBy.has_value() && kept.Machine == machineId; });
        uint32_t result = DP_OK;

        if (!in.Ok || magic != ProtocolMagic || !MCSameGuid(application, Session.Desc.guidApplication) ||
            !MCSameGuid(instance, Session.Desc.guidInstance) || nextHash.size() != MCSha256::MCDigest{}.size())
        {
            result = DPERR_NOSESSIONS;
        }
        else if (!machine || slot == Joiners.end() || MCSha256::Of(key) != machine->get().KeyHash)
        {
            // Nobody but the machine itself takes its players: its key is its own.
            result = DPERR_ACCESSDENIED;
        }

        Writer reply;
        reply.U32(result);

        if (result != DP_OK)
        {
            SDL_Log("MCDirectPlay: refused machine %u coming back to \"%s\" (0x%08x)", machineId, Session.Name.c_str(),
                    result);
            link.CloseWhenFlushed = true;
            Queue(link, FrameType::ResumeReply, reply);
            return;
        }

        const int kept = static_cast<int>(slot - Joiners.begin());

        for (auto& [id, entity] : Entities)
        {
            if (entity.Owner == kept)
            {
                entity.Owner = index;
            }
        }

        std::vector<uint8_t> held = std::move(Joiners[kept].Output);
        Joiners[kept] = Link{};
        link.Joined = true;
        link.Machine = machineId;
        machine->get().Version = version;
        machine->get().Address = MCSocket::Address{link.From.Ip, port};
        std::ranges::copy(nextHash, machine->get().KeyHash.begin());
        WriteSessionState(reply, machineId);
        QueueAhead(link, FrameType::ResumeReply, reply, std::move(held));
        SDL_Log("MCDirectPlay: machine %u came back to \"%s\"", machineId, Session.Name.c_str());
        ShareMachines();
    }

    void HostHandle(int index, FrameType type, std::span<const uint8_t> payload)
    {
        Link& link = Joiners[index];
        Reader in(payload);

        if (!link.Joined)
        {
            // Port-only (Redux HD): a machine of the session coming back to it under this new hub.
            if (type == FrameType::Resume)
            {
                HostResume(index, in);
                return;
            }

            if (type != FrameType::JoinRequest)
            {
                link.Dead = true;
                return;
            }

            const uint32_t magic = in.U32();
            const _GUID application = in.Guid();
            const _GUID instance = in.Guid();
            const std::string password = in.String();
            const bool requestOk = in.Ok;
            // Port-only (Redux HD): the rollover the joiner plays, after what an older build sends.
            Machine joiner;
            joiner.Version = in.U32();
            joiner.Address = MCSocket::Address{link.From.Ip, in.U16()};
            const std::vector<uint8_t> keyHash = in.Blob();

            if (!in.Ok || keyHash.size() != joiner.KeyHash.size())
            {
                joiner = Machine{};
            }
            else
            {
                std::ranges::copy(keyHash, joiner.KeyHash.begin());
            }

            uint32_t result = DP_OK;

            if (!requestOk || magic != ProtocolMagic || !MCSameGuid(application, Session.Desc.guidApplication) ||
                !MCSameGuid(instance, Session.Desc.guidInstance))
            {
                result = DPERR_NOSESSIONS;
            }
            else if ((Session.Desc.dwFlags & (DPSESSION_NEWPLAYERSDISABLED | DPSESSION_JOINDISABLED)) != 0 ||
                     PlayerCount() >= Session.Desc.dwMaxPlayers)
            {
                result = DPERR_NONEWPLAYERS;
            }
            else if ((Session.Desc.dwFlags & DPSESSION_PASSWORDREQUIRED) != 0 && password != Session.Password)
            {
                result = DPERR_INVALIDPASSWORD;
            }

            Writer reply;
            reply.U32(result);

            if (result == DP_OK)
            {
                link.Machine = NextMachine++;
                joiner.Id = link.Machine;
                Machines.push_back(joiner);
                WriteSessionState(reply, link.Machine);
                link.Joined = true;
            }
            else
            {
                link.CloseWhenFlushed = true;
            }

            Queue(link, FrameType::JoinReply, reply);

            if (result == DP_OK)
            {
                ShareMachines();
            }

            return;
        }

        switch (type)
        {
            case FrameType::CreatePlayer:
            case FrameType::CreateGroup:
            {
                const uint32_t token = in.U32();
                Entity entity;
                entity.Read(in);
                entity.Type = type == FrameType::CreatePlayer ? DPPLAYERTYPE_PLAYER : DPPLAYERTYPE_GROUP;
                entity.Members.clear();
                uint32_t result = in.Ok ? DP_OK : DPERR_INVALIDPARAMS;
                uint32_t id = 0;

                if (result == DP_OK && entity.Type == DPPLAYERTYPE_PLAYER &&
                    ((Session.Desc.dwFlags & DPSESSION_NEWPLAYERSDISABLED) != 0 ||
                     PlayerCount() >= Session.Desc.dwMaxPlayers))
                {
                    result = DPERR_CANTCREATEPLAYER;
                }

                // Port: the host's game has its say on a player before anyone hears of it (SetAdmission).
                std::optional<std::string> refusal;

                if (result == DP_OK && entity.Type == DPPLAYERTYPE_PLAYER && Admit)
                {
                    refusal = Admit(entity.ShortName, entity.Data);
                    result = refusal.has_value() ? DPERR_CANTCREATEPLAYER : DP_OK;
                }

                // The game's groups are the host's; a joiner gets a few, not as many as it asks for (issue #25).
                if (result == DP_OK && entity.Type == DPPLAYERTYPE_GROUP &&
                    std::ranges::count_if(
                        Entities, [&](const auto& item)
                        { return item.second.Type == DPPLAYERTYPE_GROUP && item.second.Owner == index; }) >=
                        MaxJoinerGroups)
                {
                    result = DPERR_CANTCREATEGROUP;
                }

                Writer reply;

                if (result == DP_OK)
                {
                    id = HostCreate(std::move(entity), index);
                }

                reply.U32(token);
                reply.U32(result);
                reply.U32(id);

                // A refusal's reason after (an older joiner reads only the code), and the joiner's link closes.
                if (refusal.has_value())
                {
                    reply.String(*refusal);
                    link.CloseWhenFlushed = true;
                }

                Queue(link, FrameType::CreateReply, reply);
                break;
            }

            case FrameType::AddToGroup:
            case FrameType::DeleteFromGroup:
            {
                const uint32_t group = in.U32();
                const uint32_t player = in.U32();

                // A joiner moves only its own players (issue #25: it could take anyone out of a group, or put a group
                // into one). The host makes the moves the session screen makes for the others itself.
                if (in.Ok && OwnedBy(player, DPPLAYERTYPE_PLAYER, index))
                {
                    HostMembership(type == FrameType::AddToGroup, group, player, index);
                }

                break;
            }

            case FrameType::SetGroupData:
            {
                const uint32_t group = in.U32();
                std::vector<uint8_t> data = in.Blob();

                // A joiner sets only its own groups' data (issue #25).
                if (in.Ok && OwnedBy(group, DPPLAYERTYPE_GROUP, index))
                {
                    HostSetGroupData(group, std::move(data), index);
                }

                break;
            }

            case FrameType::Data:
            {
                const uint32_t from = in.U32();
                const uint32_t to = in.U32();
                const std::vector<uint8_t> data = in.Blob();

                // A joiner may only speak for its own players.
                const auto sender = Entities.find(from);

                if (in.Ok && sender != Entities.end() && sender->second.Owner == index)
                {
                    Route(from, to, data);
                }

                break;
            }

            default:
            {
                break;
            }
        }
    }

    /// <summary>
    /// Takes the connections waiting on <see cref="Listener"/> as joiners' links, not yet joined: a host's, or (Port-only,
    /// Redux HD) a joiner's own, kept for when it takes the session over.
    /// </summary>
    void AcceptJoiners()
    {
        MCSocket::Address from;

        for (;;)
        {
            const MCSocket::Handle socket = MCSocket::AcceptTcp(Listener, from);

            if (socket == MCSocket::InvalidHandle)
            {
                break;
            }

            // Issue #25: only a few connections may wait to join at once, so a flood of them is turned away.
            if (std::ranges::count_if(Joiners, [](const Link& joiner)
                                      { return joiner.Socket != MCSocket::InvalidHandle && !joiner.Joined; }) >=
                MaxPendingJoiners)
            {
                MCSocket::Close(socket);
                continue;
            }

            KeepAliveIfAsked(Session.Desc, socket);
            Link link;
            link.Socket = socket;
            link.AcceptedAt = NowMs();
            link.From = from;

            // A closed slot nothing in the session still belongs to is reused (owners are slot indexes); one kept for a
            // machine coming back is its.
            const auto reusable = std::ranges::find_if(
                Joiners,
                [&](const Link& joiner)
                {
                    const auto slot = static_cast<int>(&joiner - Joiners.data());
                    return joiner.Socket == MCSocket::InvalidHandle && !joiner.ResumeBy.has_value() &&
                           std::ranges::none_of(Entities, [&](const auto& item) { return item.second.Owner == slot; });
                });

            if (reusable != Joiners.end())
            {
                *reusable = std::move(link);
            }
            else
            {
                Joiners.push_back(std::move(link));
            }
        }
    }

    /// <summary>
    /// Host: a joiner's link has gone (or, Port-only, Redux HD, its machine never came back to a new hub): its players
    /// leave, and it is no longer one of the session's machines.
    /// </summary>
    void DropJoiner(size_t index)
    {
        Link& joiner = Joiners[index];
        MCSocket::Close(joiner.Socket);
        const uint32_t machine = joiner.Machine;
        const bool wasMachine = joiner.Joined || joiner.ResumeBy.has_value();
        joiner = Link{};
        std::vector<uint32_t> owned;

        for (const auto& [id, entity] : Entities)
        {
            if (entity.Type == DPPLAYERTYPE_PLAYER && entity.Owner == static_cast<int>(index))
            {
                owned.push_back(id);
            }
        }

        for (uint32_t id : owned)
        {
            HostDestroyPlayer(id);
        }

        if (wasMachine && std::erase_if(Machines, [&](const Machine& known) { return known.Id == machine; }) != 0)
        {
            ShareMachines();
        }
    }

    void HostPump()
    {
        AcceptJoiners();

        for (size_t i = 0; i < Joiners.size(); i++)
        {
            // Port-only (Redux HD): a machine that hasn't come back to this new hub in time has left.
            if (Joiners[i].ResumeBy.has_value() && NowMs() > *Joiners[i].ResumeBy)
            {
                SDL_Log("MCDirectPlay: machine %u never came back to \"%s\"", Joiners[i].Machine, Session.Name.c_str());
                DropJoiner(i);
                continue;
            }

            if (Joiners[i].Socket == MCSocket::InvalidHandle)
            {
                continue;
            }

            // A connection that hasn't asked to join in time is dropped (issue #25).
            if (!Joiners[i].Joined && NowMs() - Joiners[i].AcceptedAt > JoinTimeoutMs)
            {
                Joiners[i].Dead = true;
            }

            Flush(Joiners[i]);
            ReadFrames(Joiners[i], [&](FrameType type, std::span<const uint8_t> payload)
                       { HostHandle(static_cast<int>(i), type, payload); });
            Flush(Joiners[i]);

            if (Joiners[i].Dead)
            {
                DropJoiner(i);
            }
        }

        AnswerQueries();
    }

    /// <summary>Host: answers the UDP session queries for this game.</summary>
    void AnswerQueries()
    {
        // Port-only (Redux HD): a new hub that couldn't take the session's search port answers no searches.
        if (Udp == MCSocket::InvalidHandle)
        {
            return;
        }

        std::array<uint8_t, 1500> datagram{};
        MCSocket::Address from;

        for (;;)
        {
            const int size = MCSocket::ReceiveFrom(Udp, datagram, from);

            if (size <= 0)
            {
                break;
            }

            Reader in(std::span<const uint8_t>(datagram).first(static_cast<size_t>(size)));
            const uint32_t magic = in.U32();
            const uint8_t kind = in.U8();
            const _GUID application = in.Guid();

            if (!in.Ok || magic != ProtocolMagic || kind != DatagramQuery ||
                !MCSameGuid(application, Session.Desc.guidApplication))
            {
                continue;
            }

            // The answer is many times the question, and a datagram's sender can be forged: only so many answers go out
            // each second, so the host can't be used to flood someone else (issue #25).
            if (const uint64_t now = NowMs(); now - AnswerWindowStart >= 1000)
            {
                AnswerWindowStart = now;
                AnswersInWindow = 0;
            }

            if (AnswersInWindow >= MaxAnswersPerSecond)
            {
                continue;
            }

            AnswersInWindow++;

            Writer reply;
            reply.U32(ProtocolMagic);
            reply.U8(DatagramReply);
            // The port it takes joins on: the session's, or (Port-only, Redux HD) a new hub's own.
            reply.U16(ListenPort);
            Session.Desc.dwCurrentPlayers = PlayerCount();
            Session.Write(reply, false);
            std::vector<std::reference_wrapper<const Entity>> players;

            for (const auto& [id, entity] : Entities)
            {
                if (entity.Type == DPPLAYERTYPE_PLAYER)
                {
                    players.emplace_back(entity);
                }
            }

            reply.U32(static_cast<uint32_t>(players.size()));

            for (const Entity& player : players)
            {
                reply.U32(player.Id);
                reply.String(player.ShortName);
                reply.String(player.LongName);
            }

            if (reply.Bytes.size() <= sizeof(datagram))
            {
                MCSocket::SendTo(Udp, from, reply.Bytes);
            }
        }
    }

    // ---- joiner --------------------------------------------------------------------------------------------------

    void JoinerHandle(FrameType type, std::span<const uint8_t> payload)
    {
        Reader in(payload);

        switch (type)
        {
            case FrameType::JoinReply:
            case FrameType::CreateReply:
            {
                Replies.emplace_back(type, std::vector<uint8_t>(payload.begin(), payload.end()));
                break;
            }

            case FrameType::Created:
            {
                Entity entity;
                entity.Read(in);
                const bool read = in.Ok;
                // Port-only (Redux HD): its machine after it (an older hub's is its own).
                const uint32_t machine = in.U32();
                entity.Machine = in.Ok ? machine : HubMachine;

                if (read && !Entities.contains(entity.Id))
                {
                    entity.Owner = -1;
                    QueueCreated(entity);
                    Entities[entity.Id] = std::move(entity);
                }

                break;
            }

            case FrameType::Destroyed:
            {
                const uint32_t id = in.U32();
                std::optional<Entity> removed = in.Ok ? RemoveEntity(id) : std::nullopt;

                if (removed)
                {
                    for (auto& [groupID, group] : Entities)
                    {
                        std::erase(group.Members, id);
                    }

                    QueueDestroyed(*removed);
                }

                break;
            }

            case FrameType::AddToGroup:
            case FrameType::DeleteFromGroup:
            {
                const uint32_t group = in.U32();
                const uint32_t player = in.U32();
                const bool add = type == FrameType::AddToGroup;

                if (in.Ok && ApplyMembership(add, group, player))
                {
                    QueueMembership(add ? DPSYS_ADDPLAYERTOGROUP : DPSYS_DELETEPLAYERFROMGROUP, group, player);
                }

                break;
            }

            case FrameType::SetGroupData:
            {
                const uint32_t group = in.U32();
                std::vector<uint8_t> data = in.Blob();
                const auto found = Entities.find(group);

                if (in.Ok && found != Entities.end())
                {
                    found->second.Data = std::move(data);
                    Incoming message;
                    message.SystemType = DPSYS_SETPLAYERORGROUPDATA;
                    message.Subject = found->second;
                    QueueSystem(std::move(message));
                }

                break;
            }

            case FrameType::SessionDesc:
            {
                SessionInfo info;
                info.Read(in);

                if (in.Ok)
                {
                    Session = info;
                    Incoming message;
                    message.SystemType = DPSYS_SETSESSIONDESC;
                    message.Session = std::move(info);
                    QueueSystem(std::move(message));
                }

                break;
            }

            case FrameType::Data:
            {
                Incoming message;
                message.From = in.U32();
                message.To = in.U32();
                message.Payload = in.Blob();

                if (in.Ok)
                {
                    Inbox.push_back(std::move(message));
                }

                break;
            }

            case FrameType::Machines:
            {
                const uint32_t hub = in.U32();
                const uint32_t count = in.U32();
                std::vector<Machine> machines;

                for (uint32_t i = 0; i < count && in.Ok; i++)
                {
                    machines.emplace_back().Read(in);
                }

                if (in.Ok)
                {
                    HubMachine = hub;
                    std::ranges::sort(machines, {}, &Machine::Id);
                    Machines = std::move(machines);
                }

                break;
            }

            case FrameType::Ending:
            {
                Ending = true;
                break;
            }

            default:
            {
                break;
            }
        }
    }

    void JoinerPump()
    {
        if (Server.Socket == MCSocket::InvalidHandle && !Roll.has_value())
        {
            return;
        }

        if (Lost)
        {
            return;
        }

        // Port-only (Redux HD): connections to this machine's own listener wait for it to take the session over.
        if (Listener != MCSocket::InvalidHandle)
        {
            AcceptJoiners();
            std::erase_if(Joiners,
                          [](Link& held)
                          {
                              const bool stale = NowMs() - held.AcceptedAt > RolloverWaitMs;

                              if (stale)
                              {
                                  MCSocket::Close(held.Socket);
                              }

                              return stale;
                          });
        }

        if (Roll.has_value())
        {
            PumpRollover();
            return;
        }

        Flush(Server);
        ReadFrames(Server, [&](FrameType type, std::span<const uint8_t> payload) { JoinerHandle(type, payload); });
        Flush(Server);

        if (Server.Dead)
        {
            // Port-only (Redux HD): a hub gone without ending the session hands it on (see the header's top).
            if (!Ending && RollsOver && (Session.Desc.dwFlags & DPSESSION_MIGRATEHOST) != 0)
            {
                StartRollover();
            }
            else
            {
                LoseSession("its host has gone");
            }
        }
    }

    /// <summary>Joiner: the session is lost (DPSYS_SESSIONLOST): everything fails until Close.</summary>
    void LoseSession(std::string_view why)
    {
        SDL_Log("MCDirectPlay: lost \"%s\": %.*s", Session.Name.c_str(), static_cast<int>(why.size()), why.data());
        Lost = true;
        Roll.reset();
        Incoming message;
        message.SystemType = DPSYS_SESSIONLOST;
        QueueSystem(std::move(message));
    }

    /// <summary>
    /// Port-only (Redux HD), a joiner whose hub has gone: the session's earliest-joined machine left that takes over is
    /// its new hub, which every joiner picks alike from the machines its hub shared. This machine takes it over itself,
    /// or comes back to it (<see cref="PumpRollover"/>).
    /// </summary>
    void StartRollover()
    {
        const uint32_t gone = HubMachine;
        const MCSocket::Address hubAddress = ServerAddress;
        MCSocket::Close(Server.Socket);
        // What it hadn't sent goes with it: the game sends its guaranteed messages again until they're answered.
        Server = Link{};
        std::erase_if(Machines, [&](const Machine& machine) { return machine.Id == gone; });
        const auto successor = std::ranges::find_if(Machines, &Machine::TakesOver);

        if (successor == Machines.end())
        {
            LoseSession("its host has gone, and no machine can take over");
            return;
        }

        if (successor->Id == SelfMachine)
        {
            TakeOver(gone);
            return;
        }

        Rolling roll;
        roll.Successor = successor->Id;
        roll.Address = successor->Address;

        // An internet game's machine is reached by the game's name, as its hub was (MCIceTransport), at its own port.
        if ((roll.Address.Ip & LinkAddressMask) == 0)
        {
            roll.Address.Ip = hubAddress.Ip;
        }

        roll.Until = NowMs() + RolloverWaitMs;
        roll.NextKey = NewKey();
        Roll = roll;
        SDL_Log("MCDirectPlay: the host of \"%s\" has gone; machine %u takes over, at %s", Session.Name.c_str(),
                roll.Successor, MCSocket::ToString(roll.Address).c_str());
    }

    /// <summary>
    /// Port-only (Redux HD), a joiner coming back to the machine taking over: connects, says who it is, and takes the
    /// session as the new hub has it (<see cref="Resumed"/>); what the game sends meanwhile follows. It fails to
    /// DPSYS_SESSIONLOST when the new hub can't be reached in time or turns it away.
    /// </summary>
    void PumpRollover()
    {
        Rolling& roll = *Roll;

        if (Server.Socket == MCSocket::InvalidHandle)
        {
            if (NowMs() > roll.Until)
            {
                LoseSession("the machine taking over can't be reached");
                return;
            }

            const MCSocket::Handle socket = MCSocket::ConnectTcp(roll.Address, RolloverConnectMs);

            if (socket == MCSocket::InvalidHandle)
            {
                return;
            }

            Server.Socket = socket;
            ServerAddress = roll.Address;
            KeepAliveIfAsked(Session.Desc, socket);
            Writer resume;
            resume.U32(ProtocolMagic);
            resume.Guid(Session.Desc.guidApplication);
            resume.Guid(Session.Desc.guidInstance);
            resume.U32(SelfMachine);
            resume.Blob(Key);
            resume.Blob(MCSha256::Of(roll.NextKey));
            resume.U32(RolloverVersion);
            resume.U16(ListenPort);
            std::vector<uint8_t> held = std::move(Server.Output);
            Server.Output.clear();
            QueueAhead(Server, FrameType::Resume, resume, std::move(held));
        }

        Flush(Server);
        ReadFrames(Server,
                   [&](FrameType type, std::span<const uint8_t> payload)
                   {
                       if (!Roll.has_value())
                       {
                           JoinerHandle(type, payload);
                       }
                       else if (type == FrameType::ResumeReply)
                       {
                           Resumed(payload);
                       }
                   });
        Flush(Server);

        if (Roll.has_value() && (Server.Dead || NowMs() > Roll->Until))
        {
            LoseSession(Server.Dead ? "the machine taking over turned it away"
                                    : "the machine taking over never answered");
        }
        else if (!Roll.has_value() && !Lost && Server.Dead)
        {
            // The new hub went straight after taking it back: the next one takes over.
            JoinerPump();
        }
    }

    /// <summary>
    /// Port-only (Redux HD): the new hub's answer to this machine coming back: the session as it has it, which the
    /// game learns of as DirectPlay's messages (<see cref="Reconcile"/>), or a refusal.
    /// </summary>
    void Resumed(std::span<const uint8_t> payload)
    {
        Reader in(payload);
        const uint32_t result = in.U32();
        std::optional<SessionState> state = result == DP_OK ? ReadSessionState(in) : std::nullopt;

        if (!state.has_value() || !state->RollsOver || state->Machine != SelfMachine)
        {
            LoseSession(result == DP_OK ? "the machine taking over sent what it can't read"
                                        : "the machine taking over turned it away");
            return;
        }

        HubMachine = Roll->Successor;
        Key = Roll->NextKey;
        Roll.reset();
        SDL_Log("MCDirectPlay: came back to \"%s\" under machine %u", Session.Name.c_str(), HubMachine);
        Reconcile(std::move(state->Session), std::move(state->Entities));
    }

    /// <summary>
    /// Port-only (Redux HD): the session becomes <paramref name="session"/> and <paramref name="entities"/>, a new hub's,
    /// and the game hears of each change as DirectPlay's messages: what went with the old hub (its players, out of their
    /// groups first) destroyed, what came meanwhile created, the groups' members and data and the description as now.
    /// </summary>
    void Reconcile(SessionInfo session, std::map<uint32_t, Entity> entities)
    {
        std::map<uint32_t, Entity> was = std::move(Entities);

        for (const uint32_t type : {uint32_t{DPPLAYERTYPE_PLAYER}, uint32_t{DPPLAYERTYPE_GROUP}})
        {
            for (const auto& [id, entity] : was)
            {
                if (entity.Type != type || entities.contains(id))
                {
                    continue;
                }

                for (const auto& [groupID, group] : was)
                {
                    if (group.Type == DPPLAYERTYPE_GROUP && std::ranges::contains(group.Members, id))
                    {
                        QueueMembership(DPSYS_DELETEPLAYERFROMGROUP, groupID, id);
                    }
                }

                QueueDestroyed(entity);
            }
        }

        for (const auto& [id, entity] : entities)
        {
            const auto before = was.find(id);

            if (before == was.end())
            {
                if (!LocalPlayers.contains(id))
                {
                    QueueCreated(entity);
                }
            }
            else if (entity.Data != before->second.Data)
            {
                Incoming message;
                message.SystemType = DPSYS_SETPLAYERORGROUPDATA;
                message.Subject = entity;
                QueueSystem(std::move(message));
            }
        }

        for (const auto& [id, group] : entities)
        {
            if (group.Type != DPPLAYERTYPE_GROUP)
            {
                continue;
            }

            const auto before = was.find(id);
            const std::vector<uint32_t> none;
            const std::vector<uint32_t>& members = before != was.end() ? before->second.Members : none;

            for (const uint32_t member : group.Members)
            {
                if (!std::ranges::contains(members, member))
                {
                    QueueMembership(DPSYS_ADDPLAYERTOGROUP, id, member);
                }
            }

            for (const uint32_t member : members)
            {
                if (!std::ranges::contains(group.Members, member) && entities.contains(member))
                {
                    QueueMembership(DPSYS_DELETEPLAYERFROMGROUP, id, member);
                }
            }
        }

        std::erase_if(LocalPlayers, [&](uint32_t id) { return !entities.contains(id); });
        Entities = std::move(entities);
        // The count of players aside, which the game reads from its own list.
        Writer before;
        Writer after;
        Session.Desc.dwCurrentPlayers = session.Desc.dwCurrentPlayers;
        Session.Write(before, false);
        session.Write(after, false);
        Session = std::move(session);

        if (before.Bytes != after.Bytes)
        {
            Incoming message;
            message.SystemType = DPSYS_SETSESSIONDESC;
            message.Session = Session;
            QueueSystem(std::move(message));
        }
    }

    /// <summary>
    /// Port-only (Redux HD), the joiner the session rolls over to: it becomes the hub, the session as it stands (the
    /// same instance, players, groups and ids, numbered on from the highest), with a slot kept for each machine to come
    /// back to; machine <paramref name="gone"/>'s players leave, and an older build's (which never comes back), and
    /// this machine's game is told it hosts (DPSYS_HOST).
    /// </summary>
    void TakeOver(uint32_t gone)
    {
        SDL_Log("MCDirectPlay: the host of \"%s\" has gone; this machine (%u) takes over", Session.Name.c_str(),
                SelfMachine);
        State = Mode::Host;
        HubMachine = SelfMachine;
        // The LAN's searches for the session are answered on its port, when it's free here.
        MCSocket::Close(Udp);
        Udp = MCSocket::OpenUdp(Port);
        uint32_t highest = 0;

        for (const auto& [id, entity] : Entities)
        {
            highest = std::max(highest, id);
        }

        NextId = highest + 1;

        for (const Machine& machine : Machines)
        {
            NextMachine = std::max(NextMachine, machine.Id + 1);

            if (machine.Id != SelfMachine && machine.Version >= RolloverVersion)
            {
                Link& slot = Joiners.emplace_back();
                slot.Machine = machine.Id;
                slot.ResumeBy = NowMs() + RolloverWaitMs;
            }
        }

        std::vector<uint32_t> leaving;

        for (auto& [id, entity] : Entities)
        {
            const auto slot = std::ranges::find_if(
                Joiners, [&](const Link& kept) { return kept.ResumeBy.has_value() && kept.Machine == entity.Machine; });
            entity.Owner = slot != Joiners.end() ? static_cast<int>(slot - Joiners.begin()) : -1;

            if (entity.Type == DPPLAYERTYPE_GROUP)
            {
                // Groups stay, the old hub's this one's (the game's teams are the host's).
                if (entity.Owner < 0)
                {
                    entity.Machine = SelfMachine;
                }

                continue;
            }

            if (entity.Machine == SelfMachine)
            {
                entity.Flags |= DPPLAYER_SERVERPLAYER;
            }
            else if (entity.Owner < 0)
            {
                // The old hub's (its game hears the hub's player left, flagged as the hub's), or an older build's.
                leaving.push_back(id);
            }
            else
            {
                entity.Flags &= ~static_cast<uint32_t>(DPPLAYER_SERVERPLAYER);
            }
        }

        std::erase_if(Machines, [&](const Machine& machine)
                      { return machine.Id != SelfMachine && machine.Version < RolloverVersion; });

        for (const uint32_t id : leaving)
        {
            HostDestroyPlayer(id);
        }

        Incoming host;
        host.SystemType = DPSYS_HOST;
        QueueSystem(std::move(host));
    }

    /// <summary>Joiner: pumps until the host answers with <paramref name="type"/> (and the token, for creates).</summary>
    std::optional<std::vector<uint8_t>> WaitForReply(FrameType type, std::optional<uint32_t> token)
    {
        const uint64_t deadline = NowMs() + ReplyTimeoutMs;

        for (;;)
        {
            for (auto it = Replies.begin(); it != Replies.end(); ++it)
            {
                if (it->first != type)
                {
                    continue;
                }

                if (token)
                {
                    Reader peek(it->second);

                    if (peek.U32() != *token)
                    {
                        continue;
                    }
                }

                std::vector<uint8_t> reply = std::move(it->second);
                Replies.erase(it);
                return reply;
            }

            if (Lost || Server.Dead || NowMs() > deadline)
            {
                return std::nullopt;
            }

            JoinerPump();
            SDL_Delay(1);
        }
    }

    // ---- session search ------------------------------------------------------------------------------------------

    void ReadAnswers()
    {
        if (Udp == MCSocket::InvalidHandle)
        {
            return;
        }

        std::array<uint8_t, 1500> datagram{};
        MCSocket::Address from;

        for (;;)
        {
            const int size = MCSocket::ReceiveFrom(Udp, datagram, from);

            if (size <= 0)
            {
                break;
            }

            Reader in(std::span<const uint8_t>(datagram).first(static_cast<size_t>(size)));
            const uint32_t magic = in.U32();
            const uint8_t kind = in.U8();

            if (magic != ProtocolMagic || kind != DatagramReply)
            {
                continue;
            }

            FoundSession found;
            found.Address = MCSocket::Address{from.Ip, in.U16()};

            // With a host typed in, only that host's answer counts (issue #25: anyone could list a made-up session in
            // its place). A LAN search takes any answer; broadcast discovery can't tell them apart.
            if (!HostName.empty() && (!QueriedIp.has_value() || from.Ip != *QueriedIp || found.Address.Port != Port))
            {
                continue;
            }

            found.Info.Read(in);
            const uint32_t count = in.U32();

            for (uint32_t i = 0; i < count && in.Ok; i++)
            {
                Entity player;
                player.Id = in.U32();
                player.ShortName = in.String();
                player.LongName = in.String();
                found.Players.push_back(std::move(player));
            }

            if (!in.Ok)
            {
                continue;
            }

            found.SeenAt = NowMs();
            const auto same = std::ranges::find_if(
                Found, [&](const FoundSession& session)
                { return MCSameGuid(session.Info.Desc.guidInstance, found.Info.Desc.guidInstance); });

            if (same != Found.end())
            {
                *same = std::move(found);
            }
            else
            {
                Found.push_back(std::move(found));
            }
        }
    }

    // ---- teardown ------------------------------------------------------------------------------------------------

    void Shutdown()
    {
        for (Link& link : Joiners)
        {
            MCSocket::Close(link.Socket);
        }

        Joiners.clear();
        MCSocket::Close(Server.Socket);
        Server = Link{};
        MCSocket::Close(Listener);
        Listener = MCSocket::InvalidHandle;

        if (State == Mode::Host)
        {
            // The host's UDP socket answered queries on the session's port; a searcher opens its own.
            MCSocket::Close(Udp);
            Udp = MCSocket::InvalidHandle;
        }

        State = Mode::None;
        Lost = false;
        Session = SessionInfo{};
        Entities.clear();
        LocalPlayers.clear();
        Inbox.clear();
        Replies.clear();
        Machines.clear();
        SelfMachine = 0;
        HubMachine = 0;
        NextMachine = 1;
        RollsOver = false;
        Ending = false;
        ListenPort = 0;
        Roll.reset();
        ServerAddress = MCSocket::Address{};
    }
};

MCDirectPlay::MCDirectPlay() : _Impl(std::make_unique<Impl>())
{
    MCSocket::Startup();
}

MCDirectPlay::~MCDirectPlay() = default;

std::vector<uint8_t> MCDirectPlay::CreateCompoundAddress(std::span<const DPCOMPOUNDADDRESSELEMENT> elements)
{
    ConnectionData data{};

    for (const DPCOMPOUNDADDRESSELEMENT& element : elements)
    {
        if (MCSameGuid(element.guidDataType, DPAID_ServiceProvider) && element.Data.size() >= sizeof(_GUID))
        {
            std::memcpy(&data.serviceProvider, element.Data.data(), sizeof(_GUID));
        }
        else if (MCSameGuid(element.guidDataType, DPAID_INet))
        {
            // The host's name, up to its terminator.
            const auto end = std::ranges::find(element.Data, uint8_t{0});
            const size_t length =
                std::min<size_t>(static_cast<size_t>(end - element.Data.begin()), data.address.size() - 1);
            std::memcpy(data.address.data(), element.Data.data(), length);
        }
    }

    std::vector<uint8_t> address(sizeof(ConnectionData));
    std::memcpy(address.data(), &data, sizeof(ConnectionData));
    return address;
}

uint32_t MCDirectPlay::EnumConnections(const MCEnumConnectionsCallback& callback, uint32_t)
{
    if (!callback)
    {
        return DPERR_INVALIDPARAMS;
    }

    struct Offer
    {
        _GUID Provider;
        std::string_view Name;
    };

    // The port offers its one transport twice: as TCP/IP (a typed-in host, or the LAN) and as IPX (the LAN).
    static const Offer offers[] = {{DPSPGUID_TCPIP, "Internet TCP/IP Connection For DirectPlay"},
                                   {DPSPGUID_IPX, "IPX Connection For DirectPlay"}};

    for (const Offer& offer : offers)
    {
        ConnectionData data{};
        data.serviceProvider = offer.Provider;
        const DPNAME name{std::string(offer.Name), std::string(offer.Name)};

        if (!callback(offer.Provider, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data), sizeof(data)),
                      name, 0))
        {
            break;
        }
    }

    return DP_OK;
}

uint32_t MCDirectPlay::InitializeConnection(std::span<const uint8_t> connection, uint32_t)
{
    if (connection.size() < sizeof(ConnectionData))
    {
        return DPERR_INVALIDPARAMS;
    }

    if (_Impl->Initialized)
    {
        return DPERR_ALREADYINITIALIZED;
    }

    ConnectionData data;
    std::memcpy(&data, connection.data(), sizeof(data));
    data.address.back() = '\0';

    if (!MCSameGuid(data.serviceProvider, DPSPGUID_TCPIP) && !MCSameGuid(data.serviceProvider, DPSPGUID_IPX))
    {
        return DPERR_UNSUPPORTED;
    }

    const auto [host, port] = SplitAddress(data.address.data());
    _Impl->HostName = host;
    _Impl->Port = port;
    _Impl->Initialized = true;
    return DP_OK;
}

uint32_t MCDirectPlay::EnumSessions(const DPSESSIONDESC2& desc, uint32_t, const MCEnumSessionsCallback& callback,
                                    uint32_t flags)
{
    Impl& impl = *_Impl;

    if (!impl.Initialized)
    {
        return DPERR_UNINITIALIZED;
    }

    if (!callback)
    {
        return DPERR_INVALIDPARAMS;
    }

    if (impl.Udp == MCSocket::InvalidHandle)
    {
        impl.Udp = MCSocket::OpenUdp(0);
    }

    if (impl.Udp != MCSocket::InvalidHandle && impl.State != Impl::Mode::Host)
    {
        Writer query;
        query.U32(ProtocolMagic);
        query.U8(DatagramQuery);
        query.Guid(desc.guidApplication);

        if (!impl.HostName.empty())
        {
            impl.QueriedIp = MCSocket::Resolve(impl.HostName);

            if (impl.QueriedIp.has_value())
            {
                MCSocket::SendTo(impl.Udp, {*impl.QueriedIp, impl.Port}, query.Bytes);
            }
        }
        else
        {
            MCSocket::SendTo(impl.Udp, {MCSocket::BroadcastIp, impl.Port}, query.Bytes);
            MCSocket::SendTo(impl.Udp, {MCSocket::LoopbackIp, impl.Port}, query.Bytes);
        }

        const uint64_t until = NowMs() + EnumListenMs;

        while (NowMs() < until)
        {
            impl.ReadAnswers();
            SDL_Delay(5);
        }

        impl.ReadAnswers();
    }

    const uint64_t now = NowMs();
    std::erase_if(impl.Found, [&](const FoundSession& found) { return now - found.SeenAt > SessionExpiryMs; });

    for (const FoundSession& found : impl.Found)
    {
        const DPSESSIONDESC2& info = found.Info.Desc;

        if (!MCSameGuid(info.guidApplication, desc.guidApplication))
        {
            continue;
        }

        if ((flags & DPENUMSESSIONS_AVAILABLE) != 0 &&
            ((info.dwFlags & (DPSESSION_NEWPLAYERSDISABLED | DPSESSION_JOINDISABLED)) != 0 ||
             info.dwCurrentPlayers >= info.dwMaxPlayers))
        {
            continue;
        }

        if ((info.dwFlags & DPSESSION_PASSWORDREQUIRED) != 0 && (flags & DPENUMSESSIONS_PASSWORDREQUIRED) == 0)
        {
            continue;
        }

        if (!callback(found.Info.View(), 0))
        {
            return DP_OK;
        }
    }

    callback(DPSESSIONDESC2{}, DPESC_TIMEDOUT);
    return DP_OK;
}

uint32_t MCDirectPlay::Open(const DPSESSIONDESC2& desc, uint32_t flags)
{
    Impl& impl = *_Impl;

    if (!impl.Initialized)
    {
        return DPERR_UNINITIALIZED;
    }

    if (impl.State != Impl::Mode::None)
    {
        return DPERR_INVALIDPARAMS;
    }

    if ((flags & DPOPEN_CREATE) != 0)
    {
        impl.Listener = MCSocket::ListenTcp(impl.Port);
        MCSocket::Close(impl.Udp);
        impl.Udp = MCSocket::OpenUdp(impl.Port);

        if (impl.Listener == MCSocket::InvalidHandle || impl.Udp == MCSocket::InvalidHandle)
        {
            SDL_Log("MCDirectPlay: cannot host on port %u", impl.Port);
            impl.State = Impl::Mode::Host;
            impl.Shutdown();
            return DPERR_CANTCREATESESSION;
        }

        impl.Session.Set(desc);
        impl.Session.Desc.guidInstance = NewGuid();

        if (impl.Session.Desc.dwMaxPlayers == 0)
        {
            impl.Session.Desc.dwMaxPlayers = 0xffffffffu;
        }

        // Ids start somewhere unremarkable, as DirectPlay's did; 0 (everyone) and 1 (the server player) are never
        // given out.
        impl.NextId = 0x00010000u + (NewGuid().Data1 & 0x0fff0000u);
        impl.State = Impl::Mode::Host;
        // Port-only (Redux HD): the host is the session's first machine (the rollover's).
        impl.ListenPort = impl.Port;
        impl.Key = NewKey();
        impl.Machines = {Machine{impl.SelfMachine, RolloverVersion, MCSocket::Address{}, MCSha256::Of(impl.Key)}};
        SDL_Log("MCDirectPlay: hosting \"%s\" on port %u", impl.Session.Name.c_str(), impl.Port);
        return DP_OK;
    }

    if ((flags & DPOPEN_JOIN) == 0)
    {
        return DPERR_INVALIDPARAMS;
    }

    const auto found = std::ranges::find_if(impl.Found, [&](const FoundSession& session)
                                            { return MCSameGuid(session.Info.Desc.guidInstance, desc.guidInstance); });

    if (found == impl.Found.end())
    {
        return DPERR_NOSESSIONS;
    }

    impl.Server = Link{};
    impl.Server.Socket = MCSocket::ConnectTcp(found->Address, ConnectTimeoutMs);

    if (impl.Server.Socket == MCSocket::InvalidHandle)
    {
        SDL_Log("MCDirectPlay: cannot reach %s", MCSocket::ToString(found->Address).c_str());
        return DPERR_NOCONNECTION;
    }

    Impl::KeepAliveIfAsked(found->Info.Desc, impl.Server.Socket);
    impl.ServerAddress = found->Address;

    // Port-only (Redux HD): a session that can migrate its host rolls over to a joiner, which takes connections from
    // its join on a port of its own (so a firewall asks as it joins, never mid-battle).
    if ((found->Info.Desc.dwFlags & DPSESSION_MIGRATEHOST) != 0)
    {
        impl.Listener = MCSocket::ListenTcp(0);
        impl.ListenPort = impl.Listener != MCSocket::InvalidHandle ? MCSocket::LocalPort(impl.Listener) : uint16_t{0};
    }

    impl.Key = NewKey();
    impl.State = Impl::Mode::Joiner;
    Writer request;
    request.U32(ProtocolMagic);
    request.Guid(desc.guidApplication);
    request.Guid(desc.guidInstance);
    request.String(desc.Password.value_or(""));
    request.U32(RolloverVersion);
    request.U16(impl.ListenPort);
    request.Blob(MCSha256::Of(impl.Key));
    Impl::Queue(impl.Server, FrameType::JoinRequest, request);
    std::optional<std::vector<uint8_t>> reply = impl.WaitForReply(FrameType::JoinReply, std::nullopt);
    uint32_t result = DPERR_TIMEOUT;

    if (reply)
    {
        Reader in(*reply);
        result = in.U32();

        if (result == DP_OK)
        {
            std::optional<Impl::SessionState> state = Impl::ReadSessionState(in);

            if (state.has_value())
            {
                impl.Session = std::move(state->Session);

                // Merged: what the host announced as its answer came is kept.
                for (auto& [id, entity] : state->Entities)
                {
                    impl.Entities.insert_or_assign(id, std::move(entity));
                }

                impl.RollsOver = state->RollsOver;
                impl.SelfMachine = state->Machine;
            }
            else
            {
                result = DPERR_GENERIC;
            }
        }
    }

    if (result != DP_OK)
    {
        impl.Shutdown();
        return result;
    }

    SDL_Log("MCDirectPlay: joined \"%s\" at %s", impl.Session.Name.c_str(), MCSocket::ToString(found->Address).c_str());
    return DP_OK;
}

uint32_t MCDirectPlay::Close()
{
    if (_Impl->State == Impl::Mode::None)
    {
        return DPERR_NOSESSIONS;
    }

    // Port-only (Redux HD): the host's joiners are told the session ends with it, so none takes it over.
    if (_Impl->State == Impl::Mode::Host)
    {
        _Impl->Broadcast(FrameType::Ending, Writer{}, -1);
    }

    _Impl->Shutdown();
    return DP_OK;
}

uint32_t MCDirectPlay::Leave()
{
    if (_Impl->State == Impl::Mode::None)
    {
        return DPERR_NOSESSIONS;
    }

    _Impl->Shutdown();
    return DP_OK;
}

bool MCDirectPlay::RollingOver() const
{
    return _Impl->Roll.has_value();
}

std::string MCDirectPlay::LastRefusal() const
{
    return _Impl->Refusal;
}

void MCDirectPlay::SetAdmission(Admission admission)
{
    _Impl->Admit = std::move(admission);
}

uint32_t MCDirectPlay::CreatePlayer(uint32_t& playerID, const DPNAME& name, std::span<const uint8_t> data,
                                    uint32_t flags)
{
    Impl& impl = *_Impl;
    Entity entity;
    entity.Type = DPPLAYERTYPE_PLAYER;
    entity.Flags = flags;
    entity.SetName(name);
    entity.Data.assign(data.begin(), data.end());

    if (impl.State == Impl::Mode::Host)
    {
        if (impl.PlayerCount() >= impl.Session.Desc.dwMaxPlayers)
        {
            return DPERR_CANTCREATEPLAYER;
        }

        playerID = impl.HostCreate(std::move(entity), -1);
        impl.LocalPlayers.insert(playerID);
        return DP_OK;
    }

    if (impl.State != Impl::Mode::Joiner || impl.Lost)
    {
        return DPERR_INVALIDOBJECT;
    }

    const uint32_t token = impl.NextToken++;
    Writer request;
    request.U32(token);
    entity.Write(request);
    Impl::Queue(impl.Server, FrameType::CreatePlayer, request);
    impl.Refusal.clear();
    std::optional<std::vector<uint8_t>> reply = impl.WaitForReply(FrameType::CreateReply, token);

    if (!reply)
    {
        return DPERR_TIMEOUT;
    }

    Reader in(*reply);
    in.U32();
    const uint32_t result = in.U32();
    const uint32_t id = in.U32();

    if (result != DP_OK)
    {
        // Port: the host's reason, if it gave one (an older host's reply ends here).
        if (std::string refusal = in.String(); in.Ok)
        {
            impl.Refusal = std::move(refusal);
        }

        return result;
    }

    entity.Id = id;
    entity.Machine = impl.SelfMachine;
    impl.Entities[id] = std::move(entity);
    impl.LocalPlayers.insert(id);
    playerID = id;
    return DP_OK;
}

uint32_t MCDirectPlay::CreateGroup(uint32_t& groupID, const DPNAME& name, std::span<const uint8_t> data, uint32_t flags)
{
    Impl& impl = *_Impl;
    Entity entity;
    entity.Type = DPPLAYERTYPE_GROUP;
    entity.Flags = flags;
    entity.SetName(name);
    entity.Data.assign(data.begin(), data.end());

    if (impl.State == Impl::Mode::Host)
    {
        groupID = impl.HostCreate(std::move(entity), -1);
        return DP_OK;
    }

    if (impl.State != Impl::Mode::Joiner || impl.Lost)
    {
        return DPERR_INVALIDOBJECT;
    }

    const uint32_t token = impl.NextToken++;
    Writer request;
    request.U32(token);
    entity.Write(request);
    Impl::Queue(impl.Server, FrameType::CreateGroup, request);
    std::optional<std::vector<uint8_t>> reply = impl.WaitForReply(FrameType::CreateReply, token);

    if (!reply)
    {
        return DPERR_TIMEOUT;
    }

    Reader in(*reply);
    in.U32();
    const uint32_t result = in.U32();
    const uint32_t id = in.U32();

    if (result != DP_OK)
    {
        return DPERR_CANTCREATEGROUP;
    }

    entity.Id = id;
    entity.Machine = impl.SelfMachine;
    impl.Entities[id] = std::move(entity);
    groupID = id;
    return DP_OK;
}

uint32_t MCDirectPlay::AddPlayerToGroup(uint32_t groupID, uint32_t playerID)
{
    Impl& impl = *_Impl;

    if (!impl.Entities.contains(groupID))
    {
        return DPERR_INVALIDGROUP;
    }

    if (!impl.Entities.contains(playerID))
    {
        return DPERR_INVALIDPLAYER;
    }

    if (impl.State == Impl::Mode::Host)
    {
        impl.HostMembership(true, groupID, playerID, -1);
        return DP_OK;
    }

    if (impl.State != Impl::Mode::Joiner || impl.Lost)
    {
        return DPERR_INVALIDOBJECT;
    }

    impl.ApplyMembership(true, groupID, playerID);
    Writer request;
    request.U32(groupID);
    request.U32(playerID);
    Impl::Queue(impl.Server, FrameType::AddToGroup, request);
    return DP_OK;
}

uint32_t MCDirectPlay::DeletePlayerFromGroup(uint32_t groupID, uint32_t playerID)
{
    Impl& impl = *_Impl;

    if (!impl.Entities.contains(groupID))
    {
        return DPERR_INVALIDGROUP;
    }

    if (!impl.Entities.contains(playerID))
    {
        return DPERR_INVALIDPLAYER;
    }

    if (impl.State == Impl::Mode::Host)
    {
        impl.HostMembership(false, groupID, playerID, -1);
        return DP_OK;
    }

    if (impl.State != Impl::Mode::Joiner || impl.Lost)
    {
        return DPERR_INVALIDOBJECT;
    }

    impl.ApplyMembership(false, groupID, playerID);
    Writer request;
    request.U32(groupID);
    request.U32(playerID);
    Impl::Queue(impl.Server, FrameType::DeleteFromGroup, request);
    return DP_OK;
}

uint32_t MCDirectPlay::SetGroupData(uint32_t groupID, std::span<const uint8_t> data, uint32_t)
{
    Impl& impl = *_Impl;
    const auto found = impl.Entities.find(groupID);

    if (found == impl.Entities.end())
    {
        return DPERR_INVALIDGROUP;
    }

    std::vector<uint8_t> bytes(data.begin(), data.end());

    if (impl.State == Impl::Mode::Host)
    {
        impl.HostSetGroupData(groupID, std::move(bytes), -1);
        return DP_OK;
    }

    if (impl.State != Impl::Mode::Joiner || impl.Lost)
    {
        return DPERR_INVALIDOBJECT;
    }

    found->second.Data = bytes;
    Writer request;
    request.U32(groupID);
    request.Blob(bytes);
    Impl::Queue(impl.Server, FrameType::SetGroupData, request);
    return DP_OK;
}

uint32_t MCDirectPlay::SetSessionDesc(const DPSESSIONDESC2& desc, uint32_t)
{
    Impl& impl = *_Impl;

    if (impl.State != Impl::Mode::Host)
    {
        return impl.State == Impl::Mode::None ? DPERR_NOSESSIONS : DPERR_ACCESSDENIED;
    }

    const _GUID instance = impl.Session.Desc.guidInstance;
    const _GUID application = impl.Session.Desc.guidApplication;
    const std::string password = impl.Session.Password;
    impl.Session.Set(desc);
    impl.Session.Desc.guidInstance = instance;
    impl.Session.Desc.guidApplication = application;

    if (!desc.Password)
    {
        impl.Session.Password = password;
    }

    impl.Session.Desc.dwCurrentPlayers = impl.PlayerCount();
    Writer note;
    impl.Session.Write(note, false);
    impl.Broadcast(FrameType::SessionDesc, note, -1);
    return DP_OK;
}

uint32_t MCDirectPlay::EnumPlayers(const _GUID& session, const MCEnumPlayersCallback& callback, uint32_t)
{
    Impl& impl = *_Impl;

    if (!callback)
    {
        return DPERR_INVALIDPARAMS;
    }

    const auto found = std::ranges::find_if(impl.Found, [&](const FoundSession& candidate)
                                            { return MCSameGuid(candidate.Info.Desc.guidInstance, session); });

    if (found == impl.Found.end())
    {
        return DPERR_NOSESSIONS;
    }

    for (const Entity& player : found->Players)
    {
        if (!callback(player.Id, DPPLAYERTYPE_PLAYER, player.Name(), 0))
        {
            break;
        }
    }

    return DP_OK;
}

uint32_t MCDirectPlay::EnumPlayers(const MCEnumPlayersCallback& callback, uint32_t)
{
    Impl& impl = *_Impl;

    if (!callback)
    {
        return DPERR_INVALIDPARAMS;
    }

    if (impl.State == Impl::Mode::None)
    {
        return DPERR_NOSESSIONS;
    }

    // Copied first: a callback may change the session.
    std::vector<Entity> players;

    for (const auto& [id, entity] : impl.Entities)
    {
        if (entity.Type == DPPLAYERTYPE_PLAYER)
        {
            players.push_back(entity);
        }
    }

    for (const Entity& player : players)
    {
        if (!callback(player.Id, DPPLAYERTYPE_PLAYER, player.Name(), player.Flags))
        {
            break;
        }
    }

    return DP_OK;
}

uint32_t MCDirectPlay::EnumGroups(const MCEnumPlayersCallback& callback, uint32_t)
{
    Impl& impl = *_Impl;

    if (!callback)
    {
        return DPERR_INVALIDPARAMS;
    }

    if (impl.State == Impl::Mode::None)
    {
        return DPERR_NOSESSIONS;
    }

    std::vector<Entity> groups;

    for (const auto& [id, entity] : impl.Entities)
    {
        if (entity.Type == DPPLAYERTYPE_GROUP)
        {
            groups.push_back(entity);
        }
    }

    for (const Entity& group : groups)
    {
        if (!callback(group.Id, DPPLAYERTYPE_GROUP, group.Name(), group.Flags))
        {
            break;
        }
    }

    return DP_OK;
}

uint32_t MCDirectPlay::Send(uint32_t fromID, uint32_t toID, uint32_t, std::span<const uint8_t> data)
{
    Impl& impl = *_Impl;

    if (impl.State == Impl::Mode::None)
    {
        return DPERR_NOSESSIONS;
    }

    if (impl.Lost)
    {
        return DPERR_SESSIONLOST;
    }

    if (!impl.LocalPlayers.contains(fromID))
    {
        return DPERR_INVALIDPLAYER;
    }

    if (toID != DPID_ALLPLAYERS && !impl.Entities.contains(toID))
    {
        return DPERR_INVALIDPLAYER;
    }

    if (impl.State == Impl::Mode::Host)
    {
        impl.Route(fromID, toID, data);
        return DP_OK;
    }

    Writer frame;
    frame.U32(fromID);
    frame.U32(toID);
    frame.Blob(data);
    Impl::Queue(impl.Server, FrameType::Data, frame);
    return DP_OK;
}

namespace
{
    /// <summary>A session's description as a system message carries it (its strings go after the message).</summary>
    DPMSG_SESSIONDESC MessageSessionDesc(const DPSESSIONDESC2& desc)
    {
        DPMSG_SESSIONDESC fields{};
        fields.dwSize = sizeof(DPMSG_SESSIONDESC);
        fields.dwFlags = desc.dwFlags;
        fields.guidInstance = desc.guidInstance;
        fields.guidApplication = desc.guidApplication;
        fields.dwMaxPlayers = desc.dwMaxPlayers;
        fields.dwCurrentPlayers = desc.dwCurrentPlayers;
        fields.dwReserved1 = desc.dwReserved1;
        fields.dwReserved2 = desc.dwReserved2;
        fields.dwUser1 = desc.dwUser1;
        fields.dwUser2 = desc.dwUser2;
        fields.dwUser3 = desc.dwUser3;
        fields.dwUser4 = desc.dwUser4;
        return fields;
    }
}

uint32_t MCDirectPlay::Receive(uint32_t& fromID, uint32_t& toID, uint32_t, std::span<uint8_t> data, uint32_t& size)
{
    Impl& impl = *_Impl;

    if (impl.Inbox.empty())
    {
        Pump();
    }

    if (impl.Inbox.empty())
    {
        return DPERR_NOMESSAGES;
    }

    Incoming& message = impl.Inbox.front();

    if (message.SystemType == 0)
    {
        const uint32_t needed = static_cast<uint32_t>(message.Payload.size());

        if (data.size() < needed)
        {
            size = needed;
            return DPERR_BUFFERTOOSMALL;
        }

        std::ranges::copy(message.Payload, data.begin());
        size = needed;
        fromID = message.From;
        toID = message.To;
        impl.Inbox.pop_front();
        return DP_OK;
    }

    // A system message: the DPMSG_* structure first, the strings and data it names after it, all inside the caller's
    // buffer (as DirectPlay laid them out), with offsets from the message's start where DirectPlay had pointers.
    std::vector<uint8_t> image;
    const Entity& subject = message.Subject;

    const auto start = [&](const auto& msg)
    {
        image.resize(sizeof(msg));
        std::memcpy(image.data(), &msg, sizeof(msg));
    };

    // Appends bytes (a string null-terminated) and writes their offset into the field at fieldOffset.
    const auto appendTail = [&](size_t fieldOffset, std::span<const uint8_t> bytes, bool terminate)
    {
        const auto at = static_cast<uint32_t>(image.size());
        image.insert(image.end(), bytes.begin(), bytes.end());

        if (terminate)
        {
            image.push_back(0);
        }

        std::memcpy(image.data() + fieldOffset, &at, sizeof(at));
    };

    const auto text = [](std::string_view value)
    { return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(value.data()), value.size()); };

    switch (message.SystemType)
    {
        case DPSYS_CREATEPLAYERORGROUP:
        {
            DPMSG_CREATEPLAYERORGROUP msg{};
            msg.dwType = message.SystemType;
            msg.dwPlayerType = subject.Type;
            msg.dpId = subject.Id;
            msg.dwCurrentPlayers = impl.PlayerCount();
            msg.dwDataSize = static_cast<uint32_t>(subject.Data.size());
            msg.dpnName.dwSize = sizeof(DPMSG_NAME);
            msg.dpIdParent = subject.Parent;
            msg.dwFlags = subject.Flags;
            start(msg);
            appendTail(offsetof(DPMSG_CREATEPLAYERORGROUP, dpnName.dwShortNameOffset), text(subject.ShortName), true);
            appendTail(offsetof(DPMSG_CREATEPLAYERORGROUP, dpnName.dwLongNameOffset), text(subject.LongName), true);

            if (!subject.Data.empty())
            {
                appendTail(offsetof(DPMSG_CREATEPLAYERORGROUP, dwDataOffset), subject.Data, false);
            }

            break;
        }

        case DPSYS_DESTROYPLAYERORGROUP:
        {
            DPMSG_DESTROYPLAYERORGROUP msg{};
            msg.dwType = message.SystemType;
            msg.dwPlayerType = subject.Type;
            msg.dpId = subject.Id;
            msg.dpnName.dwSize = sizeof(DPMSG_NAME);
            msg.dpIdParent = subject.Parent;
            msg.dwFlags = subject.Flags;
            start(msg);
            appendTail(offsetof(DPMSG_DESTROYPLAYERORGROUP, dpnName.dwShortNameOffset), text(subject.ShortName), true);
            appendTail(offsetof(DPMSG_DESTROYPLAYERORGROUP, dpnName.dwLongNameOffset), text(subject.LongName), true);
            break;
        }

        case DPSYS_ADDPLAYERTOGROUP:
        case DPSYS_DELETEPLAYERFROMGROUP:
        {
            start(DPMSG_ADDPLAYERTOGROUP{message.SystemType, message.Group, message.Player});
            break;
        }

        case DPSYS_SETPLAYERORGROUPDATA:
        {
            DPMSG_SETPLAYERORGROUPDATA msg{};
            msg.dwType = message.SystemType;
            msg.dwPlayerType = subject.Type;
            msg.dpId = subject.Id;
            msg.dwDataSize = static_cast<uint32_t>(subject.Data.size());
            start(msg);

            if (!subject.Data.empty())
            {
                appendTail(offsetof(DPMSG_SETPLAYERORGROUPDATA, dwDataOffset), subject.Data, false);
            }

            break;
        }

        case DPSYS_SETSESSIONDESC:
        {
            DPMSG_SETSESSIONDESC msg{};
            msg.dwType = message.SystemType;
            msg.dpDesc = MessageSessionDesc(message.Session.Desc);
            start(msg);
            appendTail(offsetof(DPMSG_SETSESSIONDESC, dpDesc.dwSessionNameOffset), text(message.Session.Name), true);
            break;
        }

        default:
        {
            start(DPMSG_GENERIC{message.SystemType});
            break;
        }
    }

    const uint32_t needed = static_cast<uint32_t>(image.size());

    if (data.size() < needed)
    {
        size = needed;
        return DPERR_BUFFERTOOSMALL;
    }

    std::ranges::copy(image, data.begin());
    size = needed;
    fromID = DPID_SYSMSG;
    toID = message.To;
    impl.Inbox.pop_front();
    return DP_OK;
}

std::optional<std::string> MCDirectPlayMessageString(std::span<const uint8_t> message, uint32_t offset)
{
    if (offset == 0 || offset >= message.size())
    {
        return std::nullopt;
    }

    const std::span<const uint8_t> tail = message.subspan(offset);
    const auto end = std::ranges::find(tail, uint8_t{0});
    return std::string(tail.begin(), end);
}

DPNAME MCDirectPlayMessageName(std::span<const uint8_t> message, const DPMSG_NAME& name)
{
    return DPNAME{MCDirectPlayMessageString(message, name.dwShortNameOffset),
                  MCDirectPlayMessageString(message, name.dwLongNameOffset)};
}

void MCDirectPlay::Pump()
{
    switch (_Impl->State)
    {
        case Impl::Mode::Host:
        {
            _Impl->HostPump();
            break;
        }

        case Impl::Mode::Joiner:
        {
            _Impl->JoinerPump();
            break;
        }

        default:
        {
            _Impl->ReadAnswers();
            break;
        }
    }
}

bool MCDirectPlay::IsSessionHostPlayer(uint32_t playerID) const
{
    const auto found = _Impl->Entities.find(playerID);
    return found != _Impl->Entities.end() && found->second.Type == DPPLAYERTYPE_PLAYER &&
           (found->second.Flags & DPPLAYER_SERVERPLAYER) != 0;
}
