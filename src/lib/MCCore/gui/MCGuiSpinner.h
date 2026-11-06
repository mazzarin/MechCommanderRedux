#pragma once

#include "gui/MCGuiButton.h"
#include "gui/MCGuiRef.h"

/// <summary>One arrow of an <see cref="MCGuiSpinner"/>: auto-repeats (after 1 s, then every 250 ms) while held.</summary>
/// <remarks>Original source: <c>gui\abutton.cpp</c> (<c>aSpinnerButton</c>).</remarks>
class MCGuiSpinnerButton : public MCGuiToolButton
{
public:
    /// <summary>The down picture while held, the up one otherwise (transparently).</summary>
    void Draw() override;
    /// <summary>A press runs the callback and starts timer 1; timer 1 starts timer 2, which repeats the callback.</summary>
    void HandleEvent(MCGuiEvent& event) override;
};

/// <summary>An up/down arrow pair that posts <see cref="MCGuiEventType::SpinUp"/> and
/// <see cref="MCGuiEventType::SpinDown"/> to its parent.</summary>
/// <remarks>
/// Original source: <c>gui\abutton.cpp</c> (<c>aSpinner</c>; never made in MCX.EXE). The parent must be set before
/// <see cref="Init"/>.
/// </remarks>
class MCGuiSpinner : public MCGuiObject
{
public:
    ~MCGuiSpinner() override;

    /// <summary>Makes the two arrows (art packets 0xc/0xd and 0x25/0x26), sized to fit them.</summary>
    /// <returns>-1 on success (the original's), or an arrow's error.</returns>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Port: draws itself each frame (nothing of its own; the arrows draw themselves).</summary>
    bool DrawsLive() override { return true; }

    MCGuiHandle<MCGuiSpinnerButton> UpButton;
    MCGuiHandle<MCGuiSpinnerButton> DownButton;
};
