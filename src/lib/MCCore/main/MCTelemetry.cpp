#include "stdafx.h"
#include "main/MCTelemetry.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "main/MCHdSettings.h"
#include "platform/MCDisplay.h"
#include "sprite/MCHdArt.h"

int GReports = 0;

namespace
{
    using MCTelemetry::MCBackend;
    using MCTelemetry::MCCrash;
    using MCTelemetry::MCMachine;
    using MCTelemetry::MCPrompt;
    using Clock = std::chrono::steady_clock;

    /// <summary>What was sent, and the missions not yet reported: a FIT file in the user folder.</summary>
    constexpr std::string_view StateFile = "reports.cfg";
    constexpr std::string_view StateBlock = "Reports";

    /// <summary>The server's paths: the region's question, and the reports.</summary>
    constexpr std::string_view RegionPath = "region";
    constexpr std::string_view ReportPath = "report";
    constexpr int32_t HttpOk = 200;

    /// <summary>The frame times' histogram: half-millisecond buckets to 250 ms, the last holding the longer frames.</summary>
    constexpr double BucketMilliseconds = 0.5;
    constexpr size_t Buckets = 501;
    /// <summary>Frames this long are hitches, and stalls.</summary>
    constexpr double HitchMilliseconds = 33.0;
    constexpr double StallMilliseconds = 100.0;
    /// <summary>Play shorter than this counts for nothing (a match left in its first minutes, a mission restarted).</summary>
    constexpr double MinimumPlaySeconds = 120.0;
    /// <summary>The missions' reports come this many days apart at least.</summary>
    constexpr int64_t ReportDays = 7;
    /// <summary>The machine's heat and power are read this often in play.</summary>
    constexpr double PowerSeconds = 5.0;
    constexpr int64_t SecondsPerDay = 86400;
    constexpr double MillisecondsPerSecond = 1000.0;
    constexpr double SecondsPerMinute = 60.0;
    /// <summary>A crash report's frames, at most.</summary>
    constexpr size_t MaxCrashFrames = 40;
    /// <summary>What the server is given of a string from the system (a model's name, a frame), at most.</summary>
    constexpr size_t MaxTextLength = 200;

    /// <summary>
    /// The countries whose law wants consent before the reports (PLAN.md's "Telemetry"): the EEA, whose ePrivacy rule
    /// has no exception for usage statistics yet; South Korea, China and Vietnam, whose laws put consent first; Russia,
    /// Turkey and the Gulf, not checked yet; and no country known (Cloudflare's XX, Tor's T1). The server's list can
    /// be newer: either asking is enough.
    /// </summary>
    constexpr auto AskCountries = std::to_array<std::string_view>({
        "AT", "BE", "BG", "HR", "CY", "CZ", "DK", "EE", "FI", "FR", "DE", "GR", "HU", "IE", "IT",
        "LV", "LT", "LU", "MT", "NL", "PL", "PT", "RO", "SK", "SI", "ES", "SE", "IS", "LI", "NO",
        "KR", "CN", "VN", "RU", "TR", "SA", "AE", "QA", "KW", "BH", "OM", "XX", "T1",
    });
    /// <summary>India's DPDP Act wants consent from the day its duties start.</summary>
    constexpr std::string_view India = "IN";
    constexpr int64_t IndiaConsentDay =
        std::chrono::sys_days{std::chrono::year{2027} / 5 / 13}.time_since_epoch().count();

    /// <summary>
    /// The black box: the last moments of the run, in a file mapped into memory, so a crash leaves it behind (the
    /// next start sends it with the crash). Plain data in a fixed layout.
    /// </summary>
    constexpr uint32_t BlackBoxMagic = 0x4242434d;
    constexpr uint32_t BlackBoxLayout = 1;
    constexpr size_t BlackBoxEvents = 128;
    /// <summary>A repeat of one of the last events this many back adds to it instead of taking a slot.</summary>
    constexpr uint32_t CoalesceEvents = 4;

    /// <summary>Where the run is: menus, a scenario loading, a mission or a match running, its results.</summary>
    enum class MCPhase : uint8_t
    {
        Menus,
        Loading,
        Mission,
        Match,
        Results,
    };

    enum class MCEventKind : uint16_t
    {
        Phase = 1,
        MessageIn,
        MessageOut,
        PlayerLost,
    };

    struct MCBlackBoxEvent
    {
        /// <summary>When it last happened, since the run started.</summary>
        uint32_t Milliseconds;
        MCEventKind Kind;
        /// <summary>The phase, or the message's type.</summary>
        uint16_t Value;
        /// <summary>How many times in a row, and their bytes.</summary>
        uint32_t Count;
        uint32_t Bytes;
    };

