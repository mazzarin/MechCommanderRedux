#include "stdafx.h"
#include "MCTest.h"
#include "RemadeScreens.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPrefsScreen.h"
#include "logistics/MCSettingsModel.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCPrefs.h"
#include "main/MCSystemConfig.h"
#include "main/MCUpdater.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCPresenter.h"
#include "sound/MCHdAudio.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCSceneLights.h"

// The HD settings PREFS keeps (main/MCHdSettings) and Preferences' HD section that edits them: every setting is read
// and written, a click picks a setting's choice, one that needs another is held until it is on, DEFAULTS and ORIGINAL
// set them all, CANCEL puts them back and ACCEPT writes them; with the CPU drawing (the GPU failed to start, or SOFTWARE
// chosen) what only the GPU draws is held.

namespace
{
    /// <summary>The HD settings with no row on the HD OPTIONS page yet (PREFS alone sets them).</summary>
    constexpr std::array<std::string_view, 1> Unlisted = {"MoreUnits"};

    /// <summary>Whether <paramref name="setting"/> has a row on the HD OPTIONS page.</summary>
    bool Listed(const MCHdSettings::MCSetting& setting)
    {
        return !std::ranges::contains(Unlisted, setting.Key);
    }

    /// <summary>Every HD setting as the original's game has it.</summary>
    std::vector<int32_t> OriginalSettings()
    {
        std::vector<int32_t> values;

        for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
        {
            values.push_back(setting.Original);
        }

        return values;
    }

    /// <summary>A PREFS at <paramref name="path"/> whose MechCommander block <paramref name="settings"/> writes.</summary>
    void WriteHdPrefs(const std::filesystem::path& path, const std::function<void(MCFitIniFile&)>& settings)
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Create(MCFileSystem::ToUtf8(path)), 0);
        prefs.WriteBlock("MechCommander");
        settings(prefs);
    }

    /// <summary>Every HD setting read from the PREFS at <paramref name="path"/>.</summary>
    void ReadHdPrefs(const std::filesystem::path& path)
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(path)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        MCHdSettings::Read(prefs);
    }

    /// <summary>The value PREFS at <paramref name="path"/> holds for <paramref name="key"/>, if any.</summary>
    std::optional<int32_t> SavedSetting(const std::filesystem::path& path, std::string_view key)
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(path)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);

        if (MCHdSettings::Find(key).Boolean)
        {
            const MCFitResult<bool> value = prefs.Read<bool>(key);
            return value.has_value() ? std::optional<int32_t>(*value ? 1 : 0) : std::nullopt;
        }

        const MCFitResult<int32_t> value = prefs.Read<int32_t>(key);
        return value.has_value() ? std::optional<int32_t>(*value) : std::nullopt;
    }
}

/// <summary>
/// The table of HD settings: each PREFS key once, each value its own global, the original's defaults off (fleck style
/// at the edge) and the personal build's on (but the fleck style, and REPORTS, the player's own answer, off in both).
/// </summary>
TEST_CASE("hd settings: each HD setting has its own key and global, off in the original, on in the personal build")
{
    std::set<std::string_view> keys;
    std::set<MCAddressKey> values;

    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        CHECK(keys.insert(setting.Key).second);
        CHECK(values.insert(MCAddressKey::Of(setting.Value.get())).second);
        CHECK(&MCHdSettings::Find(setting.Key) == &setting);
        CHECK_EQ(setting.Original, setting.Least);
        CHECK(setting.Personal >= setting.Least);
        CHECK(setting.Personal <= setting.Most);
        CHECK_EQ(setting.Boolean, setting.Most == 1 && setting.Key != "HDFlecks");
        CHECK((setting.Personal != setting.Original) == (setting.Key != "HDFlecks" && !setting.Player));
        // The tests keep the original's defaults (only the app calls UseHdByDefault and UsePersonalGameplay).
        CHECK_EQ(MCHdSettings::Default(setting), setting.Original);
        CHECK_EQ(setting.Gameplay, setting.Key == "MechBraking" || setting.Key == "MoreUnits");
    }

    CHECK_EQ(keys.size(), size_t{18});
}

