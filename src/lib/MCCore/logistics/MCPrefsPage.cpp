#include "stdafx.h"
#include "logistics/MCPrefsPage.h"
#include "gui/MCGuiGlobals.h"
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

MCGuiHandle<MCLogGroupBox> AddPrefsPage(MCGuiObject& screen, std::string_view title,
                                        std::span<const MCPageButton> buttons, void (*done)(),
                                        MCOptionalRef<std::vector<MCGuiHandle<MCLogTextButton>>> made)
{
    const MCGuiHandle<MCLogGroupBox> page = screen.CreateChild<MCLogGroupBox>(
        [](MCLogGroupBox& box)
        {
            box.Init(PageLeft, PageTop, PageWidth, PageHeight);
            box.SetTransparent(true);
            box.ShowGuiWindow(false);
        },
        PageOutlineColor, PageFillColor);

    page->CreateChild<MCLogLabel>(
        [](MCLogLabel& label)
        {
            label.InitAt(TitleLeft, TitleTop);
            label.ShowGuiWindow(true);
        },
        title, LgWhiteFont);

    // The buttons take the art's size; they are placed once made.
    int32_t left = ButtonInset;
    const auto place = [&](std::string_view word, void (*clicked)(), bool atRight)
    {
        return page->CreateChild<MCLogTextButton>(
            [&](MCLogTextButton& button)
            {
                button.InitWithLabel(0, 0, word, clicked);
                const int32_t top = PageHeight - ButtonInset - button.Height();
                button.MoveTo(atRight ? PageWidth - ButtonInset - button.Width() : left, top);
                button.ShowGuiWindow(true);
                left += button.Width() + ButtonInset;
            });
    };

    for (const MCPageButton& button : buttons)
    {
        MCGuiHandle<MCLogTextButton> placed = place(button.Label, button.Clicked, false);

        if (made)
        {
            made->get().push_back(std::move(placed));
        }
    }

    place("DONE", done, true);
    return page;
}

void AddPageButton(MCGuiObject& parent, std::string_view label, int32_t left, int32_t top, void (*clicked)())
{
    parent.CreateChild<MCLogTextButton>(
        [&](MCLogTextButton& button)
        {
            button.InitWithLabel(left, top, label, clicked);
            button.ShowGuiWindow(true);
        });
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
