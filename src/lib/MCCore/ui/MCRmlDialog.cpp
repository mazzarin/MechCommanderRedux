#include "stdafx.h"
#include "ui/MCRmlDialog.h"
#include "gui/MCScreenChange.h"
#include "iface/MCKeyBindings.h"
#include "platform/MCInput.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/ElementDocument.h>

namespace
{
    constexpr const char* DialogDocument = "dialog.rml";
    /// <summary>The game's mark on an extended key's scan code (as Preferences' CONTROLS reads a key).</summary>
    constexpr int16_t ExtendedScanCode = 0x100;
    constexpr const char* EnterKey = "ENTER";
    constexpr const char* EscapeKey = "ESC";
    /// <summary>The look's class for a titlebar with no title (the eyebrow over the sentence alone).</summary>
    constexpr const char* BareClass = "bare";
    constexpr const char* NoDisplay = "none";
    constexpr const char* Display = "display";

    /// <summary>An element of the dialog's, shown or not.</summary>
    void ShowElement(Rml::Element& element, bool show)
    {
        if (show)
        {
            element.RemoveProperty(Display);
        }
        else
        {
            element.SetProperty(Display, NoDisplay);
        }
    }

    /// <summary>The layers' dialogs (MCRmlDialogs::Dialog): the menus', then the mission's.</summary>
    constexpr size_t Layers = 2;

    std::array<MCGuiHandle<MCRmlDialog>, Layers>& KeptOnLayers()
    {
        static std::array<MCGuiHandle<MCRmlDialog>, Layers> kept;
        return kept;
    }

    MCGuiHandle<MCRmlDialog>& Kept(MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus)
    {
        return KeptOnLayers()[on == MCRmlFlow::MCOn::Mission ? 1 : 0];
    }

    /// <summary>The 1998 boxes shown while the menus ran (MCRmlDialogs::NoteOldBox).</summary>
    size_t& OldBoxCount()
    {
        static size_t count = 0;
        return count;
    }

    /// <summary>What the dialogs belong to when they don't say (MCRmlDialogs::SetPlace).</summary>
    std::string& Place()
    {
        static std::string place;
        return place;
    }

    /// <summary>The layer the displaced say was said on (MCRmlDialogs::Tend says it there again).</summary>
    MCRmlFlow::MCOn& DisplacedOn()
    {
        static MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus;
        return on;
    }

    /// <summary>A say that stands, hidden by a change of screens, to say itself again (MCRmlDialogs::Tend).</summary>
    std::optional<MCRmlDialog::MCSay>& Displaced()
    {
        static std::optional<MCRmlDialog::MCSay> displaced;
        return displaced;
    }
}

MCRmlDialog::MCRmlDialog()
{
    SetDocument(DialogDocument);
    Modal = true;
    Back = [this]
    {
        // Escape: the way back, or the answer when it's the only button; nothing during a wait no one can call off.
        if (!_Say.Other.empty())
        {
            PressOther();
        }
        else if (!_Say.Main.empty())
        {
            PressMain();
        }
    };
}

std::string MCRmlDialog::Escape(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());

    for (const char c : text)
    {
        switch (c)
        {
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '&':
                escaped += "&amp;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped += c;
                break;
        }
    }

    return escaped;
}

void MCRmlDialog::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("main", [this] { PressMain(); });
    OnClick("other", [this] { PressOther(); });
    Fill();
}

void MCRmlDialog::Fill()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return;
    }

    Rml::ElementDocument& page = document->get();
    const auto set = [&page](const char* id, std::string_view text)
    {
        if (Rml::Element* element = page.GetElementById(id); element != nullptr)
        {
            element->SetInnerRML(Escape(text));
            ShowElement(*element, !text.empty());
        }
    };

    set("eyebrow", _Say.Eyebrow);
    set("heading", _Say.Heading);
    set("say", _Say.Text);
    set("main", _Say.Main);
    set("other", _Say.Other);

    if (Rml::Element* titlebar = page.GetElementById("titlebar"); titlebar != nullptr)
    {
        titlebar->SetClass(BareClass, _Say.Heading.empty());
    }

    if (Rml::Element* foot = page.GetElementById("foot"); foot != nullptr)
    {
        ShowElement(*foot, !_Say.Main.empty() || !_Say.Other.empty());
    }

    Keys.clear();

    // One that leaves opens on its way back, which Enter would press: Enter isn't its answer's key.
    if (!_Say.Main.empty() && !(_Say.Leaves && !_Say.Other.empty()))
    {
        Keys.push_back({EnterKey, _Say.Main});
    }

    if (!_Say.Other.empty())
    {
        Keys.push_back({EscapeKey, _Say.Other});
    }
}

