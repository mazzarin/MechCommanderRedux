#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiInput.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiSystem.h"
#include "gui/MCUpdateDisplay.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCUtf8Path.h"

namespace MCScreenInput
{
    uint32_t ScreenHash()
    {
        const MCWindow& screen = ScreenPort()->Bitmap();
        const int32_t width = screen.XMax + 1;
        const int32_t height = screen.YMax + 1;
        uint32_t hash = 0x811c9dc5;
        hash = (hash ^ static_cast<uint32_t>(width)) * 0x01000193;
        hash = (hash ^ static_cast<uint32_t>(height)) * 0x01000193;
        std::vector<uint8_t> shown;

        if (MCInput::Display() != nullptr && MCInput::Display()->Screen().Pixels == screen.Pixels)
        {
            shown = MCInput::Display()->ComposeScreen();
        }
        else
        {
            shown.assign(screen.Pixels.get(),
                         screen.Pixels.get() + static_cast<size_t>(width) * static_cast<size_t>(height));
        }

        for (const uint8_t pixel : std::span(shown).first(static_cast<size_t>(width) * static_cast<size_t>(height)))
        {
            hash = (hash ^ pixel) * 0x01000193;
        }

        return hash;
    }

    void SaveShot(const std::string& name, uint32_t hash)
    {
        const std::optional<std::string_view> shots = MCTest::Option("shots");

        if (!shots || MCInput::Display() == nullptr)
        {
            return;
        }

        static_cast<void>(MCInput::Display()->SaveScreenshot(MCFileSystem::FromUtf8(*shots) / (name + ".bmp")));
        std::printf("  %s: 0x%08x\n", name.c_str(), hash);
    }

    void SaveShownShot(const std::string& name)
    {
        const std::optional<std::string_view> shots = MCTest::Option("shots");

        if (!shots || MCInput::Display() == nullptr)
        {
            return;
        }

        if (auto saved = MCInput::Display()->SaveShownScreenshot(MCFileSystem::FromUtf8(*shots) / (name + ".png"));
            !saved)
        {
            std::printf("  %s: %s\n", name.c_str(), saved.error().c_str());
        }
    }

    void SendMouse(int32_t type, int32_t x, int32_t y, bool leftHeld)
    {
        // A motion event moves the game's cursor without warping the real mouse (SetCursorPos would).
        if (const std::shared_ptr<MCDisplay> display = MCInput::Display(); display != nullptr)
        {
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = SDL_GetWindowID(&display->Window());
            display->LogicalToWindow(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, motion.motion.x,
                                     motion.motion.y);
            MCInput::HandleEvent(motion);
        }

        MouseScreenX = x;
        MouseScreenY = y;
        OldMouseX = x;
        OldMouseY = y;
        MCGuiEvent event;
        event.Clear();
        event.Type = static_cast<uint8_t>(type);
        event.X = x;
        event.Y = y;
        event.LeftButton = type == 1 ? 0xff : (leftHeld ? 1 : 0);
        // CheckMouse, run each frame, sees no button held and no move, so it adds no events of its own.
        DispatchGuiEvent(event);
    }

    void Click(int32_t x, int32_t y)
    {
        SendMouse(7, x, y, false);
        SendMouse(1, x, y, true);
        MCTestGame::RunFrame(1.0f / 15.0f);
        SendMouse(4, x, y, false);
    }

    void Drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
    {
        SendMouse(7, x0, y0, false);
        SendMouse(1, x0, y0, true);
        MCTestGame::RunFrame(1.0f / 15.0f);

        for (int32_t step = 1; step <= 8; step++)
        {
            SendMouse(7, x0 + (x1 - x0) * step / 8, y0 + (y1 - y0) * step / 8, true);
            MCTestGame::RunFrame(1.0f / 15.0f);
        }

        SendMouse(4, x1, y1, false);
    }

    void SendKey(uint8_t key)
    {
        MCGuiEvent event;
        event.Clear();
        event.Type = 10;
        event.Key = key;
        DispatchGuiEvent(event);
    }

    void RealMove(int32_t x, int32_t y)
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(&display->Window());
        display->LogicalToWindow(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, motion.motion.x,
                                 motion.motion.y);
        MCInput::HandleEvent(motion);
        // The first frame's UpdateDisplay reads the cursor; the second's CheckMouse sends the move.
        MCTestGame::RunFrame(1.0f / 15.0f);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    void RealButton(bool down)
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        SDL_Event button{};
        button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        button.button.windowID = SDL_GetWindowID(&display->Window());
        button.button.button = SDL_BUTTON_LEFT;
        button.button.down = down;
        button.button.clicks = 1;
        display->LogicalToWindow(static_cast<float>(MouseScreenX) + 0.5f, static_cast<float>(MouseScreenY) + 0.5f,
                                 button.button.x, button.button.y);
        MCInput::HandleEvent(button);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }

    void RealClick(int32_t x, int32_t y)
    {
        RealMove(x, y);
        RealButton(true);
        RealButton(false);
    }

    void RealKey(SDL_Scancode scancode)
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();

        for (bool down : {true, false})
        {
            SDL_Event key{};
            key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            key.key.windowID = SDL_GetWindowID(&display->Window());
            key.key.scancode = scancode;
            key.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
            key.key.down = down;
            MCInput::HandleEvent(key);
            MCTestGame::RunFrame(1.0f / 15.0f);
        }
    }

    void RealWheel(int32_t notches)
    {
        const std::shared_ptr<MCDisplay> display = MCInput::Display();
        SDL_Event wheel{};
        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.windowID = SDL_GetWindowID(&display->Window());
        wheel.wheel.y = static_cast<float>(notches);
        wheel.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        display->LogicalToWindow(static_cast<float>(MouseScreenX) + 0.5f, static_cast<float>(MouseScreenY) + 0.5f,
                                 wheel.wheel.mouse_x, wheel.wheel.mouse_y);
        MCInput::HandleEvent(wheel);
        MCTestGame::RunFrame(1.0f / 15.0f);
    }
}
