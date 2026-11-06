#include "stdafx.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

// aButton

MCGuiButton::~MCGuiButton()
{
    UpPicture.reset();
    DownPicture.reset();
    GrayPicture.reset();
    LeftCallback.reset();
    RightButtonCallback.reset();
}

auto MCGuiButton::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    const int32_t result = MCGuiObject::Init(xPos, yPos, width, height, name);

    if (result != 0)
    {
        return result;
    }

    UpPicture.reset();
    DownPicture.reset();
    GrayPicture.reset();
    LeftCallback = std::make_shared<MCGuiCallback>();
    RightButtonCallback = std::make_shared<MCGuiCallback>();
    Disabled = false;
    Framed = true;
    BackgroundColor = 0;
    VfxPaneWipe(DisplayPort->Frame(), 0);
    return 0;
}

auto MCGuiButton::OnDestroy() -> void
{
    UpPicture.reset();
    DownPicture.reset();
    GrayPicture.reset();
    LeftCallback.reset();
    RightButtonCallback.reset();
    MCGuiObject::OnDestroy();
}

template <typename Source>
auto MCGuiButton::LoadPicture(std::shared_ptr<MCGuiPort>& picture, Source source, bool sizeButton) -> void
{
    picture = std::make_shared<MCGuiPort>();

    if (picture->Init(source) != 0)
    {
        picture.reset();
        return;
    }

    if (sizeButton)
    {
        BackgroundColor = 0xff;
        Resize(picture->Width(), picture->Height());
    }
}

auto MCGuiButton::SetUpPicture(std::string_view fileName) -> void
{
    LoadPicture(UpPicture, fileName, true);
}

auto MCGuiButton::SetGrayPicture(std::string_view fileName) -> void
{
    LoadPicture(GrayPicture, fileName, true);
}

auto MCGuiButton::SetDownPicture(std::string_view fileName) -> void
{
    LoadPicture(DownPicture, fileName, false);
}

auto MCGuiButton::SetUpPicture(int32_t artPacket) -> void
{
    LoadPicture(UpPicture, artPacket, true);
}

auto MCGuiButton::SetGrayPicture(int32_t artPacket) -> void
{
    LoadPicture(GrayPicture, artPacket, true);
}

auto MCGuiButton::SetDownPicture(int32_t artPacket) -> void
{
    LoadPicture(DownPicture, artPacket, false);
}

auto MCGuiButton::EventOnButton(const MCGuiEvent& event) -> bool
{
    // (The original also redrew the button for a left-button release; the button draws itself each frame.)
    const std::shared_ptr<MCGuiObject> parent = ParentObject();
    const int32_t xPos = event.X - parent->GlobalX();
    const int32_t yPos = event.Y - parent->GlobalY();
    return X() <= xPos && xPos < X() + Width() && Y() <= yPos && yPos < Y() + Height();
}

auto MCGuiButton::HandleEvent(MCGuiEvent& event) -> void
{
    if (Disabled)
    {
        return;
    }

    MCGuiSystem& gui = GuiSystem();

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        case MCGuiEventType::RightButtonDown:
            gui.Grab(weak_from_this());
            break;
        case MCGuiEventType::LeftButtonUp:
        {
            if (gui.Grabs(*this))
            {
                gui.Release();

                if (EventOnButton(event))
                {
                    MCGuiCallback::Run(LeftCallback);
                }
            }
            break;
        }
        case MCGuiEventType::RightButtonUp:
        {
            if (gui.Grabs(*this))
            {
                gui.Release();

                if (EventOnButton(event))
                {
                    MCGuiCallback::Run(RightButtonCallback);
                }
            }
            break;
        }
    }

    if (EventRoutine)
    {
        EventRoutine(*this, event);
    }
}

auto MCGuiButton::Draw() -> void
{
    std::shared_ptr<MCGuiPort> picture;
    const bool held = GuiSystem().Grabs(*this);

    // The frame is drawn first, so an opaque picture covers it.
    if (Framed)
    {
        DrawFramed(!Disabled && held, true);
    }

    if (Disabled)
    {
        picture = GrayPicture;
    }
    else if (held)
    {
        picture = DownPicture;
    }
    else
    {
        picture = UpPicture;
    }

    if (picture != nullptr)
    {
        picture->CopyTo(DisplayPort->Frame(), 0, 0, false);
    }
    else
    {
        VfxPaneWipe(DisplayPort->Frame(), BackgroundColor);
    }

    MCGuiObject::Draw();
}

// aCloseButton

auto MCGuiCloseButton::HandleEvent(MCGuiEvent& event) -> void
{
    if (Disabled)
    {
        return;
    }

    MCGuiSystem& gui = GuiSystem();

    if (event.Type == MCGuiEventType::LeftButtonUp && gui.Grabs(*this))
    {
        gui.Release();

        if (EventOnButton(event))
        {
            // The window is closing: the event routine isn't run.
            MCGuiCallback::Run(LeftCallback);
            return;
        }
    }

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        case MCGuiEventType::RightButtonDown:
            gui.Grab(weak_from_this());
            break;
        case MCGuiEventType::RightButtonUp:
        {
            if (gui.Grabs(*this))
            {
                gui.Release();

                if (EventOnButton(event))
                {
                    MCGuiCallback::Run(RightButtonCallback);
                }
            }
            break;
        }
    }

    if (EventRoutine)
    {
        EventRoutine(*this, event);
    }
}

// aToolButton

auto MCGuiToolButton::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) -> int32_t
{
    Pushed = false;
    return MCGuiButton::Init(xPos, yPos, width, height, name);
}

auto MCGuiToolButton::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::LeftButtonDown && !Disabled)
    {
        Pushed = !Pushed;
        MCGuiCallback::Run(LeftCallback);
        MCGuiObject::HandleEvent(event);
        return;
    }

    MCGuiButton::HandleEvent(event);
}

auto MCGuiToolButton::Draw() -> void
{
    if (Disabled)
    {
        if (Framed)
        {
            DrawFramed(false, true);
        }

        if (GrayPicture != nullptr)
        {
            GrayPicture->CopyTo(DisplayPort->Frame(), 0, 0, true);
        }
        else
        {
            VfxPaneWipe(DisplayPort->Frame(), BackgroundColor);
        }

        MCGuiObject::Draw();
        return;
    }

    // Pictures are drawn transparently, then the frame over them (unfilled); without a picture the frame is filled.
    const std::shared_ptr<MCGuiPort> picture = Pushed ? DownPicture : UpPicture;

    if (picture != nullptr)
    {
        picture->CopyTo(DisplayPort->Frame(), 0, 0, true);
    }

    if (Framed)
    {
        DrawFramed(Pushed, picture == nullptr);
    }

    MCGuiObject::Draw();
}
