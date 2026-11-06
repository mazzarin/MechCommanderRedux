#pragma once

#include "gui/MCGuiObject.h"

/// <summary>
/// The startup window: an uplink sequence typed and drawn over a map, then static noise, from the three art packets
/// 0x2d..0x2f, played over the screen while the game loads.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> and <c>gui\awindow.h</c> (<c>aStartupWindow</c>).</remarks>
class MCGuiStartupWindow : public MCGuiObject
{
public:
    /// <summary>The uplink's 12 possible end points, (x, y) on the map.</summary>
    static constexpr std::array<std::array<int32_t, 2>, 12> UplinkPoints = {{
        {0x12f, 0x196},
        {0x14e, 0x17f},
        {0x17e, 0x150},
        {0x1eb, 0x71},
        {0x14e, 0x17f},
        {0x17e, 0x150},
        {0x193, 0x138},
        {0x1a3, 0x125},
        {0x1ec, 0x8a},
        {0x1e6, 0xa9},
        {0x1ec, 0x8a},
        {0x1eb, 0x71},
    }};

    void Resize(int32_t, int32_t) override {}
    /// <summary>Sets <see cref="StartupState"/> and restarts the frame count.</summary>
    void SetState(int32_t newState) override
    {
        FrameCount = 0;
        StartupState = newState;
    }

    /// <summary>Frees the art and the picture.</summary>
    void OnDestroy() override;
    /// <summary>Fills the picture with noise: random rows copied from others, others random colours 0..31.</summary>
    void DoStatic();
    /// <summary>Wipes the picture to black.</summary>
    void EndStatic();
    /// <summary>Runs the next step of the sequence (<see cref="Step"/>) and shows the picture.</summary>
    void Display() override;
    /// <summary>Port: copies <see cref="StaticPort"/> to the window.</summary>
    void Draw() override;
    /// <summary>Port: the window draws itself each frame (see <see cref="Draw"/>).</summary>
    bool DrawsLive() override { return true; }
    /// <summary>Loads art packets 0x2d, 0x2e and 0x2f, makes the picture and picks a random end point (0..11).</summary>
    /// <returns>0, -1 when the picture can't be made, or the art file's error.</returns>
    int32_t Setup();

    /// <summary>The art packets 0x2d (the map), 0x2e and 0x2f (noise frames), as shape tables.</summary>
    std::array<std::vector<uint8_t>, 3> StaticImages;
    /// <summary>The display step: bumped once per <see cref="Display"/>, it drives the whole sequence.</summary>
    int32_t StartupState = 0;
    /// <summary>Restarted by <see cref="SetState"/> and <see cref="Setup"/>.</summary>
    int32_t FrameCount = 0;
    /// <summary>The typed text's width so far on the current line.</summary>
    int32_t TextX = 0;
    /// <summary>The uplink's end point: an index into <see cref="UplinkPoints"/>, picked by <see cref="Setup"/>.</summary>
    int32_t RandomStart = 0;
    /// <summary>The looping noise sample started at the end of the sequence.</summary>
    int32_t NoiseSample = 0;

    /// <summary>
    /// Port: the picture the sequence builds up (made by <see cref="Setup"/>). The original drew it on the screen,
    /// and the noise copies rows of what is already there, so the picture is the window's state; like a movie frame,
    /// it is copied to the screen each frame.
    /// </summary>
    std::unique_ptr<MCGuiPort> StaticPort;

private:
    /// <summary>
    /// Port: one step of the sequence: its sound and its drawing into <see cref="StaticPort"/> (the body of the
    /// original's <see cref="Display"/>).
    /// </summary>
    void Step();
    /// <summary>Port: <see cref="StaticPort"/>'s pane, where the sequence draws.</summary>
    MCPane& StaticPane() const;
    /// <summary>Lets the renderers forget the art and frees it.</summary>
    void FreeImages();
};
