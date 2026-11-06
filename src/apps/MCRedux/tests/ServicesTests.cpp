#include "stdafx.h"
#include "MCMock.h"
#include "MCTest.h"
#include "TestGame.h"
#include "fakes/MCLoopbackTransport.h"
#include "fakes/MCManualClock.h"
#include "fakes/MCMemoryFileSource.h"
#include "fakes/MCNullAudioDevice.h"
#include "fakes/MCScriptedRandom.h"
#include "fixtures/MCRetailData.h"
#include "fixtures/MCTinyMap.h"
#include "gameos/MCSoundRenderer.h"
#include "gameos/MCSoundResource.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCVector2D.h"
#include "lib/MCDice.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "main/MCGameContext.h"
#include "platform/MCFileSystem.h"

// The port services (platform/MCServices.h), the context that hands them out (main/MCGameContext.h), and the fakes,
// mock helper and fixtures the tests build on.

namespace
{
    /// <summary>
    /// How long a test waits for a connection to reach its listener's queue, and how often it looks: a connecting end
    /// returns once its handshake is answered, and the connection is queued for the listener a moment later (longer on a
    /// loaded machine), so an accept straight after a connect may find nothing yet.
    /// </summary>
    constexpr auto AcceptWait = std::chrono::seconds(5);
    constexpr auto AcceptLook = std::chrono::milliseconds(1);

    /// <summary>
    /// The connection <paramref name="accept"/> (a non-blocking accept) takes within <see cref="AcceptWait"/>, or
    /// MCSocket::InvalidHandle.
    /// </summary>
    MCSocket::Handle AcceptWaiting(const std::function<MCSocket::Handle()>& accept)
    {
        const auto deadline = std::chrono::steady_clock::now() + AcceptWait;

        for (;;)
        {
            const MCSocket::Handle handle = accept();

            if (handle != MCSocket::InvalidHandle || std::chrono::steady_clock::now() >= deadline)
            {
                return handle;
            }

            std::this_thread::sleep_for(AcceptLook);
        }
    }

    /// <summary>A RIFF WAVE file: 22050 Hz, mono, 16-bit, <paramref name="samples"/> samples of silence.</summary>
    std::vector<uint8_t> SilentWave(uint32_t samples)
    {
        std::vector<uint8_t> wave;
        const auto put32 = [&](uint32_t value)
        {
            for (int i = 0; i < 4; i++)
            {
                wave.push_back(static_cast<uint8_t>(value >> (i * 8)));
            }
        };

        const auto put16 = [&](uint16_t value)
        {
            wave.push_back(static_cast<uint8_t>(value));
            wave.push_back(static_cast<uint8_t>(value >> 8));
        };

        const auto putTag = [&](std::string_view tag) { wave.insert(wave.end(), tag.begin(), tag.end()); };
        const uint32_t dataBytes = samples * 2;
        putTag("RIFF");
        put32(36 + dataBytes);
        putTag("WAVE");
        putTag("fmt ");
        put32(16);
        put16(1);
        put16(1);
        put32(22050);
        put32(22050 * 2);
        put16(2);
        put16(16);
        putTag("data");
        put32(dataBytes);
        wave.resize(wave.size() + dataBytes, 0);
        return wave;
    }
}

/// <summary>A test context's services replace the current ones, the rest come from below, and the scope restores.</summary>
TEST_CASE("services: a test context scope installs its services and restores the previous ones")
{
    MCGameContext& outer = MCGameContext::Current();
    MCRandom& outerRandom = outer.Random();
    MCClock& outerClock = outer.Clock();

    {
        MCTestContextScope scope;
        CHECK(&MCGameContext::Current() == &scope.Context());
        // Nothing given: everything comes from the context below.
        CHECK(&MCGameContext::Current().Random() == &outerRandom);
        MCScriptedRandom& random = scope.Context().SetRandom(std::make_unique<MCScriptedRandom>());
        CHECK(&MCGameContext::Current().Random() == &random);
        CHECK(&MCGameContext::Current().Clock() == &outerClock);

        {
            MCTestContextScope inner;
            CHECK(&MCGameContext::Current().Random() == &random);
        }

        CHECK(&MCGameContext::Current() == &scope.Context());
    }

    CHECK(&MCGameContext::Current() == &outer);
    CHECK(&MCGameContext::Current().Random() == &outerRandom);
}

