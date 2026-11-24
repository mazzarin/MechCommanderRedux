#include "stdafx.h"
#include "ui/MCRmlDialog.h"
#include "platform/MCInput.h"
#include "iface/MCKeyBindings.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/ElementDocument.h>

namespace
{
    constexpr const char* DialogDocument = "dialog.rml";
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

    MCGuiHandle<MCRmlDialog>& Kept()
    {
        static MCGuiHandle<MCRmlDialog> kept;
        return kept;
    }

    /// <summary>What the dialogs belong to when they don't say (MCRmlDialogs::SetPlace).</summary>
    std::string& Place()
    {
        static std::string place;
        return place;
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

    if (!_Say.Main.empty())
    {
        Keys.push_back({EnterKey, _Say.Main});
    }

    if (!_Say.Other.empty())
    {
        Keys.push_back({EscapeKey, _Say.Other});
    }
}

void MCRmlDialog::Say(MCSay say)
{
    // The say before replaced, not hidden by a change of screens; a newer say wins over one waiting to come back.
    _Standing = false;
    _Say = std::move(say);
    Hide();
    Displaced().reset();
    _Standing = true;

    if (!Document().has_value())
    {
        if (const auto opened = Open(); !opened)
        {
            SDL_Log("Menus: the dialog can't open: %s", opened.error().c_str());
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
    }
}

MCGuiHandle<MCRmlDialog> MCRmlDialogs::Dialog()
{
    if (Kept())
    {
        return Kept();
    }

    const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

    if (!menus.has_value())
    {
        return {};
    }

    Kept() = menus->get().Menus().CreateChild<MCRmlDialog>([](MCRmlDialog&) {});
    return Kept();
}

bool MCRmlDialogs::Show(MCRmlDialog::MCSay say)
{
    if (!MCRmlFlow::Showing())
    {
        return false;
    }

    const MCGuiHandle<MCRmlDialog> dialog = Dialog();

    if (!dialog)
    {
        return false;
    }

    if (say.Eyebrow.empty())
    {
        say.Eyebrow = Place();
    }

    SDL_Log("Menus: the dialog says: %s", (say.Text.empty() ? say.Heading : say.Text).c_str());
    dialog->Say(std::move(say));
    return true;
}

bool MCRmlDialogs::TakeKey(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat || !Shown() || Kept()->Said().TakeKey == nullptr)
    {
        return false;
    }

    // Enter and Escape answer it, as any dialog's; a modifier alone is part of the next key.
    const SDL_Scancode scancode = event.key.scancode;
    const MCInput::MCScanCode scan = MCInput::Win32ScanCode(scancode);
    constexpr int16_t Extended = 0x100;
    const auto code = static_cast<int16_t>(scan.Code | (scan.Extended ? Extended : 0));

    if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER || scancode == SDL_SCANCODE_ESCAPE ||
        scan.Code == 0 || MCKeyBindings::IsModifierKey(code))
    {
        return false;
    }

    // Copied: taking it may say something new in the dialog.
    const std::function<void(uint32_t)> take = Kept()->Said().TakeKey;
    take(MCKeyBindings::FromKeyEvent(code, (event.key.mod & SDL_KMOD_SHIFT) != 0, (event.key.mod & SDL_KMOD_CTRL) != 0,
                                     (event.key.mod & SDL_KMOD_ALT) != 0));
    return true;
}

void MCRmlDialogs::SetPlace(std::string place)
{
    Place() = std::move(place);
}

void MCRmlDialogs::Close()
{
    Displaced().reset();

    if (Kept())
    {
        Kept()->End();
    }
}

bool MCRmlDialogs::Shown()
{
    return Kept() && Kept()->Shown();
}

void MCRmlDialogs::EndWait()
{
    const auto isWait = [](const MCRmlDialog::MCSay& say)
    { return say.Wait || (say.Main.empty() && say.Other.empty()); };

    if (Displaced().has_value() && isWait(*Displaced()))
    {
        Displaced().reset();
    }

    if (Shown() && isWait(Kept()->Said()))
    {
        Kept()->End();
    }
}

bool MCRmlDialogs::Returning(std::optional<std::string_view> text)
{
    return Displaced().has_value() && (!text.has_value() || Displaced()->Text == *text);
}

void MCRmlDialogs::Tend()
{
    if (!Displaced().has_value())
    {
        return;
    }

    MCRmlDialog::MCSay say = *std::exchange(Displaced(), std::nullopt);
    const std::function<void()> lost = say.OnLost;

    if (Show(std::move(say)))
    {
        return;
    }

    SDL_Log("Menus: a dialog's say stood through a change to a 1998 screen: its old dialog shows it");

    if (lost != nullptr)
    {
        lost();
    }
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

bool MCRmlDialogs::Message(std::string_view text, std::function<void()> then)
{
    return Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .OnMain = std::move(then)});
}

bool MCRmlDialogs::Waiting(std::string_view text)
{
    return Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .Main = {}, .Wait = true});
}
