#include "stdafx.h"
#include "logistics/MCPreferencesMenu.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCControlsPanel.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCHdOptionsPanel.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "main/MCHdSettings.h"
#include "main/MCLogistics.h"
#include "main/MCSystemConfig.h"
#include "platform/MCDisplay.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCSpriteManager.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCArtModes.h"

int32_t GameDifficulty = 1;
bool Only45Pixel = false;
bool Force32MB = false;
bool Force16MB = false;

namespace
{
    // Port: the preferences screen is laid out in code over the panel art (prefs_00.tga, element 7), so it can hold
    // more settings than the art has room for. A panel covers the art's, taller, and holds a box for each group of
    // settings with its labels and controls: the sliders (each drawing its own track from the art), the
    // DIFFICULTY and RENDERER drop-downs, and ACCEPT and CANCEL side by side at the bottom right. The original's
    // DIFFICULTY checks (elements 8..10) are hidden. Places are in the art's pixels: the panel's from the art's
    // corner, everything else's from the corner of the box it is on.

    /// <summary>The fit's ACCEPT and CANCEL buttons.</summary>
    constexpr int32_t AcceptElement = 1;
    constexpr int32_t CancelElement = 2;

    /// <summary>The fit's sliders: brightness, music, radio and sound effects.</summary>
    constexpr int32_t BrightnessSliderElement = 3;
    constexpr int32_t MusicSliderElement = 4;
    constexpr int32_t RadioSliderElement = 5;
    constexpr int32_t FxSliderElement = 6;

    /// <summary>The panel art.</summary>
    constexpr int32_t PanelArtElement = 7;

    /// <summary>The difficulty checks: elements 8 (easy), 9 (regular) and 10 (hard).</summary>
    constexpr int32_t FirstDifficultyToggle = 8;

    /// <summary>A place and a size, the outline included.</summary>
    struct MCPrefsPlace
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    /// <summary>The panel: the art's panel at its corner, made tall enough for the settings and the buttons.</summary>
    constexpr MCPrefsPlace PanelPlace{13, 51, 263, 270};

    /// <summary>
    /// The boxes on the panel: the art's (SOUND one row taller, its top in line with RENDERER's), then RENDERER, UI
    /// RESOLUTION, INTERFACE SIZE and SCROLL SPEED under DIFFICULTY, each drop-down's box as far round it as
    /// BRIGHTNESS's round its slider, and HD (its OPTIONS button showing the HD OPTIONS page) under SOUND, level with
    /// SCROLL SPEED.
    /// </summary>
    constexpr MCPrefsPlace BrightnessBoxPlace{10, 11, 112, 38};
    constexpr MCPrefsPlace SoundBoxPlace{10, 57, 112, 115};
    constexpr MCPrefsPlace DifficultyBoxPlace{132, 11, 120, 42};
    constexpr MCPrefsPlace RendererBoxPlace{132, 57, 120, 42};
    constexpr MCPrefsPlace UiResolutionBoxPlace{132, 103, 120, 42};
    constexpr MCPrefsPlace InterfaceSizeBoxPlace{132, 149, 120, 42};
    constexpr MCPrefsPlace ScrollSpeedBoxPlace{132, 195, 120, 42};
    constexpr MCPrefsPlace HdBoxPlace{10, 195, 112, 42};

    /// <summary>The panel's outline (the art's panel's grey) and every box's fill (black).</summary>
    constexpr uint8_t PanelOutlineColor = 0x18;
    constexpr uint8_t BoxFillColor = 0x10;
    /// <summary>A box's outline.</summary>
    constexpr uint8_t BoxOutlineColor = 0x13;

    /// <summary>A slider's label and the slider in a box: the label at (7, labelTop), the slider at (6, sliderTop).</summary>
    struct MCPrefsSliderRow
    {
        std::string_view Label;
        int32_t LabelTop = 0;
        int32_t SliderTop = 0;
    };

