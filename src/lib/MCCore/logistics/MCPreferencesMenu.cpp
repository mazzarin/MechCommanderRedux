#include "stdafx.h"
#include "logistics/MCPreferencesMenu.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFitIniFile.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogComboBox.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogSlider.h"
#include "logistics/MCLogToolButton.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCRegistrySettings.h"
#include "main/MCLogistics.h"
#include "platform/MCPresenter.h"
#include "platform/MCRenderer.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdArt.h"
#include "sprite/MCSpriteManager.h"
#include "vfx/MCVfxFunctions.h"
#include "logistics/MCSplashScreen.h"

int32_t GameDifficulty = 1;
bool Only45Pixel = false;
bool Force32MB = false;
bool Force16MB = false;

namespace
{
    // Port: the preferences screen is laid out in code over the panel art (prefs_00.tga, element 7), so it can hold
    // more settings than the art has room for. A panel covers the art's, taller, and holds a box for each group of
    // settings with its labels and controls: the sliders (each drawing its own track, cut from the art), the
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
    constexpr MCPrefsPlace PanelPlace{13, 51, 263, 275};

    /// <summary>
    /// The boxes on the panel: the art's (SOUND one row taller), then RENDERER and UI RESOLUTION under DIFFICULTY.
    /// </summary>
    constexpr MCPrefsPlace BrightnessBoxPlace{10, 11, 112, 38};
    constexpr MCPrefsPlace SoundBoxPlace{10, 59, 112, 113};
    constexpr MCPrefsPlace DifficultyBoxPlace{132, 11, 120, 63};
    constexpr MCPrefsPlace RendererBoxPlace{132, 78, 120, 50};
    constexpr MCPrefsPlace UiResolutionBoxPlace{132, 132, 120, 50};

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
    constexpr MCPrefsSliderRow MusicRow{"MUSIC VOLUME", 9, 19};
    constexpr MCPrefsSliderRow RadioRow{"RADIO MESSAGE VOLUME", 35, 45};
    constexpr MCPrefsSliderRow FxRow{"SOUND FX VOLUME", 62, 72};
    constexpr MCPrefsSliderRow InterfaceRow{"INTERFACE VOLUME", 88, 98};

    /// <summary>
    /// A slider's track in the art (the brightness one, with the black rows above and below it that the thumb covers),
    /// and how far in from its ends the thumb's travel lies: the art's tracks are a pixel wider each side than the
    /// fit's sliders.
    /// </summary>
    constexpr MCPrefsPlace SliderTrackArt{29, 82, 98, 8};
    constexpr int32_t SliderThumbInset = 1;

    /// <summary>A volume slider's range.</summary>
    constexpr int32_t VolumeMax = 127;

    /// <summary>A drop-down box's title and drop-down (under the title, in the art's checks' column and first row).</summary>
    constexpr int32_t BoxTitleLeft = 32;
    constexpr int32_t BoxTitleTop = 9;
    constexpr int32_t DropDownLeft = 32;
    constexpr int32_t DropDownTop = 21;
    constexpr int32_t DropDownWidth = 76;

    /// <summary>ACCEPT and CANCEL on the panel: side by side at its bottom right, 10 pixels in.</summary>
    constexpr int32_t AcceptLeft = 120;
    constexpr int32_t CancelLeft = 189;
    constexpr int32_t ButtonsTop = 250;

    /// <summary>The INTERFACE VOLUME slider (the screen owns it).</summary>
    std::weak_ptr<MCLogSlider> InterfaceSlider;

    /// <summary>The renderer choice, the UI resolution and the interface volume when the screen opened (for CancelPrefs).</summary>
    int32_t SavedRendererPreference = 0;
    int32_t SavedUiResolution = 0;
    int32_t SavedInterfaceLevel = 0;

    /// <summary>The click INTERFACE VOLUME plays at its new level.</summary>
    constexpr uint32_t InterfacePreviewSample = 0x10;

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

