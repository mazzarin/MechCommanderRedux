#include "stdafx.h"
#include "logistics/MCLogObject.h"
#include "gui/MCGuiAnimation.h"
#include "platform/MCInput.h"
#include "sound/MCSoundSystem.h"
#include "vfx/MCVfxFunctions.h"

auto PlayLogSound(uint32_t sampleId) -> void
{
    // A test may run the controls without a sound system.
    if (const std::shared_ptr<MCSoundSystem> sound = SoundSystem(); sound != nullptr)
    {
        sound->PlayInterfaceSample(sampleId);
    }
}

auto MCLogObject::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height) -> int32_t
{
    // The base's init, inlined with a logistics port for the port.
    const bool live = DrawsLive();
    InitWithoutPort(xPos, yPos, width, height);
    auto port = std::make_shared<MCLogPort>();
    const int32_t result = live ? port->InitView(width, height) : port->Init(width, height);
    SetOwnPort(std::move(port));
    return result;
}

auto MCLogObject::InitWithoutPort(int32_t xPos, int32_t yPos, int32_t width, int32_t height) -> void
{
    _Port.reset();
    Transparent = false;
    Place(xPos, yPos, width, height);
    MCGuiObject::BackgroundPort.reset();
    _BackgroundPort.reset();
}

auto MCLogObject::SetOwnPort(std::shared_ptr<MCLogPort> port) -> void
{
    _Port = std::move(port);
}

auto MCLogObject::OnDestroy() -> void
{
    // The base's destroy does the rest: the children (MCX.EXE's own loop here deleted all but the first of several,
    // OB-071; the base destroys each child made on the object once), the timers, the pane and the parent.
    _Port.reset();
    _BackgroundPort.reset();
    MCGuiObject::OnDestroy();
}

auto MCLogObject::Draw() -> void
{
    const MCGuiWindowState state = WinState;

    if (state == MCGuiWindowState::Iconized)
    {
        IconAnimation->Draw(_Port->Frame(), 0, 0);
    }
    else
    {
        if (_BackgroundPort != nullptr)
        {
            _BackgroundPort->CopyTo(_Port->Frame(), 0, 0, true);
        }

        if (WindowAnimation != nullptr && Animating)
        {
            WindowAnimation->Draw(_Port->Frame(), 0, 0);
        }
    }

    if (state != MCGuiWindowState::Iconized)
    {
        Paint();

        for (size_t i = 0; i < ChildList.size(); i++)
        {
            if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
            {
                DrawChild(*child);
            }
        }
    }
}

auto MCLogObject::Display() -> void
{
    if (!ShowWindow)
    {
        return;
    }

    if (IsHidden() != 0 && HideOffset == 0)
    {
        return;
    }

    // An object that draws itself does so after the slide has moved it.
    if (!DrawsLive())
    {
        if (WinState == MCGuiWindowState::Iconized)
        {
            if (IconAnimation != nullptr)
            {
                Draw();
            }
        }
        else if (WindowAnimation != nullptr)
        {
            WindowAnimation->Draw(_Port->Frame(), 0, 0);
            Draw();
        }
    }

    if (HideOffset != 0)
    {
        // A slide (HideMe) moves the whole offset each frame until the object is off the screen, or back home.
        if (HideDirection == MCDirection::Left || HideDirection == MCDirection::Right)
        {
            MoveTo(X() + HideOffset, Y(), true);
        }
        else
        {
            MoveTo(X(), Y() + HideOffset, true);
        }

        if (Hidden)
        {
            if (RectIntersect(0, 0, GuiSystem().Width(), GuiSystem().Height()) == 0)
            {
                HideOffset = 0;
            }
        }
        else
        {
            bool home = false;

            if (HideOffset < 0)
            {
                home = HomeX >= GlobalX() && HomeY >= GlobalY();
            }
            else if (HideOffset > 0)
            {
                home = HomeX <= GlobalX() && HomeY <= GlobalY();
            }

            if (home)
            {
                const int32_t homeYOffset = HomeY - ParentObject()->GlobalY();
                MoveTo(HomeX - ParentObject()->GlobalX(), homeYOffset, true);
                HideOffset = 0;
            }
        }
    }

    if (DrawsLive() && _Port != nullptr)
    {
        DrawInFramePass(*_Port);
        return;
    }

    if (_Port != nullptr)
    {
        _Port->CopyTo(*FramePane, 0, 0, Transparent);
    }

    if (WinState != MCGuiWindowState::Iconized)
    {
        for (size_t i = 0; i < ChildList.size(); i++)
        {
            if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
            {
                child->Display();
            }
        }
    }
}

auto MCLogObject::Resize(int32_t width, int32_t height) -> void
{
    if (width > 0 && height > 0 && (width != WinWidth || height != WinHeight))
    {
        if (_Port != nullptr)
        {
            _Port->Resize(width, height);
        }

        WinWidth = width;
        WinHeight = height;
        FramePane->X1 = FramePane->X0 - 1 + width;
        FramePane->Y1 = FramePane->Y0 - 1 + height;
    }
}

auto MCLogObject::FillBox(int16_t left, int16_t top, int16_t right, int16_t bottom, uint8_t color) -> void
{
    // The rectangle is in the port's bitmap coordinates (the pane's own origin is not added).
    MCPane box = _Port->Frame();
    box.X0 = left;
    box.Y0 = top;
    box.X1 = right;
    box.Y1 = bottom;
    VfxPaneWipe(box, color);
}

auto MCLogObject::SetBackground(std::string_view fileName) -> int32_t
{
    _BackgroundPort = std::make_shared<MCLogPort>();
    _BackgroundPort->Load(fileName);
    return 0;
}