/// <summary>
/// A copy without HD, its looks the original's: BRAKING and MoreUnits, which play rather than look and need nothing of
/// the pack, still read as on when PREFS lacks them; the HD answer then turns the looks on too.
/// </summary>
TEST_CASE_ISOLATED(
    "hd settings: the gameplay settings are on by default in a copy without HD, whose looks are the original's")
{
    UsePersonalGameplay();

    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        CHECK_EQ(MCHdSettings::Default(setting), setting.Gameplay ? setting.Personal : setting.Original);
    }

    CHECK_EQ(MCHdSettings::Default(MCHdSettings::Find("MechBraking")), 1);
    CHECK_EQ(MCHdSettings::Default(MCHdSettings::Find("MoreUnits")), 1);

    // PREFS without MoreUnits: the personal build's player may own 200 units, and the setting at its default isn't
    // written.
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "mc_personal_gameplay_prefs.cfg";
    WriteHdPrefs(path, [](MCFitIniFile&) {});
    ReadHdPrefs(path);
    CHECK_EQ(GMoreUnits, 1);
    CHECK_EQ(MaxUnits(), 200);
    WriteHdPrefs(path, [](MCFitIniFile& prefs) { MCHdSettings::Write(prefs); });
    CHECK(!SavedSetting(path, "MoreUnits").has_value());
    std::filesystem::remove(path);
    UseHdByDefault();

    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        CHECK_EQ(MCHdSettings::Default(setting), setting.Personal);
    }
}

/// <summary>
/// PREFS keeps every HD setting: written at values other than their defaults, they read back; a PREFS without them
/// reads the defaults; a value past a setting's choices is clamped.
/// </summary>
TEST_CASE("hd settings: PREFS keeps every HD setting and reads it back")
{
    const std::vector<int32_t> was = MCHdSettings::Now();
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "mc_hd_settings_prefs.cfg";
    const auto write = [&](const std::function<void(MCFitIniFile&)>& settings) { WriteHdPrefs(path, settings); };
    const auto read = [&] { ReadHdPrefs(path); };

    // Each at its most: written and read back.
    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        setting.Value.get() = setting.Most;
    }

    const std::vector<int32_t> most = MCHdSettings::Now();
    write([](MCFitIniFile& prefs) { MCHdSettings::Write(prefs); });

    // ORIGINAL sets every look back, and leaves the player's own answers (REPORTS).
    std::vector<int32_t> original = OriginalSettings();

    for (size_t i = 0; i < original.size(); i++)
    {
        original[i] = MCHdSettings::All()[i].Player ? most[i] : original[i];
    }

    MCHdSettings::UseOriginal();
    CHECK(MCHdSettings::Now() == original);
    read();
    CHECK(MCHdSettings::Now() == most);

    // None written: the defaults (the original's, in the tests).
    write([](MCFitIniFile&) {});
    read();
    CHECK(MCHdSettings::Now() == OriginalSettings());

    // Past the choices: clamped.
    write([](MCFitIniFile& prefs) { prefs.WriteIdLong("HDLighting", 9); });
    read();
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));

    std::filesystem::remove(path);
    MCHdSettings::Restore(was);
}

/// <summary>
/// Port-only (MoreUnits): PREFS's MoreUnits raises the unit limit: read as on, a player outside a match may own 200
/// mechs and vehicles; written only while it differs from its default (off, in the tests), so PREFS without it reads
/// off, the original's 50.
/// </summary>
TEST_CASE("hd settings: PREFS keeps MoreUnits, which raises the unit limit to 200")
{
    const std::vector<int32_t> was = MCHdSettings::Now();
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "mc_more_units_prefs.cfg";

    WriteHdPrefs(path, [](MCFitIniFile& prefs) { prefs.WriteIdBoolean("MoreUnits", true); });
    ReadHdPrefs(path);
    CHECK_EQ(GMoreUnits, 1);
    CHECK_EQ(MaxUnits(), 200);
    WriteHdPrefs(path, [](MCFitIniFile& prefs) { MCHdSettings::Write(prefs); });
    CHECK_EQ(SavedSetting(path, "MoreUnits"), std::optional<int32_t>(1));

    GMoreUnits = 0;
    WriteHdPrefs(path, [](MCFitIniFile& prefs) { MCHdSettings::Write(prefs); });
    CHECK(!SavedSetting(path, "MoreUnits").has_value());
    GMoreUnits = 1;
    ReadHdPrefs(path);
    CHECK_EQ(GMoreUnits, 0);
    CHECK_EQ(MaxUnits(), 50);

    std::filesystem::remove(path);
    MCHdSettings::Restore(was);
}