    /// <summary>Gives <paramref name="slider"/> the art's track and places it with its label in <paramref name="box"/>.</summary>
    void PlaceSlider(MCGenericScreen& screen, MCGuiObject& box, MCLogSlider& slider, const MCPrefsSliderRow& row,
                     std::shared_ptr<MCLogPort> track)
    {
        AddLabel(screen, box, row.Label, SliderLabelLeft, row.LabelTop);
        slider.Resize(SliderTrackArt.Width, SliderTrackArt.Height);
        slider.SetTrack(std::move(track), SliderThumbInset);
        slider.MoveTo(SliderLeft, row.SliderTop);
    }

    /// <summary>Moves the fit's slider <paramref name="element"/> into <paramref name="box"/> with its label and track.</summary>
    void MoveSlider(MCGenericScreen& screen, MCGuiObject& box, int32_t element, const MCPrefsSliderRow& row,
                    std::shared_ptr<MCLogPort> track)
    {
        MoveOnto(screen, element, box, SliderLeft, row.SliderTop);
        PlaceSlider(screen, box, *screen.Element<MCLogSlider>(element), row, std::move(track));
    }

    /// <summary>The art's slider track, cut out as a picture of its own.</summary>
    std::shared_ptr<MCLogPort> CutSliderTrack(MCGenericScreen& screen)
    {
        auto track = std::make_shared<MCLogPort>();
        track->Init(SliderTrackArt.Width, SliderTrackArt.Height);
        const MCLogPort& art = *screen.Element<MCLogImage>(PanelArtElement)->Art;
        VfxPaneCopy(art.Frame(), SliderTrackArt.Left, SliderTrackArt.Top, track->Frame(), 0, 0, VfxNoColor);
        return track;
    }

    /// <summary>Adds a drop-down editing <paramref name="setting"/> in <paramref name="box"/>.</summary>
    void AddDropDown(MCGenericScreen& screen, MCGuiObject& box, int32_t& setting,
                     std::vector<MCLogComboBox::Item> items, std::function<void(int32_t value)> changed)
    {
        auto dropDown = MCMakeGui<MCLogComboBox>();
        dropDown->Init(DropDownLeft, DropDownTop, DropDownWidth, setting, std::move(items), std::move(changed));
        dropDown->ShowGuiWindow(true);
        screen.AdoptChildOf(box, std::move(dropDown));
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
    const std::shared_ptr<MCLogPort> track = CutSliderTrack(screen);

    MCLogGroupBox& brightness = AddBox(screen, panel, BrightnessBoxPlace, BoxOutlineColor);
    MoveSlider(screen, brightness, BrightnessSliderElement, BrightnessRow, track);

    MCLogGroupBox& sound = AddBox(screen, panel, SoundBoxPlace, BoxOutlineColor);
    MoveSlider(screen, sound, MusicSliderElement, MusicRow, track);
    MoveSlider(screen, sound, RadioSliderElement, RadioRow, track);
    MoveSlider(screen, sound, FxSliderElement, FxRow, track);

    // INTERFACE VOLUME: a slider of the volumes' range.
    auto newSlider = MCMakeGui<MCLogSlider>();
    newSlider->Init(SliderLeft, InterfaceRow.SliderTop, SliderTrackArt.Width, SliderTrackArt.Height, {});
    newSlider->MinValue = 0;
    newSlider->MaxValue = VolumeMax;
    newSlider->SetEventRoutine(SlideInterfaceVolume);
    newSlider->ShowGuiWindow(true);
    InterfaceSlider = newSlider;
    MCLogSlider& interfaceSlider = screen.AdoptChildOf(sound, std::move(newSlider));
    PlaceSlider(screen, sound, interfaceSlider, InterfaceRow, track);

    MCLogGroupBox& difficulty = AddBox(screen, panel, DifficultyBoxPlace, BoxOutlineColor);
    AddLabel(screen, difficulty, "DIFFICULTY", BoxTitleLeft, BoxTitleTop);
    AddDropDown(screen, difficulty, GameDifficulty, {{"EASY", 0}, {"REGULAR", 1}, {"HARD", 2}}, nullptr);

    MCLogGroupBox& renderer = AddBox(screen, panel, RendererBoxPlace, BoxOutlineColor);
    AddLabel(screen, renderer, "RENDERER", BoxTitleLeft, BoxTitleTop);
    AddDropDown(screen, renderer, GRendererPreference,
                {{"VULKAN", static_cast<int32_t>(MCRendererKind::Vulkan)},
                 {"SOFTWARE", static_cast<int32_t>(MCRendererKind::Software)}},
                RendererChanged);

    // UI RESOLUTION: the most lines the in-mission UI is drawn at, from the next scenario on.
    MCLogGroupBox& uiResolution = AddBox(screen, panel, UiResolutionBoxPlace, BoxOutlineColor);
    AddLabel(screen, uiResolution, "UI RESOLUTION", BoxTitleLeft, BoxTitleTop);
    std::vector<MCLogComboBox::Item> uiResolutions = {{"NATIVE", 0}};

    for (const int lines : UiResolutions)
    {
        uiResolutions.push_back({std::format("{}P", lines), lines});
    }

    AddDropDown(screen, uiResolution, GUiResolution, std::move(uiResolutions), nullptr);

    MoveOnto(screen, AcceptElement, panel, AcceptLeft, ButtonsTop);
    MoveOnto(screen, CancelElement, panel, CancelLeft, ButtonsTop);
}

void ShowPreferences()
{
    EnsureRegistryVersion();

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
    SavedInterfaceLevel = SoundSystem()->InterfaceLevel;
    MCGenericScreen& screen = *logistics->PrefScreen;
    screen.Element<MCLogSlider>(BrightnessSliderElement)->SetCurrentValue(brightness);
    screen.Element<MCLogSlider>(MusicSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs3));
    screen.Element<MCLogSlider>(RadioSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs4));
    screen.Element<MCLogSlider>(FxSliderElement)->SetCurrentValue(static_cast<int32_t>(logistics->SavedPrefs5));

    if (const std::shared_ptr<MCLogSlider> interfaceSlider = InterfaceSlider.lock(); interfaceSlider != nullptr)
    {
        interfaceSlider->SetCurrentValue(SavedInterfaceLevel);
    }

    // A difficulty past regular lights hard.
    DifficultyToggle(0)->Toggled = GameDifficulty == 0;
    DifficultyToggle(1)->Toggled = GameDifficulty == 1;
    DifficultyToggle(2)->Toggled = GameDifficulty != 0 && GameDifficulty != 1;
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
    Cancel();
}

