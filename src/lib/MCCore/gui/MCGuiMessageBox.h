#pragma once

#include "gui/MCGuiButton.h"
#include "gui/MCGuiRef.h"

/// <summary>A centred box with a line of text and an OK button (the version dialog).</summary>
/// <remarks>Original source: <c>gui\asystem.cpp</c> (<c>aMessageBox</c>).</remarks>
class MCGuiMessageBox : public MCGuiObject
{
public:
    ~MCGuiMessageBox() override;

    /// <summary>
    /// Sizes the box to <paramref name="text"/> in the white font, centres it on the screen, adds the OK button
    /// (which runs <see cref="DestroyVersion"/>) and keeps the text.
    /// </summary>
    /// <returns>0, -3 without the white font, or the button's error.</returns>
    int32_t Init(std::string_view text);
    using MCGuiObject::Init;

    /// <summary>Passes events inside the box to the button.</summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Port: the box, its text and its outline (the original wrote them into the picture once, in init).</summary>
    void Draw() override;
    /// <summary>Port: draws itself each frame from its text.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The OK button.</summary>
    MCGuiHandle<MCGuiButton> OkButton;
    /// <summary>Port: the text shown.</summary>
    std::string Message;
};
