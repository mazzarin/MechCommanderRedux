#include "stdafx.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCMenuDialog.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"

auto ShowMenuMessage(std::string_view text, std::function<void(int32_t)> callback, std::string_view upArt,
                     std::string_view downArt) -> void
{
    // Port-only (SPEC-menus.md's stage 4): over a remade screen, the remade dialog says it (MCMenuDialog::Activate).
    MCMenuDialog& dialog = *GlobalLog()->Message;
    dialog.SetText(text);
    dialog.SetTwoButton(false);
    dialog.Callback = std::move(callback);
    dialog.Ok.SetPictures(upArt, downArt);
    dialog.Ok.Disabled = false;
    dialog.Activate();
}

auto ShowMenuMessage(uint32_t stringId, std::function<void(int32_t)> callback, std::string_view upArt,
                     std::string_view downArt) -> void
{
    ShowMenuMessage(LoadGameString(stringId, 0xfe), std::move(callback), upArt, downArt);
}

auto AskMenuQuestion(MCMenuDialog& dialog, uint32_t stringId, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons) -> void
{
    AskMenuQuestion(dialog, LoadGameString(stringId, 0xfe), std::move(okExec), std::move(cancelExec), cancelDownArt,
                    enableButtons);
}

auto AskMenuQuestion(MCMenuDialog& dialog, std::string_view text, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons) -> void
{
    // Over a remade screen the dialog says itself in the remade one (MCMenuDialog::Activate), its buttons its own.
    dialog.SetText(text);
    dialog.SetTwoButton(true);
    dialog.Callback = nullptr;
    dialog.Ok.SetPictures("bh_okay.tga", "bg_okay.tga");

    if (enableButtons)
    {
        dialog.Ok.Disabled = false;
    }

    dialog.Ok.Exec = std::move(okExec);
    dialog.Cancel.SetPictures("bh_cancl.tga", cancelDownArt);

    if (enableButtons)
    {
        dialog.Cancel.Disabled = false;
    }

    dialog.Cancel.Exec = std::move(cancelExec);
    dialog.Activate();
}

auto ElementText(MCGenericScreen& screen, int32_t index) -> std::string
{
    return std::string(screen.Element<MCLogTextObject>(index)->Text());
}

auto ElementNumber(MCGenericScreen& screen, int32_t index) -> int32_t
{
    return std::atoi(ElementText(screen, index).c_str());
}

auto ImageHandleEvent(MCGuiObject& object, MCGuiEvent& event) -> void
{
    if (!object.Parent.expired())
    {
        object.ParentObject()->HandleEvent(event);
    }
}
