#include "stdafx.h"
#include "gui/MCGuiSystem.h"
#include "color/MCPalette.h"
#include "engine/MCFont.h"
#include "gameos/MCSoundRenderer.h"
#include "gui/MCGuiConsole.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiInput.h"
#include "gui/MCGuiMessageBox.h"
#include "gui/MCGuiStartup.h"
#include "gui/MCHardwareCursor.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCPacketFile.h"
#include "main/MCGamePaths.h"
#include "logistics/MCMissionLogisticsBridge.h"
#include "main/MCGameContext.h"
#include "main/MCGameSession.h"
#include "main/MCSystemConfig.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMission.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCDisplay.h"
#include "platform/MCFileSystem.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCRenderer.h"
#include "platform/MCSmacker.h"
#include "vfx/MCVfxFunctions.h"

namespace
{
    /// <summary>A font for <see cref="MCGuiSystem::Start"/>; an empty file gives a font without letters.</summary>
    std::shared_ptr<MCGuiFont> LoadFont(std::string_view fileName)
    {
        std::expected<std::unique_ptr<MCGuiFont>, std::string> font = MCGuiFont::Create(fileName);
        return font ? std::shared_ptr<MCGuiFont>(std::move(*font)) : std::make_shared<MCGuiFont>();
    }
}

auto GuiSystem() -> MCGuiSystem&
{
    // The context's slot keeps the system: the reference outlives this pin.
    const std::shared_ptr<MCGuiSystem> gui = MCGameContext::Current().GuiSystem();
    SDL_assert(gui != nullptr);
    return *gui;
}

auto HasGuiSystem() -> bool
{
    return MCGameContext::Current().GuiSystem() != nullptr;
}

auto ScreenWindow() -> std::shared_ptr<MCGuiObject>
{
    return HasGuiSystem() ? GuiSystem().Screen : nullptr;
}

auto ScreenPort() -> std::shared_ptr<MCGuiPort>
{
    return HasGuiSystem() ? GuiSystem().ScreenPixels : nullptr;
}

auto LineFont() -> std::shared_ptr<MCFont>
{
    return HasGuiSystem() ? GuiSystem().EngineFont : nullptr;
}

MCGuiSystem::MCGuiSystem()
{
    MouseTrackerCallback->SetExec(CheckMouse);
}

MCGuiSystem::~MCGuiSystem()
{
    Session.reset();
    _Retired.clear();
    Console.reset();
    VersionDialog.reset();
    SmackerWindow.reset();
    Screen.reset();
    TimerManager.reset();
    FreeFonts();

    // No text field outlives the GUI.
    MCInput::SetTextInput(false);
}

auto MCGuiSystem::Width() -> int32_t
{
    return ScreenWidth;
}

auto MCGuiSystem::Height() -> int32_t
{
    return ScreenHeight;
}

auto MCGuiSystem::SetScrollRect() -> void
{
    ScrollRect.left = 1;
    ScrollRect.right = Width() - 4;
    ScrollRect.top = 1;
    ScrollRect.bottom = Height() - 4;
}

auto MCGuiSystem::MakeScreen(int32_t width, int32_t height) -> void
{
    ScreenPixels = std::make_shared<MCGuiPort>();
    ScreenPixels->InitScreen(width, height);
    Screen = MCMakeGui<MCGuiObject>();
    Screen->Init(0, 0, ScreenWidth, ScreenHeight, {});
    Screen->SetDepth(-100);
    Screen->ObjectType = 1;
}