    constexpr int32_t SliderLabelLeft = 7;
    constexpr int32_t SliderLeft = 6;
    constexpr MCPrefsSliderRow BrightnessRow{"SCREEN BRIGHTNESS", 9, 20};
    constexpr MCPrefsSliderRow MusicRow{"MUSIC VOLUME", 11, 21};
    constexpr MCPrefsSliderRow RadioRow{"RADIO MESSAGE VOLUME", 37, 47};
    constexpr MCPrefsSliderRow FxRow{"SOUND FX VOLUME", 64, 74};
    constexpr MCPrefsSliderRow InterfaceRow{"INTERFACE VOLUME", 90, 100};

    /// <summary>
    /// A slider's track in the art (the brightness one, with the black rows above and below it that the thumb covers),
    /// and how far in from its ends the thumb's travel lies: the art's tracks are a pixel wider each side than the
    /// fit's sliders.
    /// </summary>
    constexpr MCPrefsPlace SliderTrackArt{29, 82, 98, 8};
    constexpr int32_t SliderThumbInset = 1;

    /// <summary>A volume slider's range.</summary>
    constexpr int32_t VolumeMax = 127;

    /// <summary>
    /// A drop-down box's title and drop-down (under the title), lined up with the sliders' labels and as wide as the
    /// box allows (DropDownLeft in from each side: 104 in the right column), so UI RESOLUTION's NATIVE can say it's the
    /// one for a Retina or HD screen.
    /// </summary>
    constexpr int32_t BoxTitleLeft = 8;
    constexpr int32_t BoxTitleTop = 9;
    constexpr int32_t DropDownLeft = 8;
    constexpr int32_t DropDownTop = 21;
    /// <summary>UI RESOLUTION's native choice: its lines follow the screen's scale (GUiResolution).</summary>
    constexpr std::string_view NativeUiResolution = "NATIVE (RETINA/HD)";
    /// <summary>SCROLL SPEED's and INTERFACE SIZE's AUTO choice.</summary>
    constexpr std::string_view AutoChoice = "AUTO";
    /// <summary>SCROLL SPEED's choices are factors of the original's speed: "1.5X" is 150 percent.</summary>
    constexpr double PercentPerFactor = 100.0;

    /// <summary>ACCEPT and CANCEL on the panel: side by side at its bottom right, 10 pixels in.</summary>
    constexpr int32_t AcceptLeft = 120;
    constexpr int32_t CancelLeft = 189;
    constexpr int32_t ButtonsTop = 246;
    /// <summary>CONTROLS on the panel: at its bottom left, 10 pixels in, in line with ACCEPT and CANCEL.</summary>
    constexpr int32_t ControlsLeft = 10;

    /// <summary>The INTERFACE VOLUME slider (the screen owns it).</summary>
    std::weak_ptr<MCLogSlider> InterfaceSlider;
    /// <summary>The INTERFACE SIZE drop-down (the screen owns it), and its AUTO item's index.</summary>
    std::weak_ptr<MCLogComboBox> InterfaceSizeDropDown;
    constexpr size_t AutoInterfaceSizeItem = 0;

    /// <summary>
    /// The RENDERER drop-down (the screen owns it), its GPU item's index, and what that item says when this run's GPU
    /// failed to start.
    /// </summary>
    std::weak_ptr<MCLogComboBox> RendererDropDownBox;
    constexpr size_t GpuRendererItem = 0;
    constexpr std::string_view GpuFailedNote = "FAILED";

    /// <summary>
    /// The renderer choice, the UI resolution, the interface size, the interface volume, the scroll speed and the HD
    /// settings when the screen opened (for CancelPrefs).
    /// </summary>
    int32_t SavedRendererPreference = 0;
    int32_t SavedUiResolution = 0;
    int32_t SavedInterfaceSize = AutoInterfaceSize;
    int32_t SavedInterfaceLevel = 0;
    int32_t SavedScrollSpeed = AutoScrollSpeed;
    std::vector<int32_t> SavedHdSettings = MCHdSettings::Now();