    struct MCBlackBox
    {
        uint32_t Magic;
        uint32_t Layout;
        /// <summary>1 while the game runs; a clean quit sets 0, so 1 at the next start is a run that ended badly.</summary>
        uint32_t Running;
        uint32_t Frames;
        /// <summary>When the run started (seconds since 1970): the system's crash report for it is newer.</summary>
        int64_t Started;
        std::array<char, 32> App;
        std::array<char, 32> Mission;
        MCPhase Phase;
        uint8_t Server;
        uint8_t Players;
        uint8_t PlayersLost;
        float PlaySeconds;
        /// <summary>The events written in all: the next goes in slot <c>Written % BlackBoxEvents</c>.</summary>
        uint32_t Written;
        uint32_t Reserved;
        std::array<MCBlackBoxEvent, BlackBoxEvents> Events;
    };

    static_assert(std::is_trivially_copyable_v<MCBlackBox>);

    /// <summary>A stretch of play measured: missions or a match.</summary>
    struct MCTally
    {
        int32_t Count = 0;
        double Seconds = 0.0;
        /// <summary>The loads' seconds, all together.</summary>
        double LoadSeconds = 0.0;
        double WorstMilliseconds = 0.0;
        int32_t MoviesBehind = 0;
        int32_t Thermal = 0;
        bool Battery = false;
        bool LowPower = false;
        std::vector<uint32_t> Histogram = std::vector<uint32_t>(Buckets);

        void Add(const MCTally& other)
        {
            Count += other.Count;
            Seconds += other.Seconds;
            LoadSeconds += other.LoadSeconds;
            WorstMilliseconds = std::max(WorstMilliseconds, other.WorstMilliseconds);
            MoviesBehind += other.MoviesBehind;
            Thermal = std::max(Thermal, other.Thermal);
            Battery = Battery || other.Battery;
            LowPower = LowPower || other.LowPower;

            for (size_t i = 0; i < Buckets; i++)
            {
                Histogram[i] += other.Histogram[i];
            }
        }
    };

    /// <summary>The scenario being played.</summary>
    struct MCPlay
    {
        bool Loading = false;
        bool Running = false;
        bool Match = false;
        std::string Mission;
        int32_t Players = 0;
        bool Server = false;
        int32_t PlayersLost = 0;
        Clock::time_point LoadStart;
        double SincePower = 0.0;
        MCTally Tally;
    };

    /// <summary>What reports.cfg keeps.</summary>
    struct MCState
    {
        /// <summary>The report version whose notice or question the player saw (or chose REPORTS since); 0 none.</summary>
        int32_t Seen = 0;
        /// <summary>A report was sent once: the next ones aren't a new player's first.</summary>
        bool FirstSent = false;
        /// <summary>The day the missions were last reported (days since 1970); 0 never.</summary>
        int32_t MissionDay = 0;
        /// <summary>The missions (and HD movies) since.</summary>
        MCTally Missions;
    };

    /// <summary>A reply from the backend, from any thread, waiting for the next frame.</summary>
    struct MCReply
    {
        uint32_t Generation;
        uint32_t Id;
        int32_t Status;
        std::string Body;
    };

    /// <summary>The replies come in on the backend's threads, and wait here for the main thread.</summary>
    struct MCInbox
    {
        std::mutex Lock;
        std::vector<MCReply> Replies;
    };

    /// <summary>
    /// Made once and never freed: a reply can come in on the backend's thread while the game quits, after the statics
    /// are gone.
    /// </summary>
    MCInbox& Inbox()
    {
        static auto* const inbox = new MCInbox;
        return *inbox;
    }

    std::unique_ptr<MCBackend> Backend;
    /// <summary>The backend's number: a reply to an earlier one is dropped.</summary>
    uint32_t Generation = 0;
    uint32_t NextRequest = 0;
    std::map<uint32_t, std::function<void(int32_t, const std::string&)>> Handlers;
    bool Started = false;
    MCMachine Machine;
    MCState State;
    MCPlay Play;
    MCPrompt Pending = MCPrompt::None;
    Clock::time_point RunStart = Clock::now();
    std::optional<Clock::time_point> LastFrame;
    /// <summary>The black box: in the backend's mapped file, or here without one.</summary>
    MCBlackBox MemoryBox{};
    MCBlackBox* Box = &MemoryBox;

    /// <summary>
    /// <paramref name="text"/> as a JSON string of ASCII, cut at <see cref="MaxTextLength"/> (other bytes are '?', so a
    /// cut never splits a character).
    /// </summary>
    std::string Quote(std::string_view text)
    {
        constexpr unsigned char firstPrintable = 0x20;
        constexpr unsigned char lastAscii = 0x7f;
        std::string quoted = "\"";

        for (const char c : text.substr(0, MaxTextLength))
        {
            const auto byte = static_cast<unsigned char>(c);

            if (c == '"' || c == '\\')
            {
                quoted += '\\';
                quoted += c;
            }
            else if (byte < firstPrintable)
            {
                quoted += std::format("\\u{:04x}", byte);
            }
            else
            {
                quoted += byte < lastAscii ? c : '?';
            }
        }

        return quoted + '"';
    }

