#pragma once

#include "gui/MCGuiCallback.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"
#include "gui/MCGuiTimerManager.h"
#include "logistics/MCSmuti.h"
#include "platform/MCBlockStore.h"
#include "platform/MCRegisteredBlock.h"

class MCDisplay;
class MCFont;
class MCGuiFont;
class MCGuiMessageBox;
class MCGuiConsole;
class MCGameSession;
class MCGuiSmackerWindow;
class MCPacketFile;

/// <summary>
/// The mouse cursor shapes (<see cref="MCGuiSystem::SetCurrentCursor"/>). The enumerator names were lost; the values
/// run 0..0x12 (0xf..0x11 are offset by the interface's cursor set, 0x12 maps to shape 1).
/// </summary>
enum MCCursorType : int32_t;

/// <summary>
/// The application: the root of the window tree (the screen window is its child), with the display, the palette and
/// its gamma, the mouse cursor, the per-frame callbacks, the objects holding the mouse or the keyboard, the modal
/// object, the timers, the fonts and the art file. There is one, <see cref="GuiSystem"/>, an <see cref="MCGameContext"/>
/// system.
/// </summary>
/// <remarks>
/// Original source: <c>gui\asystem.cpp</c> (<c>aSystem</c>). <see cref="Start"/> brings the game up (it runs
/// <c>SystemInit</c> and <c>UserInit</c>), <see cref="Run"/> is the main loop, and <see cref="Stop"/> takes everything
/// down before the system is deleted. A test can install a system that never started and give it a screen
/// (<see cref="MakeScreen"/>) to build GUI objects without booting the game.
/// </remarks>
class MCGuiSystem : public MCGuiObject
{
public:
    MCGuiSystem();
    ~MCGuiSystem() override;

    /// <summary>The screen width.</summary>
    int32_t Width() override;
    int32_t Height() override;

    /// <summary>
    /// Makes the screen: its port (<paramref name="width"/> x <paramref name="height"/>, without pixels of its own:
    /// <see cref="ALockScreen"/> points it at the display's) and its window, the root every screen is a child of.
    /// </summary>
    void MakeScreen(int32_t width, int32_t height);

    /// <summary>Opens the display (the window's size in pixels, at least 640x480).</summary>
    void OpenDisplay();
    /// <summary>Re-opens the display in another mode (full screen or a window, another size).</summary>
    void ResetDisplay(int32_t width, int32_t height, int32_t bitDepth);
    void CloseDisplay();
    /// <summary>
    /// Port-only: makes the screen the window's size in pixels (at least 640x480; no taller than PREFS "UIResolution",
    /// <see cref="GUiResolution"/>, at the window's aspect, and that many times fewer lines for PREFS "InterfaceSize",
    /// <see cref="GInterfaceSize"/>) when the window or the settings have changed: the display's
    /// buffer, <see cref="GWidth"/>/<see cref="DisplayWidth"/>/<see cref="ScreenWidth"/>, the screen window, then the
    /// original's screen-resized broadcast so the in-mission windows follow. Called when a scenario starts and every
    /// frame of one; the 640x480 screens draw only parts of the buffer, so they never resize it.
    /// </summary>
    /// <returns>Whether the screen changed size.</returns>
    bool FollowWindowSize();
    /// <summary>
    /// Port-only: the interface size in percent AUTO (<see cref="GInterfaceSize"/>'s) picks for the window as it is
    /// now, at the UI RESOLUTION set (<see cref="GUiResolution"/>), as the next <see cref="FollowWindowSize"/> takes
    /// it; the original's size before the display opens.
    /// </summary>
    int AutoInterfaceSizeNow() const;
    /// <summary>The display, or null before it opens; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCDisplay> Display() const { return _Display; }
    /// <summary>Whether the display is up (palette changes and repaints wait for it).</summary>
    bool DisplayReady() const { return _DisplayReady; }
    /// <summary>Sets the scroll-trigger rectangle: (1, 1) to the screen's size less 4.</summary>
    void SetScrollRect();