    /// <summary>The click INTERFACE VOLUME plays at its new level.</summary>
    constexpr uint32_t InterfacePreviewSample = 0x10;

    /// <summary>
    /// INTERFACE SIZE's AUTO says the size it picks for the window at the UI RESOLUTION chosen, "AUTO (200%)", so the
    /// sizes around it can be told apart.
    /// </summary>
    void LabelAutoInterfaceSize()
    {
        if (const std::shared_ptr<MCLogComboBox> dropDown = InterfaceSizeDropDown.lock(); dropDown != nullptr)
        {
            dropDown->SetLabel(AutoInterfaceSizeItem,
                               std::format("{} ({}%)", AutoChoice, GuiSystem().AutoInterfaceSizeNow()));
        }
    }

    /// <summary>
    /// RENDERER's VULKAN says "VULKAN (FAILED)" when this run asked for it and it couldn't start (the CPU draws), as
    /// AUTO says what it picks: the choice stays, for the next start.
    /// </summary>
    void LabelGpuRenderer()
    {
        if (const std::shared_ptr<MCLogComboBox> dropDown = RendererDropDownBox.lock(); dropDown != nullptr)
        {
            dropDown->SetLabel(GpuRendererItem, GpuFellBack()
                                                    ? std::format("{} ({})", MCGpuRendererChoice, GpuFailedNote)
                                                    : std::string(MCGpuRendererChoice));
        }
    }

    /// <summary>UI RESOLUTION chose other lines: AUTO's interface size follows them.</summary>
    void UiResolutionChanged(int32_t)
    {
        LabelAutoInterfaceSize();
    }

    /// <summary>The RENDERER drop-down chose <paramref name="renderer"/>: another one than this run's needs a restart.</summary>
    void RendererChanged(int32_t renderer)
    {
        if (renderer != GRenderer)
        {
            ShowMenuMessage("The new renderer takes effect when you restart MechCommander.");
        }
    }

    /// <summary>Adds a box at <paramref name="place"/> on <paramref name="parent"/>.</summary>
    MCLogGroupBox& AddBox(MCGenericScreen& screen, MCGuiObject& parent, const MCPrefsPlace& place, uint8_t outline)
    {
        auto box = MCMakeGui<MCLogGroupBox>(outline, BoxFillColor);
        box->Init(place.Left, place.Top, place.Width, place.Height);
        box->SetTransparent(true);
        box->ShowGuiWindow(true);
        return screen.AdoptChildOf(parent, std::move(box));
    }

    /// <summary>Adds a label showing <paramref name="text"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>) on <paramref name="parent"/>.</summary>
    void AddLabel(MCGenericScreen& screen, MCGuiObject& parent, std::string_view text, int32_t xPos, int32_t yPos)
    {
        auto label = MCMakeGui<MCLogLabel>(text);
        label->InitAt(xPos, yPos);
        label->ShowGuiWindow(true);
        screen.AdoptChildOf(parent, std::move(label));
    }

    /// <summary>Moves the fit's element <paramref name="element"/> onto <paramref name="parent"/> at (<paramref name="xPos"/>, <paramref name="yPos"/>).</summary>
    void MoveOnto(MCGenericScreen& screen, int32_t element, MCGuiObject& parent, int32_t xPos, int32_t yPos)
    {
        const std::shared_ptr<MCGuiObject> object = screen.Element<MCGuiObject>(element);
        screen.RemoveChild(*object);
        parent.AddChild(object);
        object->MoveTo(xPos, yPos);
    }

