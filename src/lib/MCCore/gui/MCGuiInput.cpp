#include "stdafx.h"
#include "gui/MCGuiInput.h"
#include "camera/MCCamera.h"
#include "camera/MCMainWindow.h"
#include "camera/MCViewWindow.h"
#include "gui/MCGuiConsole.h"
#include "gui/MCGuiMessageBox.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCVector2D.h"
#include "main/MCMissionGlobals.h"
#include "main/MCGameStrings.h"
#include "main/MCErrorReport.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "mission/MCScenario.h"
#include "logistics/MCConnectMenu.h"
#include "network/MCMultiPlayer.h"
#include "object/MCTeam.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "terrain/MCTerrain.h"
#include "platform/MCInput.h"
#include "platform/MCWin32Defs.h"

namespace
{
    /// <summary>A cheat code: its letters plus 0x32 (as stored in MCX.EXE).</summary>
    struct MCCheatCode
    {
        std::string_view Letters;
    };

    // The cheat codes. The can't-hit-me code is six letters but its length byte said five, so only the first five
    // count.
    constexpr MCCheatCode CheatFramegraph{"\x98\xa4\x93\x9f\x97\x99\xa4\x93\xa2\x9a"};
    constexpr MCCheatCode CheatBunnyStrike{"\x9e\xa1\xa4\x96\x94\xa7\xa0\xa0\xab"};
    constexpr MCCheatCode CheatHealAll{"\x9e\xa1\xa4\xa4\x9b\x97"};
    constexpr MCCheatCode CheatDeadEye{"\x96\x97\x93\x96\x97\xab\x97"};
    constexpr MCCheatCode CheatCantHitMe{"\xa1\xa5\x9f\x9b\xa7"};
    constexpr MCCheatCode CheatGetSalvage{"\x99\x9e\x97\xa0\xa0\xa4\xa1\x95\x9d\xa5\xa6\x9a\x97\x9a\xa1\xa7\xa5\x97"};
    constexpr MCCheatCode CheatReveal{"\x9f\x9b\xa0\x97\x97\xab\x97\xa5\x9a\x93\xa8\x97\xa5\x97\x97\xa0\xa6\x9a\x97\x99"
                                      "\x9e\xa1\xa4\xab"};
    constexpr MCCheatCode CheatDuh{"\x96\xa7\x9a"};

    /// <summary>The ring of the last keys typed (the size is the ring's mask, 0x7f, plus one).</summary>
    std::array<char, 128> CheatKey = {};
    size_t CheatPointer = 0;

    /// <summary>Whether the last keys typed spell <paramref name="code"/> (case blind).</summary>
    bool Cheat(const MCCheatCode& code)
    {
        size_t pos = CheatPointer - code.Letters.size();

        for (const char letter : code.Letters)
        {
            const char typed = CheatKey[pos & 0x7f];
            pos = (pos & 0x7f) + 1;
            const int wanted = std::tolower(static_cast<uint8_t>(letter - 0x32));

            if (static_cast<char>(std::tolower(static_cast<uint8_t>(typed))) != static_cast<char>(wanted))
            {
                return false;
            }
        }

        // MessageBeep(0), the original's confirmation, isn't played.
        return true;
    }