/// <summary>The real dice are MSVC's <c>rand</c>: from seed 1, its well-known first values.</summary>
TEST_CASE("services: the real dice give the CRT's rand sequence")
{
    MCCrtRandom random;
    const uint32_t saved = random.State();
    random.Seed(1);
    CHECK_EQ(random.Next(), 41);
    CHECK_EQ(random.Next(), 18467);
    CHECK_EQ(random.Next(), 6334);
    CHECK_EQ(random.Next(), 26500);
    CHECK_EQ(random.Next(), 19169);
    random.Seed(saved);

    // A seeded scripted random runs the same sequence once its queue is empty.
    MCScriptedRandom scripted(1);
    scripted.Returns({7});
    CHECK_EQ(scripted.Next(), 7);
    CHECK_EQ(scripted.Next(), 41);
    CHECK_EQ(scripted.Next(), 18467);
    CHECK_EQ(scripted.Remaining(), static_cast<size_t>(0));
}

/// <summary>
/// RollDice(p) succeeds when the d100 roll, rand scaled to 0..99, is below p; RandomNumber(n) scales rand to 0..n-1.
/// The game rolls through MCPort::Rand, so the dice a test installs decide the outcome.
/// </summary>
TEST_CASE("services: RollDice and RandomNumber roll the installed dice")
{
    MCTestContextScope scope;
    MCScriptedRandom& dice = scope.Context().SetRandom(std::make_unique<MCScriptedRandom>());
    // rand 0 is a roll of 0; 0x7fff a roll of 99; 16383 a roll of 49; 16384 a roll of 50.
    dice.Returns({0, 0x7fff, 16383, 16384, 0x7fff, 0});
    CHECK_EQ(RollDice(50), true);
    CHECK_EQ(RollDice(50), false);
    CHECK_EQ(RollDice(50), true);
    CHECK_EQ(RollDice(50), false);
    CHECK_EQ(RandomNumber(6), 5);
    CHECK_EQ(RandomNumber(6), 0);
    CHECK_EQ(dice.Remaining(), static_cast<size_t>(0));
    CHECK_EQ(dice.Taken(), 6);
    // The game seeds the dice with srand.
    MCPort::SeedRand(1234);
    REQUIRE_EQ(dice.Seeds().size(), static_cast<size_t>(1));
    CHECK_EQ(dice.Seeds()[0], 1234u);
}

/// <summary>The manual clock stands still until advanced, and its date is 2000-01-01 12:00:00 plus the clock.</summary>
TEST_CASE("services: the manual clock moves only when the test moves it")
{
    MCTestContextScope scope;
    MCManualClock& clock = scope.Context().SetClock(std::make_unique<MCManualClock>());
    CHECK_EQ(MCPort::Milliseconds(), 0u);
    clock.Advance(1500000000);
    CHECK_EQ(MCPort::Milliseconds(), 1500u);
    CHECK_EQ(MCPort::PerformanceFrequency(), 1000000000);
    CHECK_EQ(MCPort::PerformanceCounter(), 1500000000);

    _SYSTEMTIME time{};
    MCPort::GetLocalTime(time);
    CHECK_EQ(time.wYear, 2000);
    CHECK_EQ(time.wMonth, 1);
    CHECK_EQ(time.wDay, 1);
    CHECK_EQ(time.wHour, 12);
    CHECK_EQ(time.wSecond, 1);
    CHECK_EQ(time.wMilliseconds, 500);

    // A present moves it only once AdvanceOnPresent is set, and the first present after an advance not at all.
    clock.Presented();
    clock.Presented();
    CHECK_EQ(clock.Nanoseconds(), 1500000000ull);
    clock.AdvanceOnPresent();
    clock.Presented();
    clock.Presented();
    CHECK_EQ(clock.Nanoseconds(), 1500000000ull + 1000000000ull / 60);
}

