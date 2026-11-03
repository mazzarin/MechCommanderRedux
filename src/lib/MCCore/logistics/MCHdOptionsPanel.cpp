#include "stdafx.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCPrefsPage.h"
#include "main/MCHdSettings.h"
#include "main/MCTelemetry.h"
#include "sound/MCHdAudio.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"

namespace
{
    using Choice = MCLogOptionList::Choice;
    using Row = MCLogOptionList::Row;
    using MCParticles::MCFleckStyle;

    /// <summary>The page's title, and the button on the preferences panel that shows it.</summary>
    constexpr std::string_view Title = "HD OPTIONS";
    constexpr std::string_view ButtonLabel = "OPTIONS";

    /// <summary>What DEFAULTS and ORIGINAL say they did.</summary>
    constexpr std::string_view DefaultsNews = "THE DEFAULT SETTINGS ARE BACK.";
    constexpr std::string_view OriginalNews = "EVERY HD OPTION IS OFF: THE ORIGINAL GAME.";

    /// <summary>The settings' names: those PREFS reads as needed, and their names on the page.</summary>
    constexpr std::string_view HdArtKey = "HDArt";
    constexpr std::string_view HdLightingKey = "HDLighting";
    constexpr std::string_view ParticlesKey = "HDParticles";
    constexpr std::string_view HdArtLabel = "HD ART";
    constexpr std::string_view HdLightingLabel = "HD LIGHTING";
    constexpr std::string_view ParticlesLabel = "PARTICLES";

    /// <summary>Whether the HD audio pack has orchestral music (HD AUDIO offers ORCHESTRAL only then).</summary>
    bool OrchestralOffered()
    {
        return MCHdAudio::HasOrchestral();
    }

    constexpr std::array OffOn = {Choice{"OFF", 0}, Choice{"ON", 1}};
    constexpr std::array HdAudioChoices = {Choice{"OFF", HdAudioOff}, Choice{"ON", HdAudioOn},
                                           Choice{"ORCHESTRAL", HdAudioOrchestral, OrchestralOffered}};
    constexpr std::array HdLightingChoices = {Choice{"OFF", HdLightingOff}, Choice{"ON", HdLightingOn},
                                              Choice{"DUSK", HdLightingDusk}, Choice{"NIGHT", HdLightingNight}};
    constexpr std::array WeatherChoices = {Choice{"OFF", HdWeatherOff},
                                           Choice{"ON", HdWeatherOn},
                                           Choice{"RAIN", HdWeatherRain},
                                           Choice{"DOWNPOUR", HdWeatherDownpour},
                                           Choice{"LIGHT SNOW", HdWeatherLightSnow},
                                           Choice{"SNOW", HdWeatherSnow},
                                           Choice{"BLIZZARD", HdWeatherBlizzard}};
    constexpr std::array FleckChoices = {Choice{"EDGE", static_cast<int32_t>(MCFleckStyle::Edge)},
                                         Choice{"FALLING", static_cast<int32_t>(MCFleckStyle::Falling)}};

    /// <summary>HD ART turned on without a pack says where the pack goes.</summary>
    void HdArtChanged(int32_t hdArt)
    {
        std::error_code error;

        if (hdArt != 0 && !std::filesystem::is_directory(MCHdArt::PackFolder(), error))
        {
            ShowMenuMessage("HD ART needs an HD art pack in the HD folder beside your saved games.");
        }
    }

    /// <summary>HD AUDIO turned on without a pack says where the pack goes.</summary>
    void HdAudioChanged(int32_t hdAudio)
    {
        if (hdAudio != HdAudioOff && !MCHdAudio::PackReadable())
        {
            ShowMenuMessage("HD AUDIO needs an HD audio pack in the HD folder beside your saved games.");
        }
    }

    /// <summary>REPORTS changed here is the player's own choice: no notice or question comes after it.</summary>
    void ReportsChanged(int32_t)
    {
        MCTelemetry::Chosen();
    }

