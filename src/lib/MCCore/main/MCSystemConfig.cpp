#include "stdafx.h"
#include "main/MCSystemConfig.h"
#include "camera/MCCameraList.h"
#include "color/MCPalette.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFastFileSet.h"
#include "lib/MCFatal.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCPreferencesMenu.h"
#include "main/MCGameContext.h"
#include "main/MCGamePaths.h"
#include "main/MCGameSession.h"
#include "main/MCGameStrings.h"
#include "platform/MCFileSystem.h"
#include "platform/MCInput.h"
#include "platform/MCPresenter.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSpriteManager.h"
#include "terrain/MCTerrainTiles.h"

std::string CampaignFile = "campaign";
std::string MissionName = "MechCmdr1.fit";
bool AblDebuggerEnabled = false;
bool DebugGameSystem = false;
bool GNoSound = false;

namespace
{
    /// <summary>Reads a SYSTEM.CFG path; a missing one is fatal.</summary>
    void ReadPath(MCFitIniFile& file, std::string_view varName, std::string& path, std::string_view errMessage)
    {
        MCFitResult<std::string> value = file.Read<std::string>(varName);

        if (!value)
        {
            Fatal(std::to_underlying(value.error()), errMessage);
        }

        path = std::move(*value);
    }

    /// <summary>Reads a SYSTEM.CFG number; a missing one is fatal.</summary>
    template <typename T> T ReadNumber(MCFitIniFile& file, std::string_view varName, std::string_view errMessage)
    {
        const MCFitResult<T> value = file.Read<T>(varName);

        if (!value)
        {
            Fatal(std::to_underlying(value.error()), errMessage);
        }

        return *value;
    }

    /// <summary>Checks that SYSTEM.CFG has a number the port no longer uses; a missing one is still fatal.</summary>
    template <typename T> void RequireEntry(MCFitIniFile& file, std::string_view varName, std::string_view errMessage)
    {
        [[maybe_unused]] const T value = ReadNumber<T>(file, varName, errMessage);
    }

    /// <summary>The "can't read system.cfg" exit.</summary>
    [[noreturn]] void ClosingMessage()
    {
        MCInput::ShowCursor(true);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MechCommander Expansion or Editor already running.");

        if (!MCNoMessageBoxes)
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "MechCommander Closing",
                                     "MechCommander Expansion or Editor already running.", nullptr);
        }

        KillTheGame(1);
    }
}