/// <summary>The memory source finds files as the game spells them, with user files over the install's.</summary>
TEST_CASE("services: the memory file source looks files up as the game spells them")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\missions\\Mis0101.fit", "install");
    files.AddFile("data/missions/other.fit", "other");
    files.AddFile("data\\art\\pic.tga", "art");
    files.AddUserFile("DATA\\MISSIONS\\MIS0101.FIT", std::vector<uint8_t>{'u', 's', 'e', 'r'});

    CHECK(MCFileSystem::Exists(".\\DATA\\missions\\mis0101.FIT"));
    CHECK(!MCFileSystem::Exists("data\\missions\\missing.fit"));

    const auto image = MCFileSystem::FindImage("data\\missions\\mis0101.fit");
    REQUIRE(image.has_value());
    CHECK_EQ(std::string(image->begin(), image->end()), std::string("user"));

    // As on disk, the listing is merged without regard to case, user files first (so with the user's spelling).
    std::vector<std::string> found = MCFileSystem::FindFiles("data\\missions\\*.fit");
    std::ranges::sort(found);
    REQUIRE_EQ(found.size(), static_cast<size_t>(2));
    CHECK_EQ(found[0], std::string("MIS0101.FIT"));
    CHECK_EQ(found[1], std::string("other.fit"));

    // The user file goes; the install's shows again. The install's can't be removed.
    CHECK(MCFileSystem::RemoveFile("data\\missions\\mis0101.fit"));
    const auto installed = MCFileSystem::FindImage("data\\missions\\mis0101.fit");
    REQUIRE(installed.has_value());
    CHECK_EQ(std::string(installed->begin(), installed->end()), std::string("install"));
    CHECK(!MCFileSystem::RemoveFile("data\\missions\\mis0101.fit"));
}

/// <summary>A FIT file the game opens by name reads from memory: blocks, numbers and strings.</summary>
TEST_CASE("services: a FitIniFile reads from an in-memory file")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    files.AddFile("data\\objects\\test.fit", "FITini \r\n"
                                             "\r\n[Mech]\r\n"
                                             "l Tonnage=35\r\n"
                                             "f Speed=64.5\r\n"
                                             "st Name=\"Uller\"\r\n"
                                             "\r\n[Pilot]\r\n"
                                             "l Gunnery=4\r\n"
                                             "FITend \r\n");

    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("DATA\\OBJECTS\\TEST.FIT"), 0);
    CHECK_EQ(fit.GetNumBlocks(), 2);
    REQUIRE_EQ(fit.SeekBlock("Mech"), 0);
    int32_t tonnage = 0;
    CHECK_EQ(fit.ReadIdLong("Tonnage", tonnage), 0);
    CHECK_EQ(tonnage, 35);
    float speed = 0.0f;
    CHECK_EQ(fit.ReadIdFloat("Speed", speed), 0);
    CHECK_EQ(speed, 64.5f);
    char name[32] = {};
    CHECK_EQ(fit.ReadIdString("Name", name), 0);
    CHECK_EQ(std::string(name), std::string("Uller"));
    REQUIRE_EQ(fit.SeekBlock("Pilot"), 0);
    int32_t gunnery = 0;
    CHECK_EQ(fit.ReadIdLong("Gunnery", gunnery), 0);
    CHECK_EQ(gunnery, 4);
    fit.Close();

    // A file the source doesn't have isn't found (and nothing on disk or in the FastFiles stands in).
    MCFile missing;
    CHECK(missing.Open("data\\objects\\nothere.fit") != NO_ERR);
}