    /// <summary>A JSON object, built key by key.</summary>
    class MCJsonObject
    {
    public:
        MCJsonObject& Text(std::string_view key, std::string_view value) { return Raw(key, Quote(value)); }
        MCJsonObject& Integer(std::string_view key, int64_t value) { return Raw(key, std::to_string(value)); }
        /// <summary>A number to two decimals, or null when it isn't finite (JSON has no NaN).</summary>
        MCJsonObject& Number(std::string_view key, double value)
        {
            return Raw(key, std::isfinite(value) ? std::format("{:.2f}", value) : std::string("null"));
        }

        MCJsonObject& Flag(std::string_view key, bool value) { return Raw(key, value ? "true" : "false"); }

        /// <summary>Adds <paramref name="json"/> as it is (an object or an array).</summary>
        MCJsonObject& Raw(std::string_view key, std::string_view json)
        {
            if (_Text.size() > 1)
            {
                _Text += ',';
            }

            _Text += Quote(key);
            _Text += ':';
            _Text += json;
            return *this;
        }

        std::string Close() const { return _Text + '}'; }

    private:
        std::string _Text = "{";
    };

    /// <summary>The strings as a JSON array.</summary>
    std::string JsonArray(std::span<const std::string> items)
    {
        std::string array = "[";

        for (const std::string& item : items)
        {
            if (array.size() > 1)
            {
                array += ',';
            }

            array += Quote(item);
        }

        return array + ']';
    }

    /// <summary>The value of <paramref name="key"/> in a flat JSON object (a string's without its quotes).</summary>
    std::optional<std::string_view> JsonValue(std::string_view json, std::string_view key)
    {
        constexpr std::string_view space = " \t\r\n";
        const std::string name = Quote(key);
        size_t at = json.find(name);

        if (at == std::string_view::npos)
        {
            return std::nullopt;
        }

        at = json.find_first_not_of(space, at + name.size());

        if (at == std::string_view::npos || json[at] != ':')
        {
            return std::nullopt;
        }

        at = json.find_first_not_of(space, at + 1);

        if (at == std::string_view::npos)
        {
            return std::nullopt;
        }

        if (json[at] == '"')
        {
            const size_t end = json.find('"', at + 1);
            return end == std::string_view::npos ? std::nullopt : std::optional(json.substr(at + 1, end - at - 1));
        }

        const size_t end = json.find_first_of(",}" + std::string(space), at);
        return json.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
    }

    /// <summary>The bucket a frame of <paramref name="milliseconds"/> goes in.</summary>
    size_t Bucket(double milliseconds)
    {
        return std::min(static_cast<size_t>(std::max(milliseconds, 0.0) / BucketMilliseconds), Buckets - 1);
    }

    /// <summary>The frames of <paramref name="milliseconds"/> or longer (to the bucket).</summary>
    uint64_t FramesOver(std::span<const uint32_t> histogram, double milliseconds)
    {
        return std::accumulate(histogram.begin() + static_cast<ptrdiff_t>(Bucket(milliseconds)), histogram.end(),
                               uint64_t{0});
    }

    /// <summary>Today, in days since 1970.</summary>
    int64_t Today()
    {
        return Backend->Now() / SecondsPerDay;
    }

    /// <summary>What the main menu shows next (it looks each frame).</summary>
    void SetPrompt(MCPrompt prompt)
    {
        Pending = prompt;
    }

    /// <summary>Whether the player has decided: seen this version's notice or question, or said no to an earlier one.</summary>
    bool Decided()
    {
        return State.Seen >= MCTelemetry::ReportVersion || (State.Seen > 0 && GReports == 0);
    }

    MCTally ReadTally(MCFitIniFile& file)
    {
        MCTally tally;
        tally.Count = file.Read<int32_t>("Missions").value_or(0);
        tally.Seconds = file.Read<float>("Seconds").value_or(0.0f);
        tally.LoadSeconds = file.Read<float>("LoadSeconds").value_or(0.0f);
        tally.WorstMilliseconds = file.Read<float>("Worst").value_or(0.0f);
        tally.MoviesBehind = file.Read<int32_t>("MoviesBehind").value_or(0);
        tally.Thermal = file.Read<int32_t>("Thermal").value_or(0);
        tally.Battery = file.Read<bool>("Battery").value_or(false);
        tally.LowPower = file.Read<bool>("LowPower").value_or(false);

        if (const MCFitResult<std::vector<int32_t>> histogram = file.ReadArray<int32_t>("Histogram");
            histogram.has_value() && histogram->size() == Buckets)
        {
            std::ranges::transform(*histogram, tally.Histogram.begin(),
                                   [](int32_t count) { return static_cast<uint32_t>(std::max(count, 0)); });
        }

        return tally;
    }