auto MCGuiSystem::LoadFonts() -> void
{
    // Row = colour, column = small, medium, large (see Fonts).
    struct FontFile
    {
        std::reference_wrapper<std::shared_ptr<MCGuiFont>> Global;
        int32_t Color;
        int32_t Size;
        std::string_view Name;
    };

    static const std::array<FontFile, 26> files = {{
        {BlackFont, 0, 0, "blkfnt.fnt"},       {RedFont, 1, 0, "red.fnt"},
        {YellowFont, 2, 0, "yellow.fnt"},      {GreenFont, 3, 0, "green.fnt"},
        {BlueFont, 4, 0, "blue.fnt"},          {GreyFont, 5, 0, "gryfnt.fnt"},
        {WhiteFont, 6, 0, "white.fnt"},        {DimFont, 7, 0, "dim.fnt"},
        {YellowDropFont, 8, 0, "yelldrp.fnt"}, {BlueDropFont, 9, 0, "bluedrp.fnt"},
        {MedBlackFont, 0, 1, "blkfnt10.fnt"},  {MedRedFont, 1, 1, "red10.fnt"},
        {MedYellowFont, 2, 1, "yellow10.fnt"}, {MedGreenFont, 3, 1, "green10.fnt"},
        {MedBlueFont, 4, 1, "blue10.fnt"},     {MedGreyFont, 5, 1, "gryfnt10.fnt"},
        {MedWhiteFont, 6, 1, "white10.fnt"},   {MedDimFont, 7, 1, "dim10.fnt"},
        {LgBlackFont, 0, 2, "blkfnt12.fnt"},   {LgRedFont, 1, 2, "red12.fnt"},
        {LgYellowFont, 2, 2, "yellow12.fnt"},  {LgGreenFont, 3, 2, "green12.fnt"},
        {LgBlueFont, 4, 2, "blue12.fnt"},      {LgGreyFont, 5, 2, "gryfnt12.fnt"},
        {LgWhiteFont, 6, 2, "white12.fnt"},    {LgDimFont, 7, 2, "dim12.fnt"},
    }};

    for (const FontFile& file : files)
    {
        _Fonts.push_back(LoadFont(file.Name));
        file.Global.get() = _Fonts.back();
        Fonts[file.Color][file.Size] = _Fonts.back();
    }

    // The drop fonts have no medium size (the small ones serve) and no large one (the original's table held null
    // there, which drew nothing: a font without letters).
    Fonts[8][1] = YellowDropFont;
    Fonts[9][1] = BlueDropFont;
    _Fonts.push_back(std::make_shared<MCGuiFont>());
    Fonts[8][2] = _Fonts.back();
    Fonts[9][2] = _Fonts.back();
    SystemFont = GreyFont;
}

auto MCGuiSystem::FreeFonts() -> void
{
    for (std::shared_ptr<MCGuiFont>& global :
         {std::ref(SystemFont),  std::ref(BlackFont),      std::ref(GreyFont),      std::ref(WhiteFont),
          std::ref(RedFont),     std::ref(GreenFont),      std::ref(BlueFont),      std::ref(DimFont),
          std::ref(YellowFont),  std::ref(YellowDropFont), std::ref(BlueDropFont),  std::ref(MedBlackFont),
          std::ref(MedGreyFont), std::ref(MedWhiteFont),   std::ref(MedRedFont),    std::ref(MedGreenFont),
          std::ref(MedBlueFont), std::ref(MedDimFont),     std::ref(MedYellowFont), std::ref(LgBlackFont),
          std::ref(LgGreyFont),  std::ref(LgWhiteFont),    std::ref(LgRedFont),     std::ref(LgGreenFont),
          std::ref(LgBlueFont),  std::ref(LgDimFont),      std::ref(LgYellowFont)})
    {
        global.reset();
    }

    Fonts = {};
    _Fonts.clear();
}

auto MCGuiSystem::LoadCursors() -> void
{
    _CursorShapes = {};
    MCPacketFile cursorFile;

    if (cursorFile.Open(GamePath(SpritePath, "cursors", ".pak")) != 0 &&
        cursorFile.Open(GamePath(CDspritePath, "cursors", ".pak")) != 0)
    {
        Fatal(0, "Cannot find cursors.pak file");
    }

    const int32_t numCursors = cursorFile.GetNumPackets();

    if (numCursors >= static_cast<int32_t>(_CursorShapes.size()))
    {
        Fatal(-1, " Too Many cursor Shapes ");
    }

    for (int32_t i = 0; i < numCursors; i++)
    {
        cursorFile.SeekPacket(i);
        const auto size = static_cast<uint32_t>(cursorFile.GetPacketSize());
        _CursorShapes[static_cast<size_t>(i)] = MCRegisteredBlock(size, MCDataKind::Shapes);

        // An empty packet still fails, as it did when the system heap's malloc(0) returned null.
        if (_CursorShapes[static_cast<size_t>(i)].Empty())
        {
            Fatal(-1, " no RAM for cursors ");
        }

        cursorFile.ReadPacket(i, _CursorShapes[static_cast<size_t>(i)].Bytes());
    }

    cursorFile.Close();
    MCHardwareCursorPreload(_CursorShapes);
}

