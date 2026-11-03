#include "stdafx.h"
#include "main/MCHdSettings.h"
#include "engine/MCCraterManager.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "main/MCTelemetry.h"
#include "sound/MCHdAudio.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCMechActor.h"
#include "sprite/MCMechStops.h"
#include "sprite/MCParticles.h"
#include "sprite/MCSceneLights.h"
#include "sprite/MCWeather.h"

namespace
{
    using MCHdSettings::MCSetting;
    using MCParticles::MCFleckStyle;

    constexpr std::array Settings = {
        // HDArt draws the HD art pack's frames where it has them (sprite/MCHdArt.h).
        MCSetting{.Key = "HDArt", .Value = &GHdArt},
        // HDAudio plays the HD audio pack's music, effects and voices (sound/MCHdAudio.h).
        MCSetting{
            .Key = "HDAudio", .Value = &GHdAudio, .Most = HdAudioOrchestral, .Boolean = false, .Personal = HdAudioOn},
        // HDLighting lights the world around shots, fires and explosions, at the mission's time of day or at dusk or
        // night (sprite/MCSceneLights.h).
        MCSetting{.Key = "HDLighting",
                  .Value = &GHdLighting,
                  .Most = HdLightingNight,
                  .Boolean = false,
                  .Personal = HdLightingOn},
        // HDSunShadows draws objects' shadows darkening the ground instead of solid black (sprite/MCSceneLights.h).
        MCSetting{.Key = "HDSunShadows", .Value = &GHdSunShadows},
        // HDClouds lets clouds' shadows drift over the world (sprite/MCSceneLights.h).
        MCSetting{.Key = "HDClouds", .Value = &GHdClouds},
        // HDSway lets a forest's canopy sway before the wind (sprite/MCSceneLights.h).
        MCSetting{.Key = "HDSway", .Value = &GHdSway},
        // HDRelief lets the sun show the world's relief, the HD ground's from the pack's relief maps
        // (sprite/MCSceneLights.h).
        MCSetting{.Key = "HDRelief", .Value = &GHdRelief},
        // HDParticles draws flecks, debris, smoke, embers, splashes and wakes, its flecks flying as HDFlecks says;
        // HDShake lets large artillery strikes and buildings' ends shake the picture (sprite/MCParticles.h).
        MCSetting{.Key = "HDParticles", .Value = &GHdParticles},
        MCSetting{.Key = "HDFlecks",
                  .Value = &GHdFlecks,
                  .Least = static_cast<int32_t>(MCFleckStyle::Edge),
                  .Most = static_cast<int32_t>(MCFleckStyle::Falling),
                  .Boolean = false,
                  .Original = static_cast<int32_t>(MCFleckStyle::Edge),
                  .Personal = static_cast<int32_t>(MCFleckStyle::Edge)},
        MCSetting{.Key = "HDShake", .Value = &GHdShake},
        // HDWeather shows the missions in their own weather, or every one in rain, a downpour, light snow, snow or a
        // blizzard (sprite/MCWeather.h).
        MCSetting{.Key = "HDWeather",
                  .Value = &GHdWeather,
                  .Most = HdWeatherBlizzard,
                  .Boolean = false,
                  .Personal = HdWeatherOn},
        // HDFootprints keeps three times as many footprints and craters on the ground (engine/MCCraterManager.h).
        MCSetting{.Key = "HDFootprints", .Value = &GHdFootprints},
        // HDTurns eases a mech's turns as drawn and draws its legs between their facings (sprite/MCMechActor.h).
        MCSetting{.Key = "HDTurns", .Value = &GHdTurns},
        // HDStops has a mech stopped mid-stride with its move held up hold its stride (sprite/MCMechActor.h).
        MCSetting{.Key = "HDStops", .Value = &GHdStops},
        // MechBraking (gameplay) has mechs brake to a halt through their stopping art (sprite/MCMechStops.h).
        MCSetting{.Key = "MechBraking", .Value = &GMechBraking},
        // Reports sends anonymous reports of the computer, how the game runs and its crashes, once the player has seen
        // what they hold (main/MCTelemetry.h): off until then, whatever the build.
        MCSetting{.Key = "Reports", .Value = &GReports, .Personal = 0, .Player = true},
    };

    /// <summary>The defaults are the personal build's (<see cref="UseHdByDefault"/>).</summary>
    bool PersonalDefaults = false;

    /// <summary>A look DEFAULTS and ORIGINAL set (not the player's own answer, as REPORTS is).</summary>
    bool IsLook(const MCSetting& setting)
    {
        return !setting.Player;
    }
}

void UseHdByDefault()
{
    PersonalDefaults = true;
}

auto MCHdSettings::All() -> std::span<const MCSetting>
{
    return Settings;
}

auto MCHdSettings::Find(std::string_view key) -> const MCSetting&
{
    const auto found = std::ranges::find(Settings, key, &MCSetting::Key);

    if (found == Settings.end())
    {
        FatalMsg(std::format("MCHdSettings::Find: no HD setting {}", key));
    }

    return *found;
}

auto MCHdSettings::Default(const MCSetting& setting) -> int32_t
{
    return PersonalDefaults ? setting.Personal : setting.Original;
}

auto MCHdSettings::Read(MCFitIniFile& file) -> void
{
    for (const MCSetting& setting : Settings)
    {
        if (setting.Boolean)
        {
            *setting.Value = file.Read<bool>(setting.Key).value_or(Default(setting) != 0) ? 1 : 0;
        }
        else
        {
            *setting.Value =
                std::clamp(file.Read<int32_t>(setting.Key).value_or(Default(setting)), setting.Least, setting.Most);
        }
    }
}

auto MCHdSettings::Write(MCFitIniFile& file) -> void
{
    for (const MCSetting& setting : Settings)
    {
        if (setting.Boolean)
        {
            file.WriteIdBoolean(setting.Key, *setting.Value != 0);
        }
        else
        {
            file.WriteIdLong(setting.Key, *setting.Value);
        }
    }
}

auto MCHdSettings::Now() -> std::vector<int32_t>
{
    std::vector<int32_t> values;
    values.reserve(Settings.size());

    for (const MCSetting& setting : Settings)
    {
        values.push_back(*setting.Value);
    }

    return values;
}

auto MCHdSettings::Restore(std::span<const int32_t> values) -> void
{
    if (values.size() != Settings.size())
    {
        FatalMsg(std::format("MCHdSettings::Restore: {} values for {} settings", values.size(), Settings.size()));
    }

    for (size_t i = 0; i < Settings.size(); i++)
    {
        *Settings[i].Value = values[i];
    }
}

auto MCHdSettings::UseDefaults() -> void
{
    for (const MCSetting& setting : Settings | std::views::filter(IsLook))
    {
        *setting.Value = Default(setting);
    }
}

auto MCHdSettings::UseOriginal() -> void
{
    for (const MCSetting& setting : Settings | std::views::filter(IsLook))
    {
        *setting.Value = setting.Original;
    }
}
