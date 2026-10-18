#include "stdafx.h"
#include "gui/MCUpdateDisplay.h"
#include "engine/MCWriteTga.h"
#include "gameos/MCSoundRenderer.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCHardwareCursor.h"
#include "lib/MCDice.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "mission/MCMissionResultsScreen.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCNeverDestroyed.h"
#include "platform/MCRenderer.h"
#include "vfx/MCVfxFunctions.h"

uint32_t MouseTicks = 0;
int MouseScreenX = 320;
int MouseScreenY = 240;
std::array<float, FrameGraphLength> FrameRateArray = {};
size_t FrPointer = 0;
bool KeepScreenBlack = false;
int32_t ShotNum = 0;
auto ConnectShape() -> MCRegisteredBlock&
{
    static MCNeverDestroyed<MCRegisteredBlock> shape;
    return *shape;
}

namespace
{
    /// <summary>The mouse timer's thread (timeSetEvent's periodic callback).</summary>
    std::jthread MouseTimerThread;
    /// <summary>Whether the mouse timer runs.</summary>
    bool MouseTimerRunning = false;
    /// <summary>Set while the mouse timer's tick runs.</summary>
    std::atomic<bool> InTimerThread = false;
    /// <summary>Counts the mouse timer's ticks, for the sound streams.</summary>
    int32_t SoundTicks = 0;

    /// <summary>The screen's window: the 8-bit buffer the game draws and the display shows.</summary>
    MCWindow& ScreenBuffer()
    {
        return ScreenPort()->Bitmap();
    }

    /// <summary>
    /// Port: the frame counter (<c>GShowFps</c>) in the shown view's top-right corner: frames a second and the mean
    /// frame time over the last half second of real time, on a black box that only grows (so shorter text leaves no
    /// digits behind where nothing else redraws the screen).
    /// </summary>
    void DrawFrameCounter()
    {
        static std::chrono::steady_clock::time_point periodStart = std::chrono::steady_clock::now();
        static int32_t periodFrames = 0;
        static std::string text = "-- fps";
        static int32_t boxWidth = 0;
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        std::shared_ptr<MCGuiFont> font = MedWhiteFont;

        if (display == nullptr || font == nullptr)
        {
            return;
        }

        periodFrames++;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - periodStart).count();

        if (seconds >= 0.5)
        {
            text = std::format("{:.0f} fps  {:.1f} ms", periodFrames / seconds, seconds * 1000.0 / periodFrames);
            periodStart = now;
            periodFrames = 0;
        }

