#include "stdafx.h"
#include "logistics/MCSaves.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCMissionNames.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGamePaths.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "main/MCLogisticsShared.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"

namespace
{
    /// <summary>A save's load result before its job has run (LoadCampaign's are 0 or its errors).</summary>
    constexpr int32_t NotLoaded = -1;
    /// <summary>The briefing's music, on once a save is loaded (MCMission's BriefingTrack).</summary>
    constexpr int32_t BriefingMusic = 0x16;
    constexpr std::string_view SaveExtension = ".sav";
    /// <summary>Port-only (stage 7): a single mission's file, and its campaign's master file's name before its own.</summary>
    constexpr std::string_view SoloExtension = ".sol";
    constexpr std::string_view SoloMasterPrefix = "campaign";
    /// <summary>Between the thousands of resource points (<see cref="MCSaves::PointsText"/>).</summary>
    constexpr char ThousandsMark = ',';
    /// <summary>The campaigns' master mission files, by the saves' [Planet] Setting (Port Arthur's, Cermak's).</summary>
    constexpr std::array<std::string_view, 2> MasterFiles = {"mechcmdr1", "xmechcmdr1"};
    /// <summary>The game's names for its own saves ("PortArthurBefore_Op%i_Mis%i"), by campaign, and their length.</summary>
    constexpr std::array<uint32_t, 2> BeforeNames = {0x37b, 0x387};
    constexpr std::array<uint32_t, 2> AfterNames = {0x37a, 0x386};
    constexpr int SaveNameLength = 199;
    /// <summary>The default name for a save the player doesn't name ("savegame%03i"), its length and its numbers.</summary>
    constexpr uint32_t DefaultName = 0x37c;
    constexpr int DefaultNameLength = 0x95;
    constexpr int32_t DefaultNames = 1000;
    /// <summary>The longest default save name (the original's 0x68-byte buffer).</summary>
    constexpr size_t MaxDefaultNameLength = 0x67;
    /// <summary>The longest scenario and briefing names the game reads (GetCurrentMission's).</summary>
    constexpr size_t ScenarioChars = 0xff;
    constexpr size_t BriefingChars = 0x7f;
    /// <summary>
    /// The most a save's packet may unpack to (MCLogisticsCampaign.cpp's MostSavePacket: far over any real save's).
    /// </summary>
    constexpr int32_t MostSavePacket = 16 << 20;
    /// <summary>
    /// The room a save needs on the disk: a campaign's save is some tens of kilobytes, so with less than this the disk
    /// is as good as full and the save is refused before it's half written.
    /// </summary>
    constexpr std::uintmax_t RoomForASave = 8 << 20;
    constexpr int64_t NanosecondsPerSecond = 1'000'000'000;
    /// <summary>The names Windows keeps for its devices (MSDN's "Naming Files, Paths, and Namespaces"), lower case.</summary>
    constexpr std::array<std::string_view, 4> Devices = {"con", "prn", "aux", "nul"};
    /// <summary>The numbered ones: COM and LPT with a digit or a superscript 1, 2 or 3 (in UTF-8).</summary>
    constexpr std::array<std::string_view, 2> NumberedDevices = {"com", "lpt"};
    constexpr size_t NumberedDeviceLength = 3;
    constexpr std::array<std::string_view, 13> DeviceNumbers = {"0", "1", "2", "3",        "4",        "5",       "6",
                                                                "7", "8", "9", "\xc2\xb9", "\xc2\xb2", "\xc2\xb3"};
    /// <summary>The control characters, which no save's name takes: below the space, and DEL.</summary>
    constexpr unsigned char FirstPrintable = 0x20;
    constexpr unsigned char DeleteCharacter = 0x7f;

    bool IsControl(char letter)
    {
        const auto code = static_cast<unsigned char>(letter);
        return code < FirstPrintable || code == DeleteCharacter;
    }

    constexpr int64_t SecondsPerDay = 24 * 60 * 60;
    constexpr std::array<std::string_view, 12> Months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    using MCMissionNames::Lowered;

    std::string SaveFile(std::string_view name)
    {
        return GamePath(SavePath, name, SaveExtension);
    }

