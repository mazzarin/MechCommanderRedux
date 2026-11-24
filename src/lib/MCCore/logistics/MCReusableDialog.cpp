#include "stdafx.h"
#include "logistics/MCReusableDialog.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "iface/MCKeyBindings.h"
#include "lib/MCFatal.h"
#include "main/MCGameStrings.h"

namespace
{
    /// <summary>Loads a frame piece of the dialog.</summary>
    std::shared_ptr<MCLogPort> LoadPiece(std::string_view fileName)
    {
        auto piece = std::make_shared<MCLogPort>();
        piece->Load(fileName);
        return piece;
    }

    /// <summary>The height of a line of the dialog's text.</summary>
    int32_t TextLineHeight()
    {
        return MedBlueFont->Height() + 3;
    }

    /// <summary>
    /// Port: the text's paragraphs, each word-wrapped on lines of its own (a newline ends each but the last). The
    /// original's texts have none, so they are one paragraph, wrapped as the original wrapped them.
    /// </summary>
    std::vector<std::string_view> Paragraphs(std::string_view text)
    {
        std::vector<std::string_view> paragraphs;
        size_t start = 0;

        for (size_t end = text.find('\n'); end != std::string_view::npos; end = text.find('\n', start))
        {
            paragraphs.push_back(text.substr(start, end - start));
            start = end + 1;
        }

        paragraphs.push_back(text.substr(start));
        return paragraphs;
    }
}

// MCReusableDialog

auto MCReusableDialog::Init([[maybe_unused]] int32_t xPos, [[maybe_unused]] int32_t yPos,
                            [[maybe_unused]] int32_t width, [[maybe_unused]] int32_t height,
                            [[maybe_unused]] std::string_view name) -> int32_t
{
    // The box is its top, some middle pieces and its bottom, centred across the screen at y 200.
    TopPiece = LoadPiece("dbox_top.tga");
    MiddlePiece = LoadPiece("dbox_middle.tga");
    BottomPiece = LoadPiece("dbox_bottom.tga");
    const int32_t boxWidth = TopPiece->Width();
    int32_t result = MCLogObject::Init(GuiSystem().Width() / 2 - boxWidth / 2, 200, boxWidth,
                                       BottomPiece->Height() + MiddlePiece->Height() + TopPiece->Height());
    Assert(result == 0, static_cast<uint32_t>(result), "Error initializing reusable dialog");

    const auto initButton = [](MCLogDialogButton& button)
    {
        const int32_t buttonResult = button.Init(0, 0, 0x3f, 0xe, {});
        Assert(buttonResult == 0, static_cast<uint32_t>(buttonResult), "Error initializing reusable dialog");
    };

    OkButton = CreateChild<MCLogDialogButton>(initButton);
    CancelButton = CreateChild<MCLogDialogButton>(initButton);
    SetTwoButton(false);
    ShowGuiWindow(false);
    SetDepth(100);
    TimeoutResult = 0;
    KeepCallbacks = false;
    return 0;
}

auto MCReusableDialog::OnDestroy() -> void
{
    TopPiece.reset();
    MiddlePiece.reset();
    BottomPiece.reset();
    Text.clear();
    MCLogObject::OnDestroy();
}

auto MCReusableDialog::DrawFrame() -> void
{
    int32_t pieceY = 0;

    if (TopPiece != nullptr)
    {
        TopPiece->CopyTo(Lport()->Frame(), 0, 0, true);
        pieceY = TopPiece->Height();
    }

    for (int32_t i = 0; i < NumMiddlePieces; i++)
    {
        if (MiddlePiece != nullptr)
        {
            MiddlePiece->CopyTo(Lport()->Frame(), 0, pieceY, true);
            pieceY += MiddlePiece->Height();
        }
    }

    if (BottomPiece != nullptr)
    {
        BottomPiece->CopyTo(Lport()->Frame(), 0, pieceY, true);
    }
}

