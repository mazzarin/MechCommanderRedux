#pragma once

#include "gui/MCGuiTitleWindow.h"

/// <summary>
/// The layout of a bar of buttons: rows (or columns when not horizontal) of <see cref="MaxLength"/> buttons, each
/// <see cref="ButtonWidth"/> x <see cref="ButtonHeight"/>; the bar sizes itself to them.
/// </summary>
struct MCGuiButtonLayout
{
    /// <summary>Buttons to a row (or column when not horizontal).</summary>
    int32_t MaxLength = 0;
    /// <summary>Whether the buttons fill rows (else columns).</summary>
    bool Horizontal = true;
    /// <summary>The size of a button's place.</summary>
    int32_t ButtonWidth = 32;
    int32_t ButtonHeight = 32;

    /// <summary>Sizes <paramref name="bar"/> to <paramref name="buttons"/> and lays them out.</summary>
    template <typename TButton> void LayOut(MCGuiObject& bar, const std::vector<MCGuiHandle<TButton>>& buttons) const;
};

/// <summary>A title window (without the close button) holding tool buttons in rows; the bar sizes itself to them.</summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> (<c>aToolBar</c>).</remarks>
class MCGuiToolBar
    : public MCGuiTitleWindow
    , public MCGuiButtonLayout
{
public:
    /// <summary>A title window without the close button, 8 buttons to a row.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>
    /// Makes a <typeparamref name="TButton"/> on the bar, set up by <paramref name="setup"/> (its <c>Init</c>), adds it
    /// at the end and lays the bar out.
    /// </summary>
    /// <returns>A handle to the button (the bar owns it).</returns>
    template <typename TButton = MCGuiToolButton, typename Setup>
        requires std::derived_from<TButton, MCGuiToolButton>
    MCGuiHandle<TButton> AddButton(Setup&& setup)
    {
        MCGuiHandle<TButton> button = CreateChild<TButton>(std::forward<Setup>(setup));
        Buttons.emplace_back(button);
        PlaceButtons();
        return button;
    }

    /// <summary>Removes and destroys button <paramref name="index"/>.</summary>
    /// <returns>Whether <paramref name="index"/> was a button.</returns>
    bool RemoveButton(int32_t index);
    /// <returns>Button <paramref name="index"/>, or null.</returns>
    std::shared_ptr<MCGuiToolButton> GetButton(int32_t index);
    /// <summary>Whether button <paramref name="index"/> is pushed (false for no button).</summary>
    bool IsPushed(int32_t index);
    /// <summary>Lays the buttons out in rows (<paramref name="on"/>) or columns.</summary>
    void SetHorizontal(bool on);
    /// <summary>Sets the buttons to a row (or column) and lays them out.</summary>
    void SetMaxLength(int32_t length);
    /// <summary>Sizes the bar to the buttons and lays them out.</summary>
    void PlaceButtons();
    /// <summary>Sets the size of a button's place (laid out by the next <see cref="PlaceButtons"/>).</summary>
    void SetButtonSize(int32_t width, int32_t height);
    /// <summary>The number of buttons.</summary>
    int32_t NumButtons() const { return static_cast<int32_t>(Buttons.size()); }

    /// <summary>The buttons (the bar's children), in layout order.</summary>
    std::vector<MCGuiHandle<MCGuiToolButton>> Buttons;
};

/// <summary>A frameless bar of buttons, laid out like <see cref="MCGuiToolBar"/>.</summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> (<c>aWindowBar</c>).</remarks>
class MCGuiWindowBar
    : public MCGuiObject
    , public MCGuiButtonLayout
{
public:
    /// <summary>Port: draws itself each frame (only its background; the buttons draw themselves).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Back colour 0, 8 buttons to a row.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>
    /// Makes a <typeparamref name="TButton"/> on the bar, set up by <paramref name="setup"/> (its <c>Init</c>), puts
    /// it before button <paramref name="index"/> and lays the bar out. Nothing is made when <paramref name="index"/> is
    /// past the button count.
    /// </summary>
    /// <returns>A handle to the button (the bar owns it), or an empty one.</returns>
    template <typename TButton = MCGuiButton, typename Setup>
        requires std::derived_from<TButton, MCGuiButton>
    MCGuiHandle<TButton> InsertButton(int32_t index, Setup&& setup)
    {
        if (index < 0 || index > NumButtons())
        {
            return {};
        }

        MCGuiHandle<TButton> button = CreateChild<TButton>(std::forward<Setup>(setup));
        Buttons.emplace(Buttons.begin() + index, button);
        PlaceButtons();
        return button;
    }

    /// <summary>Like <see cref="InsertButton"/>, at the end.</summary>
    template <typename TButton = MCGuiButton, typename Setup>
        requires std::derived_from<TButton, MCGuiButton>
    MCGuiHandle<TButton> AddButton(Setup&& setup)
    {
        return InsertButton<TButton>(NumButtons(), std::forward<Setup>(setup));
    }

    /// <summary>Removes and destroys button <paramref name="index"/>.</summary>
    /// <returns>Whether <paramref name="index"/> was a button.</returns>
    bool RemoveButton(int32_t index);
    /// <returns>Button <paramref name="index"/>, or null.</returns>
    std::shared_ptr<MCGuiButton> GetButton(int32_t index);
    /// <summary>Lays the buttons out in rows (<paramref name="on"/>) or columns.</summary>
    void SetHorizontal(bool on);
    /// <summary>Sets the buttons to a row (or column) and lays them out.</summary>
    void SetMaxLength(int32_t length);
    /// <summary>Sizes the bar to the buttons and lays them out.</summary>
    void PlaceButtons();
    /// <summary>Sets the size of a button's place (laid out by the next <see cref="PlaceButtons"/>).</summary>
    void SetButtonSize(int32_t width, int32_t height);
    /// <summary>The number of buttons.</summary>
    int32_t NumButtons() const { return static_cast<int32_t>(Buttons.size()); }

    /// <summary>The buttons (the bar's children), in layout order.</summary>
    std::vector<MCGuiHandle<MCGuiButton>> Buttons;
};
