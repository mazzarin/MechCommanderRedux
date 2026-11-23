#include "stdafx.h"
#include "gui/MCGuiMessageBox.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiSystem.h"
#include "vfx/MCVfxFunctions.h"

MCGuiMessageBox::~MCGuiMessageBox()
{
    OkButton.reset();
}

auto MCGuiMessageBox::Init(std::string_view text) -> int32_t
{
    if (WhiteFont == nullptr)
    {
        return -3;
    }

    const int32_t boxWidth = std::max(WhiteFont->Width(text) + 0xc, 0x48);
    const int32_t fontHeight = WhiteFont->Height();
    // Port-only (SPEC-menus.md's stage 1): centred on what the window shows (the menus' frame, or the whole screen in a
    // mission), where the original centred it on the screen.
    const SDL_Rect shown = GuiSystem().ShownView();
    int32_t result = MCGuiObject::Init(shown.x + (shown.w - boxWidth) / 2,
                                       shown.y + (shown.h - (fontHeight + 0x28)) / 2, boxWidth, fontHeight + 0x28, {});

    if (result != 0)
    {
        return result;
    }

    auto button = TryCreateChild<MCGuiButton>(
        [&](MCGuiButton& made)
        {
            if (const int32_t buttonResult = made.Init((boxWidth - 0x30) / 2, fontHeight + 0xe, 0x3c, 0x14, {});
                buttonResult != 0)
            {
                return buttonResult;
            }

            made.SetUpPicture(0x10);
            made.SetDownPicture(0x11);
            made.Callback().SetExec(DestroyVersion);
            made.SetDepth(100);
            return 0;
        });

    if (!button)
    {
        return button.error();
    }

    OkButton = *button;
    // The box is drawn by Draw, each frame.
    Message = text;
    return 0;
}

auto MCGuiMessageBox::Draw() -> void
{
    MCPane& pane = DisplayPort->Frame();
    VfxPaneWipe(pane, 0x11);
    WhiteFont->WriteString(pane, (Width() - WhiteFont->Width(Message)) / 2, 8, Message);
    DrawBox(0x1f, -1, -1, -1, -1);
    MCGuiObject::Draw();
}

auto MCGuiMessageBox::HandleEvent(MCGuiEvent& event) -> void
{
    if (PointInside(event.X, event.Y))
    {
        OkButton->HandleEvent(event);
    }
}