auto MCGuiSystem::Start(std::string_view commandLine, int16_t screenWidth, int16_t screenHeight) -> int
{
    // The window is named after the port, not the string table's title (string 0x283).
#ifdef _DEBUG
    AppName = "MechCommander Redux (Debug)";
#else
    AppName = "MechCommander Redux";
#endif
    WindowTitle = AppName;

    // The original gave up when another copy was running (a window of class "MCX", or the "MCX" file mapping),
    // registered the window class, and checked for a Pentium with CPUID; none of that applies.
    DisplayWidth = screenWidth;
    DisplayHeight = screenHeight;
    SystemInit();
    const int32_t width = static_cast<int16_t>(DisplayWidth);
    const int32_t height = static_cast<int16_t>(DisplayHeight);
    ScreenWidth = width;
    ScreenHeight = height;
    _Callbacks.clear();
    ParseCommandLine(commandLine);

    // The window is the display's, made by OpenDisplay below; the original made it here. Messages reach WindowProc
    // through the platform layer.
    MCInput::SetWindowProc(WindowProc);
    LoadFonts();

    // The engine's line font; without its file it has no letters (the original ignored the failure too).
    std::expected<std::unique_ptr<MCFont>, std::string> lineFont = MCFont::Create("font");
    EngineFont = lineFont ? std::shared_ptr<MCFont>(std::move(*lineFont)) : std::make_shared<MCFont>();

    std::expected<std::unique_ptr<MCPalette>, std::string> palette = MCPalette::Create("palette");

    if (!palette)
    {
        Fatal(0, std::format(" Unable to initialize game palette: {} ", palette.error()));
    }

    MCGameContext::Current().SetPalette(std::move(*palette));
    InitAlphaLookup(GamePalette().Colors());

    ArtFile = std::make_unique<MCPacketFile>();

    if (ArtFile->Open(GamePath(ArtPath, "art", ".pak")) != 0)
    {
        Fatal(0, "Error opening art file");
    }

    OpenDisplay();
    LoadCursors();
    MCInput::ShowCursor(false);
    CursorShape = -1;
    MouseTimerInit();

    // The screen port is sized as asked; ALockScreen gives it the display's size and pixels.
    MakeScreen(width, height);
    GamePalette().Activate();
    CountsPerSecond = MCPort::PerformanceFrequency();
    UpdateDisplay(false, false, 0, false, 0);
    AUnlockScreen();

    TimerManager = std::make_unique<MCGuiTimerManager>();
    MCGameContext::Current().SetTacticalInterface(std::make_unique<MCTacticalInterface>());
    TacticalInterface()->Init();

    Session = std::make_unique<MCGameSession>();
    SetScrollRect();
    CursorHidden = false;
    SetCurrentCursor(static_cast<MCCursorType>(0));
    SetCursorVisible(false);
    AddCallback(MouseTrackerCallback);
    return 0;
}

auto MCGuiSystem::Stop() -> void
{
    DestroyAllFitFiles(SaveTempPath);
    // The temp folder is this process's own (temp\<pid>\, see SystemInit): it goes with its files.
    MCFileSystem::RemoveDirectory(SaveTempPath);
    RemoveCallback(*MouseTrackerCallback);

    if (Mission() != nullptr)
    {
        Mission()->CloseResultsScreen();
    }

    Session.reset();

    if (MultiPlayer() != nullptr)
    {
        MCGameContext::Current().SetMultiPlayer(nullptr);
    }

    MouseTimerKill();
    MCSoundRenderer::Uninstall();
    MCInput::ShowCursor(true);
    CloseDisplay();
    MCInput::ClipCursor(std::nullopt);
    SetCurrentObject({});
    StartupPakFile.clear();
    MCGameContext::Current().SetTacticalInterface(nullptr);

    // The screen's last windows (the original left a version box or a movie window still open to its assert).
    _Retired.clear();
    Console.reset();
    VersionDialog.reset();

    if (SmackerWindow != nullptr)
    {
        CloseMovie();
    }

    Screen.reset();
    ScreenPixels.reset();
    MCGameContext::Current().SetPalette(nullptr);
    ArtFile.reset();
    FreeFonts();
    EngineFont.reset();
    TimerManager.reset();

    // The cursor shapes went with the system heap in the original.
    _CursorShapes = {};
}

