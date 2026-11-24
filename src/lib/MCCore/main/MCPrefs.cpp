#include "stdafx.h"
#include "main/MCPrefs.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFitIniFile.h"
#include "main/MCArtModes.h"
#include "main/MCBenchmark.h"
#include "main/MCHdSettings.h"
#include "main/MCSystemConfig.h"
#include "platform/MCFileSystem.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"

int32_t GameDifficulty = 1;
bool Only45Pixel = false;
bool Force32MB = false;
bool Force16MB = false;

namespace
{
    /// <summary>Writes <paramref name="key"/> only when <paramref name="value"/> isn't its default.</summary>
    void WriteChanged(MCFitIniFile& prefs, std::string_view key, bool value, bool byDefault)
    {
        if (value != byDefault)
        {
            prefs.WriteIdBoolean(key, value);
        }
    }

    /// <summary>Writes <paramref name="key"/> only when <paramref name="value"/> isn't its default.</summary>
    void WriteChanged(MCFitIniFile& prefs, std::string_view key, int32_t value, int32_t byDefault)
    {
        if (value != byDefault)
        {
            prefs.WriteIdLong(key, value);
        }
    }
}

void SavePrefs()
{
    // Port-only (Redux HD): a benchmark's looks are its own, never the player's (main/MCBenchmark.h).
    if (MCBenchmark::Running())
    {
        SDL_Log("SavePrefs: a benchmark is running; the settings aren't saved");
        return;
    }

    MCFitIniFile prefs;

    if (const int32_t result = prefs.Create(MCFileSystem::SettingsToWrite(PrefsFile)); result != 0)
    {
        SDL_Log("SavePrefs: cannot create prefs.cfg (%#x); the settings aren't saved", static_cast<uint32_t>(result));
        return;
    }

    // Port: the format, then only the settings that differ from their defaults (MCPrefsDefaults), so a missing key
    // follows the build and a release that changes a default reaches every player who never picked otherwise. The
    // original wrote them all, Use90Pixel and Force45Pixel too, which the port doesn't read.
    prefs.WriteBlock("MechCommander");
    prefs.WriteIdLong("PrefsVersion", PrefsVersion);
    WriteChanged(prefs, "PaletteCycle", GuiSystem().PaletteCycle != 0, MCPrefsDefaults::PaletteCycle);
    WriteChanged(prefs, "DirectDraw", GFullScreen != 0, MCPrefsDefaults::FullScreen);
    WriteChanged(prefs, "Force16Mb", Force16MB, MCPrefsDefaults::Force16Mb);
    WriteChanged(prefs, "Force32Mb", Force32MB, MCPrefsDefaults::Force32Mb);
    WriteChanged(prefs, "Difficulty", GameDifficulty, MCPrefsDefaults::Difficulty);
    WriteChanged(prefs, "Brightness", GuiSystem().GammaLevel, MCPrefsDefaults::Gamma);
    WriteChanged(prefs, "MusicVolume", int32_t{SoundSystem()->MusicLevel}, MCPrefsDefaults::MusicVolume);
    WriteChanged(prefs, "RadioVolume", int32_t{SoundSystem()->RadioLevel}, MCPrefsDefaults::RadioVolume);
    const int32_t effectsVolume = SoundSystem()->DigitalMasterVolume;
    WriteChanged(prefs, "SFXVolume", effectsVolume, MCPrefsDefaults::SfxVolume);
    // Port: the port-only keys. Original behaviour (OB-101): the hidden "Resolution" key is not written back.
    // The interface sounds play at the effects' volume when PREFS lacks theirs.
    WriteChanged(prefs, "InterfaceVolume", int32_t{SoundSystem()->InterfaceLevel}, effectsVolume);
    WriteChanged(prefs, "StretchToFit", GStretchToFit != 0, MCPrefsDefaults::StretchToFit);
    WriteChanged(prefs, "SoftwareCursor", GSoftwareCursor != 0, MCPrefsDefaults::SoftwareCursor);

    // VULKAN, the default, is where GRenderer starts.
    if (GRendererPreference != static_cast<int>(MCRendererKind::Vulkan))
    {
        prefs.WriteIdString("Renderer", MCRendererKindName(static_cast<MCRendererKind>(GRendererPreference)));
    }

    WriteChanged(prefs, "UIResolution", GUiResolution, MCPrefsDefaults::UiResolution);
    WriteChanged(prefs, "ScrollSpeed", GScrollSpeed, AutoScrollSpeed);
    WriteChanged(prefs, "InterfaceSize", GInterfaceSize, AutoInterfaceSize);
    WriteChanged(prefs, "ShowFps", GShowFpsPreference != 0, MCPrefsDefaults::ShowFps);
    // Port: never a debug console's art mode (MCArtModes.h), but the player's own.
    {
        const MCPlayersArtScope playersArt;
        MCHdSettings::Write(prefs);
    }

    MCKeyBindings::WritePrefs(prefs);
}

int SteppedInterfaceSize(int size, int steps)
{
    const auto found = std::ranges::find(InterfaceSizes, size);
    const ptrdiff_t index = found != InterfaceSizes.end() ? std::distance(InterfaceSizes.begin(), found) : 0;
    const ptrdiff_t last = std::ssize(InterfaceSizes) - 1;
    return InterfaceSizes[static_cast<size_t>(std::clamp<ptrdiff_t>(index + steps, 0, last))];
}

bool StepInterfaceSize(int steps)
{
    // AUTO steps on from the size it picks for the display, and a step back onto that size is AUTO again.
    const int autoSize = GuiSystem().AutoInterfaceSizeNow();
    const int size = GInterfaceSize == AutoInterfaceSize ? autoSize : GInterfaceSize;
    const int stepped = SteppedInterfaceSize(size, steps);

    if (stepped == size)
    {
        return false;
    }

    GInterfaceSize = stepped == autoSize ? AutoInterfaceSize : stepped;
    SavePrefs();
    return true;
}