    /// <summary>Gives <paramref name="slider"/> the track in <paramref name="art"/> and places it with its label in <paramref name="box"/>.</summary>
    void PlaceSlider(MCGenericScreen& screen, MCGuiObject& box, MCLogSlider& slider, const MCPrefsSliderRow& row,
                     std::shared_ptr<MCLogPort> art)
    {
        AddLabel(screen, box, row.Label, SliderLabelLeft, row.LabelTop);
        slider.Resize(SliderTrackArt.Width, SliderTrackArt.Height);
        slider.SetTrack(std::move(art), SliderTrackArt.Left, SliderTrackArt.Top, SliderThumbInset);
        slider.MoveTo(SliderLeft, row.SliderTop);
    }

    /// <summary>
    /// Moves the fit's slider <paramref name="element"/> into <paramref name="box"/> with its label and the track in
    /// <paramref name="art"/>.
    /// </summary>
    void MoveSlider(MCGenericScreen& screen, MCGuiObject& box, int32_t element, const MCPrefsSliderRow& row,
                    std::shared_ptr<MCLogPort> art)
    {
        MoveOnto(screen, element, box, SliderLeft, row.SliderTop);
        PlaceSlider(screen, box, *screen.Element<MCLogSlider>(element), row, std::move(art));
    }

    /// <summary>Adds a drop-down editing <paramref name="setting"/> in <paramref name="box"/> (the screen owns it).</summary>
    std::weak_ptr<MCLogComboBox> AddDropDown(MCGenericScreen& screen, MCGuiObject& box, int32_t& setting,
                                             std::vector<MCLogComboBox::Item> items,
                                             std::function<void(int32_t value)> changed)
    {
        auto dropDown = MCMakeGui<MCLogComboBox>();
        dropDown->Init(DropDownLeft, DropDownTop, box.Width() - 2 * DropDownLeft, setting, std::move(items),
                       std::move(changed));
        dropDown->ShowGuiWindow(true);
        std::weak_ptr<MCLogComboBox> added = dropDown;
        screen.AdoptChildOf(box, std::move(dropDown));
        return added;
    }

    /// <summary>Difficulty toggle <paramref name="index"/> (0 easy, 1 regular, 2 hard) of the preferences screen.</summary>
    std::shared_ptr<MCLogToolButton> DifficultyToggle(int32_t index)
    {
        return GlobalLog()->PrefScreen->Element<MCLogToolButton>(FirstDifficultyToggle + index);
    }

    /// <summary>Lights difficulty toggle <paramref name="difficulty"/> (easy, regular, hard) and sets the difficulty.</summary>
    void SetDifficulty(int32_t difficulty)
    {
        for (int32_t i = 0; i < 3; i++)
        {
            DifficultyToggle(i)->Toggled = i == difficulty;
        }

        GameDifficulty = difficulty;
    }

    /// <summary>A volume slider's value as a sound level: its low byte, kept when under 0x80.</summary>
    std::optional<uint8_t> SliderVolume(MCGuiObject& slider)
    {
        const auto volume = static_cast<uint8_t>(static_cast<MCLogSlider&>(slider).CurrentValue);
        return volume < 0x80 ? std::optional<uint8_t>(volume) : std::nullopt;
    }

    /// <summary>A saved volume back: only the saved byte is compared (a volume is 0..127).</summary>
    void RestoreVolume(uint8_t& level, int32_t saved)
    {
        if (static_cast<uint8_t>(saved) < 0x80)
        {
            level = static_cast<uint8_t>(saved);
        }
    }
}