/// <summary>
/// PREFS keeps its format and only the settings that differ from their defaults: a PREFS from a build that wrote every
/// setting, read and saved again, keeps the player's changes (the music turned down, HD LIGHTING at NIGHT) and the
/// player's own answers (REPORTS), and loses the rest; then a release that changes the defaults (here the personal
/// build's) reaches every setting left out, while HD LIGHTING keeps NIGHT.
/// </summary>
TEST_CASE_ISOLATED("game: PREFS keeps only what differs from the defaults, so a changed default reaches the rest")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    constexpr int32_t QuietMusic = MCPrefsDefaults::MusicVolume / 2;
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    const auto readHd = [&] { ReadHdPrefs(prefsPath); };

    // An older build's PREFS: every HD setting, at its default but HD LIGHTING.
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Create(MCFileSystem::ToUtf8(prefsPath)), 0);
        prefs.WriteBlock("MechCommander");

        for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
        {
            prefs.WriteIdLong(setting.Key,
                              setting.Key == "HDLighting" ? HdLightingNight : MCHdSettings::Default(setting));
        }
    }

    readHd();
    REQUIRE_EQ(GHdLighting, static_cast<int>(HdLightingNight));

    // The rest of PREFS at the defaults, but the music.
    GuiSystem().PaletteCycle = MCPrefsDefaults::PaletteCycle ? 1 : 0;
    GuiSystem().GammaLevel = MCPrefsDefaults::Gamma;
    GFullScreen = MCPrefsDefaults::FullScreen ? 1 : 0;
    Force16MB = MCPrefsDefaults::Force16Mb;
    Force32MB = MCPrefsDefaults::Force32Mb;
    GameDifficulty = MCPrefsDefaults::Difficulty;
    const std::shared_ptr<MCSoundSystem> sound = SoundSystem();
    REQUIRE(sound != nullptr);
    sound->MusicLevel = static_cast<uint8_t>(QuietMusic);
    sound->RadioLevel = static_cast<uint8_t>(MCPrefsDefaults::RadioVolume);
    sound->DigitalMasterVolume = static_cast<uint8_t>(MCPrefsDefaults::SfxVolume);
    sound->InterfaceLevel = static_cast<uint8_t>(MCPrefsDefaults::SfxVolume);
    GStretchToFit = MCPrefsDefaults::StretchToFit ? 1 : 0;
    GSoftwareCursor = MCPrefsDefaults::SoftwareCursor ? 1 : 0;
    GRendererPreference = static_cast<int>(MCRendererKind::Vulkan);
    GUiResolution = MCPrefsDefaults::UiResolution;
    GScrollSpeed = AutoScrollSpeed;
    GInterfaceSize = AutoInterfaceSize;
    GShowFpsPreference = MCPrefsDefaults::ShowFps ? 1 : 0;
    DeployMapShown = MCPrefsDefaults::DeployMap;
    SavePrefs();

    // Every key the saved PREFS holds, by name.
    std::set<std::string> keys;
    {
        std::ifstream text(prefsPath);
        REQUIRE(text.is_open());

        for (std::string line; std::getline(text, line);)
        {
            const size_t space = line.find(' ');
            const size_t equals = line.find('=');

            if (space != std::string::npos && equals != std::string::npos && space < equals)
            {
                keys.insert(line.substr(space + 1, equals - space - 1));
            }
        }
    }

    const std::set<std::string> kept = {"HDLighting", "MusicVolume", "PrefsVersion", "Reports"};
    CHECK(keys == kept);

    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(prefsPath)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        CHECK_EQ(prefs.Read<int32_t>("PrefsVersion"), MCFitResult<int32_t>(PrefsVersion));
        CHECK_EQ(prefs.Read<int32_t>("MusicVolume"), MCFitResult<int32_t>(QuietMusic));
    }

    // A release with other defaults: what PREFS leaves out follows them; HD LIGHTING stays at NIGHT.
    UseHdByDefault();
    readHd();
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));

    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        if (setting.Key != "HDLighting" && !setting.Player)
        {
            CHECK_EQ(setting.Value.get(), setting.Personal);
        }
    }

    std::filesystem::remove(prefsPath);
}