/// <summary>What the game writes lands in the source's scratch folder and reads back from there.</summary>
TEST_CASE("services: files the game writes through a memory source read back")
{
    MCTestContextScope scope;
    MCMemoryFileSource& files = scope.Context().SetFiles(std::make_unique<MCMemoryFileSource>());
    {
        MCFile out;
        REQUIRE_EQ(out.Create("savegame\\test.sav"), NO_ERR);
        const uint8_t bytes[] = {1, 2, 3, 4};
        out.Write(bytes);
        out.Close();
    }

    CHECK(MCFileSystem::Exists("SAVEGAME\\TEST.SAV"));
    CHECK(std::filesystem::exists(files.ScratchFolder() / "SAVEGAME" / "TEST.SAV"));
    MCFile in;
    REQUIRE_EQ(in.Open("savegame\\test.sav"), NO_ERR);
    CHECK_EQ(in.FileSize(), 4u);
    uint8_t back[4] = {};
    in.Read(back);
    CHECK_EQ(back[3], 4);
}

/// <summary>
/// The sound renderer opens its mixer through the context's audio device, so a channel's play and stop reach a
/// recording device: a channel set to loop plays its buffer looping, and a stop stops it.
/// </summary>
TEST_CASE("services: a sound channel's play and stop reach the null audio device")
{
    MCTestContextScope scope;
    MCNullAudioDevice& device = scope.Context().SetAudio(std::make_unique<MCNullAudioDevice>());
    MCSoundRenderer& renderer = MCSoundRenderer::Install(2);
    CHECK_CALLED(device.Opened, 1);
    CHECK(SoundRenderer().get() == &renderer);

    const std::vector<uint8_t> wave = SilentWave(441);
    const std::shared_ptr<MCSoundResource> resource = renderer.CreateResource(wave);
    renderer.SetChannelLooping(1, true);
    renderer.Play(1, resource);
    CHECK_CALLED(device.Played, 1);
    CHECK(std::get<1>(device.Played.Last()) == true);
    CHECK(renderer.IsPlaying(1));
    CHECK_CALLED(device.Stopped, 0);
    renderer.Stop(1);
    CHECK_CALLED(device.Stopped, 1);
    CHECK(std::get<0>(device.Stopped.Last()) == std::get<0>(device.Played.Last()));
    CHECK(!renderer.IsPlaying(1));

    // Destroying a resource takes it off its channel; the renderer frees the rest and the mixer.
    renderer.DestroyResource(*resource);
    CHECK_EQ(renderer.ResourceCount(), 0u);
    CHECK(renderer.ChannelResource(1) == nullptr);
    MCSoundRenderer::Uninstall();
    CHECK(SoundRenderer() == nullptr);
}