void AddPreferencesPanel(MCGenericScreen& screen)
{
    // The original's DIFFICULTY checks (easy, regular, hard) give way to the drop-down.
    for (int32_t i = 0; i < 3; i++)
    {
        screen.Element<MCLogToolButton>(FirstDifficultyToggle + i)->ShowGuiWindow(false);
    }

    MCGuiObject& art = *screen.Element<MCGuiObject>(PanelArtElement);
    auto newPanel = MCMakeGui<MCLogGroupBox>(PanelOutlineColor, BoxFillColor);
    newPanel->Init(art.X() + PanelPlace.Left, art.Y() + PanelPlace.Top, PanelPlace.Width, PanelPlace.Height);
    newPanel->SetTransparent(true);
    newPanel->ShowGuiWindow(true);
    MCLogGroupBox& panel = screen.AdoptChild(std::move(newPanel));
    const std::shared_ptr<MCLogPort> panelArt = screen.Element<MCLogImage>(PanelArtElement)->Art;

    MCLogGroupBox& brightness = AddBox(screen, panel, BrightnessBoxPlace, BoxOutlineColor);
    MoveSlider(screen, brightness, BrightnessSliderElement, BrightnessRow, panelArt);

    MCLogGroupBox& sound = AddBox(screen, panel, SoundBoxPlace, BoxOutlineColor);
    MoveSlider(screen, sound, MusicSliderElement, MusicRow, panelArt);
    MoveSlider(screen, sound, RadioSliderElement, RadioRow, panelArt);
    MoveSlider(screen, sound, FxSliderElement, FxRow, panelArt);

    // INTERFACE VOLUME: a slider of the volumes' range.
    auto newSlider = MCMakeGui<MCLogSlider>();
    newSlider->Init(SliderLeft, InterfaceRow.SliderTop, SliderTrackArt.Width, SliderTrackArt.Height, {});
    newSlider->MinValue = 0;
    newSlider->MaxValue = VolumeMax;
    newSlider->SetEventRoutine(SlideInterfaceVolume);
    newSlider->ShowGuiWindow(true);
    InterfaceSlider = newSlider;
    MCLogSlider& interfaceSlider = screen.AdoptChildOf(sound, std::move(newSlider));
    PlaceSlider(screen, sound, interfaceSlider, InterfaceRow, panelArt);

    MCLogGroupBox& difficulty = AddBox(screen, panel, DifficultyBoxPlace, BoxOutlineColor);
    AddLabel(screen, difficulty, "DIFFICULTY", BoxTitleLeft, BoxTitleTop);
    AddDropDown(screen, difficulty, GameDifficulty, {{"EASY", 0}, {"REGULAR", 1}, {"HARD", 2}}, nullptr);

    MCLogGroupBox& renderer = AddBox(screen, panel, RendererBoxPlace, BoxOutlineColor);
    AddLabel(screen, renderer, "RENDERER", BoxTitleLeft, BoxTitleTop);
    RendererDropDownBox = AddDropDown(screen, renderer, GRendererPreference,
                                      {{std::string(MCGpuRendererChoice), static_cast<int32_t>(MCRendererKind::Vulkan)},
                                       {"SOFTWARE", static_cast<int32_t>(MCRendererKind::Software)}},
                                      RendererChanged);

    // UI RESOLUTION: the most lines the in-mission UI is drawn at, from the next scenario on.
    MCLogGroupBox& uiResolution = AddBox(screen, panel, UiResolutionBoxPlace, BoxOutlineColor);
    AddLabel(screen, uiResolution, "UI RESOLUTION", BoxTitleLeft, BoxTitleTop);
    std::vector<MCLogComboBox::Item> uiResolutions = {{std::string(NativeUiResolution), 0}};

    for (const int lines : UiResolutions)
    {
        uiResolutions.push_back({std::format("{}P", lines), lines});
    }

    AddDropDown(screen, uiResolution, GUiResolution, std::move(uiResolutions), UiResolutionChanged);

    // INTERFACE SIZE: the in-mission interface's size, from the next scenario on (alt+= and alt+- step it in one):
    // AUTO (the original's proportions, labelled with its size when the screen opens), or a percent of the original's.
    MCLogGroupBox& interfaceSize = AddBox(screen, panel, InterfaceSizeBoxPlace, BoxOutlineColor);
    AddLabel(screen, interfaceSize, "INTERFACE SIZE", BoxTitleLeft, BoxTitleTop);
    std::vector<MCLogComboBox::Item> interfaceSizes = {{std::string(AutoChoice), AutoInterfaceSize}};

    for (const int percent : InterfaceSizes)
    {
        interfaceSizes.push_back({std::format("{}%", percent), percent});
    }

    InterfaceSizeDropDown = AddDropDown(screen, interfaceSize, GInterfaceSize, std::move(interfaceSizes), nullptr);

    // HD: the OPTIONS button shows the HD OPTIONS page, every HD setting (MCHdOptionsPanel.h).
    MCLogGroupBox& hd = AddBox(screen, panel, HdBoxPlace, BoxOutlineColor);
    AddLabel(screen, hd, "HD", BoxTitleLeft, BoxTitleTop);
    AddHdOptionsPanel(screen, panel, hd, DropDownLeft, DropDownTop);

    // SCROLL SPEED: the battlefield's scrolling, at the screen's edges and by the scroll keys: AUTO (the original's pace
    // across the screen), or a factor of it.
    MCLogGroupBox& scrollSpeed = AddBox(screen, panel, ScrollSpeedBoxPlace, BoxOutlineColor);
    AddLabel(screen, scrollSpeed, "SCROLL SPEED", BoxTitleLeft, BoxTitleTop);
    std::vector<MCLogComboBox::Item> scrollSpeeds;

    for (const int percent : ScrollSpeeds)
    {
        const std::string factor = std::format("{}X", percent / PercentPerFactor);
        scrollSpeeds.push_back({percent == AutoScrollSpeed ? std::string(AutoChoice) : factor, percent});
    }

    AddDropDown(screen, scrollSpeed, GScrollSpeed, std::move(scrollSpeeds), nullptr);

    MoveOnto(screen, AcceptElement, panel, AcceptLeft, ButtonsTop);
    MoveOnto(screen, CancelElement, panel, CancelLeft, ButtonsTop);

    // CONTROLS: the in-mission keys, on a panel of their own.
    AddControlsPanel(screen, panel, ControlsLeft, ButtonsTop);
}

