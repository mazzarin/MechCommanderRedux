#include "stdafx.h"
#include "logistics/MCSettingsModel.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "main/MCArtModes.h"
#include "main/MCHdSettings.h"
#include "main/MCSystemConfig.h"
#include "main/MCTelemetry.h"
#include "main/MCUpdater.h"
#include "platform/MCPresenter.h"
#include "sound/MCHdAudio.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "update/MCUpdateWork.h"

namespace
{
    using MCSettingsModel::MCControl;
    using MCSettingsModel::MCInMission;
    using MCSettingsModel::MCQuestion;
    using MCSettingsModel::MCQuick;
    using MCSettingsModel::MCSection;
    using MCSettingsModel::MCSetting;

    /// <summary>The words a mission's hold puts on a row.</summary>
    constexpr std::string_view NotInMissionLine = "Not in a mission.";
    constexpr std::string_view BetweenMissionsLine = "Between missions.";

    /// <summary>More choices than a switch holds go on a stepper.</summary>
    constexpr size_t MostSwitchChoices = 4;

    /// <summary>A volume's range (the sound system's levels), and its slider's (a percent).</summary>
    constexpr int VolumeMost = 127;
    constexpr int PercentMost = 100;
    /// <summary>A volume slider's step on Left and Right, in percent.</summary>
    constexpr int VolumeStep = 5;
    /// <summary>Brightness: the gamma table's passes (MCGuiSystem::GammaCorrectCurrentPalette gives three at most).</summary>
    constexpr int BrightnessMost = 3;

    /// <summary>The samples a volume's slider plays at its new level (1998's: MCPreferencesMenu.cpp's).</summary>
    constexpr int32_t MenusMusic = 0x16;
    constexpr std::string_view RadioSpeech = "pilotd";
    constexpr int32_t RadioSpeechId = 0x15;
    constexpr uint32_t EffectsSample = 0xf;
    constexpr uint32_t EffectsChannel = 1;
    constexpr uint32_t InterfaceSample = 0x10;

    /// <summary>The settings the HD settings keep (main/MCHdSettings), by their PREFS keys.</summary>
    constexpr std::string_view HdArtKey = "HDArt";
    constexpr std::string_view HdLightingKey = "HDLighting";
    constexpr std::string_view ParticlesKey = "HDParticles";
    constexpr std::string_view HdAudioKey = "HDAudio";
    constexpr std::string_view ReportsKey = "Reports";

    /// <summary>Whether REPORTS changed on the screen open now: the player's answer, given as it's kept.</summary>
    bool ReportsChanged = false;

    const std::vector<std::string> OffOn = {"Off", "On"};

    /// <summary>The groups' names (cyan over their rows).</summary>
    constexpr std::string_view GameGroup = "Game";
    constexpr std::string_view PictureGroup = "Picture";
    constexpr std::string_view PackGroup = "Pack";
    constexpr std::string_view LightingGroup = "Lighting";
    constexpr std::string_view EffectsGroup = "Effects";
    constexpr std::string_view MechsGroup = "Mechs";
    constexpr std::string_view VolumesGroup = "Volumes";
    constexpr std::string_view HdPackGroup = "HD pack";
    constexpr std::string_view PrivacyGroup = "Privacy";

    /// <summary>The HD pack's buttons.</summary>
    constexpr std::string_view GetWord = "Get";
    constexpr std::string_view StopWord = "Stop";
    constexpr std::string_view RemoveWord = "Remove";
    using MCSettingsModel::RestartWord;

    std::function<std::vector<std::string>()> Words(std::vector<std::string> words)
    {
        return [words = std::move(words)] { return words; };
    }

    MCControl ControlFor(size_t choices)
    {
        return choices > MostSwitchChoices ? MCControl::Stepper : MCControl::Switch;
    }

    /// <summary><paramref name="first"/>, then <paramref name="reason"/> when there is one, as one sentence.</summary>
    std::string Because(std::string_view first, std::string_view reason)
    {
        if (reason.empty())
        {
            return std::format("{}.", first);
        }

        const bool stops = reason.back() == '.';
        return std::format("{}: {}{}", first, reason, stops ? "" : ".");
    }

    /// <summary>"1.2 OF 5.5 GB" as a line writes it.</summary>
    std::string Progress(uint64_t received, uint64_t whole)
    {
        std::string text = MCUpdateWork::ProgressText(received, whole);
        constexpr std::string_view Of = " OF ";

        if (const size_t at = text.find(Of); at != std::string::npos)
        {
            text.replace(at, Of.size(), " of ");
        }

        return text;
    }