    /// <summary>The operation and mission numbers <paramref name="name"/> has by <paramref name="format"/>, if it has it.</summary>
    std::optional<std::pair<int32_t, int32_t>> NumbersBy(std::string_view name, const std::string& format)
    {
        int32_t operation = 0;
        int32_t mission = 0;
        int consumed = -1;
        const std::string whole(name);
        // The game's format with %n after it: only a name it makes whole is one of its own.
        const std::string withEnd = format + "%n";

        // NOLINTNEXTLINE(cert-err34-c): the numbers are checked by %n's count, not sscanf's result.
        if (std::sscanf(whole.c_str(), withEnd.c_str(), &operation, &mission, &consumed) == 2 &&
            consumed == static_cast<int>(whole.size()))
        {
            return std::pair{operation, mission};
        }

        return std::nullopt;
    }

    /// <summary>The local date and time of <paramref name="time"/>.</summary>
    SDL_DateTime LocalOf(SDL_Time time)
    {
        SDL_DateTime date{};

        if (!SDL_TimeToDateTime(time, &date, true))
        {
            SDL_Log("MCSaves: the time %lld can't be told in local time: %s", static_cast<long long>(time),
                    SDL_GetError());
        }

        return date;
    }

    bool SameDay(const SDL_DateTime& a, const SDL_DateTime& b)
    {
        return a.year == b.year && a.month == b.month && a.day == b.day;
    }

    /// <summary>How long ago, by the calendar, <paramref name="saved"/> is from <paramref name="now"/>.</summary>
    enum class MCDay : uint8_t
    {
        Today,
        Yesterday,
        ThisYear,
        Earlier,
    };

    MCDay DayOf(const SDL_DateTime& saved, SDL_Time now)
    {
        const SDL_DateTime today = LocalOf(now);

        if (SameDay(saved, today))
        {
            return MCDay::Today;
        }

        if (SameDay(saved, LocalOf(now - SecondsPerDay * NanosecondsPerSecond)))
        {
            return MCDay::Yesterday;
        }

        return saved.year == today.year ? MCDay::ThisYear : MCDay::Earlier;
    }

    std::string_view MonthOf(const SDL_DateTime& date)
    {
        return Months[static_cast<size_t>(std::clamp(date.month, 1, static_cast<int>(Months.size())) - 1)];
    }

    /// <summary>When <paramref name="file"/> was last written, as SDL tells time; 0 if that can't be told.</summary>
    SDL_Time WrittenAt(const std::filesystem::path& file)
    {
        std::error_code error;
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(file, error);

        if (error)
        {
            SDL_Log("MCSaves: when %s was written can't be told (%s)", MCFileSystem::ToUtf8(file).c_str(),
                    error.message().c_str());
            return 0;
        }

        // The file clock's epoch is the library's own: measured from now on both clocks.
        const auto sinceNow = written - std::filesystem::file_time_type::clock::now();
        const auto atSystem = std::chrono::system_clock::now() +
                              std::chrono::duration_cast<std::chrono::system_clock::duration>(sinceNow);
        return std::chrono::duration_cast<std::chrono::nanoseconds>(atSystem.time_since_epoch()).count();
    }

    /// <summary>A text file's contents; none when it can't be read.</summary>
    std::optional<std::string> ReadTextFile(const std::string& gamePath)
    {
        MCFile file;

        if (file.Open(gamePath) != 0)
        {
            return std::nullopt;
        }

        std::string text(file.FileSize(), '\0');
        const int32_t read = file.Read(std::span(reinterpret_cast<uint8_t*>(text.data()), text.size()));
        file.Close();

        if (read < 0 || static_cast<size_t>(read) != text.size())
        {
            SDL_Log("MCSaves: %s read short (%d of %zu bytes)", gamePath.c_str(), read, text.size());
            return std::nullopt;
        }

        return text;
    }

    /// <summary>What a scenario's mission file and briefing say of it, as the menus name it.</summary>
    struct MCMissionFacts
    {
        std::string Operation;
        std::string Mission;
        /// <summary>The tons that drop (its MaxTonnage); 0 when it doesn't say.</summary>
        int32_t Tons = 0;
        /// <summary>Its briefing file.</summary>
        std::string Briefing;
    };