auto MCReusableDialog::Draw() -> void
{
    DrawFrame();

    // The text, word-wrapped to the box: each line ends where the font says the words fit, and the character there
    // (a space) is skipped.
    int32_t lineY = TopPiece->Height() + 2;

    for (std::string_view rest : Paragraphs(Text))
    {
        auto fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(rest, WrapWidth(), true));

        if (fit == rest.size())
        {
            MedBlueFont->WriteString(_Port->Frame(), 0xc, lineY, rest);
            lineY += TextLineHeight();
            continue;
        }

        while (fit > 0 && fit <= rest.size())
        {
            MedBlueFont->WriteString(_Port->Frame(), 0xc, lineY, rest.substr(0, fit));
            rest = rest.substr(fit < rest.size() ? fit + 1 : fit);
            fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(rest, WrapWidth(), true));
            lineY += TextLineHeight();
        }
    }

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
        {
            DrawChild(*child);
        }
    }
}

auto MCReusableDialog::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::KeyDown)
    {
        if (event.Key == 0x0d)
        {
            OkButton->RunCallback();
            Deactivate(TwoButton ? -1 : 0);
        }
        else if (event.Key == 0x1b && TwoButton)
        {
            // OB-074 (fixed): Escape answers a question with CANCEL (the original ran OK's callback, so Escape on "Are you
            // sure you want to exit?" left the game).
            CancelButton->RunCallback();
            Deactivate(0);
        }
        else if (event.Key == 0x1b)
        {
            // Original behaviour (OB-074): Escape runs a message's one button (OK) too.
            OkButton->RunCallback();
            Deactivate(0);
        }
        else if (KeyTaker && event.ScanCode != 0 && !MCKeyBindings::IsModifierKey(event.ScanCode))
        {
            // Port-only: the key the dialog asks for.
            if (KeyTaker(MCKeyBindings::FromKeyEvent(event.ScanCode, event.ShiftKey != 0, event.CtrlKey != 0,
                                                     event.AltKey != 0)))
            {
                Deactivate(0);
            }

            return;
        }
    }
    else if (event.Type == MCGuiEventType::Timer)
    {
        // Timed out.
        Deactivate(TimeoutResult);
    }

    // While grabbed, clicks go to the child under the mouse.
    if (GuiSystem().Grabs(*this))
    {
        std::shared_ptr<MCGuiObject> target = FindObject(event.X, event.Y);

        if (target != nullptr && target.get() != this)
        {
            target->HandleEvent(event);
            return;
        }
    }

    MCGuiObject::HandleEvent(event);
}

