#include "stdafx.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogOptionList.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCPrefsPage.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCTelemetry.h"
#include "main/MCUpdater.h"
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

    /// <summary>HD PACK's buttons, one shown at a time where the other rows have their settings.</summary>
    constexpr std::string_view GetLabel = "GET";
    constexpr std::string_view StopLabel = "STOP";
    constexpr std::string_view RemoveLabel = "REMOVE";
    constexpr std::string_view RestartLabel = "RESTART";
    std::vector<std::weak_ptr<MCLogTextButton>> HdPackButtons;

    /// <summary>The button HD PACK shows: GET, STOP while it downloads, REMOVE, RESTART once a change is ready; none.</summary>
    std::string_view HdPackAction(const MCUpdater::MCHdPack& pack)
    {
        switch (pack.Stage)
        {
            case MCUpdater::MCStage::Downloading:
                return pack.Chosen && !pack.Stopping ? StopLabel : std::string_view();
            case MCUpdater::MCStage::Ready:
                return RestartLabel;
            case MCUpdater::MCStage::Failed:
                // The same again tries again.
                return pack.Chosen ? GetLabel : RemoveLabel;
            default:
                break;
        }

        if (pack.WithUpdate)
        {
            return {};
        }

        return pack.Installed ? RemoveLabel : GetLabel;
    }

    /// <summary>HD PACK's line: what it is and its size, or where its change is.</summary>
    std::string HdPackLine()
    {
        const MCUpdater::MCHdPack& pack = MCUpdater::HdPack();

        switch (pack.Stage)
        {
            case MCUpdater::MCStage::Downloading:
            {
                if (pack.Stopping)
                {
                    return "STOPPING THE DOWNLOAD.";
                }

                if (!pack.Chosen)
                {
                    return "GETTING READY TO REMOVE IT.";
                }

                return pack.Expected > 0
                           ? std::format("DOWNLOADING: {}.", MCUpdateWork::ProgressText(pack.Received, pack.Expected))
                           : std::string("STARTING THE DOWNLOAD.");
            }
            case MCUpdater::MCStage::Ready:
                return pack.Chosen ? "DOWNLOADED. RESTART THE GAME TO SEE IT." : "RESTART THE GAME TO REMOVE IT.";
            case MCUpdater::MCStage::Failed:
            {
                std::string error = pack.Error;
                std::ranges::transform(error, error.begin(), [](char c)
                                       { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
                return std::format("{}: {}.", pack.Chosen ? "THE DOWNLOAD FAILED" : "IT COULDN'T BE REMOVED", error);
            }
            default:
                break;
        }

        if (pack.WithUpdate)
        {
            return pack.Chosen ? "IT WILL DOWNLOAD WITH THE UPDATE." : "IT WILL BE REMOVED WITH THE UPDATE.";
        }

        if (pack.Bytes == 0)
        {
            return pack.Installed ? "THE HD ART, SOUND AND MOVIES." : "THE HD ART, SOUND AND MOVIES, TO DOWNLOAD.";
        }

        return pack.Installed
                   ? std::format("THE HD ART, SOUND AND MOVIES ({}).", MCUpdateWork::SizeText(pack.Bytes))
                   : std::format("THE HD ART, SOUND AND MOVIES: {} TO DOWNLOAD.", MCUpdateWork::SizeText(pack.Bytes));
    }

    /// <summary>Why HD PACK can't change now: an update's work comes first.</summary>
    std::string HdPackHeld()
    {
        switch (MCUpdater::HdPack().Hold)
        {
            case MCUpdater::MCHdHold::Looking:
                return "LOOKING FOR AN UPDATE.";
            case MCUpdater::MCHdHold::UpdateDownloading:
                return "WAITING FOR THE UPDATE TO DOWNLOAD.";
            case MCUpdater::MCHdHold::UpdateReady:
                return "RESTART THE GAME TO INSTALL THE UPDATE FIRST.";
            default:
                return {};
        }
    }

    /// <summary>HD PACK's button over the row's setting column (none while the row is scrolled away or held).</summary>
    void PlaceHdPackButton(std::optional<std::pair<int32_t, int32_t>> centre)
    {
        const std::string_view shown = centre.has_value() ? HdPackAction(MCUpdater::HdPack()) : std::string_view();

        for (const std::weak_ptr<MCLogTextButton>& held : HdPackButtons)
        {
            const std::shared_ptr<MCLogTextButton> button = held.lock();

            if (button == nullptr)
            {
                continue;
            }

            const bool showing = button->Label() == shown;

            if (showing)
            {
                const std::shared_ptr<MCGuiObject> page = button->Parent.lock();
                const int32_t left = centre->first - button->Width() / 2 - (page != nullptr ? page->GlobalX() : 0);
                const int32_t top = centre->second - button->Height() / 2 - (page != nullptr ? page->GlobalY() : 0);

                if (button->X() != left || button->Y() != top)
                {
                    button->MoveTo(left, top);
                }
            }

            if (button->IsShowing() != showing)
            {
                button->ShowGuiWindow(showing);
            }
        }
    }

    /// <summary>What REMOVE asks first.</summary>
    constexpr std::string_view RemoveQuestion = "Remove the HD pack?";

    /// <summary>REMOVE: asked first.</summary>
    void AskToRemoveHd()
    {
        AskMenuQuestion(
            *GlobalLog()->QuestionDialog, RemoveQuestion, [] { MCUpdater::ChooseHd(false); }, nullptr, "bg_cancl.tga",
            true);
    }

    /// <summary>Adds HD PACK's buttons to <paramref name="page"/>, hidden until its row places one.</summary>
    void AddHdPackButtons(MCGenericScreen& screen, MCGuiObject& page)
    {
        const std::array<std::pair<std::string_view, std::function<void()>>, 4> buttons = {{
            {GetLabel, [] { MCUpdater::ChooseHd(true); }},
            {StopLabel, [] { MCUpdater::ChooseHd(false); }},
            {RemoveLabel, AskToRemoveHd},
            {RestartLabel, MCUpdater::RestartForHd},
        }};
        HdPackButtons.clear();

        for (const auto& [label, clicked] : buttons)
        {
            auto button = MCMakeGui<MCLogTextButton>();
            button->InitWithLabel(0, 0, label, clicked);
            button->ShowGuiWindow(false);
            HdPackButtons.push_back(button);
            screen.AdoptChildOf(page, std::move(button));
        }
    }

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

    /// <summary>
    /// HD ART or HD AUDIO turned on without a pack says what it needs: the HD pack (HD PACK, in a game that manages its
    /// content), else a pack in the HD folder.
    /// </summary>
    void SayPackNeeded(std::string_view option, std::string_view pack)
    {
        ShowMenuMessage(
            MCUpdater::HdPack().Managed
                ? std::format("{} needs the HD pack.", option)
                : std::format("{} needs an HD {} pack in the HD folder beside your saved games.", option, pack));
    }

    /// <summary>HD ART turned on without a pack says what it needs.</summary>
    void HdArtChanged(int32_t hdArt)
    {
        std::error_code error;

        if (hdArt != 0 && !std::filesystem::is_directory(MCHdArt::PackFolder(), error))
        {
            SayPackNeeded(HdArtLabel, "art");
        }
    }

    /// <summary>HD AUDIO turned on without a pack says what it needs.</summary>
    void HdAudioChanged(int32_t hdAudio)
    {
        if (hdAudio != HdAudioOff && !MCHdAudio::PackReadable())
        {
            SayPackNeeded("HD AUDIO", "audio");
        }
    }

    /// <summary>REPORTS changed here is the player's own choice: no notice or question comes after it.</summary>
    void ReportsChanged(int32_t)
    {
        MCTelemetry::Chosen();
    }

    /// <summary>
    /// The page's rows, under their headings. Those the GPU alone draws (Gpu) show dim while the CPU draws the game:
    /// the pack's pictures, the lighting, the particles, the shake (the GPU's composite moves the picture) and the
    /// weather. The CPU keeps the rest: HD audio, the footprints and craters kept, the turns eased (not their legs' HD
    /// middles), the held strides, braking and the reports.
    /// </summary>
    /// <summary>HD PACK's place: under the PACKS heading, first.</summary>
    constexpr size_t HdPackRow = 1;

    constexpr std::array RowsWithPack = {
        Row{.Label = "PACKS"},
        Row{.Label = "HD PACK",
            .Description = "THE HD ART, SOUND AND MOVIES.",
            .Line = HdPackLine,
            .HeldNow = HdPackHeld,
            .Place = PlaceHdPackButton},
        Row{.Label = HdArtLabel,
            .Description = "THE HD PACK'S PICTURES: UNITS, BUILDINGS, GROUND, EFFECTS, MENUS AND MOVIES.",
            .Key = HdArtKey,
            .Choices = OffOn,
            .Gpu = true,
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
            .Choices = HdLightingChoices,
            .Gpu = true},
        Row{.Label = "SUN SHADOWS",
            .Description = "SHADOWS DARKEN THE GROUND INSTEAD OF SOLID BLACK.",
            .Key = "HDSunShadows",
            .Choices = OffOn,
            .Gpu = true,
            .Needs = HdArtKey,
            .NeedsLabel = HdArtLabel},
        Row{.Label = "RELIEF",
            .Description = "THE SUN SHOWS THE GROUND'S BUMPS AND SLOPES.",
            .Key = "HDRelief",
            .Choices = OffOn,
            .Gpu = true,
            .Needs = HdLightingKey,
            .NeedsLabel = HdLightingLabel},
        Row{.Label = "CLOUD SHADOWS",
            .Description = "THE SHADOWS OF CLOUDS DRIFT OVER THE BATTLEFIELD WITH THE WIND.",
            .Key = "HDClouds",
            .Choices = OffOn,
            .Gpu = true},
        Row{.Label = "SWAYING TREES",
            .Description = "FORESTS SWAY IN THE WIND'S GUSTS.",
            .Key = "HDSway",
            .Choices = OffOn,
            .Gpu = true,
            .Needs = HdArtKey,
            .NeedsLabel = HdArtLabel},
        Row{.Label = "EFFECTS"},
        Row{.Label = ParticlesLabel,
            .Description = "FLECKS, DEBRIS, SMOKE, EMBERS, SPLASHES AND WAKES.",
            .Key = ParticlesKey,
            .Choices = OffOn,
            .Gpu = true},
        Row{.Label = "FLECKS",
            .Description = "EXPLOSIONS' FLECKS BREAK OFF THEIR FIRES' EDGES, OR FALL.",
            .Key = "HDFlecks",
            .Choices = FleckChoices,
            .Gpu = true,
            .Needs = ParticlesKey,
            .NeedsLabel = ParticlesLabel},
        Row{.Label = "SCREEN SHAKE",
            .Description = "LARGE ARTILLERY STRIKES AND BUILDINGS BLOWING UP SHAKE THE PICTURE.",
            .Key = "HDShake",
            .Choices = OffOn,
            .Gpu = true},
        Row{.Label = "FOOTPRINTS",
            .Description = "FEET PRINT WHERE THEY STEP, AND MORE PRINTS AND CRATERS STAY.",
            .Key = "HDFootprints",
            .Choices = OffOn},
        Row{.Label = "WEATHER",
            .Description = "RAIN OR SNOW WHERE A MAP MAKER SETS IT, OR THE WEATHER CHOSEN ON EVERY MISSION.",
            .Key = "HDWeather",
            .Choices = WeatherChoices,
            .Gpu = true},
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
            .Description = "REPORTS OF YOUR COMPUTER, FRAME RATES, SETTINGS AND CRASHES, UNDER A RANDOM ID.",
            .Key = "Reports",
            .Choices = OffOn,
            .Changed = ReportsChanged},
    };

    /// <summary><paramref name="rows"/> without row <paramref name="left"/>.</summary>
    template <size_t Count>
    constexpr std::array<Row, Count - 1> Without(const std::array<Row, Count>& rows, size_t left)
    {
        std::array<Row, Count - 1> kept{};

        for (size_t from = 0, to = 0; from < Count; from++)
        {
            if (from != left)
            {
                kept[to++] = rows[from];
            }
        }

        return kept;
    }

    /// <summary>The rows in a game that doesn't manage its content: no HD PACK.</summary>
    constexpr std::array Rows = Without(RowsWithPack, HdPackRow);

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
    const bool managed = MCUpdater::HdPack().Managed;
    newList->Init(PageListLeft, PageListTop, PageListWidth, PageListHeight,
                  managed ? std::span<const Row>(RowsWithPack) : std::span<const Row>(Rows));
    newList->ShowGuiWindow(true);
    List = newList;
    screen.AdoptChildOf(page, std::move(newList));

    // Over the list, so they draw on it.
    if (managed)
    {
        AddHdPackButtons(screen, page);
    }
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
