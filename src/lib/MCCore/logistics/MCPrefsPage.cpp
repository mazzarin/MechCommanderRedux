#include "stdafx.h"
#include "logistics/MCPrefsPage.h"
#include "gui/MCGuiGlobals.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogTextButton.h"

namespace
{
    /// <summary>A page's place, in the screen's pixels.</summary>
    constexpr int32_t PageLeft = 16;
    constexpr int32_t PageTop = 93;
    constexpr int32_t PageWidth = 608;
    constexpr int32_t PageHeight = 375;

    /// <summary>A page's outline and fill: the preferences panel's.</summary>
    constexpr uint8_t PageOutlineColor = 0x18;
    constexpr uint8_t PageFillColor = 0x10;

    /// <summary>The title, top left.</summary>
    constexpr int32_t TitleLeft = 12;
    constexpr int32_t TitleTop = 8;

    /// <summary>The buttons along the bottom: 10 pixels in from its edges and apart.</summary>
    constexpr int32_t ButtonInset = 10;
}

MCLogGroupBox& AddPrefsPage(MCGenericScreen& screen, std::string_view title, std::span<const MCPageButton> buttons,
                            void (*done)())
{
    auto newPage = MCMakeGui<MCLogGroupBox>(PageOutlineColor, PageFillColor);
    newPage->Init(PageLeft, PageTop, PageWidth, PageHeight);
    newPage->SetTransparent(true);
    newPage->ShowGuiWindow(false);
    MCLogGroupBox& page = screen.AdoptChild(std::move(newPage));

    auto label = MCMakeGui<MCLogLabel>(title, LgWhiteFont);
    label->InitAt(TitleLeft, TitleTop);
    label->ShowGuiWindow(true);
    screen.AdoptChildOf(page, std::move(label));

    // The buttons take the art's size; they are placed once made.
    int32_t left = ButtonInset;
    const auto place = [&](std::string_view word, void (*clicked)(), bool atRight)
    {
        auto button = MCMakeGui<MCLogTextButton>();
        button->InitWithLabel(0, 0, word, clicked);
        const int32_t top = PageHeight - ButtonInset - button->Height();
        button->MoveTo(atRight ? PageWidth - ButtonInset - button->Width() : left, top);
        button->ShowGuiWindow(true);
        left += button->Width() + ButtonInset;
        screen.AdoptChildOf(page, std::move(button));
    };

    for (const MCPageButton& button : buttons)
    {
        place(button.Label, button.Clicked, false);
    }

    place("DONE", done, true);
    return page;
}

void AddPageButton(MCGenericScreen& screen, MCGuiObject& parent, std::string_view label, int32_t left, int32_t top,
                   void (*clicked)())
{
    auto button = MCMakeGui<MCLogTextButton>();
    button->InitWithLabel(left, top, label, clicked);
    button->ShowGuiWindow(true);
    screen.AdoptChildOf(parent, std::move(button));
}

void ShowPrefsPage(const std::weak_ptr<MCGuiObject>& prefsPanel, const std::weak_ptr<MCGuiObject>& page, bool shown)
{
    if (const std::shared_ptr<MCGuiObject> prefs = prefsPanel.lock(); prefs != nullptr)
    {
        prefs->ShowGuiWindow(!shown);
    }

    if (const std::shared_ptr<MCGuiObject> panel = page.lock(); panel != nullptr)
    {
        panel->ShowGuiWindow(shown);
    }
}

bool PrefsPageShown(const std::weak_ptr<MCGuiObject>& page)
{
    const std::shared_ptr<MCGuiObject> panel = page.lock();
    return panel != nullptr && panel->IsShowing();
}