    void ReadState()
    {
        State = {};
        MCFitIniFile file;

        if (file.Open(StateFile) != 0 || file.SeekBlock(StateBlock) != 0)
        {
            return;
        }

        State.Seen = file.Read<int32_t>("Seen").value_or(0);
        State.FirstSent = file.Read<bool>("FirstSent").value_or(false);
        State.MissionDay = file.Read<int32_t>("MissionDay").value_or(0);
        State.Missions = ReadTally(file);
    }

    void WriteState()
    {
        MCFitIniFile file;

        if (const int32_t result = file.Create(StateFile); result != 0)
        {
            SDL_Log("Reports: cannot create %s (%#x); what was sent isn't kept", StateFile.data(),
                    static_cast<uint32_t>(result));
            return;
        }

        const MCTally& tally = State.Missions;
        std::vector<int32_t> histogram(Buckets);
        std::ranges::transform(
            tally.Histogram, histogram.begin(), [](uint32_t count)
            { return static_cast<int32_t>(std::min<uint32_t>(count, std::numeric_limits<int32_t>::max())); });
        file.WriteBlock(StateBlock);
        file.WriteIdLong("Seen", State.Seen);
        file.WriteIdBoolean("FirstSent", State.FirstSent);
        file.WriteIdLong("MissionDay", State.MissionDay);
        file.WriteIdLong("Missions", tally.Count);
        file.WriteIdFloat("Seconds", static_cast<float>(tally.Seconds));
        file.WriteIdFloat("LoadSeconds", static_cast<float>(tally.LoadSeconds));
        file.WriteIdFloat("Worst", static_cast<float>(tally.WorstMilliseconds));
        file.WriteIdLong("MoviesBehind", tally.MoviesBehind);
        file.WriteIdLong("Thermal", tally.Thermal);
        file.WriteIdBoolean("Battery", tally.Battery);
        file.WriteIdBoolean("LowPower", tally.LowPower);
        file.WriteIdLongArray("Histogram", histogram);
    }

    /// <summary>
    /// Sends <paramref name="body"/> to the server's <paramref name="path"/>; <paramref name="handler"/> has the
    /// status and the reply on the main thread, at a frame.
    /// </summary>
    void Request(std::string_view path, std::string body, std::function<void(int32_t, const std::string&)> handler)
    {
        const uint32_t id = ++NextRequest;
        const uint32_t generation = Generation;
        Handlers.emplace(id, std::move(handler));
        Backend->Request(std::string(path), std::move(body),
                         [generation, id](int32_t status, std::string reply)
                         {
                             MCInbox& inbox = Inbox();
                             const std::scoped_lock lock(inbox.Lock);
                             inbox.Replies.push_back({generation, id, status, std::move(reply)});
                         });
    }

    /// <summary>Hands the replies that came in to their handlers.</summary>
    void Pump()
    {
        std::vector<MCReply> replies;

        {
            MCInbox& inbox = Inbox();
            const std::scoped_lock lock(inbox.Lock);
            replies.swap(inbox.Replies);
        }

        for (MCReply& reply : replies)
        {
            if (reply.Generation != Generation)
            {
                continue;
            }

            if (auto handler = Handlers.extract(reply.Id); !handler.empty())
            {
                handler.mapped()(reply.Status, reply.Body);
            }
        }
    }

    /// <summary>A frame of the game's: the replies come in, and the black box counts it.</summary>
    void Tick()
    {
        Pump();
        Box->Frames++;
    }

    /// <summary>The computer, as a report holds it.</summary>
    std::string MachineJson()
    {
        return MCJsonObject()
            .Text("model", Machine.Model)
            .Text("cpu", Machine.Cpu)
            .Integer("performanceCores", Machine.PerformanceCores)
            .Integer("efficiencyCores", Machine.EfficiencyCores)
            .Integer("gpuCores", Machine.GpuCores)
            .Integer("memoryGb", Machine.MemoryGb)
            .Text("system", Machine.System)
            .Close();
    }

