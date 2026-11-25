#include "stdafx.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCMenuDialog.h"
#include "ui/MCRmlDialog.h"
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
    PlaceButtons();
    ShowGuiWindow(false);
    SetDepth(100);
    return 0;
}

auto MCReusableDialog::OnDestroy() -> void
{
    TopPiece.reset();
    MiddlePiece.reset();
    BottomPiece.reset();
    MCLogObject::OnDestroy();
}

auto MCReusableDialog::SetModel(std::weak_ptr<MCMenuDialog> model) -> void
{
    _Model = std::move(model);

    // Its buttons run the model's (a click runs its button, then closes the dialog: MCLogDialogButton).
    const auto runs = [this](bool ok)
    {
        return [model = _Model, ok]
        {
            if (const std::shared_ptr<MCMenuDialog> shown = model.lock(); shown != nullptr)
            {
                (ok ? shown->Ok : shown->Cancel).Run();
            }
        };
    };

    OkButton->Callback().SetExec(runs(true));
    CancelButton->Callback().SetExec(runs(false));
    Lay();
}

auto MCReusableDialog::ModelText() const -> std::string
{
    const std::shared_ptr<MCMenuDialog> model = _Model.lock();
    return model != nullptr ? model->Text : std::string();
}

auto MCReusableDialog::SyncButtons() -> void
{
    const std::shared_ptr<MCMenuDialog> model = _Model.lock();

    if (model == nullptr)
    {
        return;
    }

    const auto sync = [](MCLogDialogButton& button, const MCMenuDialog::MCButton& state, std::string& downArt)
    {
        if (button.UpPictureName != state.UpArt)
        {
            button.SetUpPicture(state.UpArt);
        }

        if (downArt != state.DownArt)
        {
            downArt = state.DownArt;
            button.SetDownPicture(state.DownArt);
        }

        button.Disabled = state.Disabled;

        if ((button.ShowWindow != 0) != state.Shown)
        {
            button.ShowGuiWindow(state.Shown);
        }
    };

    sync(*OkButton, model->Ok, _OkDownArt);
    sync(*CancelButton, model->Cancel, _CancelDownArt);
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
    SyncButtons();
    DrawFrame();

    // The text, word-wrapped to the box: each line ends where the font says the words fit, and the character there
    // (a space) is skipped.
    int32_t lineY = TopPiece->Height() + 2;
    const std::string text = ModelText();

    for (std::string_view rest : Paragraphs(text))
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
    const std::shared_ptr<MCMenuDialog> model = _Model.lock();

    if (event.Type == MCGuiEventType::KeyDown && model != nullptr)
    {
        if (event.Key == 0x0d)
        {
            model->Enter();
        }
        else if (event.Key == 0x1b)
        {
            model->Escape();
        }
        else if (model->KeyTaker && event.ScanCode != 0 && !MCKeyBindings::IsModifierKey(event.ScanCode))
        {
            // Port-only: the key the dialog asks for.
            model->TakeKey(MCKeyBindings::FromKeyEvent(event.ScanCode, event.ShiftKey != 0, event.CtrlKey != 0,
                                                       event.AltKey != 0));
            return;
        }
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

auto MCReusableDialog::IsShowing() -> bool
{
    const std::shared_ptr<MCMenuDialog> model = _Model.lock();
    return model != nullptr ? model->IsShowing() : ShownOld();
}

auto MCReusableDialog::Deactivate(int32_t result) -> void
{
    if (const std::shared_ptr<MCMenuDialog> model = _Model.lock(); model != nullptr)
    {
        model->Deactivate(result);
    }
}

auto MCReusableDialog::HideOld(bool release) -> void
{
    if (release)
    {
        GuiSystem().Release();
    }

    ShowGuiWindow(false);
}

auto MCReusableDialog::Lay() -> void
{
    // Count the wrapped lines; each middle piece holds two.
    int32_t lines = 0;
    const std::string text = ModelText();

    for (std::string_view rest : Paragraphs(text))
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
    PlaceButtons();
}

auto MCReusableDialog::PlaceButtons() -> void
{
    const std::shared_ptr<MCMenuDialog> model = _Model.lock();
    const bool two = model != nullptr && model->TwoButton;
    const int32_t buttonY = Height() - 0x17;
    SyncButtons();

    if (two)
    {
        CancelButton->MoveTo(0x68, buttonY, false);
        OkButton->MoveTo(0x23, buttonY, false);
        return;
    }

    if (model == nullptr)
    {
        CancelButton->ShowGuiWindow(false);
    }

    OkButton->MoveTo(0x68, buttonY, false);
}

// MCRefitDialog

auto MCRefitDialog::Lay() -> void
{
    // The text is a comma-separated list, one item per line, between six lines of framing text.
    const auto lines = static_cast<int32_t>(7 + std::ranges::count(ModelText(), ','));
    NumMiddlePieces = lines / 2;
    NumItems = lines - 6;
    Resize(Width(), MiddlePiece->Height() * NumMiddlePieces + BottomPiece->Height() + TopPiece->Height());
    PlaceButtons();
}

auto MCRefitDialog::Draw() -> void
{
    SyncButtons();
    DrawFrame();
    int32_t lineY = WrapText(LoadGameString(0x54, 0xfe), TopPiece->Height() + 2);
    lineY += TextLineHeight();

    // One item per line, cut at the commas; the last item takes the rest.
    const std::string text = ModelText();
    std::string_view items = text;

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
