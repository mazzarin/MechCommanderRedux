#include "stdafx.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCReusableDialog.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "ui/MCRmlDialog.h"

auto ShowMenuMessage(std::string_view text, std::function<void(int32_t)> callback, std::string_view upArt,
                     std::string_view downArt) -> void
{
    // Port-only (SPEC-menus.md's stage 3): over a remade screen, the remade dialog (the 1998 one would show under it).
    constexpr int32_t OkAnswer = 1;

    if (MCRmlDialogs::Message(text,
                              [callback]
                              {
                                  if (callback != nullptr)
                                  {
                                      callback(OkAnswer);
                                  }
                              }))
    {
        return;
    }

    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(text);
    dialog.SetTwoButton(false);
    dialog.Callback = std::move(callback);
    dialog.OkButton->SetUpPicture(upArt);
    dialog.OkButton->SetDownPicture(downArt);
    dialog.OkButton->Disabled = false;
    dialog.Activate();
}

auto ShowMenuMessage(uint32_t stringId, std::function<void(int32_t)> callback, std::string_view upArt,
                     std::string_view downArt) -> void
{
    ShowMenuMessage(LoadGameString(stringId, 0xfe), std::move(callback), upArt, downArt);
}

auto AskMenuQuestion(MCReusableDialog& dialog, uint32_t stringId, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons) -> void
{
    AskMenuQuestion(dialog, LoadGameString(stringId, 0xfe), std::move(okExec), std::move(cancelExec), cancelDownArt,
                    enableButtons);
}

auto AskMenuQuestion(MCReusableDialog& dialog, std::string_view text, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons) -> void
{
    // Port-only (SPEC-menus.md's stage 3): over a remade screen, the remade dialog (the 1998 one would show under it).
    if (MCRmlDialogs::Show(MCRmlDialog::MCSay{.Text = MCRmlDialogs::AsSentence(text),
                                              .Main = "OK",
                                              .Other = "Cancel",
                                              .OnMain = std::move(okExec),
                                              .OnOther = std::move(cancelExec)}))
    {
        return;
    }

    dialog.SetText(text);
    dialog.SetTwoButton(true);
    dialog.Callback = nullptr;
    dialog.OkButton->SetUpPicture("bh_okay.tga");
    dialog.OkButton->SetDownPicture("bg_okay.tga");

    if (enableButtons)
    {
        dialog.OkButton->Disabled = false;
    }

    dialog.OkButton->Callback().SetExec(std::move(okExec));
    dialog.CancelButton->SetUpPicture("bh_cancl.tga");
    dialog.CancelButton->SetDownPicture(cancelDownArt);

    if (enableButtons)
    {
        dialog.CancelButton->Disabled = false;
    }

    dialog.CancelButton->Callback().SetExec(std::move(cancelExec));
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