    /// <summary>The window, the screen and the graphics as the game draws them now.</summary>
    std::string DisplayJson()
    {
        const std::shared_ptr<MCDisplay> display = HasGuiSystem() ? GuiSystem().Display() : nullptr;

        if (display == nullptr)
        {
            return "{}";
        }

        SDL_Window* window = &display->Window();
        int pixelWidth = 0;
        int pixelHeight = 0;
        int pointWidth = 0;
        int pointHeight = 0;
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        SDL_GetWindowSize(window, &pointWidth, &pointHeight);
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        return MCJsonObject()
            .Integer("width", pixelWidth)
            .Integer("height", pixelHeight)
            .Number("scale", pointWidth > 0 ? static_cast<double>(pixelWidth) / pointWidth : 1.0)
            .Flag("fullScreen", (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0)
            .Number("refresh", mode != nullptr ? mode->refresh_rate : 0.0)
            .Text("renderer", display->RendererName())
            .Integer("interfaceSize", GInterfaceSize)
            .Close();
    }

    /// <summary>The HD settings, and whether an HD art pack is there.</summary>
    std::string SettingsJson()
    {
        MCJsonObject settings;

        for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
        {
            if (!setting.Player)
            {
                settings.Integer(setting.Key, *setting.Value);
            }
        }

        std::error_code error;
        settings.Flag("pack", std::filesystem::is_directory(MCHdArt::PackFolder(), error));
        return settings.Close();
    }

    /// <summary>A report's start: its kind, whether it's this player's first, the game and the computer.</summary>
    MCJsonObject ReportStart(std::string_view kind, std::string_view app)
    {
        MCJsonObject report;
        report.Integer("v", MCTelemetry::ReportVersion)
            .Text("kind", kind)
            .Flag("first", !State.FirstSent)
            .Text("platform", Machine.Platform)
            .Text("app", app)
            .Raw("machine", MachineJson());
        return report;
    }

    /// <summary>A tally's play: how much, the loads, the frame times, and the computer's heat and power.</summary>
    MCJsonObject PlayJson(const MCTally& tally)
    {
        MCJsonObject play;
        play.Integer("count", tally.Count)
            .Number("minutes", tally.Seconds / SecondsPerMinute)
            .Number("loadSeconds", tally.Count > 0 ? tally.LoadSeconds / tally.Count : 0.0)
            .Number("p50", MCTelemetry::Percentile(tally.Histogram, 0.5))
            .Number("p95", MCTelemetry::Percentile(tally.Histogram, 0.95))
            .Number("p99", MCTelemetry::Percentile(tally.Histogram, 0.99))
            .Number("worst", tally.WorstMilliseconds)
            .Integer("over33", static_cast<int64_t>(FramesOver(tally.Histogram, HitchMilliseconds)))
            .Integer("over100", static_cast<int64_t>(FramesOver(tally.Histogram, StallMilliseconds)))
            .Integer("moviesBehind", tally.MoviesBehind)
            .Integer("thermal", tally.Thermal)
            .Flag("battery", tally.Battery)
            .Flag("lowPower", tally.LowPower);
        return play;
    }

    /// <summary>Sends <paramref name="report"/>; <paramref name="done"/> learns whether the server took it.</summary>
    void SendReport(std::string report, std::function<void(bool)> done = nullptr)
    {
        Request(ReportPath, std::move(report),
                [done = std::move(done)](int32_t status, const std::string&)
                {
                    const bool sent = status == HttpOk;

                    if (sent && !State.FirstSent)
                    {
                        State.FirstSent = true;
                        WriteState();
                    }

                    if (!sent)
                    {
                        SDL_Log("Reports: the server didn't take a report (status %d)", status);
                    }

                    if (done)
                    {
                        done(sent);
                    }
                });
    }

    /// <summary>The missions since the last report, sent (on failure they're kept for the next).</summary>
    void SendMissions()
    {
        MCTally missions = std::move(State.Missions);
        State.Missions = {};
        State.MissionDay = static_cast<int32_t>(Today());
        WriteState();
        const std::string report = ReportStart("missions", Machine.AppVersion)
                                       .Raw("display", DisplayJson())
                                       .Raw("settings", SettingsJson())
                                       .Raw("play", PlayJson(missions).Close())
                                       .Close();
        SendReport(report,
                   [missions = std::move(missions)](bool sent)
                   {
                       if (!sent)
                       {
                           State.Missions.Add(missions);
                           WriteState();
                       }
                   });
    }

    /// <summary>The match just played, sent.</summary>
    void SendMatch()
    {
        const std::string report = ReportStart("match", Machine.AppVersion)
                                       .Raw("display", DisplayJson())
                                       .Raw("settings", SettingsJson())
                                       .Raw("play", PlayJson(Play.Tally)
                                                        .Text("mission", Play.Mission)
                                                        .Integer("players", Play.Players)
                                                        .Flag("server", Play.Server)
                                                        .Integer("playersLost", Play.PlayersLost)
                                                        .Close())
                                       .Close();
        SendReport(report);
    }

    std::string_view PhaseName(MCPhase phase)
    {
        switch (phase)
        {
            case MCPhase::Menus:
                return "menus";
            case MCPhase::Loading:
                return "loading";
            case MCPhase::Mission:
                return "mission";
            case MCPhase::Match:
                return "match";
            case MCPhase::Results:
                return "results";
        }

        return "unknown";
    }

    /// <summary>The text in <paramref name="field"/> (up to its NUL, or all of it).</summary>
    std::string_view FieldText(const std::array<char, 32>& field)
    {
        return {field.data(), static_cast<size_t>(std::ranges::find(field, '\0') - field.begin())};
    }

    /// <summary>Copies <paramref name="text"/> into <paramref name="field"/>, cut to leave a NUL.</summary>
    void SetField(std::array<char, 32>& field, std::string_view text)
    {
        field.fill('\0');
        std::ranges::copy(text.substr(0, field.size() - 1), field.begin());
    }

    /// <summary>The black box's events, oldest first, as a report holds them.</summary>
    std::vector<std::string> EventLines(const MCBlackBox& box)
    {
        std::vector<std::string> lines;
        const uint32_t count = std::min<uint32_t>(box.Written, BlackBoxEvents);
        const uint32_t first = box.Written - count;

        for (uint32_t i = first; i < box.Written; i++)
        {
            const MCBlackBoxEvent& event = box.Events[i % BlackBoxEvents];
            const double seconds = event.Milliseconds / MillisecondsPerSecond;

            switch (event.Kind)
            {
                case MCEventKind::Phase:
                    lines.push_back(std::format("{:.1f}s {}", seconds, PhaseName(static_cast<MCPhase>(event.Value))));
                    break;
                case MCEventKind::MessageIn:
                case MCEventKind::MessageOut:
                    lines.push_back(std::format("{:.1f}s {} {} x{} {}B", seconds,
                                                event.Kind == MCEventKind::MessageIn ? "in" : "out", event.Value,
                                                event.Count, event.Bytes));
                    break;
                case MCEventKind::PlayerLost:
                    lines.push_back(std::format("{:.1f}s player lost", seconds));
                    break;
            }
        }

        return lines;
    }

    /// <summary>The crash the last run ended in, with its black box.</summary>
    void SendCrash(const MCCrash& crash, const MCBlackBox& box)
    {
        std::vector<std::string> frames(crash.Frames.begin(),
                                        crash.Frames.begin() +
                                            static_cast<ptrdiff_t>(std::min(crash.Frames.size(), MaxCrashFrames)));
        const std::string state = MCJsonObject()
                                      .Text("phase", PhaseName(box.Phase))
                                      .Text("mission", FieldText(box.Mission))
                                      .Integer("players", box.Players)
                                      .Flag("server", box.Server != 0)
                                      .Integer("playersLost", box.PlayersLost)
                                      .Number("minutes", box.PlaySeconds / SecondsPerMinute)
                                      .Integer("frames", box.Frames)
                                      .Close();
        const std::string report =
            ReportStart("crash", FieldText(box.App))
                .Raw("crash", MCJsonObject().Text("what", crash.What).Raw("frames", JsonArray(frames)).Close())
                .Raw("state", state)
                .Raw("events", JsonArray(EventLines(box)))
                .Close();
        SDL_Log("Reports: the last run crashed (%s): sending its report", crash.What.c_str());
        SendReport(report);
    }

    /// <summary>Notes <paramref name="kind"/> in the black box (a repeat of a recent one adds to it).</summary>
    void Record(MCEventKind kind, uint16_t value, size_t bytes = 0)
    {
        const auto milliseconds = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - RunStart).count());
        const auto addedBytes = static_cast<uint32_t>(std::min<size_t>(bytes, std::numeric_limits<uint32_t>::max()));