    /// <summary>What <paramref name="scenario"/>'s mission file and briefing say of it.</summary>
    MCMissionFacts FactsOf(std::string_view scenario)
    {
        MCFitIniFile file;
        std::string briefing;

        if (file.Open(GamePath(MissionPath, scenario, ".fit")) != 0 || file.SeekBlock("Campaign") != 0 ||
            !ReadText(file, "BriefingFile", BriefingChars, briefing))
        {
            SDL_Log("MCSaves: the mission %.*s names no briefing", static_cast<int>(scenario.size()), scenario.data());
            return {};
        }

        MCMissionFacts facts{.Tons = file.Read<int32_t>("MaxTonnage").value_or(0), .Briefing = briefing};
        const std::optional<std::string> text = ReadTextFile(GamePath(MissionPath, briefing));

        if (!text.has_value())
        {
            SDL_Log("MCSaves: the briefing %s can't be read", briefing.c_str());
            return facts;
        }

        const MCMissionNames::MCWritten written = MCMissionNames::FromBriefing(*text);
        facts.Operation = MCMissionNames::Operation(written.Operation);
        facts.Mission = MCMissionNames::Mission(briefing, written.Mission);
        return facts;
    }

    /// <summary>
    /// Opens the settings (the first packet) of the save <paramref name="gamePath"/> into <paramref name="settings"/>
    /// (<paramref name="packets"/> holding it); false, and the log told, when it can't be read.
    /// </summary>
    bool OpenSettings(const std::string& gamePath, MCPacketFile& packets, MCFitIniFile& settings)
    {
        const int32_t opened = packets.Open(gamePath);
        packets.SetMostUnpacked(MostSavePacket);

        // One that can't be read is listed by its name (issue #25: the original read on in a file it couldn't open).
        if (opened != 0 || packets.SeekPacket(0) != 0 ||
            settings.Open(packets, static_cast<uint32_t>(packets.GetPacketSize())) != 0)
        {
            SDL_Log("MCSaves: the save %s can't be read", gamePath.c_str());
            return false;
        }

        return true;
    }

    /// <summary>How many units a save's force has under <paramref name="block"/>'s <paramref name="count"/> (0 for none).</summary>
    int32_t ForceCount(MCFitIniFile& settings, std::string_view block, std::string_view count)
    {
        return settings.SeekBlock(block) == 0 ? settings.Read<int32_t>(count).value_or(0) : 0;
    }

    /// <summary>
    /// <paramref name="settings"/>' entry <paramref name="name"/> in its block <paramref name="block"/>; none without the
    /// block. (A default MCFitResult holds a 0: a save cut short before its points read as one with 0 RP.)
    /// </summary>
    template <MCFitValue T> MCFitResult<T> ReadIn(MCFitIniFile& settings, std::string_view block, std::string_view name)
    {
        if (settings.SeekBlock(block) != 0)
        {
            return std::unexpected(MCFitError::BlockNotFound);
        }

        return settings.Read<T>(name);
    }

    /// <summary>A save's mission number (its General block's) and its points (its ResourcePoints block's).</summary>
    std::pair<MCFitResult<int32_t>, MCFitResult<uint32_t>> MissionAndPoints(MCFitIniFile& settings)
    {
        return {ReadIn<int32_t>(settings, "General", "MissionNumber"),
                ReadIn<uint32_t>(settings, "ResourcePoints", "numPoints")};
    }
}

auto MCSaves::ReadSettings(const std::string& gamePath) -> std::optional<MCSettings>
{
    MCPacketFile packets;
    MCFitIniFile settings;

    if (!OpenSettings(gamePath, packets, settings))
    {
        return std::nullopt;
    }

    const int32_t planet = ReadIn<int32_t>(settings, "Planet", "Setting").value_or(0);
    const auto [mission, points] = MissionAndPoints(settings);

    if (planet < 0 || static_cast<size_t>(planet) >= MasterFiles.size())
    {
        SDL_Log("MCSaves: the save %s names campaign %d, which the game doesn't have", gamePath.c_str(), planet);
        return std::nullopt;
    }

    if (!mission.has_value() || !points.has_value())
    {
        SDL_Log("MCSaves: the save %s names no mission or no points", gamePath.c_str());
        return std::nullopt;
    }

    return MCSettings{
        .Campaign = static_cast<MCCampaign>(planet), .MissionNumber = *mission, .ResourcePoints = *points};
}