    /// <summary>Whether the CPU draws the game as the settings stand (SOFTWARE chosen, or the GPU didn't start).</summary>
    bool CpuDrawsAsSet()
    {
        return GRendererPreference == static_cast<int>(MCRendererKind::Software) || GpuFellBack();
    }

    /// <summary>"VULKAN" as a line writes it: "Vulkan".</summary>
    std::string SentenceCaseWord(std::string_view word)
    {
        std::string written(word);

        for (size_t i = 1; i < written.size(); ++i)
        {
            written[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(written[i])));
        }

        return written;
    }

    /// <summary>A row of an HD setting (main/MCHdSettings): its choices by index from the setting's least.</summary>
    struct MCHdRow
    {
        std::string_view Id;
        MCSection Section = MCSection::Hd;
        std::string_view Group;
        std::string_view Name;
        std::string_view Key;
        std::vector<std::string> Choices;
        /// <summary>Drawn by the GPU alone: held while the CPU draws.</summary>
        bool Gpu = false;
        /// <summary>The setting it needs on, and that setting's name in its line.</summary>
        std::string_view Needs;
        std::string_view NeedsName;
        /// <summary>ORIGINAL turns it off (a look; not BRAKING, which is GAME's).</summary>
        bool Look = true;
    };

    MCSetting HdSetting(MCHdRow row)
    {
        const MCHdSettings::MCSetting& hd = MCHdSettings::Find(row.Key);
        const auto last = static_cast<int>(row.Choices.size()) - 1;
        MCSetting setting{.Id = row.Id,
                          .Section = row.Section,
                          .Group = row.Group,
                          .Name = row.Name,
                          .Control = ControlFor(row.Choices.size()),
                          .Choices = Words(row.Choices),
                          .Get = [&hd, last] { return std::clamp(hd.Value.get() - hd.Least, 0, last); },
                          .Set = [&hd](int choice)
                          { hd.Value.get() = std::clamp(hd.Least + choice, hd.Least, hd.Most); },
                          .UseDefault = [&hd] { hd.Value.get() = MCHdSettings::Default(hd); }};

        if (row.Look)
        {
            setting.UseOriginal = [&hd] { hd.Value.get() = hd.Original; };
        }

        if (row.Gpu || !row.Needs.empty())
        {
            setting.Held = [gpu = row.Gpu, needs = row.Needs, needsName = row.NeedsName]
            {
                if (gpu && CpuDrawsAsSet())
                {
                    return std::format("Needs {}.", SentenceCaseWord(MCGpuRendererChoice));
                }

                if (!needs.empty() && MCHdSettings::Find(needs).Value.get() == 0)
                {
                    return std::format("Needs {}.", needsName);
                }

                return std::string();
            };
        }

        return setting;
    }

    /// <summary>What a setting turned on without its pack needs (HD ART, HD AUDIO), as its line.</summary>
    std::string PackNeeded(std::string_view pack)
    {
        return MCUpdater::HdPack().Managed
                   ? std::string("Needs the HD pack.")
                   : std::format("Needs an HD {} pack in the HD folder beside your saved games.", pack);
    }

    /// <summary>A volume slider: the sound system's level as a percent.</summary>
    MCSetting Volume(std::string_view id, std::string_view name, uint8_t MCSoundSystem::* level,
                     std::function<int32_t()> byDefault, std::function<void(bool inMission)> preview)
    {
        return MCSetting{
            .Id = id,
            .Section = MCSection::Sound,
            .Group = VolumesGroup,
            .Name = name,
            .Control = MCControl::Slider,
            .Get =
                [level]
            {
                const std::shared_ptr<MCSoundSystem> sound = SoundSystem();
                return sound != nullptr
                           ? static_cast<int>(std::lround((*sound).*level * double{PercentMost} / VolumeMost))
                           : 0;
            },
            .Set =
                [level](int percent)
            {
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
                {
                    (*sound).*level = static_cast<uint8_t>(
                        std::clamp(std::lround(percent * double{VolumeMost} / PercentMost), 0L, long{VolumeMost}));
                }
            },
            .Preview = std::move(preview),
            .UseDefault =
                [level, byDefault = std::move(byDefault)]
            {
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
                {
                    (*sound).*level = static_cast<uint8_t>(std::clamp(byDefault(), 0, VolumeMost));
                }
            },
            .Quick = MCQuick::Sound,
            .Most = PercentMost,
            .Step = VolumeStep};
    }

    /// <summary>The HD pack's button now: GET, STOP while it downloads, REMOVE, RESTART once a change is ready; none.</summary>
    std::string_view HdPackWord(const MCUpdater::MCHdPack& pack)
    {
        switch (pack.Stage)
        {
            case MCUpdater::MCStage::Downloading:
                return pack.Chosen && !pack.Stopping ? StopWord : std::string_view();
            case MCUpdater::MCStage::Ready:
                return RestartWord;
            case MCUpdater::MCStage::Failed:
                // The same again tries again.
                return pack.Chosen ? GetWord : RemoveWord;
            default:
                break;
        }

        if (pack.WithUpdate)
        {
            return {};
        }

        return pack.Installed ? RemoveWord : GetWord;
    }

    /// <summary>The HD pack's line: its size and whether it's in, or where its change is.</summary>
    std::string HdPackLine()
    {
        const MCUpdater::MCHdPack& pack = MCUpdater::HdPack();

        switch (pack.Stage)
        {
            case MCUpdater::MCStage::Downloading:
            {
                if (pack.Stopping)
                {
                    return "Stopping the download.";
                }

                if (!pack.Chosen)
                {
                    return "Getting ready to remove it.";
                }

                return pack.Expected > 0 ? std::format("Downloading: {}.", Progress(pack.Received, pack.Expected))
                                         : std::string("Starting the download.");
            }
            case MCUpdater::MCStage::Ready:
                return pack.Chosen ? "Downloaded. Restart the game to see it." : "Restart the game to remove it.";
            case MCUpdater::MCStage::Failed:
            {
                if (pack.Error.empty())
                {
                    return std::format("{} {} tries again.",
                                       pack.Chosen ? "The download failed." : "It couldn't be removed.",
                                       HdPackWord(pack));
                }

                return Because(pack.Chosen ? "The download failed" : "It couldn't be removed", pack.Error);
            }
            default:
                break;
        }

        if (pack.WithUpdate)
        {
            return pack.Chosen ? "It will download with the update." : "It will be removed with the update.";
        }

        if (pack.Bytes == 0)
        {
            return pack.Installed ? "Installed." : "Not installed.";
        }

        const std::string size = MCUpdateWork::SizeText(pack.Bytes);
        return pack.Installed ? std::format("{}, installed.", size) : std::format("{} to download.", size);
    }

    /// <summary>Why the HD pack can't change now: an update's work comes first.</summary>
    std::string HdPackHeld()
    {
        switch (MCUpdater::HdPack().Hold)
        {
            case MCUpdater::MCHdHold::Looking:
                return "Looking for an update.";
            case MCUpdater::MCHdHold::UpdateDownloading:
                return "Waiting for the update to download.";
            case MCUpdater::MCHdHold::UpdateReady:
                return "Restart the game to install the update first.";
            default:
                return {};
        }
    }

    /// <summary>HD ART SIZE's choices.</summary>
    constexpr int FullArt = 0;
    constexpr int TwoXArt = 1;

    std::string HdArtSizeLine()
    {
        const MCUpdater::MCHdPack& pack = MCUpdater::HdPack();
        const std::string_view picked = pack.PlayerPicked ? std::string_view() : "Picked for this computer. ";

        if (pack.FullBytes == 0 || pack.TwoXBytes == 0)
        {
            return std::format("{}2x needs half the memory.", picked);
        }

        return std::format("{}Full {}, 2x {} and half the memory.", picked, MCUpdateWork::SizeText(pack.FullBytes),
                           MCUpdateWork::SizeText(pack.TwoXBytes));
    }

    std::string HdArtSizeHeld()
    {
        if (std::string held = HdPackHeld(); !held.empty())
        {
            return held;
        }

        const MCUpdater::MCHdPack& pack = MCUpdater::HdPack();

        if (!pack.Chosen)
        {
            return "Get the HD pack first.";
        }

        return pack.TwoXBytes == 0 ? "This release has one size of HD art." : std::string();
    }

    /// <summary>A pick of HD ART SIZE that downloads asks first, saying how much; back to the copy's size asks nothing.</summary>
    std::optional<MCQuestion> AskHdArtSize(int choice)
    {
        const bool twoX = choice == TwoXArt;
        const MCUpdater::MCHdPack& pack = MCUpdater::HdPack();
        const bool copyTwoX = pack.Installed ? pack.TwoX : pack.TwoXChosen;

        if (!pack.Chosen || twoX == copyTwoX)
        {
            return std::nullopt;
        }

        const std::string_view name = twoX ? "2x" : "full";
        const uint64_t bytes = twoX ? pack.TwoXBytes : pack.FullBytes;
        return MCQuestion{
            .Heading = bytes == 0 ? std::format("Download the {} HD art?", name)
                                  : std::format("Download the {} HD art ({})?", name, MCUpdateWork::SizeText(bytes)),
            .Act = "Download",
            .Yes = [twoX] { MCUpdater::ChooseHdArt(twoX); }};
    }

    /// <summary>The choices of a stepper over <paramref name="values"/>, AUTO's first when it has one.</summary>
    int IndexOf(std::span<const int> values, int value, int first)
    {
        const auto found = std::ranges::find(values, value);
        return found != values.end() ? static_cast<int>(std::distance(values.begin(), found)) + first : 0;
    }

    std::vector<MCSetting> MakeAll(bool managed)
    {
        std::vector<MCSetting> all;

        // GAME: the difficulty, and the options that change play.
        all.push_back(MCSetting{.Id = "difficulty",
                                .Section = MCSection::Game,
                                .Group = GameGroup,
                                .Name = "Difficulty",
                                .Control = MCControl::Switch,
                                .Choices = Words({"Easy", "Regular", "Hard"}),
                                // A PREFS value past them plays as REGULAR (MCDifficultySettings).
                                .Get = [] { return GameDifficulty >= 0 && GameDifficulty <= 2 ? GameDifficulty : 1; },
                                .Set = [](int choice) { GameDifficulty = choice; },
                                .UseDefault = [] { GameDifficulty = MCPrefsDefaults::Difficulty; },
                                .InMission = MCInMission::BetweenMissions});
        MCSetting braking = HdSetting({.Id = "braking",
                                       .Section = MCSection::Game,
                                       .Group = GameGroup,
                                       .Name = "Braking",
                                       .Key = "MechBraking",
                                       .Choices = OffOn,
                                       .Look = false});
        braking.ChangesPlay = true;
        braking.InMission = MCInMission::BetweenMissions;
        all.push_back(std::move(braking));

        // DISPLAY: the picture's settings.
        all.push_back(MCSetting{.Id = "brightness",
                                .Section = MCSection::Display,
                                .Group = PictureGroup,
                                .Name = "Screen brightness",
                                .Control = MCControl::Slider,
                                .Get = [] { return std::clamp(GuiSystem().GammaLevel, 0, BrightnessMost); },
                                .Set = [](int level) { GuiSystem().GammaCorrectCurrentPalette(level); },
                                .UseDefault = [] { GuiSystem().GammaCorrectCurrentPalette(MCPrefsDefaults::Gamma); },
                                .Quick = MCQuick::View,
                                .Most = BrightnessMost,
                                .Step = 1});
        all.push_back(MCSetting{
            .Id = "interface-size",
            .Section = MCSection::Display,
            .Group = PictureGroup,
            .Name = "Interface size",
            .Control = MCControl::Stepper,
            .Choices =
                []
            {
                std::vector<std::string> choices = {std::format("Auto ({}%)", GuiSystem().AutoInterfaceSizeNow())};

                for (const int percent : InterfaceSizes)
                {
                    choices.push_back(std::format("{}%", percent));
                }

                return choices;
            },
            .Get = [] { return GInterfaceSize == AutoInterfaceSize ? 0 : IndexOf(InterfaceSizes, GInterfaceSize, 1); },
            .Set =
                [](int choice)
            {
                GInterfaceSize = choice <= 0 || std::cmp_greater(choice, InterfaceSizes.size())
                                     ? AutoInterfaceSize
                                     : InterfaceSizes[static_cast<size_t>(choice - 1)];
            },
            .Line = [] { return std::string("Alt+= and Alt+- in a mission."); },
            .UseDefault = [] { GInterfaceSize = AutoInterfaceSize; },
            .Quick = MCQuick::View});
        all.push_back(MCSetting{.Id = "ui-resolution",
                                .Section = MCSection::Display,
                                .Group = PictureGroup,
                                .Name = "UI resolution",
                                .Control = MCControl::Stepper,
                                .Choices =
                                    []
                                {
                                    std::vector<std::string> choices = {"Native"};

                                    for (const int lines : UiResolutions)
                                    {
                                        choices.push_back(std::format("{}p", lines));
                                    }

                                    return choices;
                                },
                                .Get = [] { return GUiResolution == 0 ? 0 : IndexOf(UiResolutions, GUiResolution, 1); },
                                .Set =
                                    [](int choice)
                                {
                                    GUiResolution = choice <= 0 || std::cmp_greater(choice, UiResolutions.size())
                                                        ? 0
                                                        : UiResolutions[static_cast<size_t>(choice - 1)];
                                },
                                .UseDefault = [] { GUiResolution = MCPrefsDefaults::UiResolution; }});
        all.push_back(MCSetting{
            .Id = "renderer",
            .Section = MCSection::Display,
            .Group = PictureGroup,
            .Name = "Renderer",
            .Control = MCControl::Switch,
            // VULKAN says it failed when this run asked for it and it couldn't start: the choice stays, for the next.
            .Choices = []
            { return std::vector<std::string>{GpuFellBack() ? "Vulkan (failed)" : "Vulkan", "Software"}; },
            .Get = [] { return GRendererPreference == static_cast<int>(MCRendererKind::Software) ? 1 : 0; },
            .Set =
                [](int choice)
            {
                GRendererPreference = static_cast<int>(choice == 1 ? MCRendererKind::Software : MCRendererKind::Vulkan);
            },
            .Line = [] { return std::string("Takes a restart."); },
            .UseDefault = [] { GRendererPreference = static_cast<int>(MCRendererKind::Vulkan); },
            .InMission = MCInMission::NotInMission});
        all.push_back(MCSetting{.Id = "scroll-speed",
                                .Section = MCSection::Display,
                                .Group = PictureGroup,
                                .Name = "Scroll speed",
                                .Control = MCControl::Stepper,
                                .Choices =
                                    []
                                {
                                    std::vector<std::string> choices;

                                    for (const int percent : ScrollSpeeds)
                                    {
                                        choices.push_back(percent == AutoScrollSpeed
                                                              ? std::string("Auto")
                                                              : std::format("{}x", percent / double{PercentMost}));
                                    }

                                    return choices;
                                },
                                .Get = [] { return IndexOf(ScrollSpeeds, GScrollSpeed, 0); },
                                .Set =
                                    [](int choice)
                                {
                                    GScrollSpeed = choice < 0 || std::cmp_greater_equal(choice, ScrollSpeeds.size())
                                                       ? AutoScrollSpeed
                                                       : ScrollSpeeds[static_cast<size_t>(choice)];
                                },
                                .UseDefault = [] { GScrollSpeed = AutoScrollSpeed; },
                                .Quick = MCQuick::View});

        // HD: the pack (a game that manages its content), the art, lighting, effects and motion.
        if (managed)
        {
            all.push_back(MCSetting{.Id = "hd-pack",
                                    .Section = MCSection::Hd,
                                    .Group = PackGroup,
                                    .Name = "HD pack",
                                    .Control = MCControl::Button,
                                    .Press =
                                        []
                                    {
                                        const std::string_view word = HdPackWord(MCUpdater::HdPack());

                                        if (word == GetWord)
                                        {
                                            MCUpdater::ChooseHd(true);
                                        }
                                        else if (word == StopWord)
                                        {
                                            MCUpdater::StopHd();
                                        }
                                        else if (word == RestartWord)
                                        {
                                            MCUpdater::RestartForHd();
                                        }
                                    },
                                    .AskFirst = [](int) -> std::optional<MCQuestion>
                                    {
                                        if (HdPackWord(MCUpdater::HdPack()) != RemoveWord)
                                        {
                                            return std::nullopt;
                                        }

                                        return MCQuestion{.Heading = "Remove the HD pack?",
                                                          .Act = std::string(RemoveWord),
                                                          .Yes = [] { MCUpdater::ChooseHd(false); }};
                                    },
                                    .Word = [] { return std::string(HdPackWord(MCUpdater::HdPack())); },
                                    .Line = HdPackLine,
                                    .Held = HdPackHeld,
                                    .InMission = MCInMission::NotInMission,
                                    .Live = true});
        }

        MCSetting hdArt = HdSetting(
            {.Id = "hd-art", .Group = PackGroup, .Name = "HD art", .Key = HdArtKey, .Choices = OffOn, .Gpu = true});
        // Its shapes are tied to the pack as a mission loads (the console's art modes tie them again): it waits.
        hdArt.InMission = MCInMission::NotInMission;
        hdArt.Line = []
        {
            std::error_code error;
            return GHdArt != 0 && !std::filesystem::is_directory(MCHdArt::PackFolder(), error) ? PackNeeded("art")
                                                                                               : std::string();
        };

        all.push_back(std::move(hdArt));

        if (managed)
        {
            all.push_back(MCSetting{.Id = "hd-art-size",
                                    .Section = MCSection::Hd,
                                    .Group = PackGroup,
                                    .Name = "HD art size",
                                    .Control = MCControl::Switch,
                                    .Choices = Words({"Full", "2x"}),
                                    .Get = [] { return MCUpdater::HdPack().TwoXChosen ? TwoXArt : FullArt; },
                                    .Set = [](int choice) { MCUpdater::ChooseHdArt(choice == TwoXArt); },
                                    .AskFirst = AskHdArtSize,
                                    .Line = HdArtSizeLine,
                                    .Held = HdArtSizeHeld,
                                    .InMission = MCInMission::NotInMission,
                                    .Live = true});
        }

        all.push_back(HdSetting({.Id = "hd-lighting",
                                 .Group = LightingGroup,
                                 .Name = "HD lighting",
                                 .Key = HdLightingKey,
                                 .Choices = {"Off", "On", "Dusk", "Night"},
                                 .Gpu = true}));
        all.push_back(HdSetting({.Id = "sun-shadows",
                                 .Group = LightingGroup,
                                 .Name = "Sun shadows",
                                 .Key = "HDSunShadows",
                                 .Choices = OffOn,
                                 .Gpu = true,
                                 .Needs = HdArtKey,
                                 .NeedsName = "HD art"}));
        all.push_back(HdSetting({.Id = "relief",
                                 .Group = LightingGroup,
                                 .Name = "Relief",
                                 .Key = "HDRelief",
                                 .Choices = OffOn,
                                 .Gpu = true,
                                 .Needs = HdLightingKey,
                                 .NeedsName = "HD lighting"}));
        all.push_back(HdSetting({.Id = "cloud-shadows",
                                 .Group = LightingGroup,
                                 .Name = "Cloud shadows",
                                 .Key = "HDClouds",
                                 .Choices = OffOn,
                                 .Gpu = true}));
        all.push_back(HdSetting({.Id = "swaying-trees",
                                 .Group = LightingGroup,
                                 .Name = "Swaying trees",
                                 .Key = "HDSway",
                                 .Choices = OffOn,
                                 .Gpu = true,
                                 .Needs = HdArtKey,
                                 .NeedsName = "HD art"}));
        all.push_back(HdSetting({.Id = "particles",
                                 .Group = EffectsGroup,
                                 .Name = "Particles",
                                 .Key = ParticlesKey,
                                 .Choices = OffOn,
                                 .Gpu = true}));
        all.push_back(HdSetting({.Id = "flecks",
                                 .Group = EffectsGroup,
                                 .Name = "Flecks",
                                 .Key = "HDFlecks",
                                 .Choices = {"Edge", "Falling"},
                                 .Gpu = true,
                                 .Needs = ParticlesKey,
                                 .NeedsName = "particles"}));
        all.push_back(HdSetting({.Id = "screen-shake",
                                 .Group = EffectsGroup,
                                 .Name = "Screen shake",
                                 .Key = "HDShake",
                                 .Choices = OffOn,
                                 .Gpu = true}));
        all.push_back(HdSetting({.Id = "footprints",
                                 .Group = EffectsGroup,
                                 .Name = "Footprints",
                                 .Key = "HDFootprints",
                                 .Choices = OffOn}));
        all.push_back(HdSetting({.Id = "weather",
                                 .Group = EffectsGroup,
                                 .Name = "Weather",
                                 .Key = "HDWeather",
                                 .Choices = {"Off", "On", "Rain", "Downpour", "Light snow", "Snow", "Blizzard"},
                                 .Gpu = true}));
        all.push_back(HdSetting({.Id = "smooth-motion",
                                 .Group = MechsGroup,
                                 .Name = "Smooth motion",
                                 .Key = "HDSmoothMotion",
                                 .Choices = OffOn,
                                 .Gpu = true}));
        all.push_back(HdSetting(
            {.Id = "smooth-turns", .Group = MechsGroup, .Name = "Smooth turns", .Key = "HDTurns", .Choices = OffOn}));
        all.push_back(HdSetting(
            {.Id = "held-strides", .Group = MechsGroup, .Name = "Held strides", .Key = "HDStops", .Choices = OffOn}));

        // SOUND: the volumes (each previewed as 1998's sliders are, at their release), then the HD audio.
        all.push_back(Volume(
            "music", "Music", &MCSoundSystem::MusicLevel, [] { return MCPrefsDefaults::MusicVolume; },
            [](bool inMission)
            {
                // The menus' music again at its new level; a mission's plays on at it.
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr && !inMission)
                {
                    sound->StopDigitalMusic();
                    sound->PlayDigitalMusic(MenusMusic, true);
                }
            }));
        all.push_back(Volume(
            "radio", "Radio messages", &MCSoundSystem::RadioLevel, [] { return MCPrefsDefaults::RadioVolume; },
            [](bool)
            {
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
                {
                    sound->PlayPilotSpeech(RadioSpeech, RadioSpeechId);
                }
            }));
        all.push_back(Volume(
            "effects", "Sound effects", &MCSoundSystem::DigitalMasterVolume, [] { return MCPrefsDefaults::SfxVolume; },
            [](bool)
            {
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
                {
                    sound->PlayDigitalSample(EffectsSample, EffectsChannel, std::nullopt, false, false);
                }
            }));
        // The interface's default is the effects' volume (PREFS keeps it only when it differs).
        all.push_back(Volume(
            "interface", "Interface", &MCSoundSystem::InterfaceLevel,
            []
            {
                const std::shared_ptr<MCSoundSystem> sound = SoundSystem();
                return sound != nullptr ? int32_t{sound->DigitalMasterVolume} : MCPrefsDefaults::SfxVolume;
            },
            [](bool)
            {
                if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
                {
                    sound->PlayInterfaceSample(InterfaceSample);
                }
            }));

        MCSetting hdAudio = HdSetting({.Id = "hd-audio",
                                       .Section = MCSection::Sound,
                                       .Group = HdPackGroup,
                                       .Name = "HD audio",
                                       .Key = HdAudioKey,
                                       .Choices = {"Off", "On", "Orchestral"}});
        // ORCHESTRAL only when the pack has orchestral music. Without it ORCHESTRAL plays the stereo music, as ON does:
        // it shows as ON, and stays the player's ORCHESTRAL (PREFS keeps it) unless they pick another.
        hdAudio.Choices = []
        { return MCHdAudio::HasOrchestral() ? std::vector<std::string>{"Off", "On", "Orchestral"} : OffOn; };
        const auto shownAudio = []
        { return GHdAudio == HdAudioOrchestral && !MCHdAudio::HasOrchestral() ? int{HdAudioOn} : GHdAudio; };
        hdAudio.Get = shownAudio;
        hdAudio.Set = [shownAudio, stored = hdAudio.Set](int choice)
        {
            if (choice != shownAudio())
            {
                stored(choice);
            }
        };

        hdAudio.Control = MCControl::Switch;
        hdAudio.Line = []
        { return GHdAudio != HdAudioOff && !MCHdAudio::PackReadable() ? PackNeeded("audio") : std::string(); };
        all.push_back(std::move(hdAudio));

        // REPORTS: the player's own answer; DEFAULTS and ORIGINAL leave it.
        const MCHdSettings::MCSetting& reports = MCHdSettings::Find(ReportsKey);
        all.push_back(MCSetting{.Id = "reports",
                                .Section = MCSection::Reports,
                                .Group = PrivacyGroup,
                                .Name = "Reports",
                                .Control = MCControl::Switch,
                                .Choices = Words(OffOn),
                                .Get = [&reports] { return reports.Value.get() != 0 ? 1 : 0; },
                                .Set =
                                    [&reports](int choice)
                                {
                                    reports.Value.get() = choice != 0 ? 1 : 0;
                                    // The player's own choice once kept (ACCEPT): no notice or question comes after.
                                    ReportsChanged = true;
                                }});
        return all;
    }

    /// <summary>The table, made once for each kind of game (its rows' functions are listened to by address).</summary>
    const std::vector<MCSetting>& Table(bool managed)
    {
        static const std::vector<MCSetting> withPack = MakeAll(true);
        static const std::vector<MCSetting> withoutPack = MakeAll(false);
        return managed ? withPack : withoutPack;
    }

    /// <summary>Whether a running mission holds <paramref name="setting"/>.</summary>
    bool MissionHolds(const MCSetting& setting, bool inMission)
    {
        return inMission && setting.InMission != MCInMission::Free;
    }

    void RestoreVolume(uint8_t& level, int32_t saved)
    {
        level = static_cast<uint8_t>(std::clamp(saved, 0, VolumeMost));
    }
}