    /// <summary>
    /// Starts the game: the fonts, the palette, the art file, the display, the cursors, the screen, the timers, the
    /// tactical interface, then <c>UserInit</c>, and the mouse tracker.
    /// </summary>
    /// <returns>0 when started, -10 when <c>UserInit</c> failed.</returns>
    int Start(std::string_view commandLine, int16_t screenWidth, int16_t screenHeight);
    /// <summary>Shuts everything <see cref="Start"/> started down.</summary>
    void Stop();
    /// <summary>Plays Smacker movie <paramref name="fileName"/> in a new movie window centred on the screen, holding
    /// the game until it ends (<see cref="SmackerWindow"/>).</summary>
    /// <returns>0, or the movie's or window's error.</returns>
    int32_t StartSmackerMovie(std::string_view fileName);
    /// <summary>Ends the movie playing: stops it and deletes its window.</summary>
    void CloseMovie();
    /// <summary>The main loop: pumps messages, runs the callbacks and redraws until the game quits.</summary>
    void Run();
    /// <summary>Runs the frame callbacks (the ones there when the frame starts; one removed meanwhile is skipped).</summary>
    /// <param name="includeAdded">Also runs the ones added while they run, as the tests' frame step always has.</param>
    void RunFrameCallbacks(bool includeAdded = false);

    /// <summary>Adds a callback run every frame (its holder keeps it; the list refers to it).</summary>
    /// <returns>0, or 2 for none.</returns>
    int32_t AddCallback(std::weak_ptr<MCGuiCallback> callback);
    /// <returns>0, or 1 when not found.</returns>
    int32_t RemoveCallback(const MCGuiCallback& callback);
    /// <summary>The frame callbacks, in order.</summary>
    const std::vector<std::weak_ptr<MCGuiCallback>>& Callbacks() const { return _Callbacks; }

    /// <summary>Makes <paramref name="obj"/> the modal object and brings it to the front.</summary>
    void SetModalObject(MCGuiObject& obj);
    /// <summary>The modal object: only it and its children take events (null for none); the caller keeps it as its
    /// pin.</summary>
    std::shared_ptr<MCGuiObject> ModalObject() const { return Modal.lock(); }
    void ClearModal();
    /// <summary>Gives <paramref name="obj"/> the mouse.</summary>
    void Grab(std::weak_ptr<MCGuiObject> obj);
    /// <summary>Gives <paramref name="obj"/> the keyboard (the old holder gets a focus-lost event, the new one a
    /// focus-gained one); none takes it away.</summary>
    void SetText(std::weak_ptr<MCGuiObject> obj);
    /// <summary>Sets the object under the cursor.</summary>
    void SetCurrentObject(std::weak_ptr<MCGuiObject> obj);
    /// <summary>Lets go of the mouse grab.</summary>
    void Release();
    /// <summary>Lets go of the keyboard focus.</summary>
    void ReleaseText();
    /// <summary>The object holding the mouse, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiObject> GrabbedObject() const { return Grabbed.lock(); }
    /// <summary>The object holding the keyboard, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiObject> TextObject() const { return TextFocus.lock(); }
    /// <summary>The object under the cursor, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiObject> CurrentObject() const { return Current.lock(); }
    /// <summary>Whether <paramref name="obj"/> holds the mouse.</summary>
    bool Grabs(const MCGuiObject& obj) const { return MCRefersTo(Grabbed, obj); }
    /// <summary>Whether <paramref name="obj"/> holds the keyboard.</summary>
    bool HasText(const MCGuiObject& obj) const { return MCRefersTo(TextFocus, obj); }

    /// <summary>
    /// Lets go of every reference to <paramref name="obj"/>, which is being destroyed: its grab, its keyboard focus
    /// (with the focus-lost event), modal, and the object under the cursor (found again, from the screen).
    /// </summary>
    void LetGo(MCGuiObject& obj);