auto MCGuiSystem::StartSmackerMovie(std::string_view fileName) -> int32_t
{
    std::unique_ptr<MCSmackTag> movie = SmackOpen(std::string(fileName).c_str(), 0xfe000, -1);

    if (movie == nullptr)
    {
        return static_cast<int32_t>(0xddddd002);
    }

    // A window of the movie's size, centred on the screen.
    const int32_t movieWidth = movie->Player->Width();
    const int32_t movieHeight = movie->Player->Height();
    auto window = MCMakeGui<MCGuiSmackerWindow>();
    MCGuiObject& windowObject = *window;
    int32_t result = windowObject.Init(static_cast<int32_t>(static_cast<uint32_t>(Width() - movieWidth) >> 1),
                                       static_cast<int32_t>(static_cast<uint32_t>(Height() - movieHeight) >> 1),
                                       movieWidth, movieHeight, "Movie Time");

    if (result != 0)
    {
        return result;
    }

    result = window->StartSmackerMovie(std::move(movie), true);

    if (result != 0)
    {
        return result;
    }

    SmackerWindow = std::move(window);
    SmackerWindow->SetDepth(100);
    ScreenWindow()->AddChild(SmackerWindow);
    SmackerWindow->Draw();
    return 0;
}

auto MCGuiSystem::CloseMovie() -> void
{
    if (SmackerWindow != nullptr)
    {
        SmackerWindow->EndSmackerMovie();
        SmackerWindow.reset();
    }
}

auto MCGuiSystem::RunFrameCallbacks(bool includeAdded) -> void
{
    _Retired.clear();

    // The original's array: a callback removed while they run shifts the later ones down (the next one waits a frame),
    // and the slots past the end read null.
    const size_t count = _Callbacks.size();

    for (size_t i = 0; i < (includeAdded ? _Callbacks.size() : count); i++)
    {
        if (i < _Callbacks.size())
        {
            MCGuiCallback::Run(_Callbacks[i]);
        }
    }
}

auto MCGuiSystem::Run() -> void
{
    for (int32_t i = 0; i < ScreenWindow()->NumberOfChildren(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ScreenWindow()->Child(i); child != nullptr)
        {
            child->Draw();
        }
    }

    UpdateDisplay(false, false, 0, false, 0);
    bool quit = false;

    do
    {
        PerfStartTime = MCPort::PerformanceCounter();
        MCFrameLog::NextFrame();

        // The PeekMessage / TranslateMessage / DispatchMessage loop, which stopped at WM_QUIT.
        if (MCFrameLog::Scope pump("pump"); !MCInput::PumpMessages())
        {
            quit = true;
        }

        if (ApplicationActive)
        {
            if (SmackerWindow == nullptr)
            {
                MCFrameLog::Scope logic("logic");
                RunFrameCallbacks();
            }

            int32_t staticNoise = 0;
            int32_t noiseChance = 0;

            if (Scenario() != nullptr)
            {
                staticNoise = Scenario()->StartingUp;
                noiseChance = Scenario()->StartUpCountdown;
            }

            UpdateDisplay(TakeScreenShot, staticNoise, noiseChance, false, 0);
            TakeScreenShot = false;
        }
        else if (!quit)
        {
            MCInput::WaitMessage(-1);
        }
        else
        {
            // Quitting while inactive: close the movie and the feature screen.
            CloseMovie();
            FeatureScreen.reset();
        }

        PerfStopTime = MCPort::PerformanceCounter();

        // The frame's length. The counters are split in 32-bit halves and the low halves' difference taken as a
        // signed 32-bit number, so a carry into the high half counts 2^32 too many (OB-062): that frame reads as
        // longer than 0.25 s and is clamped.
        const int32_t highDifference = static_cast<int32_t>(static_cast<uint64_t>(PerfStopTime) >> 32) -
                                       static_cast<int32_t>(static_cast<uint64_t>(PerfStartTime) >> 32);
        const int32_t lowDifference =
            static_cast<int32_t>(static_cast<uint32_t>(PerfStopTime) - static_cast<uint32_t>(PerfStartTime));
        PrevStart = PerfStartTime;
        double elapsed = static_cast<double>(highDifference) * 4294967296.0 + static_cast<double>(lowDifference);

        if (elapsed == 0.0)
        {
            elapsed = 9.999999747378752e-05;
        }

        const double countsLow = static_cast<double>(static_cast<uint32_t>(CountsPerSecond));
        FrameLength = static_cast<float>(elapsed / countsLow);
        FrameRate = static_cast<float>(
            (static_cast<double>(static_cast<int32_t>(static_cast<uint64_t>(CountsPerSecond) >> 32)) * 4294967296.0 +
             countsLow) /
            elapsed);

        if (FrameLength > 0.25f)
        {
            FrameLength = 0.25f;
        }

        if (FrameRate < 4.0f)
        {
            FrameRate = 4.0f;
        }

        if (LockFrameRate)
        {
            const int32_t wait = static_cast<int32_t>(67.0f - FrameLength * 1000.0f);

            if (wait > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                FrameLength = 0.06666667f;
                FrameRate = 15.0f;
            }
        }
    } while (!quit);
}