auto MCSaves::ReadScenarios(std::string_view master) -> std::vector<MCScenario>
{
    std::vector<MCScenario> scenarios;
    MCFitIniFile file;

    if (file.Open(GamePath(MissionPath, master, ".fit")) != 0 || file.SeekBlock("Scenarios") != 0)
    {
        SDL_Log("MCSaves: the master mission file %.*s can't be read", static_cast<int>(master.size()), master.data());
        return scenarios;
    }

    const uint32_t count = file.Read<uint32_t>("numScenarios").value_or(0);

    for (uint32_t index = 0; index < count; index++)
    {
        std::string name;

        if (!ReadText(file, std::format("Scenario{}", index), ScenarioChars, name))
        {
            break;
        }

        scenarios.push_back(MCScenario{.File = std::move(name)});
    }

    if (file.SeekBlock("OpInfo") == 0)
    {
        for (size_t index = 0; index < scenarios.size(); index++)
        {
            scenarios[index].Operation = file.Read<int32_t>(std::format("Scenario{}Operation", index)).value_or(0);
            scenarios[index].Mission = file.Read<int32_t>(std::format("Scenario{}Mission", index)).value_or(0);
        }
    }

    return scenarios;
}

MCSaves& MCSaves::Game()
{
    static MCSaves saves;
    return saves;
}

void MCSaves::Refresh()
{
    std::array<std::vector<MCScenario>, MasterFiles.size()> campaigns;

    for (size_t index = 0; index < MasterFiles.size(); index++)
    {
        campaigns[index] = ReadScenarios(MasterFiles[index]);
    }

    std::unordered_map<std::string, MCNamed> named;
    _Saves.clear();

    for (const MCFoundFile& found : MCFileSystem::FindFileSources(GamePath(SavePath, "*", SaveExtension)))
    {
        MCSave& save = _Saves.emplace_back();
        save.Name = MCFileSystem::ToUtf8(MCFileSystem::FromUtf8(found.Name).stem());
        save.InUserFolder = found.InUserFolder;
        save.InGameFolder = found.InGameFolder;
        save.Saved = WrittenAt(MCFileSystem::Resolve(SaveFile(save.Name)));
        ReadSave(save, campaigns, named);
    }

    std::ranges::stable_sort(_Saves, [](const MCSave& a, const MCSave& b)
                             { return a.Saved != b.Saved ? a.Saved > b.Saved : Lowered(a.Name) < Lowered(b.Name); });
    _Revision++;
}

void MCSaves::ReadSave(MCSave& save, std::span<const std::vector<MCScenario>> campaigns,
                       std::unordered_map<std::string, MCNamed>& named) const
{
    // A damaged save is listed by its name alone, off CONTINUE and CAMPAIGNS (Latest).
    const std::optional<MCSettings> settings = ReadSettings(SaveFile(save.Name));

    if (!settings.has_value())
    {
        return;
    }

    const size_t campaign = static_cast<size_t>(settings->Campaign);
    const std::vector<MCScenario>& scenarios = campaigns[campaign];
    save.Campaign = settings->Campaign;
    save.ResourcePoints = settings->ResourcePoints;
    const int32_t missionNumber = settings->MissionNumber;
    // The game's own saves name their mission (the one begun or played); a named save is the briefing's it was made at.
    std::optional<size_t> scenario;

    if (missionNumber >= 0 && static_cast<size_t>(missionNumber) < scenarios.size())
    {
        scenario = static_cast<size_t>(missionNumber);
        // Port-only (stage 7): the mission it opens on is its number's (an after save's, the next one).
        save.OpensOperation = scenarios[*scenario].Operation;
        save.OpensMission = scenarios[*scenario].Mission;
    }

    const auto byNumbers = [&](MCWhen when, uint32_t format)
    {
        const std::optional<std::pair<int32_t, int32_t>> numbers =
            NumbersBy(save.Name, LoadGameString(format, SaveNameLength));

        if (!numbers.has_value())
        {
            return false;
        }

        save.When = when;
        const auto found =
            std::ranges::find_if(scenarios, [&](const MCScenario& entry)
                                 { return entry.Operation == numbers->first && entry.Mission == numbers->second; });

        if (found != scenarios.end())
        {
            scenario = static_cast<size_t>(found - scenarios.begin());
        }

        return true;
    };

    if (!byNumbers(MCWhen::Before, BeforeNames[campaign]) && !byNumbers(MCWhen::After, AfterNames[campaign]))
    {
        save.When = MCWhen::Named;
    }

    if (!scenario.has_value())
    {
        SDL_Log("MCSaves: the save %s's mission %d isn't in its campaign", save.Name.c_str(), missionNumber);
        return;
    }

    NameMission(save, scenarios[*scenario], named);

    // A save whose number names no mission of its campaign opens on the one it names.
    if (save.OpensOperation == 0)
    {
        save.OpensOperation = save.Operation;
        save.OpensMission = save.Mission;
    }

    save.Read = true;
}

