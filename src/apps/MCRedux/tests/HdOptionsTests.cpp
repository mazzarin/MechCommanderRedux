#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCPresenter.h"
#include "sound/MCHdAudio.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCSceneLights.h"

// The HD settings PREFS keeps (main/MCHdSettings) and the preferences' HD OPTIONS page that edits them: every setting is
// read and written, a click and a right click step a setting through its choices, an option that needs another shows
// dim until it is on, DEFAULTS and ORIGINAL set them all, CANCEL puts them back and ACCEPT writes them; with the CPU
// drawing (the GPU failed to start, or SOFTWARE chosen) what only the GPU draws shows dim.

namespace
{
    using MCScreenInput::FindTextButton;

    /// <summary>Runs a few frames, as a player's screen would between inputs.</summary>
    void Frames()
    {
        constexpr int32_t frames = 3;

        for (int32_t frame = 0; frame < frames; frame++)
        {
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    /// <summary>Clicks the middle of <paramref name="object"/> as a player does.</summary>
    void RealClickOn(MCGuiObject& object)
    {
        MCScreenInput::RealClick(object.GlobalX() + object.Width() / 2, object.GlobalY() + object.Height() / 2);
        Frames();
    }

    /// <summary>The row of the option editing PREFS key <paramref name="key"/>.</summary>
    int32_t RowOf(const MCLogOptionList& list, std::string_view key)
    {
        const std::optional<int32_t> row = list.RowOf(key);
        REQUIRE(row.has_value());
        return *row;
    }

    /// <summary>Scrolls <paramref name="key"/>'s option into the list and clicks its setting (right: a right click).</summary>
    void RealClickOption(MCLogOptionList& list, std::string_view key, bool right = false)
    {
        const int32_t row = RowOf(list, key);
        list.ScrollTo(row - list.VisibleRows() / 2);
        const std::optional<std::pair<int32_t, int32_t>> centre = list.SettingCentre(row);
        REQUIRE(centre.has_value());

        if (right)
        {
            MCScreenInput::RealMove(centre->first, centre->second);
            MCScreenInput::SendMouse(MCGuiEventType::RightButtonDown, centre->first, centre->second, false);
        }
        else
        {
            MCScreenInput::RealClick(centre->first, centre->second);
        }

        Frames();
    }

    /// <summary>Checks that the menus' message dialog is up, saves its shot as <paramref name="shot"/>, and closes it with OK.</summary>
    void CloseMenuMessage(std::string_view shot)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        CHECK(dialog.ShowWindow != 0);
        MCScreenInput::SaveShot(std::string(shot), MCScreenInput::ScreenHash());
        MCLogDialogButton& ok = *dialog.OkButton;
        MCScreenInput::Click(ok.GlobalX() + ok.Width() / 2, ok.GlobalY() + ok.Height() / 2);
        Frames();
        CHECK(dialog.ShowWindow == 0);
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
        CHECK_EQ(setting.Gameplay, setting.Key == "MechBraking");
    }

    CHECK_EQ(keys.size(), size_t{16});
}

/// <summary>
/// A copy without HD, its looks the original's: BRAKING, which plays rather than looks and needs nothing of the pack,
/// still reads as on when PREFS lacks it; the HD answer then turns the looks on too.
/// </summary>
TEST_CASE_ISOLATED("hd settings: braking is on by default in a copy without HD, whose looks are the original's")
{
    UsePersonalGameplay();

    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        CHECK_EQ(MCHdSettings::Default(setting), setting.Gameplay ? setting.Personal : setting.Original);
    }