    /// <summary>Whether a key is held (GetAsyncKeyState's top bit).</summary>
    bool KeyHeld(int vk)
    {
        return (MCInput::GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    /// <summary>Whether Ctrl and Alt are held (the debug keys).</summary>
    bool CtrlAltHeld()
    {
        return KeyHeld(VK_CONTROL) && KeyHeld(VK_MENU);
    }

    /// <summary>Whether a cheat key may work: in a single-player scenario with cheats on.</summary>
    bool SinglePlayerCheats()
    {
        return Scenario() != nullptr && CheatsOn && MultiPlayer() == nullptr;
    }

    /// <summary>Whether the mouse buttons were down when <see cref="CheckMouse"/> last looked.</summary>
    bool LeftMouseButtonDown = false;
    bool RightMouseButtonDown = false;

    /// <summary>Alt+Enter switches between full screen and a window (always on in MCX.EXE).</summary>
    constexpr bool AllowMagicWindowSwitching = true;

    /// <summary>Whether <paramref name="target"/> is the modal object or one of its children (or there is none).</summary>
    bool UnderModal(std::shared_ptr<MCGuiObject> target)
    {
        std::shared_ptr<MCGuiObject> modal = GuiSystem().ModalObject();

        if (GuiSystem().GrabbedObject() != nullptr || modal == nullptr)
        {
            return true;
        }

        std::shared_ptr<MCGuiObject> owner = std::move(target);

        while (owner != nullptr && owner != modal)
        {
            owner = owner->ParentObject();
        }

        return owner == modal;
    }

    /// <summary>The global keys handled before a key-down event goes on (pause, escape, the debug and cheat keys).</summary>
    /// <returns>False when the event stops here.</returns>
    bool HandleGlobalKey(MCGuiEvent& event)
    {
        MCGuiSystem& gui = GuiSystem();

        if (FeatureScreen != nullptr)
        {
            FeatureScreenDone = -1;
        }

        const uint8_t key = event.Key;

        switch (key)
        {
            case VK_RETURN:
            {
                if (Scenario() != nullptr && (GamePaused || GameAsked) && event.CtrlKey == 0 && event.AltKey == 0)
                {
                    Mission()->EndScenarioRequested = -1;

                    if (GameAsked)
                    {
                        ScenarioResult = 3;
                    }

                    GamePaused = false;
                    GameAsked = false;
                }
                else if (event.CtrlKey == 0 && event.AltKey == 0 && ConsoleUsable())
                {
                    // Port: the debug console (-debug).
                    OpenConsole();
                }
                break;
            }
            default:
                KeySetting = static_cast<char>(key);
                break;
            case VK_PAUSE:
            {
                if (Scenario() != nullptr && MultiPlayer() == nullptr && Turn > 0)
                {
                    GamePaused = !GamePaused;
                }

                if (event.AltKey != 0 && AssertTest(0x80, "User Break"))
                {
                    SDL_TriggerBreakpoint();
                    return false;
                }
                break;
            }
            case VK_ESCAPE:
            {
                if (Scenario() != nullptr && EventsToMissionResultsScreen == 0 && Scenario()->StartingUp == 0 &&
                    Scenario()->StartUpTurns < Turn)
                {
                    if (MultiPlayer() == nullptr)
                    {
                        GamePaused = !GamePaused;
                    }
                    else
                    {
                        GameAsked = !GameAsked;
                    }
                }
                break;
            }
            case 'D':
            {
                if (SinglePlayerCheats() && CtrlAltHeld())
                {
                    DisableHomeTeamTargets();
                }
                break;
            }
            case 'G':
            {
                if (Scenario() == nullptr)
                {
                    break;
                }

                if (SinglePlayerCheats() && CtrlAltHeld())
                {
                    ForceGatesClosed = true;
                }

                // Original bug (OB-063): no break, so the gate cheat also runs the kill cheat below.
                [[fallthrough]];
            }
            case 'K':
            {
                if (SinglePlayerCheats() && CtrlAltHeld())
                {
                    KillHomeTeamTargets();
                }
                break;
            }
            case 'L':
            {
                if (Scenario() != nullptr && CheatsOn && KeyHeld(VK_CONTROL))
                {
                    DrawTerrainGrid = !DrawTerrainGrid;
                }
                break;
            }
            case 'P':
            {
                if (CtrlAltHeld())
                {
                    DisplayProfileData = (DisplayProfileData + 1) % 3;
                }
                break;
            }
            case 'Q':
            {
                if (SinglePlayerCheats() && event.CtrlKey != 0 && event.AltKey != 0)
                {
                    Mission()->EndScenarioRequested = -1;
                }
                break;
            }
            case 'S':
            {
                if (CtrlAltHeld())
                {
                    LockFrameRate = !LockFrameRate;
                }
                break;
            }
            case 'V':
            {
                if (CheatsOn && CtrlAltHeld())
                {
                    const std::string release = LoadGameString(0x282, 0xfe);
                    DestroyVersion();
                    gui.VersionDialog = MCMakeGui<MCGuiMessageBox>();
                    gui.VersionDialog->Init(std::format("Release Version: {}", release));
                    ScreenWindow()->AddChild(gui.VersionDialog);
                    gui.Grab(gui.VersionDialog);
                }
                break;
            }
            case 'W':
            {
                if (SinglePlayerCheats() && event.CtrlKey != 0 && event.AltKey != 0)
                {
                    Scenario()->StartingUp = false;
                    Mission()->EndScenarioRequested = -1;
                    ScenarioResult = 5;
                    Scenario()->StartUpCountdown = 0;
                }
                break;
            }
            case 'Z':
            {
                if (KeyHeld(VK_CONTROL) && gui.SmackerWindow == nullptr)
                {
                    gui.GammaCorrectCurrentPalette();
                }
                break;
            }
        }

        return true;
    }

    /// <summary>The cheat codes typed (with cheats on): each toggles its cheat with Betty's confirmation.</summary>
    void CheckCheatCodes(char typed)
    {
        CheatKey[CheatPointer] = typed;
        CheatPointer = (CheatPointer + 1) & 0x7f;

        if (Cheat(CheatFramegraph))
        {
            AndyFramerate = !AndyFramerate;
        }

        if (Scenario() == nullptr || Turn <= 0 || MultiPlayer() != nullptr)
        {
            return;
        }

        if (Cheat(CheatHealAll))
        {
            SoundSystem()->PlayBettySample(0x1c);
            TacticalInterface()->CheatHealAll();
        }

        if (Cheat(CheatDeadEye))
        {
            SoundSystem()->PlayBettySample(0x1c);
            TacticalInterface()->CheatDeadEye();
        }

        if (Cheat(CheatCantHitMe))
        {
            SoundSystem()->PlayBettySample(0x1c);
            CantHitMe = !CantHitMe;
        }

        if (Cheat(CheatGetSalvage))
        {
            SoundSystem()->PlayBettySample(0x1c);
            CantBlowSalvage = !CantBlowSalvage;
        }

        if (Cheat(CheatReveal))
        {
            SoundSystem()->PlayBettySample(0x1c);
            RevealAll();
        }

        if (Cheat(CheatBunnyStrike))
        {
            SoundSystem()->PlayBettySample(0x16);
            BunnyStrikesOn = !BunnyStrikesOn;
        }

        if (Cheat(CheatDuh))
        {
            SoundSystem()->PlayBettySample(0x1c);
            Duh = !Duh;
        }
    }

    /// <summary>
    /// The mouse wheel. The original ignored it. Over the battlefield (the main pane itself, not the interface drawn
    /// over it) it zooms like the zoom keys: up in, down out. Over anything else it scrolls the first of the object and
    /// its parents that has a scroll bar (<see cref="MCGuiObject::MouseWheel"/>). While an object holds the mouse, only
    /// that object is offered it (an open drop-down list; a dragged thumb doesn't take it).
    /// </summary>
    void MouseWheel(int16_t delta, tagPOINT cursor)
    {
        MCGuiSystem& gui = GuiSystem();

        if (ScreenWindow() == nullptr)
        {
            return;
        }

        std::shared_ptr<MCGuiObject> grabbed = gui.GrabbedObject();
        std::shared_ptr<MCGuiObject> target;

        if (grabbed != nullptr)
        {
            target = grabbed;
        }
        else if (EventsToMissionResultsScreen != 0 && Mission() != nullptr && Mission()->ResultsScreen != nullptr)
        {
            target = Mission()->ResultsScreen->FindObject(cursor.x, cursor.y);
        }
        else
        {
            target = ScreenWindow()->FindObject(cursor.x, cursor.y);
        }

        if (target == nullptr)
        {
            return;
        }

        if (grabbed == nullptr && TacticalInterface() != nullptr && Scenario() != nullptr && Turn > 0 &&
            EventsToMissionResultsScreen == 0 && MainHolder() != nullptr && target == MainHolder()->GetActivePane())
        {
            // A step per notch (finer wheels zoom finer); not while paused or asked.
            const float step = std::pow(MCTacticalInterface::ZoomWheelStep, std::fabs(delta / 120.0f));

            if (delta > 0)
            {
                MCTacticalInterface::ZoomIn(step, false);
            }
            else if (delta < 0)
            {
                MCTacticalInterface::ZoomOut(step, false);
            }

            return;
        }

        // A modal object only takes the wheel for itself and its children, as for other events.
        if (!UnderModal(target))
        {
            return;
        }

        // A notch is 120; finer wheels and touchpads send less, so the remainder carries over. Wheel up scrolls up
        // (negative steps).
        static int32_t wheelRemainder = 0;

        if ((wheelRemainder > 0 && delta < 0) || (wheelRemainder < 0 && delta > 0))
        {
            wheelRemainder = 0;
        }

        wheelRemainder += delta;
        const int32_t steps = -(wheelRemainder / 120);
        wheelRemainder %= 120;

        if (steps == 0)
        {
            return;
        }

        for (std::shared_ptr<MCGuiObject> object = target; object != nullptr;
             object = grabbed != nullptr ? nullptr : object->ParentObject())
        {
            if (object->MouseWheel(steps, cursor.x, cursor.y))
            {
                break;
            }
        }
    }

    /// <summary>OB-184: the part of a whole step the last frame's scroll had left over, carried into the next.</summary>
    float ScrollCarry = 0.0f;

    /// <summary>Port: SCROLL SPEED (<see cref="GScrollSpeed"/>) as a factor of the original's speed.</summary>
    float ScrollSpeedFactor()
    {
        return static_cast<float>(GScrollSpeed) / static_cast<float>(OriginalScrollSpeed);
    }
}

auto DispatchGuiEvent(MCGuiEvent& event) -> void
{
    MCGuiSystem& gui = GuiSystem();

    if (ScreenWindow() == nullptr)
    {
        return;
    }

    const int32_t type = event.Type;

    if (type == MCGuiEventType::Timer || (type >= MCGuiEventType::FirstPosted && type < 0x2401))
    {
        // Timer events and posted messages go to the tactical interface.
        if (Scenario() == nullptr || Turn < 1)
        {
            return;
        }

        TacticalInterface()->HandleEvent(event);
        return;
    }

    std::shared_ptr<MCGuiObject> target;
    const bool keyEvent =
        type == MCGuiEventType::Character || type == MCGuiEventType::KeyDown || type == MCGuiEventType::KeyUp;

    if (gui.TextObject() != nullptr && keyEvent)
    {
        target = gui.TextObject();
    }
    else if (gui.GrabbedObject() != nullptr)
    {
        target = gui.GrabbedObject();
    }
    else if (EventsToMissionResultsScreen != 0 && Mission()->ResultsScreen != nullptr)
    {
        const std::shared_ptr<MCGuiObject> results = Mission()->ResultsScreen;
        target = type == MCGuiEventType::Character && event.Key == VK_ESCAPE ? results
                                                                             : results->FindObject(event.X, event.Y);
    }
    else
    {
        if (type == MCGuiEventType::KeyDown && !HandleGlobalKey(event))
        {
            return;
        }

        target = ScreenWindow()->FindObject(event.X, event.Y);
    }

    if ((event.Type == MCGuiEventType::KeyUp || event.Type == MCGuiEventType::KeyDown) && gui.TextObject() == nullptr &&
        TacticalInterface() != nullptr && EventsToMissionResultsScreen == 0 && Scenario() != nullptr && Turn > 0)
    {
        TacticalInterface()->HandleEvent(event);
    }

    if (!UnderModal(target))
    {
        return;
    }

    if (event.Type == MCGuiEventType::LeftButtonDown || event.Type == MCGuiEventType::RightButtonDown)
    {
        if (gui.TextObject() != nullptr && target != gui.TextObject())
        {
            gui.ReleaseText();
        }
    }
    else if (event.Type == MCGuiEventType::MouseMove && gui.GrabbedObject() == nullptr && target != gui.CurrentObject())
    {
        if (const std::shared_ptr<MCGuiObject> current = gui.CurrentObject(); current != nullptr)
        {
            current->Leave();
        }

        if (target != nullptr)
        {
            target->Enter();
        }

        gui.SetCurrentObject(target);
    }

    if (target == nullptr)
    {
        return;
    }

    event.Target = target;
    target->HandleEvent(event);
}

auto TranslateGuiMessage(uint32_t message, uint32_t wParam, int32_t lParam) -> int32_t
{
    if (!ApplicationActive)
    {
        return 0;
    }

    const tagPOINT cursor = GetMessageCursorLoc();
    MCGuiEvent event;
    const uint8_t low = static_cast<uint8_t>(wParam);
    const int16_t scanCode = static_cast<int16_t>((lParam >> 16) & 0x1ff);
    const auto modifier = [](int vk) -> uint8_t { return (MCInput::GetKeyState(vk) & 0x8000) != 0 ? 0xff : 0; };

    switch (message)
    {
        case WM_PAINT:
            event.Type = MCGuiEventType::Paint;
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            // Only the first press, not the repeats.
            if ((lParam & 0xffff) == 1)
            {
                event.Type = MCGuiEventType::KeyDown;
                event.Key = low;
                event.ScanCode = scanCode;
                event.CtrlKey = modifier(VK_CONTROL);
                event.ShiftKey = modifier(VK_SHIFT);
            }
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            event.Type = MCGuiEventType::KeyUp;
            event.Key = low;
            event.ScanCode = scanCode;
            event.CtrlKey = modifier(VK_CONTROL);
            event.ShiftKey = modifier(VK_SHIFT);
            break;
        }
        case WM_CHAR:
        {
            if (CheatsOn)
            {
                CheckCheatCodes(static_cast<char>(low));
            }

            event.Type = MCGuiEventType::Character;
            event.Key = low;
            event.ScanCode = scanCode;
            // The character's modifiers are read from wParam's MK_ bits, as for a mouse message.
            event.CtrlKey = low & MK_CONTROL;
            event.ShiftKey = low & MK_SHIFT;
            break;
        }
        case WM_TIMER:
            event.Type = MCGuiEventType::Timer;
            break;
        case WM_LBUTTONDBLCLK:
        {
            event.Type = MCGuiEventType::LeftDoubleClick;
            event.CtrlKey = low & MK_CONTROL;
            event.ShiftKey = low & MK_SHIFT;
            break;
        }
        case WM_RBUTTONDBLCLK:
        {
            event.Type = MCGuiEventType::RightDoubleClick;
            event.CtrlKey = low & MK_CONTROL;
            event.ShiftKey = low & MK_SHIFT;
            break;
        }
        case WM_MOUSEWHEEL:
        {
            MouseWheel(static_cast<int16_t>(wParam >> 16), cursor);
            return 1;
        }
    }

    // Mouse moves and presses come from CheckMouse, not from here. Messages from WM_USER + 0x1000 up are posted game
    // messages.
    if (message >= static_cast<uint32_t>(MCGuiEventType::FirstPosted))
    {
        event.Type = static_cast<int32_t>(message);
    }

    event.LParam = lParam;
    event.X = cursor.x;
    event.Data = static_cast<int32_t>(wParam);
    event.LeftButton = low & MK_LBUTTON;
    event.MiddleButton = low & MK_MBUTTON;
    event.RightButton = low & MK_RBUTTON;
    event.Y = cursor.y;
    event.AltKey = modifier(VK_MENU);

    if (event.Type != 0)
    {
        DispatchGuiEvent(event);
    }

    return 0;
}

auto ScrollScreen() -> void
{
    std::shared_ptr<MCCamera> camera;
    const tagRECT scrollArea = GuiSystem().ScrollRect;

    if (TacticalInterface() == nullptr)
    {
        return;
    }

    if (Scenario() != nullptr && Turn < 5)
    {
        return;
    }

    int16_t speed = TacticalInterface()->ScrollSpeed;

    if (MainHolder() != nullptr && MainHolder()->GetActivePane() != nullptr)
    {
        camera = MainHolder()->GetActivePane()->GetCamera();
    }

    float dx = 0.0f;
    float dy = 0.0f;

    if (camera != nullptr)
    {
        // At full size the original scrolls at half the speed (zoomed out, at the whole speed).
        speed = static_cast<int16_t>(speed / 2);

        // MCX.EXE's constant is a hair under 15 (14.999999); it scrolled whole steps. OB-184 (fixed): it dropped the rest
        // of each frame's step, so the more frames a second, the slower it scrolled (at iface.fit's speed, a fifth slower
        // at 144 frames a second, and not at all past 360); the rest now carries into the next frame's step. Port: SCROLL
        // SPEED multiplies it.
        const float pace = FrameLength * 0x1.dffffep+3f * static_cast<float>(speed) * ScrollSpeedFactor() + ScrollCarry;
        float step = std::trunc(pace);
        const float carry = pace - step;

        // Port: the same speed on the screen at any zoom (the world surface's pixels per screen pixel). The step
        // stays fractional: zoomed in, a whole step would round to nothing.
        if (camera->View() != nullptr && camera->View()->WorldScaleY() > 0.0f)
        {
            step /= camera->View()->WorldScaleY();
        }

        // The direction first (dx and dy each -1, 0 or 1), then the step.
        constexpr float unit = 1.0f;
        bool scroll = true;

        if (TacticalInterface()->ScrollDirection == -1)
        {
            // Scroll by the mouse at the screen's edge, after the interface's start delay.
            const MCPoint cursor = MCInput::GetCursorPos();

            if (PtInRect(scrollArea, POINT{cursor.x, cursor.y}) == 0)
            {
                if (ScrollWait == 0)
                {
                    ScrollWait = MCPort::Milliseconds();
                }
                else
                {
                    const int16_t delay = TacticalInterface()->ScrollStart;

                    if (static_cast<uint32_t>(delay + static_cast<int32_t>(ScrollWait)) > MCPort::Milliseconds())
                    {
                        scroll = false;
                    }
                }

                if (scroll)
                {
                    if (cursor.x < scrollArea.left)
                    {
                        dx = -unit;
                    }
                    else if (cursor.x > scrollArea.right)
                    {
                        dx = unit;
                    }

                    if (cursor.y < scrollArea.top)
                    {
                        dy = -unit;
                    }
                    else if (cursor.y > scrollArea.bottom)
                    {
                        dy = unit;
                    }
                }
            }
            else
            {
                ScrollWait = 0;
                scroll = false;
            }
        }
        else
        {
            // The eight directions, clockwise from up.
            switch (TacticalInterface()->ScrollDirection)
            {
                case 0:
                    dy = -unit;
                    break;
                case 1:
                {
                    dx = unit;
                    dy = -unit;
                    break;
                }
                case 2:
                    dx = unit;
                    break;
                case 3:
                {
                    dx = unit;
                    dy = dx;
                    break;
                }
                case 4:
                    dy = unit;
                    break;
                case 5:
                {
                    dx = -unit;
                    dy = unit;
                    break;
                }
                case 6:
                    dx = -unit;
                    break;
                case 7:
                {
                    dx = -unit;
                    dy = dx;
                    break;
                }
                default:
                    scroll = false;
                    break;
            }
        }

        // OB-184: the carry stays while the map scrolls, even over frames too short for a whole step.
        ScrollCarry = scroll && (dx != 0.0f || dy != 0.0f) ? carry : 0.0f;
        dx *= step;
        dy *= step;

        if (scroll && (dx != 0.0f || dy != 0.0f))
        {
            // Keep the window's anchor point (selectionBox's first corner) on the same spot of the world.
            // Port: the box is in the view's own coordinates, the projection on its world surface (through the zoom).
            const std::shared_ptr<MCViewWindow> window = camera->View();
            MCVector2D anchor(window->SelectionBox[0] / window->WorldScaleX(),
                              window->SelectionBox[1] / window->WorldScaleY());
            MCVector3D point;
            camera->InverseProject(anchor, point);
            camera->ScrollCamera(dx, dy);
            const float offsetX = point.X - camera->Position.X;
            const float offsetY = point.Y - camera->Position.Y;
            const float offsetZ = point.Z - camera->Position.Z;
            const MCVector2D moved(offsetY * camera->CosAngle + offsetX * camera->CosAngle + camera->HalfWidth,
                                   ((offsetX * camera->SinAngle + camera->HalfHeight) - offsetY * camera->SinAngle) -
                                       offsetZ);
            const MCVector2D shown = window->WorldToWindow(moved);
            window->SelectionBox[0] = shown.X;
            window->SelectionBox[1] = shown.Y;
        }
    }

    // The tactical map scrolls by its buttons.
    if (TacticalMap() == nullptr)
    {
        return;
    }

    const int16_t mapSpeed = TacticalInterface()->TacScrollSpeed;
    int32_t mapDx = 0;
    int32_t mapDy = 0;

    switch (TacticalInterface()->TacScrollDirection)
    {
        case 0:
            mapDy = -mapSpeed;
            break;
        case 1:
        {
            mapDx = mapSpeed;
            mapDy = -mapDx;
            break;
        }
        case 2:
            mapDx = mapSpeed;
            break;
        case 3:
        {
            mapDx = mapSpeed;
            mapDy = mapDx;
            break;
        }
        case 4:
            mapDy = mapSpeed;
            break;
        case 5:
        {
            mapDy = mapSpeed;
            mapDx = -mapDy;
            break;
        }
        case 6:
            mapDx = -mapSpeed;
            break;
        case 7:
        {
            mapDx = -mapSpeed;
            mapDy = mapDx;
            break;
        }
        default:
            return;
    }

    if (mapDx == 0 && mapDy == 0)
    {
        return;
    }

    TacticalMap()->ScrollMap(mapDx, mapDy);
}

auto WindowProc(uint32_t message, uint32_t wParam, int32_t lParam) -> int32_t
{
    // The GDI palette work (SelectPalette / RealizePalette on the desktop DC) and the window placement calls have no
    // counterpart: the display owns the palette and the window. What remains is when the original repainted. Every
    // message ends with 0 (the original swallowed the window menu, maximizing and minimizing too).
    constexpr uint32_t wmErasebkgnd = 0x14;
    MCGuiSystem& gui = GuiSystem();

    switch (message)
    {
        case wmErasebkgnd:
        {
            if (gui.DisplayReady())
            {
                return -1;
            }
            break;
        }
        case WM_DESTROY:
            MCInput::PostQuitMessage(0);
            break;
        case WM_SIZE:
        {
            // The original kept the client size and snapped the window back to the screen's size (SetWindowPos); the display letterboxes.
            return 0;
        }
        case WM_PAINT:
        {
            if (gui.DisplayReady() && GFullScreen == 0)
            {
                UpdateDisplay(false, false, 0, false, 0);
            }
            break;
        }
        case WM_QUERYNEWPALETTE:
            return gui.DisplayReady() && GFullScreen == 0 ? -1 : 0;
        case WM_PALETTECHANGED:
            return 0;
        case WM_ACTIVATEAPP:
        {
            gui.CloseMovie();

            if (wParam == 0 && TacticalInterface() != nullptr)
            {
                TacticalInterface()->ReleaseScrollKeys();
            }

            if (MultiPlayer() == nullptr)
            {
                ApplicationActive = wParam != 0;
            }
            else if (GFullScreen != 0 && ApplicationActive)
            {
                InitWindowMode();
            }

            if (ApplicationActive && gui.DisplayReady())
            {
                UpdateDisplay(false, false, 0, false, 0);
            }
            break;
        }
        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE && gui.SmackerWindow != nullptr)
            {
                gui.SmackerWindow->EndSmackerMovie();
                EscapedSmackerMovie = -1;
            }
            break;
        }
    }

    if (TranslateGuiMessage(message, wParam, lParam) != 0)
    {
        return 0;
    }

    // Alt+Enter switches between full screen and a window (not during a movie).
    if (message == WM_SYSKEYDOWN && wParam == VK_RETURN && AllowMagicWindowSwitching && gui.DisplayReady() &&
        gui.SmackerWindow == nullptr)
    {
        if (GFullScreen != 0)
        {
            InitWindowMode();
        }
        else
        {
            InitFullScreen();
        }
    }

    return 0;
}