void ReadSystemConfig(MCFitIniFile& file)
{
    // The "systemHeap" block (systemHeapSize, guiHeapSize, logisticsHeapSize) sized the original's heaps; the port has
    // none and doesn't read it.

    // Empty blocks switch sound and music on.
    UseSound = file.SeekBlock("UseSound") == 0 ? 1 : 0;

    if (file.SeekBlock("UseMusic") == 0)
    {
        UseMusic = UseSound != 0 ? 1 : 0;
    }
    else
    {
        UseSound = 0;
        UseMusic = 0;
    }

    if (GNoSound)
    {
        UseSound = 0;
        UseMusic = 0;
    }

    if (file.SeekBlock("DebugGameSystem") == 0)
    {
        DebugGameSystem = true;
    }

    //---------------------------------------------------------------------------------------------------------------
    // ABL.
    if (file.SeekBlock("ABL") != 0)
    {
        Fatal(0, " Unable to find ABL settings. ");
    }

    // ABL's heap, stack, code block, module and static variable sizes and its debugger limits are gone (the port's
    // ABL grows), and nothing reads IncludeDebugInfo; the keys are still read, so a SYSTEM.CFG without them still
    // fails as before.
    RequireEntry<uint32_t>(file, "SymbolTableHeapSize", "Could not find ABL SymbolTableHeapSize. ");
    RequireEntry<uint32_t>(file, "StackHeapSize", "Could not find ABL StackHeapSize. ");
    RequireEntry<uint32_t>(file, "CodeHeapSize", "Could not find ABL CodeHeapSize. ");
    RequireEntry<uint32_t>(file, "RunTimeStackSize", "Could not find ABL RunTimeStackSize. ");
    RequireEntry<uint32_t>(file, "MaxCodeBlockSize", "Could not find ABL MaxCodeBlockSize. ");
    RequireEntry<uint32_t>(file, "MaxRegisteredModules", "Could not find ABL MaxRegisteredModules. ");
    RequireEntry<uint32_t>(file, "MaxStaticVariables", "Could not find ABL MaxStaticVariables. ");
    RequireEntry<uint32_t>(file, "IncludeDebugInfo", "Could not find ABL IncludeDebugInfo. ");
    AblDebuggerEnabled = ReadNumber<uint32_t>(file, "DebuggerEnabled", "Could not find ABL DebuggerEnabled. ") != 0;
    RequireEntry<int32_t>(file, "MaxWatchesPerModule", "Could not find ABL MaxWatchesPerModule. ");
    RequireEntry<int32_t>(file, "MaxBreakPointsPerModule", "Could not find ABL MaxBreakPointsPerModule. ");

    //---------------------------------------------------------------------------------------------------------------
    // Paths.
    if (const int32_t result = file.SeekBlock("systemPaths"); result != 0)
    {
        Fatal(result, "Could not find systemPaths.  Using Defaults.");
    }

    ReadPath(file, "savePath", SavePath, " Could not find save path ");
    // Copies of the game on one machine share the user folder: each keeps its temp FITs under its process ID, or a
    // multiplayer client loads the host's generated scenario (bridge.fit) or the reverse.
    SaveTempPath = std::format("{}temp\\{}\\", SavePath, MCPort::ProcessId());
    MCFileSystem::MakeDirectory(SaveTempPath);
    ReadPath(file, "terrainPath", TerrainPath, " Could not find terrain path ");
    ReadPath(file, "palettePath", PalettePath, " Could not find palette path ");
    ReadPath(file, "artPath", ArtPath, " Could not find art path ");
    ReadPath(file, "fontPath", FontPath, " Could not find font path ");
    ReadPath(file, "soundPath", SoundPath, " Could not find sound path ");
    ReadPath(file, "spritePath", SpritePath, " Could not find sprite path ");
    ReadPath(file, "shapesPath", ShapesPath, " Could not find shapes path ");
    ReadPath(file, "objectPath", ObjectPath, " Could not find object path ");
    ReadPath(file, "missionPath", MissionPath, " Could not find mission path ");
    ReadPath(file, "warriorPath", WarriorPath, " Could not find warrior path ");
    ReadPath(file, "profilePath", ProfilePath, " Could not find profile path ");
    ReadPath(file, "cameraPath", CameraPath, " Could not find camera path ");
    ReadPath(file, "tilePath", TilePath, " Could not find tile path ");
    ReadPath(file, "tile90Path", Tile90Path, " Could not find tile90 path ");
    ReadPath(file, "interfacePath", InterfacePath, " Could not find interface path ");
    ReadPath(file, "moviePath", MoviePath, " Could not find movie path ");
    ReadPath(file, "missionName", MissionName, " Could not find Mission File Name ");
    ReadPath(file, "CDsoundPath", CDsoundPath, " Could not find CD sound path ");
    ReadPath(file, "CDspritePath", CDspritePath, " Could not find CD sprite path ");
    ReadPath(file, "CDmoviePath", CDmoviePath, " Could not find CD movie path ");
}