std::shared_ptr<MCLogComboBox> PrefsRendererDropDown()
{
    return RendererDropDownBox.lock();
}

void ShowPreferences()
{
    EnsureRegistryVersion();
    // Port: an art mode the debug console showed is for the run only (MCArtModes.h): the screen shows and saves the
    // player's own HD ART and HD LIGHTING.
    EndArtMode();

    // The settings as they are, for CancelPrefs.
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const int32_t brightness = GuiSystem().GammaLevel;
    logistics->SavedPrefs0 = GuiSystem().PaletteCycle;
    logistics->SavedPrefs1 = Only45Pixel ? 1 : 0;
    logistics->SavedPrefs2 = brightness;
    logistics->SavedPrefs3 = SoundSystem()->MusicLevel;
    logistics->SavedPrefs4 = SoundSystem()->RadioLevel;
    logistics->SavedPrefs5 = SoundSystem()->DigitalMasterVolume;
    logistics->SavedPrefs6 = GameDifficulty;
    SavedRendererPreference = GRendererPreference;
    SavedUiResolution = GUiResolution;
    SavedInterfaceSize = GInterfaceSize;
    SavedInterfaceLevel = SoundSystem()->InterfaceLevel;
    SavedScrollSpeed = GScrollSpeed;
    MCGenericScreen& screen = *logistics->PrefScreen;
    screen.Element<MCLogSlider>(BrightnessSliderElement)->SetCurrentValue(brightness);
    screen.Element<MCLogSlider>(MusicSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs3));
    screen.Element<MCLogSlider>(RadioSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs4));
    screen.Element<MCLogSlider>(FxSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs5));

    if (const std::shared_ptr<MCLogSlider> interfaceSlider = InterfaceSlider.lock(); interfaceSlider != nullptr)
    {
        interfaceSlider->SetCurrentValue(SavedInterfaceLevel);
    }

    LabelAutoInterfaceSize();
    LabelGpuRenderer();

    // A difficulty past regular lights hard.
    DifficultyToggle(0)->Toggled = GameDifficulty == 0;
    DifficultyToggle(1)->Toggled = GameDifficulty == 1;
    DifficultyToggle(2)->Toggled = GameDifficulty != 0 && GameDifficulty != 1;
    // Port: the preferences panel shows first, CONTROLS edits the keys as they are, and HD OPTIONS starts at its top
    // (CANCEL puts the HD settings back as it shows them).
    ResetControls();
    ResetHdOptions();
    SavedHdSettings = MCHdSettings::Now();
    logistics->MainScreen->ShowGuiWindow(false);
    logistics->SetCurrentScreen(logistics->PrefScreen);
    logistics->PrefScreen->ShowGuiWindow(true);
    logistics->LogisticsState = 9;
}