/// <summary>The loopback network carries TCP streams and UDP datagrams between its own sockets.</summary>
TEST_CASE("services: the loopback transport connects, streams, broadcasts and closes")
{
    MCTestContextScope scope;
    MCLoopbackTransport& net = scope.Context().SetNet(std::make_unique<MCLoopbackTransport>());
    CHECK(MCSocket::Startup());
    CHECK(MCSocket::Resolve("localhost") == std::optional<uint32_t>(MCSocket::LoopbackIp));
    CHECK(MCSocket::Resolve("10.0.0.7") == std::optional<uint32_t>(0x0a000007u));
    CHECK(!MCSocket::Resolve("no.such.host").has_value());

    // Nobody listens yet.
    CHECK_EQ(MCSocket::ConnectTcp({MCSocket::LoopbackIp, 28800}, 100), MCSocket::InvalidHandle);
    const MCSocket::Handle listener = MCSocket::ListenTcp(28800);
    REQUIRE(listener != MCSocket::InvalidHandle);
    CHECK_EQ(MCSocket::ListenTcp(28800), MCSocket::InvalidHandle);
    const MCSocket::Handle client = MCSocket::ConnectTcp({MCSocket::LoopbackIp, 28800}, 100);
    REQUIRE(client != MCSocket::InvalidHandle);
    MCSocket::Address from;
    const MCSocket::Handle server = AcceptWaiting([&] { return MCSocket::AcceptTcp(listener, from); });
    REQUIRE(server != MCSocket::InvalidHandle);
    CHECK_EQ(from.Port, MCSocket::LocalPort(client));
    MCSocket::Address none;
    CHECK_EQ(MCSocket::AcceptTcp(listener, none), MCSocket::InvalidHandle);

    std::array<uint8_t, 16> buffer{};
    const auto text = [](std::string_view bytes)
    { return std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()); };
    const auto read = [&](size_t count) { return std::string(reinterpret_cast<const char*>(buffer.data()), count); };
    CHECK_EQ(MCSocket::Receive(server, buffer), 0);
    CHECK_EQ(MCSocket::Send(client, text("hello")), 5);
    CHECK_EQ(MCSocket::Receive(server, std::span(buffer).first(3)), 3);
    CHECK_EQ(read(3), std::string("hel"));
    CHECK_EQ(MCSocket::Receive(server, buffer), 2);
    CHECK_EQ(read(2), std::string("lo"));

    // A closed end: the other reads the end of the stream, and sends fail.
    MCSocket::Close(client);
    CHECK_EQ(MCSocket::Receive(server, buffer), -1);
    CHECK_EQ(MCSocket::Send(server, text("x")), -1);
    MCSocket::Close(server);
    MCSocket::Close(listener);

    // A broadcast reaches every UDP socket on the port, with the sender's port.
    const MCSocket::Handle a = MCSocket::OpenUdp(28801);
    const MCSocket::Handle b = MCSocket::OpenUdp(0);
    REQUIRE(a != MCSocket::InvalidHandle);
    REQUIRE(b != MCSocket::InvalidHandle);
    CHECK(MCSocket::SendTo(b, {MCSocket::BroadcastIp, 28801}, text("ping")));
    CHECK_EQ(MCSocket::ReceiveFrom(a, buffer, from), 4);
    CHECK_EQ(read(4), std::string("ping"));
    CHECK_EQ(from.Port, MCSocket::LocalPort(b));
    CHECK_EQ(MCSocket::ReceiveFrom(a, buffer, from), 0);
    CHECK_EQ(MCSocket::ReceiveFrom(b, buffer, from), 0);
    MCSocket::Close(a);
    MCSocket::Close(b);
    CHECK_EQ(net.OpenSockets(), static_cast<size_t>(0));
}

/// <summary>
/// The operating system's sockets: a connection kept alive is watched for the machine at its other end going, by
/// MCSocket::LinkKeepAlive's times, at whichever end asked; one not asked keeps the system's default, no watch.
/// </summary>
TEST_CASE("services: the operating system watches a kept-alive connection for the other machine going")
{
    MCSocketTransport net;
    REQUIRE(net.Startup());
    const MCSocket::Handle listener = net.ListenTcp(0);
    REQUIRE(listener != MCSocket::InvalidHandle);
    constexpr int connectMs = 1000;
    const MCSocket::Handle client = net.ConnectTcp({MCSocket::LoopbackIp, net.LocalPort(listener)}, connectMs);
    REQUIRE(client != MCSocket::InvalidHandle);
    MCSocket::Address from;
    const MCSocket::Handle server = AcceptWaiting([&] { return net.AcceptTcp(listener, from); });
    REQUIRE(server != MCSocket::InvalidHandle);

    CHECK(!MCSocketTransport::KeepAliveOf(client).has_value());
    CHECK(!MCSocketTransport::KeepAliveOf(server).has_value());
    net.KeepAlive(server);
    CHECK(MCSocketTransport::KeepAliveOf(server) == std::optional(MCSocket::LinkKeepAlive));
    CHECK(!MCSocketTransport::KeepAliveOf(client).has_value());
    net.KeepAlive(client);
    CHECK(MCSocketTransport::KeepAliveOf(client) == std::optional(MCSocket::LinkKeepAlive));
    net.Close(server);
    net.Close(client);
    net.Close(listener);
}