    CHECK_EQ(MCHdSettings::Default(MCHdSettings::Find("MechBraking")), 1);
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
    const auto write = [&](const std::function<void(MCFitIniFile&)>& settings)
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Create(MCFileSystem::ToUtf8(path)), 0);
        prefs.WriteBlock("MechCommander");
        settings(prefs);
    };

    const auto read = [&]
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(path)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        MCHdSettings::Read(prefs);
    };

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
    const auto readHd = [&]
    {
        MCFitIniFile prefs;
        REQUIRE_EQ(prefs.Open(MCFileSystem::ToUtf8(prefsPath)), 0);
        REQUIRE_EQ(prefs.SeekBlock("MechCommander"), 0);
        MCHdSettings::Read(prefs);
    };

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
/// The HD OPTIONS page with a player's mouse: HD AUDIO's ORCHESTRAL without the pack's shows as ON; OPTIONS in the
/// preferences' HD box shows the page; a click steps a setting on
/// through its choices and round to the first, a right click back; HD AUDIO offers no ORCHESTRAL without the pack's;
/// an option needing another shows dim (and says so) until that one is on; the wheel scrolls; DEFAULTS and ORIGINAL
/// set every setting and say so; DONE goes back; CANCEL puts every setting back and ACCEPT writes them to PREFS.
/// </summary>
TEST_CASE_ISOLATED("game: the HD OPTIONS page sets the HD settings with a player's mouse")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    REQUIRE(MCTestGame::StartLogistics());

    // The page as the GPU shows it: on a machine whose GPU couldn't start, the next case's.
    if (GpuFellBack())
    {
        std::cout << "  (skipped: the GPU renderer couldn't start on this machine)\n";
        return;
    }

    const std::filesystem::path prefsPath = MCFileSystem::UserRoot() / "prefs.cfg";
    std::filesystem::remove(prefsPath);
    REQUIRE(MCHdSettings::Now() == OriginalSettings());
    REQUIRE(!MCHdAudio::HasOrchestral());
    MCGenericScreen& screen = *GlobalLog()->PrefScreen;

    // ORCHESTRAL without the pack's orchestral music shows as ON (it plays the stereo music); CANCEL keeps that.
    GHdAudio = HdAudioOrchestral;
    ShowPreferences();
    Frames();
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOn));
    CancelPrefs();
    Frames();
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOn));
    GHdAudio = HdAudioOff;

    ShowPreferences();
    Frames();
    CHECK(!HdOptionsShown());
    MCScreenInput::SaveShot("hd options prefs", MCScreenInput::ScreenHash());
    const std::shared_ptr<MCLogTextButton> options = FindTextButton(screen, "OPTIONS");
    REQUIRE(options != nullptr);
    RealClickOn(*options);
    REQUIRE(HdOptionsShown());
    const std::shared_ptr<MCLogOptionList> list = HdOptionsList();
    REQUIRE(list != nullptr);
    CHECK_EQ(list->FirstRow(), 0);
    CHECK(list->Status().empty());
    MCScreenInput::SaveShot("hd options", MCScreenInput::ScreenHash());

    // Every HD setting is listed once.
    for (const MCHdSettings::MCSetting& setting : MCHdSettings::All())
    {
        CHECK(list->RowOf(setting.Key).has_value());
    }

    // HD LIGHTING steps OFF, ON, DUSK, NIGHT and round; a right click goes back.
    const int32_t lighting = RowOf(*list, "HDLighting");
    CHECK_EQ(list->ChoiceName(lighting), "OFF");
    RealClickOption(*list, "HDLighting");
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOn));
    CHECK_EQ(list->ChoiceName(lighting), "ON");
    RealClickOption(*list, "HDLighting");
    RealClickOption(*list, "HDLighting");
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    RealClickOption(*list, "HDLighting");
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));
    RealClickOption(*list, "HDLighting", true);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingNight));
    RealClickOption(*list, "HDLighting", true);
    RealClickOption(*list, "HDLighting", true);
    RealClickOption(*list, "HDLighting", true);
    CHECK_EQ(GHdLighting, static_cast<int>(HdLightingOff));

    // RELIEF needs HD LIGHTING: dim until it's on, and it can still be set.
    const int32_t relief = RowOf(*list, "HDRelief");
    CHECK(!list->CanShow(relief));
    const uint32_t dimShown = MCScreenInput::ScreenHash();
    RealClickOption(*list, "HDRelief");
    CHECK_EQ(GHdRelief, 1);
    RealClickOption(*list, "HDLighting");
    CHECK(list->CanShow(relief));
    MCScreenInput::SaveShot("hd options relief shows", MCScreenInput::ScreenHash());
    CHECK(MCScreenInput::ScreenHash() != dimShown);

    // HD AUDIO, without the pack's orchestral music, steps OFF and ON; ON without a pack says where it goes.
    RealClickOption(*list, "HDAudio");
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOn));
    CloseMenuMessage("hd options hd audio needs a pack");
    RealClickOption(*list, "HDAudio");
    CHECK_EQ(GHdAudio, static_cast<int>(HdAudioOff));
    CHECK(GlobalLog()->MessageDialog->ShowWindow == 0);

    // HD ART on without a pack says where it goes.
    REQUIRE(!std::filesystem::exists(MCHdArt::PackFolder()));
    RealClickOption(*list, "HDArt");
    CHECK_EQ(GHdArt, 1);
    CloseMenuMessage("hd options hd art needs a pack");

    // The last rows: the wheel scrolls down to them; BRAKING turns on.
    list->ScrollTo(0);
    const std::optional<std::pair<int32_t, int32_t>> first = list->SettingCentre(lighting);
    REQUIRE(first.has_value());
    MCScreenInput::RealMove(first->first, first->second);
    constexpr int32_t notchesDown = -3;
    MCScreenInput::RealWheel(notchesDown);
    CHECK_EQ(list->FirstRow(), -notchesDown);
    MCScreenInput::SaveShot("hd options scrolled", MCScreenInput::ScreenHash());
    RealClickOption(*list, "MechBraking");
    CHECK_EQ(GMechBraking, 1);

    // ORIGINAL turns everything off, DEFAULTS sets the defaults (the original's, in the tests); each says so.
    const std::shared_ptr<MCLogTextButton> original = FindTextButton(screen, "ORIGINAL");
    REQUIRE(original != nullptr);
    RealClickOn(*original);
    CHECK(MCHdSettings::Now() == OriginalSettings());
    CHECK_EQ(list->Status(), "EVERY HD OPTION IS OFF: THE ORIGINAL GAME.");
    RealClickOption(*list, "HDClouds");
    CHECK(list->Status().empty());
    const std::shared_ptr<MCLogTextButton> defaults = FindTextButton(screen, "DEFAULTS");
    REQUIRE(defaults != nullptr);
    RealClickOn(*defaults);
    CHECK(MCHdSettings::Now() == OriginalSettings());
    CHECK_EQ(list->Status(), "THE DEFAULT SETTINGS ARE BACK.");

    // DONE, then CANCEL: every setting is as it was, and the page opens at its top next time.
    RealClickOption(*list, "HDSunShadows");
    RealClickOption(*list, "HDTurns");
    const std::shared_ptr<MCLogTextButton> done = FindTextButton(screen, "DONE");
    REQUIRE(done != nullptr);
    RealClickOn(*done);
    CHECK(!HdOptionsShown());
    CancelPrefs();
    Frames();
    CHECK(MCHdSettings::Now() == OriginalSettings());
    ShowPreferences();
    Frames();
    CHECK(!HdOptionsShown());
    ShowHdOptions();
    Frames();
    CHECK_EQ(list->FirstRow(), 0);

    // Set again, ACCEPT keeps them and writes the ones off their defaults.
    RealClickOption(*list, "HDSunShadows");
    RealClickOption(*list, "HDFlecks");
    RealClickOption(*list, "HDParticles");
    WritePrefs();
    Frames();
    CHECK_EQ(GHdSunShadows, 1);
    CHECK_EQ(SavedSetting(prefsPath, "HDSunShadows"), std::optional<int32_t>(1));
    CHECK_EQ(SavedSetting(prefsPath, "HDParticles"), std::optional<int32_t>(1));
    CHECK_EQ(SavedSetting(prefsPath, "HDFlecks"), std::optional<int32_t>(1));
    // HD CLOUDS, at its default, isn't written: PREFS without it reads the default.
    CHECK(!SavedSetting(prefsPath, "HDClouds").has_value());

    std::filesystem::remove(prefsPath);
    MCHdSettings::UseOriginal();
}