/// <summary>
/// PREFS is written as it always was (its writing left the 1998 preferences screen for main/MCPrefs): every setting it
/// writes away from its default, HD LIGHTING at NIGHT and two keys changed, the file holds these bytes exactly.
/// </summary>
TEST_CASE_ISOLATED("game: PREFS is written byte for byte as the 1998 preferences screen wrote it")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    constexpr int32_t Gamma = 3;
    constexpr uint8_t Music = 0x21;
    constexpr uint8_t Radio = 0x22;
    constexpr uint8_t Effects = 0x23;
    constexpr uint8_t Interface = 0x24;
    constexpr int32_t Hard = 2;
    constexpr int32_t UiResolutionChoice = 720;
    constexpr int32_t ScrollSpeedChoice = 200;
    constexpr int32_t InterfaceSizeChoice = 150;
    constexpr uint32_t JumpKey = 0x25;
    GuiSystem().PaletteCycle = MCPrefsDefaults::PaletteCycle ? 0 : 1;
    GuiSystem().GammaLevel = Gamma;
    GFullScreen = MCPrefsDefaults::FullScreen ? 0 : 1;
    Force16MB = !MCPrefsDefaults::Force16Mb;
    Force32MB = !MCPrefsDefaults::Force32Mb;
    GameDifficulty = Hard;
    const std::shared_ptr<MCSoundSystem> sound = SoundSystem();
    REQUIRE(sound != nullptr);
    sound->MusicLevel = Music;
    sound->RadioLevel = Radio;
    sound->DigitalMasterVolume = Effects;
    sound->InterfaceLevel = Interface;
    GStretchToFit = MCPrefsDefaults::StretchToFit ? 0 : 1;
    GSoftwareCursor = MCPrefsDefaults::SoftwareCursor ? 0 : 1;
    GRendererPreference = static_cast<int>(MCRendererKind::Software);
    GUiResolution = UiResolutionChoice;
    GScrollSpeed = ScrollSpeedChoice;
    GInterfaceSize = InterfaceSizeChoice;
    GShowFpsPreference = MCPrefsDefaults::ShowFps ? 0 : 1;
    DeployMapShown = !MCPrefsDefaults::DeployMap;
    GHdLighting = static_cast<int>(HdLightingNight);
    MCKeyBindings::MCKeyTable keys = MCKeyBindings::Player();
    MCKeyBindings::Bind(keys, MCKeyCommand::Jump, JumpKey | KeyCtrl);
    keys[static_cast<size_t>(MCKeyCommand::Eject)] = MCKeyBindings::NoKey;
    MCKeyBindings::SetPlayer(keys);
    SavePrefs();

    std::string written;
    {
        std::ifstream file(prefsPath, std::ios::binary);
        REQUIRE(file.is_open());
        written.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // As the 1998 preferences screen's SavePrefs wrote them (captured from it before the move).
    constexpr std::string_view Written = "FITini \r\n"
                                         "\r\n"
                                         "[MechCommander]\r\n"
                                         "l PrefsVersion=1\r\n"
                                         "b PaletteCycle=TRUE\r\n"
                                         "b DirectDraw=TRUE\r\n"
                                         "b Force16Mb=TRUE\r\n"
                                         "b Force32Mb=TRUE\r\n"
                                         "l Difficulty=2\r\n"
                                         "l Brightness=3\r\n"
                                         "l MusicVolume=33\r\n"
                                         "l RadioVolume=34\r\n"
                                         "l SFXVolume=35\r\n"
                                         "l InterfaceVolume=36\r\n"
                                         "b StretchToFit=TRUE\r\n"
                                         "b SoftwareCursor=TRUE\r\n"
                                         "st Renderer=\"software\"\r\n"
                                         "l UIResolution=720\r\n"
                                         "l ScrollSpeed=200\r\n"
                                         "l InterfaceSize=150\r\n"
                                         "b ShowFps=TRUE\r\n"
                                         "b DeployMap=FALSE\r\n"
                                         "l HDLighting=3\r\n"
                                         "b Reports=FALSE\r\n"
                                         "\r\n"
                                         "[Controls]\r\n"
                                         "st Jump=\"CTRL+K\"\r\n"
                                         "st Eject=\"NONE\"\r\n"
                                         "FITend \r\n";
    CHECK_EQ(written, std::string_view(Written));
    std::filesystem::remove(prefsPath);
}