void MCSaves::NameMission(MCSave& save, const MCScenario& scenario, std::unordered_map<std::string, MCNamed>& named)
{
    save.Operation = scenario.Operation;
    save.Mission = scenario.Mission;
    save.Scenario = scenario.File;
    auto [names, made] = named.try_emplace(Lowered(scenario.File));

    if (made)
    {
        MCMissionFacts facts = FactsOf(scenario.File);
        names->second = MCNamed{std::move(facts.Operation), std::move(facts.Mission)};
    }

    save.OperationName = names->second.Operation;
    save.MissionName = names->second.Mission;
}

auto MCSaves::Unsaved(MCCampaign campaign, int32_t mission, uint32_t points) -> MCSave
{
    MCSave save{.Campaign = campaign, .When = MCWhen::Named, .ResourcePoints = points};
    const std::vector<MCScenario> scenarios = ReadScenarios(MasterFiles[static_cast<size_t>(campaign)]);

    if (mission < 0 || static_cast<size_t>(mission) >= scenarios.size())
    {
        SDL_Log("MCSaves: the campaign's mission %d isn't in its master mission file", mission);
        return save;
    }

    std::unordered_map<std::string, MCNamed> named;
    NameMission(save, scenarios[static_cast<size_t>(mission)], named);
    save.OpensOperation = save.Operation;
    save.OpensMission = save.Mission;
    save.Read = true;
    return save;
}

auto MCSaves::ReadSolo(std::string name) -> MCSolo
{
    MCSolo solo{.Name = std::move(name)};
    MCPacketFile packets;
    MCFitIniFile settings;

    if (!OpenSettings(GamePath(SavePath, solo.Name, SoloExtension), packets, settings))
    {
        return solo;
    }

    const auto [missionNumber, points] = MissionAndPoints(settings);
    // Its campaign of one mission, as the game loads it (MCLogistics::ReadCampaign's "campaign" and its name).
    const std::vector<MCScenario> scenarios = ReadScenarios(std::string(SoloMasterPrefix) + solo.Name);

    if (!missionNumber.has_value() || !points.has_value() || *missionNumber < 0 ||
        static_cast<size_t>(*missionNumber) >= scenarios.size())
    {
        SDL_Log("MCSaves: the single mission %s names no mission or points its campaign has", solo.Name.c_str());
        return solo;
    }

    solo.ResourcePoints = *points;
    solo.Units = ForceCount(settings, "Mechs", "NumMechs") + ForceCount(settings, "AssMechs", "NumAssMechs") +
                 ForceCount(settings, "Vehicles", "NumVehicles") +
                 ForceCount(settings, "AssVehicles", "NumAssVehicles");
    const MCMissionFacts facts = FactsOf(scenarios[static_cast<size_t>(*missionNumber)].File);
    solo.MissionName = facts.Mission;
    solo.Briefing = facts.Briefing;
    solo.Tons = facts.Tons;

    if (solo.MissionName.empty())
    {
        SDL_Log("MCSaves: the single mission %s's briefing %s names no mission", solo.Name.c_str(),
                solo.Briefing.c_str());
    }

    solo.Read = !solo.MissionName.empty();
    return solo;
}

auto MCSaves::SoloMissions() -> std::vector<MCSolo>
{
    std::vector<MCSolo> missions;

    for (const MCFoundFile& found : MCFileSystem::FindFileSources(GamePath(SavePath, "*", SoloExtension)))
    {
        missions.push_back(ReadSolo(MCFileSystem::ToUtf8(MCFileSystem::FromUtf8(found.Name).stem())));
    }

    std::ranges::sort(missions, [](const MCSolo& a, const MCSolo& b) { return Lowered(a.Name) < Lowered(b.Name); });
    return missions;
}