    /// <summary>
    /// Sets <paramref name="count"/> palette entries from <paramref name="first"/> (cut to 10..245 unless a movie
    /// plays), through the gamma table; <paramref name="sixBit"/> shifts 6-bit values up.
    /// </summary>
    void TweakPalette(int32_t first, int32_t count, std::span<const MCVfxRgb> colors, bool sixBit);
    /// <summary>Steps the gamma level (0..3) and re-applies the palette.</summary>
    void GammaCorrectCurrentPalette();
    void GammaCorrectCurrentPalette(int32_t level);
    /// <summary>
    /// Port-only: on a sharp screen (HD art, the GPU drawing), every font's glyphs made ahead off the game's thread
    /// (platform/MCSharpPictures), each font once; nothing otherwise. Start does it as the display opens, so the boot is
    /// its time (a launcher's game shows its room soon after), and logistics when HD art comes on.
    /// </summary>
    void MakeFontsAhead() const;

    /// <summary>
    /// Fades the screen to black, then blacks out palette entries 10..245. Port-only: <paramref name="whileFading"/>'s
    /// pieces of work are done in order, one after each step from the first, any still left once the screen is black;
    /// the fade keeps its pace (it follows the time, so a piece's step darkens by more).
    /// </summary>
    void FadeDownCurrentPalette(std::span<const std::function<void()>> whileFading = {});
    /// <summary>
    /// Sets the palette's entries from <paramref name="first"/> on, or, with <paramref name="first"/> 0, keeps
    /// <paramref name="count"/> entries in <see cref="PendingPalette"/> for the next frame to fade to.
    /// </summary>
    void ActivatePalette(std::span<const uint8_t> colors, int32_t first, int32_t count);
    /// <summary>Activates the palette of TGA file <paramref name="fileName"/> (under <c>ArtPath</c>).</summary>
    void ActivatePaletteFromTga(std::string_view fileName);
    /// <summary>Shows a movie's palette (8-bit entries, all 256).</summary>
    void ActivateSmackerPalette(std::span<const uint8_t> colors);

    int32_t AddTimer(std::weak_ptr<MCGuiObject> target, int16_t id, int32_t interval, int32_t eventType,
                     int32_t eventData, bool useScenarioTime) const;
    int32_t AddUniqueTimer(std::weak_ptr<MCGuiObject> target, int16_t id, int32_t interval, int32_t eventType,
                           int32_t eventData, bool useScenarioTime) const;
    void RemoveTimer(const MCGuiObject& target, int16_t id) const;
    void RemoveTimers(const MCGuiObject& target) const;
    /// <summary>
    /// Port: hands what refers to <paramref name="from"/> over to <paramref name="to"/>, which stands for the same thing
    /// on another screen (a view of shared state: the chat's input line): its timers as they run, and the text focus,
    /// without a focus event (the original had one object, which kept both as it moved).
    /// </summary>
    void HandOver(const MCGuiObject& from, const std::weak_ptr<MCGuiObject>& to);
    /// <summary>Sets the mouse cursor (ignored while the cursor is hidden).</summary>
    void SetCurrentCursor(MCCursorType cursor);
    void SetCursorVisible(bool show);

    /// <summary>
    /// Hides <paramref name="object"/> now and takes it off its parent (which destroys it) when the next frame starts:
    /// for an object whose own event handling ends it (the version box's OK button, OB-158).
    /// </summary>
    void Retire(MCGuiHandle<MCGuiObject> object);

