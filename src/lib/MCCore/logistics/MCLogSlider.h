#pragma once

#include "logistics/MCLogObject.h"

/// <summary>A horizontal slider (the preferences screen's volume and brightness bars).</summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>lSlider</c>).</remarks>
class MCLogSlider : public MCLogObject
{
public:
    /// <summary>Loads the thumb picture (<c>prefs_02.tga</c>).</summary>
    MCLogSlider();

    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Frees the thumb picture.</summary>
    void OnDestroy() override;

    /// <summary>Draws the track, when it has one, and the thumb at the current value.</summary>
    void Draw() override;

    /// <summary>
    /// Port: gives the slider a track to draw under the thumb: the slider's size of <paramref name="art"/> from
    /// (<paramref name="left"/>, <paramref name="top"/>) (the original's tracks were the screen's art; copied from the
    /// art itself, HD art shows the pack's), with the thumb's travel <paramref name="thumbInset"/> pixels in from each
    /// end.
    /// </summary>
    void SetTrack(std::shared_ptr<MCLogPort> art, int32_t left, int32_t top, int32_t thumbInset);

    /// <summary>Port: the slider draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Sets the value, clamped to [<see cref="MinValue"/>, <see cref="MaxValue"/>].</summary>
    void SetCurrentValue(int32_t value);

    /// <summary>The value for the mouse at screen x <paramref name="mouseX"/>: its share of the thumb's travel.</summary>
    int32_t ValueAt(int32_t mouseX);

    /// <summary>The thumb's x in the slider for the current value.</summary>
    int32_t ThumbX();

    /// <summary>Dragging moves the value; then the event routine runs.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    int32_t MinValue = 0;
    int32_t MaxValue = 0;
    int32_t CurrentValue = 0;
    /// <summary>The thumb picture.</summary>
    std::shared_ptr<MCLogPort> ThumbPort;
    /// <summary>Port: the art the track is copied from (null: none, the screen's art shows through), and where.</summary>
    std::shared_ptr<MCLogPort> TrackArt;
    int32_t TrackLeft = 0;
    int32_t TrackTop = 0;
    /// <summary>Port: how far in from each end the thumb's travel starts and stops.</summary>
    int32_t ThumbInset = 0;

private:
    /// <summary>How far the thumb travels: the width less the insets and the thumb.</summary>
    int32_t Travel();
};