        if (kind == MCEventKind::MessageIn || kind == MCEventKind::MessageOut)
        {
            for (uint32_t back = 1; back <= std::min(CoalesceEvents, Box->Written); back++)
            {
                MCBlackBoxEvent& event = Box->Events[(Box->Written - back) % BlackBoxEvents];

                if (event.Kind == kind && event.Value == value)
                {
                    event.Milliseconds = milliseconds;
                    event.Count++;
                    event.Bytes += addedBytes;
                    return;
                }
            }
        }

        Box->Events[Box->Written % BlackBoxEvents] = {milliseconds, kind, value, 1, addedBytes};
        Box->Written++;
    }

    void SetPhase(MCPhase phase)
    {
        Box->Phase = phase;
        Record(MCEventKind::Phase, static_cast<uint16_t>(phase));
    }

    /// <summary>
    /// Maps the black box, and gives back what the last run left in it when that run ended badly (no clean quit).
    /// </summary>
    std::optional<MCBlackBox> OpenBlackBox()
    {
        const std::span<std::byte> mapped = Backend->MapBlackBox(sizeof(MCBlackBox));
        std::optional<MCBlackBox> last;

        if (mapped.size() >= sizeof(MCBlackBox))
        {
            MCBlackBox previous;
            std::memcpy(&previous, mapped.data(), sizeof(MCBlackBox));

            if (previous.Magic == BlackBoxMagic && previous.Layout == BlackBoxLayout && previous.Running != 0)
            {
                last = previous;
            }

            // The mapping is page-aligned plain memory: the box is written in place (its layout is trivially copyable).
            Box = reinterpret_cast<MCBlackBox*>(mapped.data());
        }

        *Box = {};
        Box->Magic = BlackBoxMagic;
        Box->Layout = BlackBoxLayout;
        Box->Running = 1;
        Box->Started = Backend->Now();
        SetField(Box->App, Machine.AppVersion);
        RunStart = Clock::now();
        SetPhase(MCPhase::Menus);
        return last;
    }

    /// <summary>Asks the server where the player is, and what's due: the notice or the question.</summary>
    void AskRegion()
    {
        const int64_t day = Today();

        for (const std::string& country : Backend->LocalCountries())
        {
            // The system's own settings say the question is due: nothing is sent first, not even the region's.
            if (MCTelemetry::AskFirst(country, day))
            {
                SetPrompt(MCPrompt::Ask);
                return;
            }
        }

        Request(RegionPath, {},
                [day](int32_t status, const std::string& reply)
                {
                    if (status != HttpOk)
                    {
                        SDL_Log("Reports: the region wasn't found (status %d): the next start asks again", status);
                        return;
                    }

                    // The player chose REPORTS in HD OPTIONS while the server was asked: nothing is due.
                    if (Decided())
                    {
                        return;
                    }

                    const std::string_view country = JsonValue(reply, "country").value_or("");
                    const bool ask = JsonValue(reply, "ask").value_or("true") != "false";
                    SDL_Log("Reports: the region is %.*s", static_cast<int>(country.size()), country.data());
                    SetPrompt(ask || MCTelemetry::AskFirst(country, day) ? MCPrompt::Ask : MCPrompt::Notice);
                });
    }
}

