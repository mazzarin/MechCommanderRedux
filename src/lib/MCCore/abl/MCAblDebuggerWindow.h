#pragma once

#include "abl/MCScrollingTextWindow.h"
#include "gui/MCGuiRef.h"
#include "gui/MCGuiTitleWindow.h"

class MCGuiTextObject;

/// <summary>The ABL debugger window: the output pane over a one-line command box (honorb.cpp creates it).</summary>
/// <remarks>Original source: <c>abl\abldbug.cpp</c>, 0x4c0 bytes.</remarks>
class MCAblDebuggerWindow : public MCGuiTitleWindow
{
public:
    /// <summary>Port: its output window scrolls its picture, so it keeps one.</summary>
    bool DrawsLive() override { return false; }

    /// <summary>Creates the window, its output pane ("ABL Out") and its command box.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Resizes the output pane and moves the command box to the bottom.</summary>
    void Resize(int32_t width, int32_t height) override;

    /// <summary>The command box (after Init).</summary>
    MCGuiTextObject& Input() const { return *_Input; }

    /// <summary>The output pane (after Init).</summary>
    MCScrollingTextWindow& Output() const { return *_Output; }

private:
    /// <summary>The output pane (a child).</summary>
    MCGuiHandle<MCScrollingTextWindow> _Output;
    /// <summary>The command box (a child).</summary>
    MCGuiHandle<MCGuiTextObject> _Input;
};