namespace
{
    /// <summary>Whether Preferences' row of setting <paramref name="id"/> is held (greyed, its line saying why).</summary>
    bool Held(const MCPrefsScreen& prefs, const std::string& id)
    {
        const Rml::Element* row = MCRemadeTest::Find(prefs, "row-" + id);
        REQUIRE(row != nullptr);
        return row->IsClassSet("held");
    }

    /// <summary>The line under the name of Preferences' row of setting <paramref name="id"/>.</summary>
    std::string Line(const MCPrefsScreen& prefs, const std::string& id)
    {
        return MCRemadeTest::Words(prefs, "line-" + id);
    }

    /// <summary>How far Preferences' rows are scrolled down.</summary>
    float RowsScrolled(const MCPrefsScreen& prefs)
    {
        Rml::Element* rows = MCRemadeTest::Find(prefs, "rows");
        REQUIRE(rows != nullptr);
        return rows->GetScrollTop();
    }

    /// <summary>
    /// The settings model's rows whose value follows HD setting <paramref name="setting"/>: those whose shown value moves
    /// when it moves off the original's.
    /// </summary>
    size_t RowsFollowing(const MCHdSettings::MCSetting& setting)
    {
        const std::span<const MCSettingsModel::MCSetting> rows = MCSettingsModel::All();
        std::vector<int> before;

        for (const MCSettingsModel::MCSetting& row : rows)
        {
            before.push_back(row.Get ? row.Get() : 0);
        }

        const int32_t kept = setting.Value.get();
        setting.Value.get() = setting.Original != setting.Most ? setting.Most : setting.Least;
        size_t following = 0;

        for (size_t row = 0; row < rows.size(); row++)
        {
            following += rows[row].Get && rows[row].Get() != before[row] ? 1 : 0;
        }

        setting.Value.get() = kept;
        return following;
    }
}