auto MCTelemetry::SetBackend(std::unique_ptr<MCBackend> backend) -> void
{
    // The box leaves the old backend's mapping before the backend (and its mapping) goes.
    Box = &MemoryBox;
    MemoryBox = {};
    Backend = std::move(backend);
    Generation++;
    Handlers.clear();
    Started = false;
    Machine = {};
    State = {};
    Play = {};
    Pending = MCPrompt::None;
    LastFrame.reset();
}

auto MCTelemetry::Available() -> bool
{
    return Backend != nullptr;
}

auto MCTelemetry::Start() -> void
{
    if (Backend == nullptr || Started)
    {
        return;
    }

    Started = true;
    Machine = Backend->Machine();
    ReadState();
    const std::optional<MCBlackBox> last = OpenBlackBox();

    if (last.has_value() && Reporting())
    {
        if (const std::optional<MCCrash> crash = Backend->Crash(last->Started); crash.has_value())
        {
            SendCrash(*crash, *last);
        }
    }

    if (!Decided())
    {
        AskRegion();
    }
}

auto MCTelemetry::Quit() -> void
{
    Box->Running = 0;
}

auto MCTelemetry::Prompt() -> MCPrompt
{
    return Pending;
}

auto MCTelemetry::Answered(bool send) -> void
{
    // Only an answer to what was shown counts (a late one never overrides REPORTS chosen in HD OPTIONS).
    if (Backend == nullptr || Pending == MCPrompt::None)
    {
        return;
    }

    SDL_Log("Reports: %s", send ? "on" : "off (the player's answer)");
    GReports = send ? 1 : 0;
    State.Seen = ReportVersion;
    WriteState();
    SetPrompt(MCPrompt::None);
}

auto MCTelemetry::Chosen() -> void
{
    if (Backend == nullptr)
    {
        return;
    }

    State.Seen = ReportVersion;
    WriteState();
    SetPrompt(MCPrompt::None);
}

auto MCTelemetry::Reporting() -> bool
{
    return Backend != nullptr && Started && GReports != 0 && State.Seen >= ReportVersion;
}

auto MCTelemetry::Frame() -> void
{
    const Clock::time_point now = Clock::now();
    const std::optional<Clock::time_point> last = std::exchange(LastFrame, now);

    if (last.has_value())
    {
        Frame(std::chrono::duration<double, std::milli>(now - *last).count());
    }
    else if (Backend != nullptr)
    {
        // Nothing to measure from: the first frame, or the first after a load (the load isn't a frame).
        Tick();
    }
}