auto MCGuiSystem::AddCallback(std::weak_ptr<MCGuiCallback> callback) -> int32_t
{
    if (callback.expired())
    {
        return 2;
    }

    _Callbacks.push_back(std::move(callback));
    return 0;
}

auto MCGuiSystem::RemoveCallback(const MCGuiCallback& callback) -> int32_t
{
    const auto found = std::ranges::find_if(_Callbacks, [&](const std::weak_ptr<MCGuiCallback>& listed)
                                            { return listed.lock().get() == &callback; });

    if (found == _Callbacks.end())
    {
        return 1;
    }

    _Callbacks.erase(found);
    return 0;
}

auto MCGuiSystem::SetModalObject(MCGuiObject& obj) -> void
{
    Modal = obj.weak_from_this();
    obj.BringToFront(false);
}

auto MCGuiSystem::ClearModal() -> void
{
    Modal.reset();
}

auto MCGuiSystem::Grab(std::weak_ptr<MCGuiObject> obj) -> void
{
    Grabbed = std::move(obj);
    MCInput::SetCapture();
}

auto MCGuiSystem::SetText(std::weak_ptr<MCGuiObject> obj) -> void
{
    if (!TextFocus.expired())
    {
        ReleaseText();
    }

    TextFocus = obj;
    MCInput::SetTextInput(!TextFocus.expired());

    if (const std::shared_ptr<MCGuiObject> holder = obj.lock(); holder != nullptr)
    {
        MCGuiEvent event;
        event.Type = MCGuiEventType::Focus;
        event.Target = holder;
        event.Data = 7;
        holder->HandleEvent(event);
    }
}

auto MCGuiSystem::SetCurrentObject(std::weak_ptr<MCGuiObject> obj) -> void
{
    Current = std::move(obj);
}

auto MCGuiSystem::Release() -> void
{
    Grabbed.reset();
    MCInput::ReleaseCapture();
}

auto MCGuiSystem::ReleaseText() -> void
{
    if (const std::shared_ptr<MCGuiObject> holder = TextFocus.lock(); holder != nullptr)
    {
        MCGuiEvent event;
        event.Type = MCGuiEventType::Focus;
        event.Data = 8;
        event.Target = holder;
        holder->HandleEvent(event);
    }

    TextFocus.reset();
    MCInput::SetTextInput(false);
}

auto MCGuiSystem::LetGo(MCGuiObject& obj) -> void
{
    if (MCRefersTo(Grabbed, obj))
    {
        Release();
    }

    if (MCRefersTo(TextFocus, obj))
    {
        // The holder hears that it lost the focus, as it did when it destroyed itself (its last share may be gone).
        MCGuiEvent event;
        event.Type = MCGuiEventType::Focus;
        event.Data = 8;
        event.Target = TextFocus;
        obj.HandleEvent(event);
        TextFocus.reset();
        MCInput::SetTextInput(false);
    }

    if (MCRefersTo(Modal, obj))
    {
        ClearModal();
    }

    if (MCRefersTo(Current, obj))
    {
        const std::shared_ptr<MCGuiObject> screen = ScreenWindow();
        const MCPoint cursor = MCInput::GetCursorPos();
        SetCurrentObject(screen != nullptr && screen.get() != &obj ? screen->FindObject(cursor.x, cursor.y) : nullptr);
    }
}

auto MCGuiSystem::AddTimer(std::weak_ptr<MCGuiObject> target, int16_t id, int32_t interval, int32_t eventType,
                           int32_t eventData, bool useScenarioTime) const -> int32_t
{
    MCMouseThreadLock lock;
    return TimerManager->AddTimer(std::move(target), id, static_cast<uint32_t>(interval), eventType, eventData,
                                  useScenarioTime);
}

