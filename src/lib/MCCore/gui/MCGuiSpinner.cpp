#include "stdafx.h"
#include "gui/MCGuiSpinner.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"

// aSpinnerButton

auto MCGuiSpinnerButton::HandleEvent(MCGuiEvent& event) -> void
{
    MCGuiSystem& gui = GuiSystem();

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            Pushed = true;
            gui.Grab(weak_from_this());
            gui.AddTimer(weak_from_this(), 1, 1000, 0, 0, false);
            MCGuiCallback::Run(LeftCallback);
            break;
        }
        case MCGuiEventType::LeftButtonUp:
        {
            Pushed = false;
            gui.Release();
            gui.RemoveTimer(*this, 1);
            gui.RemoveTimer(*this, 2);
            break;
        }
        case MCGuiEventType::Timer:
        {
            // Timer 1 (the first second held) hands over to the repeating timer 2.
            if (event.Data == 1)
            {
                gui.RemoveTimer(*this, 1);
                gui.AddTimer(weak_from_this(), 2, 250, 0, 0, false);
            }

            if (event.Data == 2)
            {
                MCGuiCallback::Run(LeftCallback);
            }
            break;
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCGuiSpinnerButton::Draw() -> void
{
    if (const std::shared_ptr<MCGuiPort> picture = Pushed ? DownPicture : UpPicture; picture != nullptr)
    {
        picture->CopyTo(DisplayPort->Frame(), 0, 0, true);
    }

    MCGuiObject::Draw();
}

// aSpinner

MCGuiSpinner::~MCGuiSpinner() = default;

auto MCGuiSpinner::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height,
                        [[maybe_unused]] std::string_view name) -> int32_t
{
    const std::weak_ptr<MCGuiObject> owner = Parent;

    if (owner.expired())
    {
        Fatal(0, "Hey Scott! You have to set the parent before the init! Remember?");
    }

    int32_t result = MCGuiObject::Init(xPos, yPos, width, height, {});

    if (result != 0)
    {
        return result;
    }

    // The base's init clears the parent; put it back.
    SetParent(owner);
    // Each button is added once its art is set (the original added both after sizing the spinner, which doesn't
    // touch its children).
    const auto makeButton = [&](int32_t upArt, int32_t downArt)
    {
        return TryCreateChild<MCGuiSpinnerButton>(
            [&](MCGuiSpinnerButton& made)
            {
                if (const int32_t buttonResult = made.Init(1, 1, 10, 10, {}); buttonResult != 0)
                {
                    return buttonResult;
                }

                made.SetUpPicture(upArt);
                made.SetDownPicture(downArt);
                return 0;
            });
    };

    auto up = makeButton(0xc, 0xd);

    if (!up)
    {
        return up.error();
    }

    UpButton = *up;
    auto down = makeButton(0x25, 0x26);

    if (!down)
    {
        return down.error();
    }

    DownButton = *down;
    Resize(std::max(UpButton->Width(), DownButton->Width()), UpButton->Height() + DownButton->Height());
    UpButton->MoveTo(0, 0);
    DownButton->MoveTo(0, UpButton->Height());
    UpButton->Callback().SetMessage(Parent, MCGuiEventType::SpinUp);
    DownButton->Callback().SetMessage(Parent, MCGuiEventType::SpinDown);
    return -1;
}