/// <summary>
/// Preferences' HD section with a player's mouse: every HD setting has one row, but those with none yet; a click on a
/// choice of HD LIGHTING picks it (OFF, ON, DUSK, NIGHT and back); RELIEF needs HD LIGHTING: held (greyed, its line
/// saying so, a click refused) until it's on; HD ART and HD AUDIO on without a pack say what they need on their lines;
/// the wheel scrolls the rows; ORIGINAL turns every HD option off and DEFAULTS puts the defaults back; CANCEL puts every
/// setting back, Preferences opening again at the top of its rows; ACCEPT writes the ones off their defaults to PREFS.
/// </summary>
TEST_CASE_ISOLATED("game: the HD OPTIONS page sets the HD settings with a player's mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const MCTestGame::KeepPrefs keptPrefs;
    REQUIRE(MCTestGame::StartLogistics());

    // The rows as the GPU shows them: on a machine whose GPU couldn't start, the next case's.
    if (GpuFellBack())
    {
        std::cout << "  (skipped: the GPU renderer couldn't start on this machine)\n";
        return;
    }

    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    REQUIRE(MCHdSettings::Now() == OriginalSettings());
    REQUIRE(!MCHdAudio::HasOrchestral());

    // Every HD setting has one row, but those with none yet.
    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        MCTest::Scope scope(std::string(setting.Key));
        CHECK_EQ(RowsFollowing(setting), Listed(setting) ? 1u : 0u);
    }

    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Hd);
    REQUIRE(prefs != nullptr);

    // HD LIGHTING: each choice clicked is picked.
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "hd-lighting"), "Off");

    for (const auto& [choice, lighting, words] :
         {std::tuple{1, HdLightingOn, "On"}, std::tuple{2, HdLightingDusk, "Dusk"},
          std::tuple{3, HdLightingNight, "Night"}, std::tuple{0, HdLightingOff, "Off"}})
    {
        REQUIRE(MCRemadeTest::ClickChoice(*prefs, "hd-lighting", choice));
        CHECK_EQ(GHdLighting, static_cast<int>(lighting));
        CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "hd-lighting"), words);
    }

    // RELIEF needs HD LIGHTING: held until it's on, a click on it refused.
    CHECK(Held(*prefs, "relief"));
    CHECK_EQ(Line(*prefs, "relief"), "Needs HD lighting.");
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "relief", 1));
    CHECK_EQ(GHdRelief, 0);
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "hd-lighting", 1));
    CHECK(!Held(*prefs, "relief"));
    CHECK_EQ(Line(*prefs, "relief"), "");
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "relief", 1));
    CHECK_EQ(GHdRelief, 1);

    // HD ART on without a pack says what it needs.
    REQUIRE(!std::filesystem::exists(MCHdArt::PackFolder()));
    const auto packNeeded = [](std::string_view pack)
    {
        return MCUpdater::HdPack().Managed
                   ? std::string("Needs the HD pack.")
                   : std::format("Needs an HD {} pack in the HD folder beside your saved games.", pack);
    };

    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "hd-art", 1));
    CHECK_EQ(GHdArt, 1);
    CHECK_EQ(Line(*prefs, "hd-art"), packNeeded("art"));

    // The wheel scrolls the rows down.
    {
        Rml::Element* rows = MCRemadeTest::Find(*prefs, "rows");
        REQUIRE(rows != nullptr);
        const Rml::Vector2f middle =
            rows->GetAbsoluteOffset(Rml::BoxArea::Border) + rows->GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
        MCScreenInput::PixelMove(middle.x, middle.y, MCRemadeTest::FrameSeconds);
        constexpr int32_t NotchesDown = -3;
        CHECK_EQ(RowsScrolled(*prefs), 0.0f);
        MCScreenInput::RealWheel(NotchesDown);
        MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
        CHECK(RowsScrolled(*prefs) > 0.0f);
    }

    // ORIGINAL turns everything off; DEFAULTS sets the defaults (the original's, in the tests).
    REQUIRE(MCRemadeTest::Press(*prefs, "original"));
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    CHECK(MCHdSettings::Now() == OriginalSettings());
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "cloud-shadows", 1));
    CHECK_EQ(GHdClouds, 1);
    REQUIRE(MCRemadeTest::Press(*prefs, "defaults"));
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    CHECK(MCHdSettings::Now() == OriginalSettings());

    // HD AUDIO (SOUND's) on without a pack says what it needs; off again, nothing.
    prefs->ShowSection(MCSettingsModel::MCSection::Sound);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "hd-audio", 1));
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOn));
    CHECK_EQ(Line(*prefs, "hd-audio"), packNeeded("audio"));
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "hd-audio", 0));
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOff));
    CHECK_EQ(Line(*prefs, "hd-audio"), "");
    CHECK(!MCRmlDialogs::Shown());

    // CANCEL: every setting is as it was, and the rows open at their top next time.
    prefs->ShowSection(MCSettingsModel::MCSection::Hd);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "cloud-shadows", 1));
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "smooth-turns", 1));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK(MCHdSettings::Now() == OriginalSettings());
    prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Hd);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(RowsScrolled(*prefs), 0.0f);

    // Set again, ACCEPT keeps them and writes the ones off their defaults.
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "particles", 1));
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "flecks", 1));
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "smooth-turns", 1));
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "accept"));
    CHECK_EQ(MCHdSettings::Find("HDParticles").Value.get(), 1);
    CHECK_EQ(SavedSetting(prefsPath, "HDParticles"), std::optional<int32_t>(1));
    CHECK_EQ(SavedSetting(prefsPath, "HDFlecks"), std::optional<int32_t>(1));
    CHECK_EQ(SavedSetting(prefsPath, "HDTurns"), std::optional<int32_t>(1));
    // HD CLOUDS, at its default, isn't written: PREFS without it reads the default.
    CHECK(!SavedSetting(prefsPath, "HDClouds").has_value());

    MCHdSettings::UseOriginal();
}