MCOptionalRef<Rml::Element> MCRmlDialog::FirstFocus(Rml::ElementDocument& document)
{
    if (_Say.Leaves && !_Say.Other.empty())
    {
        if (Rml::Element* other = document.GetElementById("other"); other != nullptr)
        {
            return *other;
        }
    }

    return MCRmlScreen::FirstFocus(document);
}

void MCRmlDialog::Say(MCSay say)
{
    // The say before replaced, not hidden by a change of screens; a newer say wins over one waiting to come back on
    // this layer (the other layer's comes back on its own).
    _Standing = false;
    _Say = std::move(say);
    Hide();

    if (DisplacedOn() == _On)
    {
        Displaced().reset();
    }

    _Standing = true;

    if (!Document().has_value())
    {
        // Port-only (SPEC-menus.md's stage 9): no 1998 box says it instead, so a dialog that can't open stops the game.
        if (const auto opened = Open(); !opened)
        {
            MCRmlFlow::Broken(std::format("the dialog can't open: {}", opened.error()));
        }

        return;
    }

    Fill();
    Show();
}

void MCRmlDialog::PressMain()
{
    // Copied: the answer may say something new in this dialog, or close the screen under it.
    const std::function<void()> then = _Say.OnMain;
    End();

    if (then != nullptr)
    {
        then();
    }
}

void MCRmlDialog::PressOther()
{
    const std::function<void()> then = _Say.OnOther;
    End();

    if (then != nullptr)
    {
        then();
    }
}

void MCRmlDialog::End()
{
    _Standing = false;
    Hide();
}

void MCRmlDialog::OnHide()
{
    // Hidden unanswered by a change of screens (the remade screens hidden for a 1998 one, which the next may follow): a
    // say that stands comes back at MCRmlDialogs::Tend.
    if (std::exchange(_Standing, false) && _Say.Stands())
    {
        Displaced() = _Say;
        DisplacedOn() = _On;
    }
}

bool MCRmlDialog::TakesKeys() const
{
    return Shown() && _Say.TakesKey != nullptr;
}

bool MCRmlDialog::OnRawKey(const SDL_KeyboardEvent& key)
{
    // Its buttons' keys are RmlUi's (Enter its answer, Escape its answer or way back), and keys let go or held.
    if (!key.down || key.repeat || key.scancode == SDL_SCANCODE_RETURN || key.scancode == SDL_SCANCODE_KP_ENTER ||
        key.scancode == SDL_SCANCODE_ESCAPE)
    {
        return false;
    }

    const MCInput::MCScanCode scan = MCInput::Win32ScanCode(key.scancode);
    const auto code = static_cast<int16_t>(scan.Code | (scan.Extended ? ExtendedScanCode : 0));

    // A key without a code (the game couldn't name it), or a modifier alone, isn't offered (as 1998's dialog's).
    if (code == 0 || MCKeyBindings::IsModifierKey(code))
    {
        return false;
    }

    // Copied: taking the key may close this dialog.
    const std::function<std::optional<std::string>(uint32_t)> take = _Say.TakesKey;
    const std::optional<std::string> refused = take(MCKeyBindings::FromKeyEvent(
        code, (key.mod & SDL_KMOD_SHIFT) != 0, (key.mod & SDL_KMOD_CTRL) != 0, (key.mod & SDL_KMOD_ALT) != 0));

    if (!refused.has_value())
    {
        PressMain();
        return true;
    }

    _Say.Text = MCRmlDialogs::AsSentence(*refused);
    SDL_Log("Menus: the dialog says: %s", _Say.Text.c_str());
    Fill();
    return true;
}

MCGuiHandle<MCRmlDialog> MCRmlDialogs::Dialog(MCRmlFlow::MCOn on)
{
    MCGuiHandle<MCRmlDialog>& kept = Kept(on);

    if (kept)
    {
        return kept;
    }

    MCRmlMenus& menus = MCRmlFlow::Menus();
    MCRmlLayer& layer = on == MCRmlFlow::MCOn::Mission ? menus.Mission() : menus.Menus();
    kept = layer.CreateChild<MCRmlDialog>([on](MCRmlDialog& made) { made.SetLayer(on); });
    return kept;
}

