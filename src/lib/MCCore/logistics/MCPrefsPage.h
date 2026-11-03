#pragma once

// Port-only: the preferences screen's pages (CONTROLS, HD OPTIONS), and the main menu's UPDATE page. A page is a panel covering the screen below the logo
// (the main menu's art and the preferences' art, as the preferences panel covers its art), shown in the preferences
// panel's place: its title at the top left, a list under it, buttons along its bottom (DONE at the right goes back).

class MCGenericScreen;
class MCGuiObject;
class MCLogGroupBox;
class MCLogTextButton;

/// <summary>The list's place on a page, from the page's corner, the outline included.</summary>
constexpr int32_t PageListLeft = 10;
constexpr int32_t PageListTop = 30;
constexpr int32_t PageListWidth = 588;
constexpr int32_t PageListHeight = 312;

/// <summary>A button along a page's bottom: its word and what a click does.</summary>
struct MCPageButton
{
    std::string_view Label;
    void (*Clicked)() = nullptr;
};

/// <summary>
/// Adds a hidden page titled <paramref name="title"/> to <paramref name="screen"/>, with <paramref name="buttons"/> from
/// its bottom left and DONE, running <paramref name="done"/>, at its bottom right; <paramref name="made"/>, when given,
/// gets the buttons made from <paramref name="buttons"/>, in their order.
/// </summary>
/// <returns>The page.</returns>
MCLogGroupBox& AddPrefsPage(MCGenericScreen& screen, std::string_view title, std::span<const MCPageButton> buttons,
                            void (*done)(), std::vector<std::weak_ptr<MCLogTextButton>>* made = nullptr);

/// <summary>Adds a button showing <paramref name="label"/> to <paramref name="parent"/> at (<paramref name="left"/>, <paramref name="top"/>); a click runs <paramref name="clicked"/>.</summary>
void AddPageButton(MCGenericScreen& screen, MCGuiObject& parent, std::string_view label, int32_t left, int32_t top,
                   void (*clicked)());

/// <summary>Shows <paramref name="page"/> in <paramref name="prefsPanel"/>'s place, or (not <paramref name="shown"/>) the preferences panel again.</summary>
void ShowPrefsPage(const std::weak_ptr<MCGuiObject>& prefsPanel, const std::weak_ptr<MCGuiObject>& page, bool shown);

/// <summary>Whether <paramref name="page"/> is showing.</summary>
bool PrefsPageShown(const std::weak_ptr<MCGuiObject>& page);