/// <summary>
/// A run that asked for the GPU renderer and draws on the CPU, the GPU having failed to start (the display's
/// fallback): Preferences' RENDERER says "Vulkan (failed)" and keeps VULKAN for the next start; the HD settings only the
/// GPU draws are held, each line saying it needs Vulkan, and refuse a click; the ones the CPU keeps (HD AUDIO,
/// FOOTPRINTS, the mechs' motion, BRAKING, REPORTS) aren't. With SOFTWARE chosen they stay held; a run on the software
/// renderer by choice with VULKAN chosen for the next start holds none of them for the GPU.
/// </summary>
TEST_CASE_ISOLATED("game: after the GPU fails to start, HD OPTIONS dims what only the GPU draws and RENDERER says so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The display opens on the CPU, as it does when the GPU can't start; then the run is one that asked for the GPU.
    const MCTestGame::KeepPrefs keptPrefs;
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    REQUIRE(GuiSystem().Display() != nullptr);
    REQUIRE(GuiSystem().Display()->RendererKind() == MCRendererKind::Software);
    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    GRenderer = static_cast<int>(MCRendererKind::Vulkan);
    GRendererPreference = GRenderer;
    REQUIRE(GpuFellBack());
    REQUIRE(MCHdSettings::Now() == OriginalSettings());

    // RENDERER: VULKAN, which failed; still the choice.
    std::shared_ptr<MCPrefsScreen> prefs = MCRemadeTest::OpenPreferences(MCSettingsModel::MCSection::Display);
    REQUIRE(prefs != nullptr);
    CHECK_EQ(MCRemadeTest::ChosenWords(*prefs, "renderer"), "Vulkan (failed)");
    CHECK_EQ(GRendererPreference, static_cast<int>(MCRendererKind::Vulkan));
    prefs->ShowSection(MCSettingsModel::MCSection::Hd);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);

    // Every setting with a row is one the GPU alone draws or one the CPU keeps (their rows' ids).
    constexpr std::array<std::string_view, 11> gpuOnly = {"hd-art",        "hd-lighting",   "sun-shadows",  "relief",
                                                          "cloud-shadows", "swaying-trees", "particles",    "flecks",
                                                          "screen-shake",  "weather",       "smooth-motion"};
    constexpr std::array<std::string_view, 6> cpuKeeps = {"hd-audio",     "footprints", "smooth-turns",
                                                          "held-strides", "braking",    "reports"};
    CHECK_EQ(gpuOnly.size() + cpuKeeps.size() + Unlisted.size(), MCHdSettings::All().size());

    // The settings held as they shouldn't be, or not held as they should, named.
    std::string wrong;

    for (const std::string_view id : gpuOnly)
    {
        const std::string row(id);
        wrong += Held(*prefs, row) && Line(*prefs, row) == "Needs Vulkan." ? "" : std::format("{} not held; ", id);
    }

    for (const std::string_view id : cpuKeeps)
    {
        wrong += Held(*prefs, std::string(id)) ? std::format("{} held; ", id) : "";
    }

    CHECK_EQ(wrong, "");

    // A held setting refuses a click.
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "cloud-shadows", 1));
    CHECK_EQ(GHdClouds, 0);

    // SOFTWARE chosen: they stay held.
    prefs->ShowSection(MCSettingsModel::MCSection::Display);
    MCTestGame::RunFrame(MCRemadeTest::FrameSeconds);
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "renderer", 1));
    CHECK_EQ(GRendererPreference, static_cast<int>(MCRendererKind::Software));
    CHECK(Held(*prefs, "cloud-shadows"));
    CHECK_EQ(Line(*prefs, "cloud-shadows"), "Needs Vulkan.");

    // On the software renderer by choice, VULKAN chosen for the next start: nothing held for the GPU.
    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCRemadeTest::ClickChoice(*prefs, "renderer", 0));
    CHECK(!GpuFellBack());
    CHECK(!Held(*prefs, "cloud-shadows"));
    CHECK(Held(*prefs, "relief"));
    CHECK_EQ(Line(*prefs, "relief"), "Needs HD lighting.");
    REQUIRE(MCRemadeTest::LeavePreferences(*prefs, "cancel"));
    CHECK(MCHdSettings::Now() == OriginalSettings());
}