void ReadPreferences(MCFitIniFile& file)
{
    if (const int32_t result = file.SeekBlock("MechCommander"); result != 0)
    {
        Fatal(result, "Could not find MechCommander Prefs.");
    }

    GuiSystem().PaletteCycle = file.Read<bool>("PaletteCycle").value_or(false) ? 1 : 0;
    GuiSystem().GammaLevel = file.Read<int32_t>("Gamma").value_or(0);
    // One sprite size wins: 90-pixel sprites unless 45 is forced; without 90, 45 only. Port: the full-size (90-pixel)
    // mech art is always loaded and used: the camera stays at scale 100 and the zoom scales the world view instead (the
    // prefs only mattered for machines short of memory). Use90Pixel and Force45Pixel are not read.
    Use90PixelSprite = 1;
    Only45Pixel = false;
    Force32MB = file.Read<bool>("Force32Mb").value_or(false);

    // Both forced: 16 MB wins.
    Force16MB = file.Read<bool>("Force16Mb").value_or(false);

    if (Force16MB)
    {
        Force32MB = false;
    }

    GFullScreen = file.Read<bool>("DirectDraw").value_or(false) ? 1 : 0;
    // Port-only keys: the picture keeps 4:3 with bars unless StretchToFit is set; the cursor is the system's unless
    // SoftwareCursor is; ShowFps shows the frame counter in the top-right corner (the command line's -fps sets it too).
    GStretchToFit = file.Read<bool>("StretchToFit").value_or(false) ? 1 : 0;
    GSoftwareCursor = file.Read<bool>("SoftwareCursor").value_or(false) ? 1 : 0;
    const bool showFps = file.Read<bool>("ShowFps").value_or(false);
    GShowFpsPreference = showFps ? 1 : 0;
    GShowFps = showFps ? 1 : GShowFps;
    // Port-only: HDArt draws the HD art pack's frames where it has them (sprite/MCHdArt.h).
    GHdArt = file.Read<bool>("HDArt").value_or(false) ? 1 : 0;

    // Port-only: "vulkan" (the default) or "software". The command line's -renderer wins.
    if (const MCFitResult<std::string> renderer = file.Read<std::string>("Renderer"); renderer.has_value())
    {
        if (const std::optional<MCRendererKind> kind = MCRendererKindFromName(*renderer))
        {
            GRenderer = static_cast<int>(*kind);
        }
    }

    GRendererPreference = GRenderer;

    // Port-only: the in-mission UI's lines (0 native, else one of UiResolutions).
    const int32_t uiResolution = file.Read<int32_t>("UIResolution").value_or(0);
    GUiResolution = std::ranges::contains(UiResolutions, uiResolution) ? uiResolution : 0;

    // Port: the mode is still read, but the screen is the window's size (MCGuiSystem::OpenDisplay).
    switch (file.Read<int32_t>("Resolution").value_or(0))
    {
        case 1:
        {
            DisplayWidth = 800;
            DisplayHeight = 600;
            break;
        }

        case 2:
        {
            DisplayWidth = 1024;
            DisplayHeight = 768;
            break;
        }

        case 3:
        {
            DisplayWidth = 1280;
            DisplayHeight = 1024;
            break;
        }

        default:
        {
            break;
        }
    }

    LanguageOffset = file.Read<int32_t>("Language").value_or(0);
    GameDifficulty = file.Read<int32_t>("Difficulty").value_or(1);
    // OB-168: Brightness goes to the same field as Gamma, so it wins (and its absence resets gamma to 0).
    GuiSystem().GammaLevel = file.Read<int32_t>("Brightness").value_or(0);
    MusicVolume = file.Read<int32_t>("MusicVolume").value_or(0x40);
    RadioVolume = file.Read<int32_t>("RadioVolume").value_or(0x40);
    SfxVolume = file.Read<int32_t>("SFXVolume").value_or(0x60);
    // Port: the interface sounds' own volume; a PREFS.CFG without it plays them at the effects volume, as before.
    InterfaceVolume = file.Read<int32_t>("InterfaceVolume").value_or(SfxVolume);
}

void SystemInit()
{
    // Port: the original registered the "QueryCancelAutoPlay" window message to keep the CD's autorun from starting
    // while the game ran, and refused to run when system.cfg could not be opened exclusively (another copy of the game
    // or the editor had it). The port has no autorun and reads the file shared.
    {
        MCFitIniFile systemFile;

        if (systemFile.Open("system.cfg") != 0)
        {
            ClosingMessage();
        }

        ReadSystemConfig(systemFile);

        // Every *.fst in the game folder is a FastFile (SYSTEM.CFG's [FastFiles] list isn't read). One that doesn't
        // open is left out, as in the original.
        MCFastFileSet& fastFiles = MCGameContext::Current().FastFiles();

        for (const std::string& name : MCFileSystem::FindFiles("*.fst"))
        {
            [[maybe_unused]] const auto opened = fastFiles.Open(name);
        }
    }

    MCFitIniFile prefsFile;

    if (const int32_t result = prefsFile.Open("prefs.cfg"); result != 0)
    {
        Fatal(result, "Could not open prefs.cfg.");
    }

    ReadPreferences(prefsFile);

    // The original looked for the game CD here (and before each mission); the port reads everything from the install.
}
