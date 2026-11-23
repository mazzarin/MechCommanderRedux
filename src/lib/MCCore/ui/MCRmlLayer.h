#pragma once

#include "ui/MCRmlScreen.h"

namespace Rml
{
    class Context;
}

class MCRmlKit;

/// <summary>
/// Port-only (SPEC-menus.md, "Where the menus sit"): one of the menus' two RmlUi contexts over the window, with the
/// screens made on it. The menus' layer lays out in the menus' unit (one 1998 pixel, the window over
/// <c>min(width / 640, height / 480)</c>); the mission's, over a running mission (its results, its pause), in the
/// mission's INTERFACE SIZE. Its dp ratio is its unit: a style sheet's <c>dp</c> is one unit at any window size.
/// </summary>
/// <remarks>
/// The keys (SPEC-menus.md, "Without a mouse"): RmlUi moves the focus (Tab, the arrows by <c>nav</c>) and presses what
/// has it (Enter); the layer takes Escape to the top screen's <see cref="MCRmlScreen::Back"/>, Q and E to its tabs,
/// shows the focus's ring only after a key and takes it away when the mouse moves, and hands the screen the keys left
/// (<see cref="MCRmlScreen::OnKey"/>).
/// </remarks>
class MCRmlLayer
{
    struct Key
    {
        explicit Key() = default;
    };

public:
    /// <summary>Which of the two it is.</summary>
    enum class MCKind
    {
        /// <summary>The menus' own, out of a mission.</summary>
        Menus,
        /// <summary>Over a running mission, at INTERFACE SIZE.</summary>
        Mission
    };

    /// <summary>The layer's unit in window pixels for a window of a size (its dp ratio).</summary>
    using MCUnit = std::function<float(int32_t width, int32_t height)>;

    /// <summary>A layer over context <paramref name="context"/> (its owner's, MCRmlMenus).</summary>
    MCRmlLayer(Key, Rml::Context& context, MCKind kind, MCUnit unit, MCRmlKit& kit);
    ~MCRmlLayer();
    MCRmlLayer(const MCRmlLayer&) = delete;
    MCRmlLayer& operator=(const MCRmlLayer&) = delete;

    /// <summary>Made by the menus' owner alone.</summary>
    static std::unique_ptr<MCRmlLayer> Make(Rml::Context& context, MCKind kind, MCUnit unit, MCRmlKit& kit)
    {
        return std::make_unique<MCRmlLayer>(Key{}, context, kind, std::move(unit), kit);
    }

    /// <summary>
    /// Makes a <typeparamref name="T"/> from <paramref name="args"/> on this layer, sets it up with
    /// <paramref name="setup"/> (its document, its keys, its Back), and keeps it: the layer is its only owner.
    /// </summary>
    template <typename T, typename Setup, typename... Args>
        requires std::derived_from<T, MCRmlScreen> && std::invocable<Setup&, T&>
    MCGuiHandle<T> CreateChild(Setup&& setup, Args&&... args)
    {
        MCGuiRef<T> made = MCGuiRef<T>::Make(std::forward<Args>(args)...);
        made->Attach(*this);
        setup(*made);
        MCGuiHandle<T> handle = made.Handle();
        _Screens.push_back(MCGuiRef<MCRmlScreen>(std::move(made)));
        return handle;
    }

    /// <summary>Lets go of <paramref name="screen"/>, which destroys it (MCRmlScreen::Close).</summary>
    void Remove(const MCRmlScreen& screen);

    MCKind Kind() const { return _Kind; }
    Rml::Context& Context() const { return _Context; }
    MCRmlKit& Kit() const { return _Kit; }

    /// <summary>The layer's unit now, in window pixels (its dp ratio).</summary>
    float Unit() const;
    /// <summary>The window is <paramref name="width"/> x <paramref name="height"/> pixels: the layer laid out for it.</summary>
    void SetWindow(int32_t width, int32_t height);

    /// <summary>The screen shown on top (the last one shown); none when none shows.</summary>
    MCOptionalRef<MCRmlScreen> Top() const;
    /// <summary>Whether any screen of the layer shows.</summary>
    bool Showing() const { return Top().has_value(); }
    /// <summary>The screens on the layer, oldest first.</summary>
    size_t Count() const { return _Screens.size(); }

    /// <summary>
    /// The window's event <paramref name="event"/> for the layer's screens; whether it was theirs (a mouse, key or text
    /// event while a screen shows: the game doesn't see it).
    /// </summary>
    bool HandleEvent(const SDL_Event& event);

    /// <summary>A screen shows (<see cref="MCRmlScreen::Show"/>): on top from now on.</summary>
    void Shown(MCRmlScreen& screen);
    /// <summary>A screen hides.</summary>
    void Hidden(MCRmlScreen& screen);

    /// <summary>The layer's frame, before its draws are recorded: the kit's controls kept up, then RmlUi's layout.</summary>
    void Update();

    /// <summary>
    /// A key RmlUi left (RmlUi's KeyIdentifier): Escape to the top screen's Back, Q and E to its tabs, the rest to the
    /// screen (MCRmlScreen::OnKey); whether it was used.
    /// </summary>
    bool TakeKey(int key, int modifiers);

private:
    /// <summary>Whether a text box has the focus (its letters are typed, not keys).</summary>
    bool Typing() const;

    Rml::Context& _Context;
    MCKind _Kind;
    MCUnit _Unit;
    MCRmlKit& _Kit;
    int32_t _Width = 0;
    int32_t _Height = 0;
    std::vector<MCGuiRef<MCRmlScreen>> _Screens;
    /// <summary>The screens shown, the top last.</summary>
    std::vector<MCGuiHandle<MCRmlScreen>> _ShownOrder;
};