void MCRmlDialogs::Show(MCRmlDialog::MCSay say, MCRmlFlow::MCOn on)
{
    const MCGuiHandle<MCRmlDialog> dialog = Dialog(on);

    // Port-only (SPEC-menus.md's stage 9): with no 1998 box to say it instead, a dialog not made stops the game.
    if (!dialog)
    {
        MCRmlFlow::Broken(std::format("the dialog can't say: {}", say.Text.empty() ? say.Heading : say.Text));
    }

    if (say.Eyebrow.empty())
    {
        say.Eyebrow = Place();
    }

    SDL_Log("Menus: the dialog says: %s", (say.Text.empty() ? say.Heading : say.Text).c_str());
    dialog->Say(std::move(say));
}

void MCRmlDialogs::SetPlace(std::string place)
{
    Place() = std::move(place);
}

void MCRmlDialogs::Close()
{
    Displaced().reset();

    for (const MCGuiHandle<MCRmlDialog>& kept : KeptOnLayers())
    {
        if (kept)
        {
            kept->End();
        }
    }
}

void MCRmlDialogs::Drop(MCRmlFlow::MCOn on)
{
    if (Displaced().has_value() && DisplacedOn() == on)
    {
        Displaced().reset();
    }

    if (const MCGuiHandle<MCRmlDialog>& kept = Kept(on); kept)
    {
        kept->End();
    }
}

bool MCRmlDialogs::Shown()
{
    return std::ranges::any_of(KeptOnLayers(),
                               [](const MCGuiHandle<MCRmlDialog>& kept) { return kept && kept->Shown(); });
}

void MCRmlDialogs::NoteOldBox(std::string_view text)
{
    SDL_Log("Dialogs: a 1998 box shows: %.*s", static_cast<int>(text.size()), text.data());
    ++OldBoxCount();
}

size_t MCRmlDialogs::OldBoxes()
{
    return OldBoxCount();
}

void MCRmlDialogs::EndWait()
{
    const auto isWait = [](const MCRmlDialog::MCSay& say)
    { return say.Wait || (say.Main.empty() && say.Other.empty()); };

    if (Displaced().has_value() && isWait(*Displaced()))
    {
        Displaced().reset();
    }

    for (const MCGuiHandle<MCRmlDialog>& kept : KeptOnLayers())
    {
        if (kept && kept->Shown() && isWait(kept->Said()))
        {
            kept->End();
        }
    }
}

bool MCRmlDialogs::Returning(std::optional<std::string_view> text)
{
    return Displaced().has_value() && (!text.has_value() || Displaced()->Text == *text);
}

void MCRmlDialogs::Tend()
{
    // Port-only (stage 9): back once the change is done, over the screen it shows (not over its fade).
    if (!Displaced().has_value() || MCScreenChange::Running())
    {
        return;
    }

    Show(*std::exchange(Displaced(), std::nullopt), DisplacedOn());
}

std::string MCRmlDialogs::SentenceCase(std::string_view text)
{
    std::string sentence;
    sentence.reserve(text.size());
    bool start = true;

    for (const char c : text)
    {
        const auto letter = static_cast<unsigned char>(c);
        sentence += static_cast<char>(start ? std::toupper(letter) : std::tolower(letter));

        if (std::isalpha(letter) != 0)
        {
            start = false;
        }
        else if (c == '.' || c == '?' || c == '\n')
        {
            start = true;
        }
    }

    return sentence;
}

std::string MCRmlDialogs::AsSentence(std::string_view text)
{
    const bool capitals =
        std::ranges::none_of(text, [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; });
    return capitals ? SentenceCase(text) : std::string(text);
}

void MCRmlDialogs::Message(std::string_view text, std::function<void()> then)
{
    Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .OnMain = std::move(then)});
}

void MCRmlDialogs::AskKey(std::string_view text, std::function<std::optional<std::string>(uint32_t key)> take,
                          std::function<void()> then)
{
    Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .OnMain = std::move(then), .TakesKey = std::move(take)});
}

void MCRmlDialogs::Waiting(std::string_view text)
{
    Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .Main = {}, .Wait = true});
}