std::string_view MCSettingsModel::SectionName(MCSection section)
{
    switch (section)
    {
        case MCSection::Game:
            return "Game";
        case MCSection::Display:
            return "Display";
        case MCSection::Hd:
            return "HD";
        case MCSection::Sound:
            return "Sound";
        case MCSection::Controls:
            return "Controls";
        case MCSection::Reports:
            return "Reports";
    }

    return {};
}

std::span<const MCSetting> MCSettingsModel::All()
{
    return Table(MCUpdater::HdPack().Managed);
}

MCOptionalRef<const MCSetting> MCSettingsModel::Find(std::string_view id)
{
    for (const MCSetting& setting : All())
    {
        if (setting.Id == id)
        {
            return setting;
        }
    }

    return std::nullopt;
}

std::string MCSettingsModel::HeldNow(const MCSetting& setting, bool inMission)
{
    if (MissionHolds(setting, inMission))
    {
        return std::string(setting.InMission == MCInMission::NotInMission ? NotInMissionLine : BetweenMissionsLine);
    }

    return setting.Held ? setting.Held() : std::string();
}

int MCSettingsModel::LevelPercent(const MCSetting& setting, int level)
{
    return setting.Most > 0 ? static_cast<int>(std::lround(level * double{PercentMost} / setting.Most)) : 0;
}