auto MCReusableDialog::WordOf(const MCLogButton& button) -> std::string
{
    constexpr std::string_view CancelPicture = "bh_cancl";
    std::string name = button.UpPictureName;
    std::ranges::transform(name, name.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return name.starts_with(CancelPicture) ? "Cancel" : "OK";
}

auto MCReusableDialog::IsShowing() -> bool
{
    return MCLogObject::IsShowing() || StillSaid();
}

auto MCReusableDialog::StillSaid() const -> bool
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

auto MCReusableDialog::SayOverRemade() -> bool
{
    // Port-only (SPEC-menus.md's stage 9): whenever the menus run, over a remade screen or not; the 1998 box only where
    // they can't start.
    if (OldBoxOnly() || !MCRmlFlow::Menus().has_value())
    {
        return false;
    }

    // Its buttons as Enter and Escape press them here (HandleEvent): OK, then the dialog closed with OK's result; CANCEL,
    // or a message's one button for Escape, with 0.
    const std::weak_ptr<MCGuiObject> self = weak_from_this();
    const auto answer = [self](bool ok)
    {
        const std::shared_ptr<MCGuiObject> object = self.lock();

        if (object == nullptr)
        {
            return;
        }

        auto& dialog = static_cast<MCReusableDialog&>(*object);
        dialog._SaidOverRemade = false;
        const bool two = dialog.TwoButton;
        (ok ? *dialog.OkButton : *dialog.CancelButton).RunCallback();
        dialog.Deactivate(ok && two ? -1 : 0);
    };

    _Said = MCRmlDialogs::AsSentence(Text);
    MCRmlDialog::MCSay say{.Text = _Said,
                           .Main = OkButton->Disabled ? std::string() : WordOf(*OkButton),
                           .Other = TwoButton ? WordOf(*CancelButton) : std::string(),
                           .OnMain = [answer] { answer(true); },
                           .OnOther = [answer] { answer(false); }};

    // Through a change to a 1998 screen with no remade one after it, it shows the old way, as the original's stayed up.
    say.OnLost = [self]
    {
        const std::shared_ptr<MCGuiObject> object = self.lock();

        if (object == nullptr)
        {
            return;
        }

        auto& dialog = static_cast<MCReusableDialog&>(*object);

        if (std::exchange(dialog._SaidOverRemade, false))
        {
            dialog.ShowOld();
        }
    };

    if (!MCRmlDialogs::Show(std::move(say)))
    {
        return false;
    }

    _SaidOverRemade = true;
    ShowGuiWindow(false);

    if (Timeout > 0)
    {
        GuiSystem().AddTimer(weak_from_this(), TimeoutTimer, Timeout, 0, 0, false);
    }

    return true;
}

auto MCReusableDialog::OldBoxOnly() -> bool&
{
    static bool only = false;
    return only;
}

auto MCReusableDialog::Activate() -> void
{
    // Port-only (SPEC-menus.md's stage 4): over a remade screen, the remade dialog says it (a key asked for, it passes
    // here).
    if (SayOverRemade())
    {
        return;
    }

    _SaidOverRemade = false;
    ShowOld();

    if (Timeout > 0)
    {
        GuiSystem().AddTimer(weak_from_this(), TimeoutTimer, Timeout, 0, 0, false);
    }
}

auto MCReusableDialog::ShowOld() -> void
{
    MCRmlDialogs::NoteOldBox(Text);
    GuiSystem().Grab(weak_from_this());
    // The original painted the dialog afresh: a press left on its buttons was gone.
    OkButton->PressedDown = false;
    CancelButton->PressedDown = false;
    // Port-only (SPEC-menus.md's stage 1): centred on what the window shows (the menus' frame: (320, 240) as the
    // original's), on its parent.
    const SDL_Rect shown = GuiSystem().ShownView();
    const std::shared_ptr<MCGuiObject> parent = Parent.lock();
    MoveTo(shown.x + shown.w / 2 - (parent != nullptr ? parent->GlobalX() : 0) - Width() / 2,
           shown.y + shown.h / 2 - (parent != nullptr ? parent->GlobalY() : 0) - Height() / 2, false);
    ShowGuiWindow(true);
}

auto MCReusableDialog::Deactivate(int32_t dialogResult) -> void
{
    // Port-only (SPEC-menus.md's stage 4): said over the remade screens, it grabbed nothing; closed by its timeout (or
    // by its owner), the remade dialog closes too.
    if (_SaidOverRemade)
    {
        if (StillSaid())
        {
            MCRmlDialogs::Close();
        }

        _SaidOverRemade = false;
    }
    else
    {
        GuiSystem().Release();
    }

    ShowGuiWindow(false);
    // Port-only (SPEC-menus.md's stage 3): a wait said over a remade screen in this dialog's place ends with it.
    MCRmlDialogs::EndWait();

    // Port-only: the keyboard a dialog asking for a key held (KeyTaker) goes back.
    if (GuiSystem().HasText(*this))
    {
        GuiSystem().ReleaseText();
    }

    // The original also skipped a callback pointer IsBadReadPtr rejected.
    if (Callback)
    {
        Callback(dialogResult);
    }

    if (KeepCallbacks)
    {
        KeepCallbacks = false;
        return;
    }

    Callback = nullptr;
    KeyTaker = nullptr;
    OkButton->Callback().SetExec(nullptr);
    CancelButton->Callback().SetExec(nullptr);
    GuiSystem().RemoveTimer(*this, TimeoutTimer);
    Timeout = 0;
    TimeoutResult = 0;
}

auto MCReusableDialog::SetText(std::string_view newText) -> void
{
    Text = newText.substr(0, newText.find('\0'));

    // Count the wrapped lines; each middle piece holds two.
    int32_t lines = 0;

    for (std::string_view rest : Paragraphs(Text))
    {
        auto fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(rest, WrapWidth(), true));
        lines++;

        while (fit >= 1 && fit < rest.size())
        {
            rest = rest.substr(fit + 1);
            lines++;
            fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(rest, WrapWidth(), true));
        }
    }

    NumMiddlePieces = (lines + 1) / 2;
    Resize(Width(), MiddlePiece->Height() * NumMiddlePieces + BottomPiece->Height() + TopPiece->Height());
    SetTwoButton(TwoButton);
}

