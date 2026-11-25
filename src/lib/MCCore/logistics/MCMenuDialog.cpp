#include "stdafx.h"
#include "logistics/MCMenuDialog.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiObject.h"
#include "gui/MCGuiSystem.h"
#include "logistics/MCReusableDialog.h"
#include "ui/MCRmlDialog.h"

/// <summary>The GUI object a dialog's timeouts' timers go to: its timer closes the dialog with its timeout's result.</summary>
class MCMenuDialogTimer final : public MCGuiObject
{
public:
    explicit MCMenuDialogTimer(std::weak_ptr<MCMenuDialog> dialog) : _Dialog(std::move(dialog)) {}

    void HandleEvent(MCGuiEvent& event) override
    {
        if (event.Type != MCGuiEventType::Timer)
        {
            MCGuiObject::HandleEvent(event);
            return;
        }

        // Timed out.
        if (const std::shared_ptr<MCMenuDialog> dialog = _Dialog.lock(); dialog != nullptr)
        {
            dialog->Deactivate(dialog->TimeoutResult);
        }
    }

private:
    std::weak_ptr<MCMenuDialog> _Dialog;
};

namespace
{
    /// <summary>The 1998 cancel button's picture (its word in the remade dialog is "Cancel").</summary>
    constexpr std::string_view CancelPicture = "bh_cancl";
    constexpr const char* CancelWord = "Cancel";
    constexpr const char* OkWord = "OK";
    /// <summary>The result Enter closes a question with (its OK); every other answer is 0.</summary>
    constexpr int32_t QuestionOkResult = -1;
}

// MCMenuDialog::MCButton

void MCMenuDialog::MCButton::SetPictures(std::string_view up, std::string_view down)
{
    UpArt = up;
    DownArt = down;
}

void MCMenuDialog::MCButton::Run() const
{
    if (Exec)
    {
        const std::function<void()> exec = Exec;
        exec();
    }
}

bool MCMenuDialog::MCButton::Runs(void (&func)()) const
{
    return Exec.target_type() == typeid(void (*)()) && *Exec.target<void (*)()>() == &func;
}