    /// <summary>
    /// PREFS "PaletteCycle" (read as an int): the water palette animates when nonzero (-1 until the prefs are read;
    /// the mission sets 1 after a movie).
    /// </summary>
    int32_t PaletteCycle = -1;
    /// <summary>The current palette (8 bits per channel), before gamma.</summary>
    std::array<MCVfxRgb, 256> CurrentPalette = {};
    /// <summary>The gamma level, 0..3 (PREFS "Brightness", read as an int).</summary>
    int32_t GammaLevel = 0;
    /// <summary>
    /// A palette <see cref="ActivatePalette"/> was given from entry 0: the next frame fades the screen down, then
    /// shows these entries from <see cref="PendingPaletteFirst"/>. Empty when none waits.
    /// </summary>
    std::vector<MCVfxRgb> PendingPalette;
    int32_t PendingPaletteFirst = 0;
    int32_t PendingPaletteCount = 0;
    /// <summary>The text formatter the inventory blocks and the chat window share.</summary>
    MCSmuti TextFormatter;
    int32_t ScreenWidth = 0;
    int32_t ScreenHeight = 0;
    /// <summary>The object holding the mouse.</summary>
    std::weak_ptr<MCGuiObject> Grabbed;
    /// <summary>The object holding the keyboard.</summary>
    std::weak_ptr<MCGuiObject> TextFocus;
    /// <summary>The object under the cursor.</summary>
    std::weak_ptr<MCGuiObject> Current;
    /// <summary>The modal object.</summary>
    std::weak_ptr<MCGuiObject> Modal;
    MCCursorType CurrentCursor = static_cast<MCCursorType>(0);
    /// <summary>Set while the cursor is hidden (<see cref="SetCursorVisible"/>).</summary>
    bool CursorHidden = false;
    /// <summary>The cursor shape drawn (-1 hidden).</summary>
    int32_t CursorShape = -1;

    /// <summary>The shape table of cursor <paramref name="index"/> (empty for an id past the file's shapes).</summary>
    std::span<uint8_t> CursorShapeData(int32_t index) const
    {
        return _CursorShapes[static_cast<size_t>(index)].Bytes();
    }

    /// <summary>Port-only: every cursor's shape table (the hardware cursor makes its pictures from them).</summary>
    std::span<const MCRegisteredBlock> CursorShapes() const { return _CursorShapes; }

    /// <summary>Where the mouse scrolls the map: outside (1, 1)..(width - 4, height - 4).</summary>
    tagRECT ScrollRect = {};
    std::unique_ptr<MCGuiTimerManager> TimerManager;
    /// <summary>The game's systems from the end of <see cref="Start"/> to <see cref="Stop"/>.</summary>
    std::unique_ptr<MCGameSession> Session;
    /// <summary>The movie window, a child of the screen, while a movie plays (the game waits for it).</summary>
    MCGuiHandle<MCGuiSmackerWindow> SmackerWindow;
    /// <summary>
    /// The window every screen is a child of (<see cref="ScreenWindow"/>): the root of the tree, the one GUI object
    /// owned from outside it. Letting it go destroys it and every window made on it.
    /// </summary>
    MCGuiRef<MCGuiObject> Screen;
    /// <summary>The port of the whole screen (<see cref="ScreenPort"/>).</summary>
    std::shared_ptr<MCGuiPort> ScreenPixels;
    /// <summary>The art packet file (<c>art\art.pak</c>) the ports load numbered pictures from.</summary>
    std::unique_ptr<MCPacketFile> ArtFile;
    /// <summary>The engine font used for lines of text drawn in the world (<see cref="LineFont"/>).</summary>
    std::shared_ptr<MCFont> EngineFont;
    /// <summary>The version box (Ctrl+Alt+V with cheats on), a child of the screen.</summary>
    MCGuiHandle<MCGuiMessageBox> VersionDialog;
    /// <summary>Port: the debug console, a child of the screen, while it is open (<see cref="OpenConsole"/>).</summary>
    MCGuiHandle<MCGuiConsole> Console;
    /// <summary>The frame callback that sends the mouse's moves and clicks (<see cref="CheckMouse"/>).</summary>
    std::shared_ptr<MCGuiCallback> MouseTrackerCallback = std::make_shared<MCGuiCallback>();

private:
    /// <summary>Loads the fonts and fills the font globals.</summary>
    void LoadFonts();
    /// <summary>Lets go of the fonts and clears the font globals.</summary>
    void FreeFonts();
    /// <summary>Loads the cursor shapes (<c>cursors.pak</c>) into <c>_CursorShapes</c>.</summary>
    void LoadCursors();
    /// <summary>Hands <paramref name="count"/> shown colours from <paramref name="first"/> to the display.</summary>
    void ShowPalette(int32_t first, int32_t count);