auto MCReusableDialog::SetTwoButton(bool twoButtons) -> void
{
    TwoButton = twoButtons;
    const int32_t buttonY = Height() - 0x17;
    // Port: OK shows again after a waiting message (ShowWaitingDialog), which hides the buttons: the message that
    // followed one (COULDN'T JOIN THE ROOM AGAIN...) had nothing to click, only Return closing it.
    OkButton->ShowGuiWindow(true);

    if (TwoButton)
    {
        CancelButton->ShowGuiWindow(true);
        CancelButton->MoveTo(0x68, buttonY, false);
        OkButton->MoveTo(0x23, buttonY, false);
        return;
    }

    CancelButton->ShowGuiWindow(false);
    OkButton->MoveTo(0x68, buttonY, false);
}

// MCRefitDialog

auto MCRefitDialog::SetText(std::string_view newText) -> void
{
    Text = newText.substr(0, newText.find('\0'));
    // The text is a comma-separated list, one item per line, between six lines of framing text.
    const auto lines = static_cast<int32_t>(7 + std::ranges::count(Text, ','));
    NumMiddlePieces = lines / 2;
    NumItems = lines - 6;
    Resize(Width(), MiddlePiece->Height() * NumMiddlePieces + BottomPiece->Height() + TopPiece->Height());
    MCReusableDialog::SetTwoButton(TwoButton);
}

auto MCRefitDialog::Draw() -> void
{
    DrawFrame();
    int32_t lineY = WrapText(LoadGameString(0x54, 0xfe), TopPiece->Height() + 2);
    lineY += TextLineHeight();

    // One item per line, cut at the commas; the last item takes the rest.
    std::string_view items = Text;

    for (int32_t i = NumItems; i > 0; i--)
    {
        const size_t comma = items.find(',');
        MedBlueFont->WriteString(_Port->Frame(), 0x14, lineY, items.substr(0, comma));
        lineY += TextLineHeight();

        if (comma != std::string_view::npos)
        {
            items = items.substr(comma + 1);
        }
    }

    WrapText(LoadGameString(0x62, 0xfe), lineY + TextLineHeight());

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].Lock(); child != nullptr)
        {
            DrawChild(*child);
        }
    }
}

auto MCRefitDialog::WrapText(std::string_view text, int32_t yPos) -> int32_t
{
    text = text.substr(0, text.find('\0'));
    auto fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(text, WrapWidth(), true));

    if (fit == text.size())
    {
        MedBlueFont->WriteString(_Port->Frame(), 0xc, yPos, text);
        return yPos;
    }

    // Each piece ends where the font says the words fit; the space there is skipped. The last piece ends at the end
    // of the text (OB-075: the original wrote over the end and read on past it).
    while (fit > 0)
    {
        MedBlueFont->WriteString(_Port->Frame(), 0xc, yPos, text.substr(0, fit));
        yPos += TextLineHeight();

        if (fit >= text.size())
        {
            break;
        }

        text = text.substr(fit + 1);
        fit = static_cast<size_t>(MedBlueFont->CharactersToWidth(text, WrapWidth(), true));
    }

    return yPos;
}