        const int32_t textWidth = font->Width(text);
        boxWidth = std::max(boxWidth, textWidth + 8);
        const SDL_Rect view = display->View();
        const int32_t right = view.x + view.w - 1;
        const int32_t top = view.y;
        MCWindow& screen = ScreenBuffer();
        MCRenderer::For(screen).Clear(screen, MCRect{right - boxWidth + 1, top, right, top + font->Height() + 3}, 0);
        font->WriteString(ScreenPort()->Frame(), right - 3 - textWidth, top + 2, text);
    }

    /// <summary>
    /// Port: picks the part of the screen the display shows. Only a running scenario uses the whole screen; the
    /// menus, logistics and the results screen are laid out for 640x480 and drawn in the top-left corner of a larger
    /// one, so the display shows just that corner, scaled to the window. A full-screen movie is centred on the
    /// screen, so the view is a 640x480 (or the movie's size, if larger) centred on it.
    /// </summary>
    void UpdateDisplayView(MCDisplay& display)
    {
        constexpr int32_t interfaceWidth = 640;
        constexpr int32_t interfaceHeight = 480;
        int32_t x = 0;
        int32_t y = 0;
        int32_t width = display.Width();
        int32_t height = display.Height();
        const std::shared_ptr<MCGuiObject> movie = HasGuiSystem() ? GuiSystem().SmackerWindow : nullptr;

        if (movie != nullptr)
        {
            width = std::max(interfaceWidth, movie->Width());
            height = std::max(interfaceHeight, movie->Height());
            x = movie->GlobalX() + movie->Width() / 2 - width / 2;
            y = movie->GlobalY() + movie->Height() / 2 - height / 2;
        }
        else if (Mission() == nullptr || Mission()->State != MCMissionState::Scenario)
        {
            width = interfaceWidth;
            height = interfaceHeight;
        }

        if (display.SetView(x, y, width, height))
        {
            MCInput::RefreshMouseArea();
        }
    }

    /// <summary>
    /// Shows the screen buffer: the original's flip (DirectDraw Blt/Flip full screen, BitBlt in a window, with the
    /// lost-surface retries and display resets) is the port's present.
    /// </summary>
    void PresentScreen()
    {
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            MCFrameLog::Scope present("present");
            UpdateDisplayView(*display);
            static_cast<void>(display->Present());
        }
    }

    /// <summary>Where the cursor is on the screen, kept inside it.</summary>
    void ReadCursorPosition()
    {
        const MCPoint cursor = MCInput::GetCursorPos();
        MouseScreenX = std::min(cursor.x, GWidth - 1);
        MouseScreenY = std::min(cursor.y, GHeight - 1);
    }

    /// <summary>Draws the software cursor into the screen buffer, or moves the system's.</summary>
    void DrawCursorInBuffer()
    {
        // Port fix: the cursor is the system's, which follows the mouse between frames; the original drew it here.
        if (GSoftwareCursor == 0)
        {
            MCFrameLog::Scope cursor("cursor");
            MCHardwareCursorUpdate();
            return;
        }

        if (GuiSystem().CursorShape == -1)
        {
            return;
        }

        // (The original saved the screen under the cursor first, for a page-flipping display's timer to erase it.)
        AGShapeDraw(ScreenPort()->Frame(), GuiSystem().CursorShapeData(GuiSystem().CursorShape), 0, MouseScreenX,
                    MouseScreenY);
    }

    /// <summary>Turns rows of the screen to static, each with 1-in-<paramref name="noiseChance"/> odds.</summary>
    void DrawStatic(int32_t noiseChance)
    {
        // Port: the original wrote the noise into the screen's memory; the row goes to the renderer.
        const int32_t pairs = GuiSystem().Width() >> 1;
        std::vector<uint8_t> line(static_cast<size_t>(pairs) * 2);

        for (int32_t row = 0; row < GuiSystem().Height(); row++)
        {
            if (RollDice(noiseChance) == 0)
            {
                continue;
            }

            for (int32_t i = 0; i < pairs; i++)
            {
                const int32_t noise = MCPort::Rand();
                line[static_cast<size_t>(i) * 2] = static_cast<uint8_t>(noise & 0x1f);
                line[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>((noise >> 5) & 0x1f);
            }

            MCWindow& screen = ScreenBuffer();
            MCRenderer::For(screen).Write(screen, 0, row, line);
        }
    }

    /// <summary>The connect shape at the screen's centre, with a progress bar of <paramref name="progress"/> percent.</summary>
    void DrawProgress(int32_t progress)
    {
        // Port fix: centre on the part of the screen that is shown. While a mission loads, the screen already has
        // the mission's size (e.g. 1280x720) but the display still shows the menus' 640x480 view of it, which put the
        // bar half off the screen.
        int32_t width = GuiSystem().Width();
        int32_t height = GuiSystem().Height();
        int32_t viewX = 0;
        int32_t viewY = 0;

        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            const SDL_Rect view = display->View();

            if (view.w > 0 && view.h > 0)
            {
                viewX = view.x;
                viewY = view.y;
                width = std::min(width - viewX, view.w);
                height = std::min(height - viewY, view.h);
            }
        }

        const int32_t centerY = viewY + (height >> 1);
        const int32_t centerX = viewX + (width >> 1);
        AGShapeDraw(ScreenPort()->Frame(), ConnectShape().Bytes(), 0, centerX, centerY);
        const int32_t left = centerX - 0x5f;
        const auto right = static_cast<int32_t>(progress * 0.01 * 188.0 + left);
        std::array<MCScreenVertex, 4> bar = {};
        bar[0].X = left;
        bar[0].Y = centerY + 3;
        bar[1].X = right;
        bar[1].Y = centerY + 3;
        bar[2].X = right;
        bar[2].Y = centerY + 10;
        bar[3].X = left;
        bar[3].Y = centerY + 10;

        for (MCScreenVertex& vertex : bar)
        {
            vertex.C = 0xe40000;
        }

        VfxFlatPolygon(ScreenPort()->Frame(), bar);
    }

    /// <summary>The frame graph: 512 frames, 3 pixels per frame a second, 60 at the top.</summary>
    void DrawFrameGraph()
    {
        WhiteFont->WriteString(ScreenWindow()->Frame(), 0x20, 7, "60+ f/s");
        WhiteFont->WriteString(ScreenWindow()->Frame(), 0x20, 0xbb, "0   f/s");
        // Port: the original set the two lines in the screen's memory.
        MCWindow& screen = ScreenBuffer();
        MCRenderer::For(screen).Clear(screen, MCRect{0x40, 9, 0x40 + 0x1ff, 9}, 0xff);
        MCRenderer::For(screen).Clear(screen, MCRect{0x40, 0xbf, 0x40 + 0x1ff, 0xbf}, 0xff);

        for (int32_t y = 0xaf; y > 10; y -= 0xf)
        {
            AGPixelWrite(ScreenPort()->Frame(), 0x240, y, 0xff);
        }

        size_t frame = FrPointer;

        for (int32_t x = 0; x < static_cast<int32_t>(FrameGraphLength); x++)
        {
            const auto height = static_cast<int32_t>(FrameRateArray[frame] * 3.0f);
            AGPixelWrite(ScreenPort()->Frame(), x + 0x40, 0xbe - height, 0xf9);
            frame = (frame + 1) % FrameGraphLength;
        }
    }
}