    /// <summary>The display (DirectDraw's objects and the window in the original).</summary>
    /// <summary>The display; shared, so the input layer can refer to it (weak_ptr).</summary>
    std::shared_ptr<MCDisplay> _Display;
    /// <summary>Set once the display is up: palette changes and repaints wait for it.</summary>
    bool _DisplayReady = false;
    /// <summary>The palette as shown: <see cref="CurrentPalette"/> through the gamma table.</summary>
    std::array<MCVfxRgb, 256> _LogicalPalette = {};
    /// <summary>The frame callbacks (their holders keep them).</summary>
    std::vector<std::weak_ptr<MCGuiCallback>> _Callbacks;
    /// <summary>
    /// The cursor shapes by id. 128 are kept, though <c>cursors.pak</c> has fewer: the ids the game sets index the
    /// table, and an id past the file's shapes finds an empty slot.
    /// </summary>
    std::array<MCRegisteredBlock, 128> _CursorShapes;
    /// <summary>The fonts the font globals share.</summary>
    std::vector<std::shared_ptr<MCGuiFont>> _Fonts;
    /// <summary>Objects <see cref="Retire"/> hid, taken off their parents when the next frame starts.</summary>
    std::vector<MCGuiHandle<MCGuiObject>> _Retired;

    /// <summary>Takes the objects <see cref="Retire"/> hid off their parents, which destroys them.</summary>
    void CloseRetired();
};

/// <summary>
/// The GUI system: present from the game's start-up to its exit (asserted); <see cref="HasGuiSystem"/> asks for the
/// tests that install none.
/// </summary>
MCGuiSystem& GuiSystem();
/// <summary>Whether a GUI system is installed.</summary>
bool HasGuiSystem();
/// <summary>
/// Port-only: whether this run asked for the GPU renderer (<see cref="GRenderer"/>) and the CPU draws instead, the GPU
/// having failed to start (MCDisplay's fallback): what the GPU alone draws doesn't show.
/// </summary>
bool GpuFellBack();
/// <summary>The window every screen is a child of, or null; the caller keeps it as its pin.</summary>
std::shared_ptr<MCGuiObject> ScreenWindow();
/// <summary>The port of the whole screen, or null; the caller keeps it as its pin.</summary>
std::shared_ptr<MCGuiPort> ScreenPort();
/// <summary>The engine font used for lines of text drawn in the world, or null; the caller keeps it as its pin.</summary>
std::shared_ptr<MCFont> LineFont();
/// <summary>The window's size in pixels when it changed: see <see cref="MCGuiSystem::FollowWindowSize"/>.</summary>
bool MCFollowWindowSize();

/// <summary>Draws every screen into its picture.</summary>
void ARedrawScreen();
/// <summary>Points the screen port at the display's buffer (the original locked the DirectDraw back surface).</summary>
int32_t ALockScreen();
int32_t AUnlockScreen();
/// <summary>Sends an event of type <paramref name="message"/> to <paramref name="obj"/> at once.</summary>
void APostMessage(MCGuiObject& obj, int32_t message);
/// <summary>Sends an event of type <paramref name="message"/> to <paramref name="obj"/> at once; nothing for none (the
/// original skipped unreadable pointers).</summary>
void APostMessage(std::weak_ptr<MCGuiObject> obj, int32_t message);
/// <summary>The version box's OK button: takes the box away.</summary>
void DestroyVersion();