auto MCGuiSystem::AddUniqueTimer(std::weak_ptr<MCGuiObject> target, int16_t id, int32_t interval, int32_t eventType,
                                 int32_t eventData, bool useScenarioTime) const -> int32_t
{
    MCMouseThreadLock lock;
    return TimerManager->AddUniqueTimer(std::move(target), id, static_cast<uint32_t>(interval), eventType, eventData,
                                        useScenarioTime);
}

auto MCGuiSystem::RemoveTimer(const MCGuiObject& target, int16_t id) const -> void
{
    if (TimerManager != nullptr)
    {
        MCMouseThreadLock lock;
        TimerManager->RemoveTimer(target, id);
    }
}

auto MCGuiSystem::RemoveTimers(const MCGuiObject& target) const -> void
{
    if (TimerManager != nullptr)
    {
        MCMouseThreadLock lock;
        TimerManager->RemoveTimers(target);
    }
}

auto MCGuiSystem::SetCurrentCursor(MCCursorType cursor) -> void
{
    if (CursorHidden)
    {
        return;
    }

    CurrentCursor = cursor;
    CursorShape = static_cast<int32_t>(cursor);

    // 0xf..0x11 are offset by the interface's cursor set; 0x12 is shape 1.
    switch (static_cast<int32_t>(cursor))
    {
        case 0xf:
        {
            if (TacticalInterface() != nullptr)
            {
                CursorShape = TacticalInterface()->CursorOffset + 0xf;
            }
            break;
        }
        case 0x10:
        {
            if (TacticalInterface() != nullptr)
            {
                CursorShape = TacticalInterface()->CursorOffset + 0x2f;
            }
            break;
        }
        case 0x11:
        {
            if (TacticalInterface() != nullptr)
            {
                CursorShape = TacticalInterface()->CursorOffset + 0x4f;
            }
            break;
        }
        case 0x12:
            CursorShape = 1;
            break;
    }
}

auto MCGuiSystem::SetCursorVisible(bool show) -> void
{
    if (show)
    {
        CursorHidden = false;
        SetCurrentCursor(static_cast<MCCursorType>(0));
        return;
    }

    CursorShape = -1;
    CursorHidden = true;
}

auto MCGuiSystem::Retire(MCGuiOwned<MCGuiObject> object) -> void
{
    if (object == nullptr)
    {
        return;
    }

    if (const std::shared_ptr<MCGuiObject> parent = object->ParentObject(); parent != nullptr)
    {
        parent->RemoveChild(*object);
    }

    object->ShowGuiWindow(false);
    _Retired.push_back(std::move(object));
}

// The screen as a whole.

auto ARedrawScreen() -> void
{
    const std::shared_ptr<MCGuiObject> screen = ScreenWindow();

    for (int32_t i = 0; i < screen->NumberOfChildren(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = screen->Child(i); child != nullptr)
        {
            child->Draw();
        }
    }
}

namespace
{
    /// <summary>Set while the screen port shows the display's buffer (ALockScreen).</summary>
    bool ScreenLocked = false;
}

auto ALockScreen() -> int32_t
{
    if (!ScreenLocked)
    {
        ScreenLocked = true;
        // The original locked the DirectDraw back surface here in 16-bit full screen and drew straight into it; the
        // port's screen is always 8-bit, so the screen port always shows the display's buffer.
        const std::shared_ptr<MCGuiPort> port = ScreenPort();
        port->Resize(GWidth, GHeight);
        const std::shared_ptr<MCDisplay> display = GuiSystem().Display();
        port->Bitmap().Pixels = display != nullptr ? display->Screen().Pixels : nullptr;
    }

    return 0;
}

auto AUnlockScreen() -> int32_t
{
    // Unlocking the 16-bit full-screen surface is gone with it (see ALockScreen).
    ScreenLocked = false;
    return 0;
}

auto APostMessage(MCGuiObject& obj, int32_t message) -> void
{
    MCGuiEvent event;
    event.Type = message;
    obj.HandleEvent(event);
}

auto APostMessage(std::weak_ptr<MCGuiObject> obj, int32_t message) -> void
{
    if (const std::shared_ptr<MCGuiObject> target = obj.lock(); target != nullptr)
    {
        APostMessage(*target, message);
    }
}

auto DestroyVersion() -> void
{
    // The box goes from its own OK button's click: taken off the screen now, deleted after the click (OB-158).
    if (HasGuiSystem() && GuiSystem().VersionDialog != nullptr)
    {
        GuiSystem().Retire(std::move(GuiSystem().VersionDialog));
    }
}