void BlankScreen()
{
    ALockScreen();
    VfxPaneWipe(ScreenPort()->Frame(), 0);
    AUnlockScreen();
    ReadCursorPosition();
    DrawCursorInBuffer();
    PresentScreen();
}

int32_t UpdateDisplay(bool screenShot, bool staticNoise, int32_t noiseChance, bool showProgress, int32_t progress)
{
    if (InMouseCritSec != 0)
    {
        return 0;
    }

    MCGuiSystem& gui = GuiSystem();

    if (!gui.PendingPalette.empty())
    {
        gui.FadeDownCurrentPalette();
    }

    std::scoped_lock lock(MouseCritSec);
    InMouseCritSec = 1;
    // Port: dragged objects draw into the cursor while the frame is drawn (MCHardwareCursorCarry).
    MCHardwareCursorNewFrame();
    // Port: a new frame's op tables (translucent UI over the world view; see MCUnderlay).
    MCRenderer::ResetOpTables();

    if (gui.SmackerWindow == nullptr)
    {
        if (const std::shared_ptr<MCWindow> screen = ScreenWindow()->Frame().Window.lock();
            GlobalLog() != nullptr && screen != nullptr && screen->Pixels != nullptr)
        {
            VfxPaneWipe(ScreenWindow()->Frame(), 0);
        }

        MCFrameLog::Scope draw("draw");

        for (int32_t i = 0; i < ScreenWindow()->NumberOfChildren(); i++)
        {
            ScreenWindow()->Child(i)->Display();
        }
    }
    else
    {
        // Full screen with page flipping, the original first copied the front page to the back one.
        ALockScreen();
        gui.SmackerWindow->Display();
        AUnlockScreen();
    }

    ALockScreen();

    if (screenShot)
    {
        ShotNum++;

        // Port: the screen as shown (the world view under the key, read back when the GPU draws the frame), not its
        // memory.
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            const std::vector<uint8_t> shown = display->ComposeScreen();
            WriteTga8Bit(std::format("scrn{:04}.tga", ShotNum), shown, static_cast<uint32_t>(display->Width()),
                         static_cast<uint32_t>(display->Height()));
        }
    }

    if (staticNoise && noiseChance != 0)
    {
        DrawStatic(noiseChance);
    }

    if (!ConnectShape().Empty() && showProgress)
    {
        DrawProgress(progress);
    }

    FrameRateArray[FrPointer] = std::min(FrameRate, 60.0f);
    FrPointer = (FrPointer + 1) % FrameGraphLength;

    if (AndyFramerate != 0)
    {
        DrawFrameGraph();
    }

    if (KeepScreenBlack)
    {
        VfxPaneWipe(ScreenPort()->Frame(), 0);
    }

    if (GShowFps != 0)
    {
        DrawFrameCounter();
    }

    AUnlockScreen();

    if (gui.SmackerWindow != nullptr)
    {
        if (MovieOver == 0)
        {
            gui.SmackerWindow->CheckSmackerPalette();
            PresentScreen();
        }
        else
        {
            gui.CloseMovie();
        }
    }

    ReadCursorPosition();
    DrawCursorInBuffer();
    PresentScreen();

    if (!gui.PendingPalette.empty())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(17));

        if (GBitDepth != 16)
        {
            gui.TweakPalette(gui.PendingPaletteFirst, gui.PendingPaletteCount, gui.PendingPalette, true);
        }

        gui.PendingPaletteFirst = 0;
        gui.PendingPaletteCount = 0;
        gui.PendingPalette.clear();
    }

    InMouseCritSec = 0;
    return 0;
}