    /// <summary>The page's rows, under their headings.</summary>
    constexpr std::array Rows = {
        Row{.Label = "PACKS"},
        Row{.Label = HdArtLabel,
            .Description = "THE HD PACK'S PICTURES: UNITS, BUILDINGS, GROUND, EFFECTS, MENUS AND MOVIES.",
            .Key = HdArtKey,
            .Choices = OffOn,
            .Changed = HdArtChanged},
        Row{.Label = "HD AUDIO",
            .Description = "THE HD PACK'S MUSIC, EFFECTS AND VOICES.",
            .Key = "HDAudio",
            .Choices = HdAudioChoices,
            .Changed = HdAudioChanged},
        Row{.Label = "LIGHTING"},
        Row{.Label = HdLightingLabel,
            .Description = "SHOTS, FIRES AND EXPLOSIONS LIGHT THE WORLD. DUSK OR NIGHT DARKENS EVERY MISSION.",
            .Key = HdLightingKey,
            .Choices = HdLightingChoices},
        Row{.Label = "SUN SHADOWS",
            .Description = "SHADOWS DARKEN THE GROUND INSTEAD OF SOLID BLACK.",
            .Key = "HDSunShadows",
            .Choices = OffOn,
            .Needs = HdArtKey,
            .NeedsLabel = HdArtLabel},
        Row{.Label = "RELIEF",
            .Description = "THE SUN SHOWS THE GROUND'S BUMPS AND SLOPES.",
            .Key = "HDRelief",
            .Choices = OffOn,
            .Needs = HdLightingKey,
            .NeedsLabel = HdLightingLabel},
        Row{.Label = "CLOUD SHADOWS",
            .Description = "THE SHADOWS OF CLOUDS DRIFT OVER THE BATTLEFIELD WITH THE WIND.",
            .Key = "HDClouds",
            .Choices = OffOn},
        Row{.Label = "SWAYING TREES",
            .Description = "FORESTS SWAY IN THE WIND'S GUSTS.",
            .Key = "HDSway",
            .Choices = OffOn,
            .Needs = HdArtKey,
            .NeedsLabel = HdArtLabel},
        Row{.Label = "EFFECTS"},
        Row{.Label = ParticlesLabel,
            .Description = "FLECKS, DEBRIS, SMOKE, EMBERS, SPLASHES AND WAKES.",
            .Key = ParticlesKey,
            .Choices = OffOn},
        Row{.Label = "FLECKS",
            .Description = "EXPLOSIONS' FLECKS BREAK OFF THEIR FIRES' EDGES, OR FALL.",
            .Key = "HDFlecks",
            .Choices = FleckChoices,
            .Needs = ParticlesKey,
            .NeedsLabel = ParticlesLabel},
        Row{.Label = "SCREEN SHAKE",
            .Description = "LARGE ARTILLERY STRIKES AND BUILDINGS BLOWING UP SHAKE THE PICTURE.",
            .Key = "HDShake",
            .Choices = OffOn},
        Row{.Label = "FOOTPRINTS",
            .Description = "FEET PRINT WHERE THEY STEP; THREE TIMES AS MANY PRINTS AND CRATERS STAY.",
            .Key = "HDFootprints",
            .Choices = OffOn},
        Row{.Label = "WEATHER",
            .Description = "RAIN OR SNOW WHERE A MAP MAKER SETS IT, OR THE WEATHER CHOSEN ON EVERY MISSION.",
            .Key = "HDWeather",
            .Choices = WeatherChoices},
        Row{.Label = "MECHS"},
        Row{.Label = "SMOOTH TURNS",
            .Description = "MECHS TURN SMOOTHLY, THEIR LEGS DRAWN BETWEEN THEIR FACINGS.",
            .Key = "HDTurns",
            .Choices = OffOn},
        Row{.Label = "HELD STRIDES",
            .Description = "A MECH HELD UP MID-STRIDE KEEPS ITS STRIDE AND WALKS ON FROM IT.",
            .Key = "HDStops",
            .Choices = OffOn},
        Row{.Label = "BRAKING",
            .Description = "MECHS BRAKE TO A HALT INSTEAD OF STOPPING DEAD. CHANGES PLAY.",
            .Key = "MechBraking",
            .Choices = OffOn},
        Row{.Label = "PRIVACY"},
        Row{.Label = "REPORTS",
            .Description = "ANONYMOUS REPORTS OF YOUR COMPUTER, HOW SMOOTHLY THE GAME RUNS, AND CRASHES.",
            .Key = "Reports",
            .Choices = OffOn,
            .Changed = ReportsChanged},
    };

    /// <summary>The panels.</summary>
    std::weak_ptr<MCGuiObject> PrefsPanel;
    std::weak_ptr<MCGuiObject> Page;
    std::weak_ptr<MCLogOptionList> List;

    /// <summary>DEFAULTS: every HD setting to this build's default.</summary>
    void UseDefaults()
    {
        MCHdSettings::UseDefaults();

        if (const std::shared_ptr<MCLogOptionList> list = List.lock(); list != nullptr)
        {
            list->SetNews(DefaultsNews);
        }
    }

    /// <summary>ORIGINAL: every HD setting off.</summary>
    void UseOriginal()
    {
        MCHdSettings::UseOriginal();

        if (const std::shared_ptr<MCLogOptionList> list = List.lock(); list != nullptr)
        {
            list->SetNews(OriginalNews);
        }
    }
}

void AddHdOptionsPanel(MCGenericScreen& screen, MCGuiObject& prefsPanel, MCGuiObject& box, int32_t buttonLeft,
                       int32_t buttonTop)
{
    PrefsPanel = prefsPanel.weak_from_this();
    AddPageButton(screen, box, ButtonLabel, buttonLeft, buttonTop, ShowHdOptions);

    constexpr std::array<MCPageButton, 2> buttons = {
        MCPageButton{"DEFAULTS", UseDefaults},
        MCPageButton{"ORIGINAL", UseOriginal},
    };

    MCLogGroupBox& page = AddPrefsPage(screen, Title, buttons, HideHdOptions);
    Page = page.weak_from_this();

    auto newList = MCMakeGui<MCLogOptionList>();
    newList->Init(PageListLeft, PageListTop, PageListWidth, PageListHeight, Rows);
    newList->ShowGuiWindow(true);
    List = newList;
    screen.AdoptChildOf(page, std::move(newList));
}

void ResetHdOptions()
{
    // ORCHESTRAL without the pack's orchestral music plays its stereo music, as ON does: it shows as ON.
    if (GHdAudio == HdAudioOrchestral && !MCHdAudio::HasOrchestral())
    {
        GHdAudio = HdAudioOn;
    }

    if (const std::shared_ptr<MCLogOptionList> list = List.lock(); list != nullptr)
    {
        list->Reset();
    }

    ShowPrefsPage(PrefsPanel, Page, false);
}

void ShowHdOptions()
{
    ShowPrefsPage(PrefsPanel, Page, true);
}

void HideHdOptions()
{
    ShowPrefsPage(PrefsPanel, Page, false);
}

bool HdOptionsShown()
{
    return PrefsPageShown(Page);
}

std::shared_ptr<MCLogOptionList> HdOptionsList()
{
    return List.lock();
}