auto MCTelemetry::Frame(double milliseconds) -> void
{
    if (Backend == nullptr)
    {
        return;
    }

    Tick();

    if (!Play.Running)
    {
        return;
    }

    MCTally& tally = Play.Tally;
    tally.Histogram[Bucket(milliseconds)]++;
    tally.Seconds += milliseconds / MillisecondsPerSecond;
    tally.WorstMilliseconds = std::max(tally.WorstMilliseconds, milliseconds);
    Box->PlaySeconds = static_cast<float>(tally.Seconds);
    Play.SincePower += milliseconds / MillisecondsPerSecond;

    if (Play.SincePower >= PowerSeconds)
    {
        Play.SincePower = 0.0;
        const MCPower power = Backend->Power();
        tally.Thermal = std::max(tally.Thermal, power.Thermal);
        tally.Battery = tally.Battery || power.Battery;
        tally.LowPower = tally.LowPower || power.LowPower;
    }
}

auto MCTelemetry::PlayLoading(std::string_view mission, bool match) -> void
{
    // A name only, should a mission ever be loaded by its path (which can hold the player's name).
    mission = mission.substr(mission.find_last_of("/\\") + 1);
    Play = {};
    Play.Loading = true;
    Play.Match = match;
    Play.Mission = mission;
    Play.LoadStart = Clock::now();
    SetField(Box->Mission, mission);
    Box->Players = 0;
    Box->Server = 0;
    Box->PlayersLost = 0;
    Box->PlaySeconds = 0.0f;
    SetPhase(MCPhase::Loading);
}

auto MCTelemetry::PlayStarted(int32_t players, bool server) -> void
{
    if (!Play.Loading)
    {
        return;
    }

    Play.Loading = false;
    Play.Running = true;
    Play.Players = players;
    Play.Server = server;
    Play.Tally.Count = 1;
    Play.Tally.LoadSeconds = std::chrono::duration<double>(Clock::now() - Play.LoadStart).count();
    // The frame the load took isn't play: the next frame measures from the next.
    LastFrame.reset();
    Box->Players = static_cast<uint8_t>(std::clamp(players, 0, static_cast<int32_t>(UINT8_MAX)));
    Box->Server = server ? 1 : 0;
    SetPhase(Play.Match ? MCPhase::Match : MCPhase::Mission);
}

auto MCTelemetry::PlayEnded() -> void
{
    if (!Play.Running)
    {
        Play = {};
        SetPhase(MCPhase::Menus);
        return;
    }

    SetPhase(MCPhase::Results);

    if (Reporting() && Play.Tally.Seconds >= MinimumPlaySeconds)
    {
        if (Play.Match)
        {
            SendMatch();
        }
        else
        {
            State.Missions.Add(Play.Tally);
            const int64_t day = Today();

            if (State.MissionDay == 0 || day - State.MissionDay >= ReportDays)
            {
                SendMissions();
            }
            else
            {
                WriteState();
            }
        }
    }

    Play = {};
}

auto MCTelemetry::PlayerLost() -> void
{
    if (Play.Running)
    {
        Play.PlayersLost++;
    }

    Box->PlayersLost = static_cast<uint8_t>(std::min<int32_t>(Box->PlayersLost + 1, UINT8_MAX));
    Record(MCEventKind::PlayerLost, 0);
}

auto MCTelemetry::MovieFellBehind() -> void
{
    if (Reporting())
    {
        State.Missions.MoviesBehind++;
        WriteState();
    }
}

auto MCTelemetry::MessageIn(uint16_t type, size_t bytes) -> void
{
    Record(MCEventKind::MessageIn, type, bytes);
}

auto MCTelemetry::MessageOut(uint16_t type, size_t bytes) -> void
{
    Record(MCEventKind::MessageOut, type, bytes);
}

auto MCTelemetry::Percentile(std::span<const uint32_t> histogram, double share) -> double
{
    const uint64_t total = std::accumulate(histogram.begin(), histogram.end(), uint64_t{0});

    if (total == 0)
    {
        return 0.0;
    }

    // The bucket that holds the frame at that share, by its upper edge.
    const auto wanted = static_cast<uint64_t>(std::ceil(share * static_cast<double>(total)));
    uint64_t seen = 0;

    for (size_t i = 0; i < histogram.size(); i++)
    {
        seen += histogram[i];

        if (seen >= std::max<uint64_t>(wanted, 1))
        {
            return static_cast<double>(i + 1) * BucketMilliseconds;
        }
    }

    return static_cast<double>(histogram.size()) * BucketMilliseconds;
}

auto MCTelemetry::AskFirst(std::string_view country, int64_t day) -> bool
{
    constexpr size_t codeLength = 2;

    if (country.size() != codeLength || std::ranges::contains(AskCountries, country))
    {
        return true;
    }

    return country == India && day >= IndiaConsentDay;
}