namespace
{
    /// <summary>Switches the display to <paramref name="fullScreen"/> under the mouse thread's lock.</summary>
    void SwitchDisplay(int fullScreen)
    {
        MCMouseThreadLock lock;
        const bool marked = MouseThreadStarted != 0;

        if (marked)
        {
            InMouseCritSec = 1;
        }

        GFullScreen = fullScreen;
        GuiSystem().ResetDisplay(GWidth, GHeight, 8);

        if (marked)
        {
            InMouseCritSec = 0;
        }
    }
}

auto InitWindowMode() -> void
{
    // The original centred the window on the desktop the first time (or restored its saved placement) and showed it;
    // leaving full screen puts the SDL window back where it was.
    if (GFullScreen != 0)
    {
        SwitchDisplay(0);
    }
}

auto InitFullScreen() -> void
{
    // The original saved the window's placement and made it a popup (WS_POPUP) first.
    if (GFullScreen == 0 && GuiSystem().Display() != nullptr)
    {
        SwitchDisplay(1);
    }
}

auto CheckMouse() -> void
{
    const int16_t alt = MCInput::GetAsyncKeyState(VK_MENU);
    const int16_t ctrl = MCInput::GetAsyncKeyState(VK_CONTROL);
    const int16_t shift = MCInput::GetAsyncKeyState(VK_SHIFT);
    const int16_t left = MCInput::GetAsyncKeyState(VK_LBUTTON);
    const int16_t right = MCInput::GetAsyncKeyState(VK_RBUTTON);
    const auto held = [](int16_t state) -> uint8_t { return (static_cast<uint16_t>(state) >> 15) & 1; };

    // The left button: down when any state bit shows, up when the held bit clears.
    if (!LeftMouseButtonDown ? left != 0 : (left & 0x8000) == 0)
    {
        MCGuiEvent event;
        event.RightButton = held(right);
        event.CtrlKey = held(ctrl);
        event.ShiftKey = held(shift);
        event.AltKey = held(alt);
        LeftMouseButtonDown = !LeftMouseButtonDown;
        event.Type = LeftMouseButtonDown ? MCGuiEventType::LeftButtonDown : MCGuiEventType::LeftButtonUp;
        event.LeftButton = LeftMouseButtonDown ? 0xff : 0;
        event.X = MouseScreenX;
        event.Y = MouseScreenY;
        DispatchGuiEvent(event);
    }

    // The right button likewise.
    if (!RightMouseButtonDown ? right != 0 : (right & 0x8000) == 0)
    {
        MCGuiEvent event;
        event.LeftButton = held(left);
        event.ShiftKey = held(shift);
        RightMouseButtonDown = !RightMouseButtonDown;
        event.Type = RightMouseButtonDown ? MCGuiEventType::RightButtonDown : MCGuiEventType::RightButtonUp;
        event.RightButton = RightMouseButtonDown ? 0xff : 0;
        event.CtrlKey = held(ctrl);
        event.AltKey = held(alt);
        event.X = MouseScreenX;
        event.Y = MouseScreenY;
        DispatchGuiEvent(event);
    }

    if (OldMouseX != MouseScreenX || OldMouseY != MouseScreenY)
    {
        MCGuiEvent event;
        event.LeftButton = held(left);
        event.RightButton = held(right);
        event.Type = MCGuiEventType::MouseMove;
        event.AltKey = held(alt);
        event.CtrlKey = held(ctrl);
        event.ShiftKey = held(shift);
        event.X = MouseScreenX;
        event.Y = MouseScreenY;
        OldMouseX = MouseScreenX;
        OldMouseY = MouseScreenY;
        DispatchGuiEvent(event);
    }
}

auto GetMessageCursorLoc() -> tagPOINT
{
    // The message's position is already on the logical screen in the port (MapWindowPoints in the original).
    const MCPoint position = MCInput::GetMessagePos();
    return tagPOINT{position.x, position.y};
}