/// <summary>
/// A run that asked for the GPU renderer and draws on the CPU, the GPU having failed to start (the display's
/// fallback): the preferences' RENDERER says "VULKAN (FAILED)" and keeps VULKAN for the next start; HD OPTIONS shows
/// the options only the GPU draws dim, each line saying it needs VULKAN, the status line saying VULKAN didn't start;
/// they can still be set; the options the CPU keeps (HD AUDIO, FOOTPRINTS, the mechs' motion, BRAKING, REPORTS) show.
/// With SOFTWARE chosen the status line says the software renderer draws none of them; a run on the software renderer
/// by choice with VULKAN chosen for the next start shows them all.
/// </summary>
TEST_CASE_ISOLATED("game: after the GPU fails to start, HD OPTIONS dims what only the GPU draws and RENDERER says so")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    // The display opens on the CPU, as it does when the GPU can't start; then the run is one that asked for the GPU.
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
    ShowPreferences();
    Frames();
    const std::shared_ptr<MCLogComboBox> renderer = PrefsRendererDropDown();
    REQUIRE(renderer != nullptr);
    CHECK_EQ(renderer->Items().front().Label, "VULKAN (FAILED)");
    CHECK_EQ(renderer->Items().front().Value, static_cast<int32_t>(MCRendererKind::Vulkan));
    CHECK_EQ(renderer->Selected(), 0);
    MCScreenInput::SaveShot("prefs gpu failed", MCScreenInput::ScreenHash());

    const std::shared_ptr<MCLogTextButton> options = FindTextButton(*GlobalLog()->PrefScreen, "OPTIONS");
    REQUIRE(options != nullptr);
    RealClickOn(*options);
    REQUIRE(HdOptionsShown());
    const std::shared_ptr<MCLogOptionList> list = HdOptionsList();
    REQUIRE(list != nullptr);
    CHECK_EQ(list->Status(), "VULKAN DIDN'T START ON THIS COMPUTER: THE CPU DRAWS THE GAME.");

    // Every setting is one the GPU alone draws or one the CPU keeps.
    constexpr std::array<std::string_view, 10> gpuOnly = {"HDArt",    "HDLighting", "HDSunShadows", "HDRelief",
                                                          "HDClouds", "HDSway",     "HDParticles",  "HDFlecks",
                                                          "HDShake",  "HDWeather"};
    constexpr std::array<std::string_view, 6> cpuKeeps = {"HDAudio", "HDFootprints", "HDTurns",
                                                          "HDStops", "MechBraking",  "Reports"};
    CHECK_EQ(gpuOnly.size() + cpuKeeps.size(), MCHdSettings::All().size());

    // The settings showing as they shouldn't, named.
    std::string wrong;

    for (const std::string_view key : gpuOnly)
    {
        wrong += list->CanShow(RowOf(*list, key)) ? std::format("{} shows; ", key) : "";
    }

    for (const std::string_view key : cpuKeeps)
    {
        wrong += list->CanShow(RowOf(*list, key)) ? "" : std::format("{} dim; ", key);
    }

    CHECK_EQ(wrong, "");

    // (The mouse kept off the rows, so no box is lit in the screens compared.)
    constexpr int32_t awayX = 4;
    constexpr int32_t awayY = 4;
    MCScreenInput::RealMove(awayX, awayY);
    Frames();
    const uint32_t dimShown = MCScreenInput::ScreenHash();
    MCScreenInput::SaveShot("hd options gpu failed", dimShown);

    // A dim option can still be set (it shows once VULKAN starts).
    RealClickOption(*list, "HDClouds");
    CHECK_EQ(GHdClouds, 1);
    CHECK(!list->CanShow(RowOf(*list, "HDClouds")));
    RealClickOption(*list, "HDClouds");
    CHECK_EQ(GHdClouds, 0);

    // SOFTWARE chosen: the status line says the software renderer draws none of them.
    GRendererPreference = static_cast<int>(MCRendererKind::Software);
    CHECK_EQ(list->Status(), "THE SOFTWARE RENDERER DRAWS NO OPTION THAT NEEDS VULKAN.");
    CHECK(!list->CanShow(RowOf(*list, "HDClouds")));

    // On the software renderer by choice, VULKAN chosen for the next start: nothing dims for the GPU, no status.
    GRenderer = static_cast<int>(MCRendererKind::Software);
    GRendererPreference = static_cast<int>(MCRendererKind::Vulkan);
    CHECK(!GpuFellBack());
    CHECK(list->Status().empty());
    CHECK(list->CanShow(RowOf(*list, "HDClouds")));
    CHECK(!list->CanShow(RowOf(*list, "HDRelief")));
    list->ScrollTo(0);
    MCScreenInput::RealMove(awayX, awayY);
    Frames();
    MCScreenInput::SaveShot("hd options software then vulkan", MCScreenInput::ScreenHash());
    CHECK(MCScreenInput::ScreenHash() != dimShown);

    HideHdOptions();
    CancelPrefs();
    Frames();
    CHECK(MCHdSettings::Now() == OriginalSettings());
    std::filesystem::remove(prefsPath);
}