void MCSettingsModel::Opened(bool inMission)
{
    ReportsChanged = false;

    if (!inMission)
    {
        EnsureRegistryVersion();
        // A debug console's art mode is for the run only (MCArtModes.h): the screen shows and keeps the player's own.
        EndArtMode();
    }
}

auto MCSettingsModel::Save() -> MCSaved
{
    MCSaved saved{.PaletteCycle = GuiSystem().PaletteCycle,
                  .Only45Pixel = Only45Pixel,
                  .Brightness = GuiSystem().GammaLevel,
                  .Difficulty = GameDifficulty,
                  .Renderer = GRendererPreference,
                  .UiResolution = GUiResolution,
                  .InterfaceSize = GInterfaceSize,
                  .ScrollSpeed = GScrollSpeed,
                  .Hd = MCHdSettings::Now()};

    if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
    {
        saved.Music = sound->MusicLevel;
        saved.Radio = sound->RadioLevel;
        saved.Effects = sound->DigitalMasterVolume;
        saved.Interface = sound->InterfaceLevel;
    }

    return saved;
}

void MCSettingsModel::Restore(const MCSaved& saved)
{
    GuiSystem().PaletteCycle = saved.PaletteCycle;
    Only45Pixel = saved.Only45Pixel;

    if (GuiSystem().GammaLevel != saved.Brightness)
    {
        GuiSystem().GammaCorrectCurrentPalette(saved.Brightness);
    }

    if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
    {
        RestoreVolume(sound->MusicLevel, saved.Music);
        RestoreVolume(sound->RadioLevel, saved.Radio);
        RestoreVolume(sound->DigitalMasterVolume, saved.Effects);
        RestoreVolume(sound->InterfaceLevel, saved.Interface);
    }

    GameDifficulty = saved.Difficulty;
    GRendererPreference = saved.Renderer;
    GUiResolution = saved.UiResolution;
    GInterfaceSize = saved.InterfaceSize;
    GScrollSpeed = saved.ScrollSpeed;
    MCHdSettings::Restore(saved.Hd);
    ReportsChanged = false;
}

void MCSettingsModel::Keep()
{
    // REPORTS changed here is the player's answer: the notice is seen, and no question comes after.
    if (std::exchange(ReportsChanged, false))
    {
        MCTelemetry::Chosen();
    }

    SavePrefs();
}

void MCSettingsModel::UseDefaults(MCSection section, bool inMission)
{
    for (const MCSetting& setting : All())
    {
        if (setting.Section == section && setting.UseDefault && !MissionHolds(setting, inMission))
        {
            setting.UseDefault();
        }
    }
}

void MCSettingsModel::UseOriginal(bool inMission)
{
    for (const MCSetting& setting : All())
    {
        if (setting.UseOriginal && !MissionHolds(setting, inMission))
        {
            setting.UseOriginal();
        }
    }
}