std::string MCMenuDialog::MCButton::Word() const
{
    std::string name = UpArt;
    std::ranges::transform(name, name.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return name.starts_with(CancelPicture) ? CancelWord : OkWord;
}

// MCMenuDialog

MCMenuDialog::MCMenuDialog() = default;

MCMenuDialog::~MCMenuDialog() = default;

void MCMenuDialog::SetView(MCGuiHandle<MCReusableDialog> view)
{
    _View = std::move(view);

    if (const std::shared_ptr<MCReusableDialog> shown = _View.Lock(); shown != nullptr)
    {
        shown->SetModel(weak_from_this());
    }
}

void MCMenuDialog::SetText(std::string_view text)
{
    Text = text.substr(0, text.find('\0'));
    SetTwoButton(TwoButton);
}

void MCMenuDialog::SetTwoButton(bool two)
{
    TwoButton = two;
    // Port: OK shows again after a waiting message (ShowWaitingDialog), which hides the buttons: the message that
    // followed one (COULDN'T JOIN THE ROOM AGAIN...) had nothing to click, only Return closing it.
    Ok.Shown = true;
    Cancel.Shown = two;

    if (const std::shared_ptr<MCReusableDialog> view = _View.Lock(); view != nullptr)
    {
        view->Lay();
    }
}

bool MCMenuDialog::IsShowing() const
{
    return StillSaid();
}

bool MCMenuDialog::StillSaid() const
{
    if (!_SaidOverRemade)
    {
        return false;
    }

    // Hidden by a change of screens, its say comes back (MCRmlDialogs::Tend) unless a newer one took its place.
    if (MCRmlDialogs::Returning(_Said))
    {
        return true;
    }

    const MCGuiHandle<MCRmlDialog> dialog = MCRmlDialogs::Dialog();
    return MCRmlDialogs::Shown() && dialog && dialog->Said().Text == _Said;
}

void MCMenuDialog::AnswerRemade(bool ok)
{
    const std::shared_ptr<MCMenuDialog> pinned = shared_from_this();
    // As Enter and Escape press its buttons in the 1998 box: OK, then the dialog closed with OK's result; CANCEL with 0.
    _SaidOverRemade = false;
    const bool two = TwoButton;
    (ok ? Ok : Cancel).Run();
    Deactivate(ok && two ? QuestionOkResult : 0);
}

void MCMenuDialog::SayOverRemade()
{
    const std::weak_ptr<MCMenuDialog> self = weak_from_this();
    const auto answer = [self](bool ok)
    {
        if (const std::shared_ptr<MCMenuDialog> dialog = self.lock(); dialog != nullptr)
        {
            dialog->AnswerRemade(ok);
        }
    };

    _Said = MCRmlDialogs::AsSentence(Text);
    MCRmlDialog::MCSay say{.Text = _Said,
                           .Main = Ok.Disabled ? std::string() : Ok.Word(),
                           .Other = TwoButton ? Cancel.Word() : std::string(),
                           .OnMain = [answer] { answer(true); },
                           .OnOther = [answer] { answer(false); },
                           // Through a change of screens it stands, as the original's box stayed up.
                           .UntilAnswered = true};

    MCRmlDialogs::Show(std::move(say));
    _SaidOverRemade = true;

    if (const std::shared_ptr<MCReusableDialog> view = _View.Lock(); view != nullptr)
    {
        view->HideOld(false);
    }

    StartTimeout();
}

void MCMenuDialog::Activate()
{
    // Port-only (SPEC-menus.md's stage 9): the remade dialog says it, over a remade screen or none (the 1998 box went).
    SayOverRemade();
}

void MCMenuDialog::StartTimeout()
{
    if (Timeout > 0)
    {
        CloseIn(Timeout);
    }
}

void MCMenuDialog::CloseIn(int32_t milliseconds)
{
    if (_Timer == nullptr)
    {
        _Timer = MCGuiRef<MCMenuDialogTimer>::Make(weak_from_this());
    }

    GuiSystem().AddTimer(_Timer.Pin(), TimeoutTimer, milliseconds, 0, 0, false);
}

void MCMenuDialog::Deactivate(int32_t result)
{
    // Port-only: kept alive through its answer (here, and in Enter, Escape, TakeKey and AnswerRemade), which may end
    // logistics, and the dialog with it, before it returns.
    const std::shared_ptr<MCMenuDialog> pinned = shared_from_this();
    const std::shared_ptr<MCReusableDialog> view = _View.Lock();

    // Port-only (SPEC-menus.md's stage 4): said over the remade screens, it grabbed nothing; closed by its timeout (or by
    // its owner), the remade dialog closes too.
    if (_SaidOverRemade)
    {
        if (StillSaid())
        {
            MCRmlDialogs::Close();
        }

        _SaidOverRemade = false;

        if (view != nullptr)
        {
            view->HideOld(false);
        }
    }
    else if (view != nullptr)
    {
        view->HideOld(true);
    }

    // Port-only (SPEC-menus.md's stage 3): a wait said over a remade screen in this dialog's place ends with it.
    MCRmlDialogs::EndWait();

    // Port-only: the keyboard a dialog asking for a key held (KeyTaker) goes back.
    if (view != nullptr && GuiSystem().HasText(*view))
    {
        GuiSystem().ReleaseText();
    }

    // The original also skipped a callback pointer IsBadReadPtr rejected. Called from a copy: it may set another.
    if (Callback)
    {
        const std::function<void(int32_t)> callback = Callback;
        callback(result);
    }

    if (KeepCallbacks)
    {
        KeepCallbacks = false;
        return;
    }

    Callback = nullptr;
    KeyTaker = nullptr;
    Ok.Exec = nullptr;
    Cancel.Exec = nullptr;

    if (_Timer != nullptr)
    {
        GuiSystem().RemoveTimer(*_Timer, TimeoutTimer);
    }

    Timeout = 0;
    TimeoutResult = 0;
}

void MCMenuDialog::Enter()
{
    const std::shared_ptr<MCMenuDialog> pinned = shared_from_this();
    Ok.Run();
    Deactivate(TwoButton ? QuestionOkResult : 0);
}

void MCMenuDialog::Escape()
{
    const std::shared_ptr<MCMenuDialog> pinned = shared_from_this();
    // OB-074 (fixed): Escape answers a question with CANCEL; original behaviour (OB-074): it runs a message's one button
    // (OK) too.
    (TwoButton ? Cancel : Ok).Run();
    Deactivate(0);
}

void MCMenuDialog::TakeKey(uint32_t key)
{
    const std::shared_ptr<MCMenuDialog> pinned = shared_from_this();

    if (KeyTaker && KeyTaker(key))
    {
        Deactivate(0);
    }
}

void MCMenuDialog::TakeKeyboard()
{
    if (const std::shared_ptr<MCReusableDialog> view = _View.Lock(); view != nullptr)
    {
        GuiSystem().SetText(view);
    }
}
