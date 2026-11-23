#include "stdafx.h"
#include "ui/MCRmlDialog.h"
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
    _Say = std::move(say);
    Hide();

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
    Hide();

    if (then != nullptr)
    {
        then();
    }
}

void MCRmlDialog::PressOther()
{
    const std::function<void()> then = _Say.OnOther;
    Hide();

    if (then != nullptr)
    {
        then();
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

    SDL_Log("Menus: the dialog says: %s", say.Text.c_str());
    dialog->Say(std::move(say));
    return true;
}

void MCRmlDialogs::SetPlace(std::string place)
{
    Place() = std::move(place);
}

void MCRmlDialogs::Close()
{
    if (Kept())
    {
        Kept()->Hide();
    }
}

bool MCRmlDialogs::Shown()
{
    return Kept() && Kept()->Shown();
}

void MCRmlDialogs::EndWait()
{
    if (Shown() && Kept()->Said().Main.empty() && Kept()->Said().Other.empty())
    {
        Kept()->Hide();
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
    return Show(MCRmlDialog::MCSay{.Text = AsSentence(text), .Main = {}});
}