/// <summary>The mock helper records calls and arguments and hands out queued results.</summary>
TEST_CASE("services: MCMock records calls and returns queued results")
{
    MCMock::Calls<int, std::string> sent;
    CHECK_CALLED(sent, 0);
    sent(7, "hello");
    sent(8, "bye");
    CHECK_CALLED(sent, 2);
    CHECK_CALLED_WITH(sent, 7, "hello");
    CHECK(!sent.CalledWith(7, "bye"));
    CHECK_EQ(std::get<0>(sent.At(1)), 8);
    CHECK_EQ(std::get<1>(sent.Last()), std::string("bye"));
    sent.Clear();
    CHECK_CALLED(sent, 0);

    MCMock::Returning<bool, int> ready;
    ready.Returns(true, false);
    CHECK(ready(1));
    CHECK(!ready(2));
    // Past the queue: the default.
    CHECK(!ready(3));
    CHECK_CALLED(ready, 3);
    CHECK_CALLED_WITH(ready, 2);
    CHECK_EQ(ready.Pending(), static_cast<size_t>(0));
}

/// <summary>
/// The tiny map is all passable, a blocked cell isn't, and the map's own world-to-cell conversion puts each cell
/// centre in its cell.
/// </summary>
TEST_CASE("services: the tiny map is flat and passable with the cells it blocks")
{
    const bool before = MCGameContext::Current().HasMoveSystem();
    {
        MCTinyMap map(8);
        CHECK_EQ(GameMap().Width, 8);
        CHECK_EQ(GameMap().Height, 8);
        CHECK(map.Passable(0, 0));
        CHECK(map.Passable(23, 23));
        CHECK(!map.Passable(24, 0));
        map.Block(4, 5);
        CHECK(!map.Passable(4, 5));
        CHECK(map.Passable(4, 4));
        CHECK(map.Passable(5, 5));

        for (const auto [row, col] : {std::pair{0, 0}, std::pair{4, 5}, std::pair{23, 23}, std::pair{11, 2}})
        {
            MCTest::Scope where(std::format("cell {},{}", row, col));
            int32_t tileR = 0;
            int32_t tileC = 0;
            int32_t cellR = 0;
            int32_t cellC = 0;
            GameMap().WorldToMapPos(map.CellCentre(row, col), tileR, tileC, cellR, cellC);
            CHECK_EQ(tileR * MapCellDim + cellR, row);
            CHECK_EQ(tileC * MapCellDim + cellC, col);
        }
    }

    CHECK_EQ(MCGameContext::Current().HasMoveSystem(), before);
}

/// <summary>The retail fixture reads files the game reads (here from the FastFiles) into memory.</summary>
TEST_CASE("game: the retail data fixture loads FIT files into a memory source")
{
    std::unique_ptr<MCMemoryFileSource> data = MCRetailData::Load({"data\\missions\\gamesys.fit"});

    if (data == nullptr)
    {
        return;
    }

    CHECK_EQ(data->Count(), static_cast<size_t>(1));
    MCTestContextScope scope;
    scope.Context().SetFiles(std::move(data));
    MCFitIniFile fit;
    REQUIRE_EQ(fit.Open("data\\missions\\gamesys.fit"), 0);
    CHECK_EQ(fit.SeekBlock("Pathfinding"), 0);
    int32_t range = 0;
    CHECK_EQ(fit.ReadIdLong("SimplePathTileRange", range), 0);
    CHECK(range > 0);
}

/// <summary>
/// Mission 1 booted inside a test's context, with seeded scripted dice in place of the real ones, plays out exactly as
/// the recorded run (StateHashTests): the services a test installs are the ones the game uses, and the fakes behave
/// as the real ones.
/// </summary>
TEST_CASE_ISOLATED("game: mission 1 inside a test context matches the recorded run")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    MCTestContextScope scope;
    MCScriptedRandom& dice = scope.Context().SetRandom(std::make_unique<MCScriptedRandom>(1u));
    REQUIRE(MCTestGame::StartMission(1));
    uint32_t frames = 0x811c9dc5;

    for (int32_t frame = 0; frame < 600; frame++)
    {
        MCTestGame::RunFrame(1.0f / 15.0f);
        frames = (frames ^ MCTestGame::StateHash()) * 0x01000193;
    }

    std::cout << std::format("  mission 1 in a test context: state 0x{:08x}, {} rolls\n", frames, dice.Taken());
    CHECK(dice.Taken() > 0);
    CHECK_EQ(frames, 0x9ff2e0b4u);
}