void WritePrefs()
{
    MCFitIniFile prefs;
    prefs.Create("prefs.cfg");
    prefs.WriteBlock("MechCommander");
    prefs.WriteIdBoolean("PaletteCycle", GuiSystem().PaletteCycle);
    prefs.WriteIdBoolean("DirectDraw", GFullScreen != 0);
    prefs.WriteIdBoolean("Use90Pixel", Use90PixelSprite);
    prefs.WriteIdBoolean("Force45Pixel", Only45Pixel);
    prefs.WriteIdBoolean("Force16Mb", Force16MB);
    prefs.WriteIdBoolean("Force32Mb", Force32MB);
    prefs.WriteIdLong("Difficulty", GameDifficulty);
    prefs.WriteIdLong("Brightness", GuiSystem().GammaLevel);
    prefs.WriteIdLong("MusicVolume", SoundSystem()->MusicLevel);
    prefs.WriteIdLong("RadioVolume", SoundSystem()->RadioLevel);
    prefs.WriteIdLong("SFXVolume", SoundSystem()->DigitalMasterVolume);
    // Port: keep the port-only keys. Original behaviour (OB-101): the hidden "Resolution" key is not written back.
    prefs.WriteIdLong("InterfaceVolume", SoundSystem()->InterfaceLevel);
    prefs.WriteIdBoolean("StretchToFit", GStretchToFit != 0);
    prefs.WriteIdBoolean("SoftwareCursor", GSoftwareCursor != 0);
    prefs.WriteIdString("Renderer", MCRendererKindName(static_cast<MCRendererKind>(GRendererPreference)));
    prefs.WriteIdLong("UIResolution", GUiResolution);
    prefs.WriteIdBoolean("ShowFps", GShowFpsPreference != 0);
    prefs.WriteIdBoolean("HDArt", GHdArt != 0);
    Cancel();
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
