#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"

/// <summary>A window holding up to two panes, tiled side by side (or stacked) or one at a time.</summary>
/// <remarks>
/// Original source: <c>gui\asystem.cpp</c> and <c>gui\asystem.h</c> (<c>aHolderObject</c>). It has no port of its own:
/// it only shows its panes. The camera's main window and the empty title window derive from it.
/// </remarks>
class MCGuiHolderObject : public MCGuiObject
{
public:
    /// <summary>Places the holder (no port), and clears the panes; no pane is active.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Forgets the panes.</summary>
    void OnDestroy() override;
    /// <summary>Resizes the holder (snapped as the base does) and lays its panes out again.</summary>
    void Resize(int32_t newWidth, int32_t newHeight) override;
    /// <summary>Forgets <paramref name="oldChild"/> if it is a pane, then removes it.</summary>
    void RemoveChild(MCGuiObject& oldChild) override;
    /// <summary>Displays the children (the holder has no picture).</summary>
    void Display() override;
    /// <summary>Draws no frame.</summary>
    void DrawFramed(bool, bool) override {}
    /// <summary>Draws no box.</summary>
    void DrawBox(uint8_t, int32_t, int32_t, int32_t, int32_t) override {}
    using MCGuiObject::DrawBox;

    /// <summary>Tiles the two panes, or shows only the active one.</summary>
    virtual void SetTiled(bool tiled);
    virtual bool GetTiled() { return Tiled; }
    /// <summary>Lays the panes out for the current mode.</summary>
    virtual void Retile();
    virtual void SetVertical(bool on)
    {
        Vertical = on;
        Retile();
    }

    virtual bool GetVertical() { return Vertical; }
    /// <summary>Whether a pane slot is free (the holder lays out two panes at most).</summary>
    bool HasFreePane() const { return Panes[0].expired() || Panes[1].expired(); }

    /// <summary>
    /// Makes a <typeparamref name="T"/> on the holder, set up by <paramref name="setup"/> (its <c>Init</c>, which
    /// returns 0 or an error), puts it in the first free pane slot and re-tiles. Nothing is made when no slot is free
    /// (<see cref="HasFreePane"/>).
    /// </summary>
    /// <returns>The pane (empty when no slot was free), or <paramref name="setup"/>'s error.</returns>
    template <typename T, typename Setup>
        requires std::derived_from<T, MCGuiObject> && std::is_invocable_r_v<int32_t, Setup&, T&>
    std::expected<MCGuiHandle<T>, int32_t> TryCreatePane(Setup&& setup)
    {
        if (!HasFreePane())
        {
            return MCGuiHandle<T>();
        }

        std::expected<MCGuiHandle<T>, int32_t> pane = TryCreateChild<T>(std::forward<Setup>(setup));

        if (pane.has_value())
        {
            TakePaneSlot(pane->Weak());
        }

        return pane;
    }

    /// <summary>Takes <paramref name="pane"/> out of its slot and off the children.</summary>
    virtual void RemovePane(MCGuiObject& pane);

    /// <summary>Pane 0 or 1, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiObject> GetPane(int32_t index)
    {
        return (index >= 0 && index < 2) ? Panes[static_cast<size_t>(index)].lock() : nullptr;
    }

    /// <summary>The active pane, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiObject> GetActivePane()
    {
        return ActivePane >= 0 ? Panes[static_cast<size_t>(ActivePane)].lock() : nullptr;
    }

    /// <summary>The pane that isn't active, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiObject> GetInactivePane()
    {
        return ActivePane == 0 ? Panes[1].lock() : (ActivePane == 1 ? Panes[0].lock() : nullptr);
    }

    /// <summary>0, 1, or -1 when none.</summary>
    virtual int32_t GetActivePaneNumber() { return ActivePane; }
    /// <summary>Makes <paramref name="pane"/> active: the second pane when it is that, else the first.</summary>
    virtual void SetActivePane(MCGuiObject& pane);
    /// <summary>
    /// Makes pane <paramref name="index"/> active (0 always; 1 only when there is a second pane) and re-tiles.
    /// </summary>
    virtual void SetActivePaneNumber(int32_t index);

    /// <summary>The two panes (children, owned elsewhere): the holder lays out two at most.</summary>
    std::array<std::weak_ptr<MCGuiObject>, 2> Panes;
    bool Tiled = false;
    /// <summary>The active pane: 0, 1 or -1 (none).</summary>
    int32_t ActivePane = -1;
    /// <summary>Whether the tiled panes sit side by side (else they are stacked).</summary>
    bool Vertical = false;

private:
    /// <summary>Puts <paramref name="pane"/>, a child just made, in the first free slot and re-tiles.</summary>
    void TakePaneSlot(std::weak_ptr<MCGuiObject> pane);
};