void MouseTimerInit()
{
    // timeGetDevCaps / timeBeginPeriod ("No Mouse Timer available") have no counterpart.
    InMouseCritSec = 0;
    const uint32_t period = 1000 / ResultsStepTicks;
    MouseTimerThread = std::jthread(
        [period](std::stop_token stop)
        {
            auto next = std::chrono::steady_clock::now();

            while (!stop.stop_requested())
            {
                next += std::chrono::milliseconds(period);
                std::this_thread::sleep_until(next);

                if (stop.stop_requested())
                {
                    break;
                }

                MouseTimerTick();
            }
        });
    MouseTimerRunning = true;
    FrameRateArray.fill(0.0f);
    MouseThreadStarted = 1;
}

void MouseTimerKill()
{
    MouseThreadStarted = 0;

    if (!MouseTimerRunning)
    {
        return;
    }

    if (InMouseCritSec != 0)
    {
        // Killed from inside UpdateDisplay (a Fatal while drawing): the original left the lock it held.
        MouseCritSec.unlock();
        InMouseCritSec = 0;
    }

    while (InTimerThread)
    {
        std::this_thread::yield();
    }

    MouseTimerThread.request_stop();

    if (MouseTimerThread.joinable() && MouseTimerThread.get_id() != std::this_thread::get_id())
    {
        MouseTimerThread.join();
    }

    while (InTimerThread)
    {
        std::this_thread::yield();
    }

    MouseTimerRunning = false;
}

void MouseTimerTick()
{
    InTimerThread = true;

    {
        std::scoped_lock lock(MouseCritSec);
        MouseTicks++;
    }

    SoundTicks++;

    if (static_cast<int32_t>(ResultsStepTicks / 2) < SoundTicks)
    {
        SoundTicks = 0;

        if (const std::shared_ptr<MCSoundRenderer> renderer = SoundRenderer(); renderer != nullptr)
        {
            renderer->ServiceStreams();
        }
    }

    InTimerThread = false;
}