const std::vector<MCSaves::MCSave>& MCSaves::All() const
{
    return _Saves;
}

std::vector<std::reference_wrapper<const MCSaves::MCSave>> MCSaves::Of(std::optional<MCCampaign> campaign) const
{
    std::vector<std::reference_wrapper<const MCSave>> saves;

    for (const MCSave& save : _Saves)
    {
        // A save that can't be read is every campaign's: it can be deleted wherever it's listed.
        if (!campaign.has_value() || !save.Read || save.Campaign == *campaign)
        {
            saves.emplace_back(save);
        }
    }

    return saves;
}

MCOptionalRef<const MCSaves::MCSave> MCSaves::Latest(std::optional<MCCampaign> campaign) const
{
    const auto found = std::ranges::find_if(
        _Saves, [&](const MCSave& save) { return save.Read && (!campaign.has_value() || save.Campaign == *campaign); });
    return found != _Saves.end() ? MCOptionalRef<const MCSave>(*found) : std::nullopt;
}

MCOptionalRef<const MCSaves::MCSave> MCSaves::Find(std::string_view name) const
{
    const std::string wanted = Lowered(name);
    const auto found = std::ranges::find_if(_Saves, [&](const MCSave& save) { return Lowered(save.Name) == wanted; });
    return found != _Saves.end() ? MCOptionalRef<const MCSave>(*found) : std::nullopt;
}

bool MCSaves::Delete(std::string_view name)
{
    // The original cleared the file's read-only attribute first.
    const std::string file = SaveFile(name);
    const bool removed = _Remove ? _Remove(file) : MCFileSystem::RemoveSave(file);

    if (!removed)
    {
        SDL_Log("MCSaves: the save %.*s couldn't be deleted", static_cast<int>(name.size()), name.data());
    }

    Refresh();
    return removed;
}

bool MCSaves::HasRoom() const
{
    if (_Room)
    {
        return _Room() >= RoomForASave;
    }

    std::error_code error;
    const std::filesystem::path root =
        MCFileSystem::UserRoot().empty() ? MCFileSystem::GameRoot() : MCFileSystem::UserRoot();
    const std::filesystem::space_info space = std::filesystem::space(root, error);

    if (error)
    {
        SDL_Log("MCSaves: the room on %s's disk can't be told (%s)", MCFileSystem::ToUtf8(root).c_str(),
                error.message().c_str());
        return true;
    }

    return space.available >= RoomForASave;
}

MCSaves::MCWritten MCSaves::Write(std::string_view name)
{
    if (!CanName(name))
    {
        SDL_Log("MCSaves: '%.*s' can't name a save's file", static_cast<int>(name.size()), name.data());
        return MCWritten::BadName;
    }

    if (!HasRoom())
    {
        SDL_Log("MCSaves: no room on the disk to save %.*s", static_cast<int>(name.size()), name.data());
        return MCWritten::DiskFull;
    }

    const int32_t result = MCLogistics::SaveCampaign(name);
    Refresh();

    if (result == 0)
    {
        return MCWritten::Saved;
    }

    SDL_Log("MCSaves: saving %.*s failed (%d)", static_cast<int>(name.size()), name.data(), result);
    return HasRoom() ? MCWritten::Failed : MCWritten::DiskFull;
}

std::optional<std::string> MCSaves::FreeName()
{
    for (int32_t number = 0; number < DefaultNames; number++)
    {
        std::string candidate = MCFormatPrintf(LoadGameString(DefaultName, DefaultNameLength).c_str(), number);
        candidate.resize(std::min(candidate.size(), MaxDefaultNameLength));

        if (!FileExists(SaveFile(candidate)))
        {
            return candidate;
        }
    }

    return std::nullopt;
}