void CancelPrefs()
{
    std::shared_ptr<MCLogistics> logistics = GlobalLog();
    GuiSystem().PaletteCycle = logistics->SavedPrefs0;
    Only45Pixel = logistics->SavedPrefs1 != 0;
    GuiSystem().GammaCorrectCurrentPalette(logistics->SavedPrefs2);
    RestoreVolume(SoundSystem()->MusicLevel, logistics->SavedPrefs3);
    RestoreVolume(SoundSystem()->RadioLevel, logistics->SavedPrefs4);
    RestoreVolume(SoundSystem()->DigitalMasterVolume, logistics->SavedPrefs5);
    RestoreVolume(SoundSystem()->InterfaceLevel, SavedInterfaceLevel);
    GameDifficulty = logistics->SavedPrefs6;
    GRendererPreference = SavedRendererPreference;
    GUiResolution = SavedUiResolution;
    GInterfaceSize = SavedInterfaceSize;
    GScrollSpeed = SavedScrollSpeed;
    MCHdSettings::Restore(SavedHdSettings);
    Cancel();
}

void WritePrefs()
{
    // Port: the keys CONTROLS edited become the player's, and are written after the settings.
    AcceptControls();
    SavePrefs();
    Cancel();
}

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
    MCFitIniFile prefs;

    if (const int32_t result = prefs.Create("prefs.cfg"); result != 0)
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

void PrefScreenHandleEvent(MCGuiObject&, MCGuiEvent&)
{
}

void SlideScreenBrightness(MCGuiObject& object, MCGuiEvent&)
{
    GuiSystem().GammaCorrectCurrentPalette(static_cast<MCLogSlider&>(object).CurrentValue);
}

void SlideMusicVolume(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 4)
    {
        return;
    }

    if (const std::optional<uint8_t> volume = SliderVolume(object))
    {
        SoundSystem()->MusicLevel = *volume;
    }

    SoundSystem()->StopDigitalMusic();
    SoundSystem()->PlayDigitalMusic(0x16, true);
}

void SlideRadioVolume(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 4)
    {
        return;
    }

    if (const std::optional<uint8_t> volume = SliderVolume(object))
    {
        SoundSystem()->RadioLevel = *volume;
    }

    SoundSystem()->PlayPilotSpeech(const_cast<char*>("pilotd"), 0x15);
}

void SlideFXVolume(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 4)
    {
        return;
    }

    if (const std::optional<uint8_t> volume = SliderVolume(object))
    {
        SoundSystem()->DigitalMasterVolume = *volume;
    }

    SoundSystem()->PlayDigitalSample(0xf, 1, std::nullopt, false, false);
}

void SlideInterfaceVolume(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 4)
    {
        return;
    }

    if (const std::optional<uint8_t> volume = SliderVolume(object))
    {
        SoundSystem()->InterfaceLevel = *volume;
    }

    SoundSystem()->PlayInterfaceSample(InterfacePreviewSample);
}

void EasyToggle()
{
    SetDifficulty(0);
}

void RegularToggle()
{
    SetDifficulty(1);
}

void HardToggle()
{
    SetDifficulty(2);
}