bool MCSaves::CanName(std::string_view name)
{
    // The characters no file system's name takes, or that would put the save in another folder.
    constexpr std::string_view NotInAName = "\\/:*?\"<>|";

    if (name.empty() || name.find_first_not_of(' ') == std::string_view::npos ||
        name.find_first_of(NotInAName) != std::string_view::npos ||
        std::ranges::any_of(name, [](char letter) { return IsControl(letter); }))
    {
        return false;
    }

    // Windows drops a name's last dots and spaces, so "Kaio." would save as another name ("." and ".." are folders).
    if (name.back() == '.' || name.back() == ' ')
    {
        return false;
    }

    // A device's name is a file's whatever its extension ("con.sav", "LPT1 .txt"): the part before the first dot,
    // without its last spaces.
    std::string stem = Lowered(name.substr(0, name.find('.')));
    stem.erase(stem.find_last_not_of(' ') + 1);

    if (std::ranges::find(Devices, stem) != Devices.end())
    {
        return false;
    }

    const std::string_view numbered(stem);
    return !(numbered.size() > NumberedDeviceLength &&
             std::ranges::find(NumberedDevices, numbered.substr(0, NumberedDeviceLength)) != NumberedDevices.end() &&
             std::ranges::find(DeviceNumbers, numbered.substr(NumberedDeviceLength)) != DeviceNumbers.end());
}

uint64_t MCSaves::Revision() const
{
    return _Revision;
}

std::string MCSaves::PointsText(uint32_t points)
{
    constexpr size_t Group = 3;
    std::string digits = std::to_string(points);

    for (size_t at = digits.size(); at > Group; at -= Group)
    {
        digits.insert(at - Group, 1, ThousandsMark);
    }

    return digits;
}

std::string_view MCSaves::CampaignName(MCCampaign campaign)
{
    return campaign == MCCampaign::Expansion ? "Expansion campaign" : "Original campaign";
}

std::string_view MCSaves::CampaignShortName(MCCampaign campaign)
{
    return campaign == MCCampaign::Expansion ? "Expansion" : "Original";
}

std::string MCSaves::DateText(SDL_Time saved) const
{
    if (saved == 0)
    {
        return {};
    }

    const SDL_DateTime date = LocalOf(saved);
    const std::string time = std::format("{:02}:{:02}", date.hour, date.minute);

    switch (DayOf(date, Now()))
    {
        case MCDay::Today:
            return "Today " + time;
        case MCDay::Yesterday:
            return "Yesterday " + time;
        case MCDay::ThisYear:
            return std::format("{} {} {}", date.day, MonthOf(date), time);
        case MCDay::Earlier:
            break;
    }

    return std::format("{} {} {}", date.day, MonthOf(date), date.year);
}

std::string MCSaves::DateSentence(SDL_Time saved) const
{
    if (saved == 0)
    {
        return {};
    }

    const SDL_DateTime date = LocalOf(saved);
    const std::string time = std::format("{:02}:{:02}", date.hour, date.minute);

    switch (DayOf(date, Now()))
    {
        case MCDay::Today:
            return "today at " + time;
        case MCDay::Yesterday:
            return "yesterday at " + time;
        case MCDay::ThisYear:
            return std::format("on {} {} at {}", date.day, MonthOf(date), time);
        case MCDay::Earlier:
            break;
    }

    return std::format("on {} {} {}", date.day, MonthOf(date), date.year);
}

void MCSaves::SetClock(std::function<SDL_Time()> clock)
{
    _Clock = std::move(clock);
}

SDL_Time MCSaves::Now() const
{
    if (_Clock)
    {
        return _Clock();
    }

    SDL_Time now = 0;

    if (!SDL_GetCurrentTime(&now))
    {
        SDL_Log("MCSaves: the time now can't be told: %s", SDL_GetError());
    }

    return now;
}

MCSavedGameLoad MCSaves::LoadSavedGameAhead(std::string_view name, bool campaign)
{
    MCSavedGameLoad load{.Result = std::make_shared<int32_t>(NotLoaded)};
    load.Job.push_back({.Run = [campaign]
                        {
                            Solo = !campaign;
                            LastLogisticsMissionState = 0;
                        }});
    std::ranges::move(GlobalLog()->LoadCampaignPieces(name, campaign ? ".sav" : ".sol", false, true, load.Result),
                      std::back_inserter(load.Job));
    return load;
}

bool MCSaves::ShowSavedGame(int32_t result)
{
    SoundSystem()->StopDigitalMusic();

    if (result != 0)
    {
        return false;
    }

    GlobalLog()->SetUpBriefingScreen(false);
    SoundSystem()->PlayDigitalMusic(BriefingMusic, true);
    return true;
}
