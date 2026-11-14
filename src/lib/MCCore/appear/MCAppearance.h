#pragma once

#include "lib/MCDice.h"
#include "lib/MCFrameOfRef.h"
#include "lib/MCOptionalRef.h"
#include "lib/MCVector2D.h"

class MCAppearanceType;
class MCCamera;
class MCGameObject;

/// <summary>
/// What an object looks like on screen: the base of every actor (mechs, vehicles, buildings, effects...). The
/// object owns one; each frame the object queue calls <see cref="Update"/>, <see cref="RecalcBounds"/> against the
/// camera and <see cref="Render"/>, which adds draw elements to the frame's draw list.
/// </summary>
/// <remarks>Original source: <c>appear\appear.h</c> (inline virtuals) and <c>appear\appear.cpp</c>.</remarks>
class MCAppearance
{
public:
    MCAppearance() = default;
    virtual ~MCAppearance() = default;
    MCAppearance(const MCAppearance&) = delete;
    MCAppearance& operator=(const MCAppearance&) = delete;

    /// <summary>
    /// Binds the appearance to its type <paramref name="tree"/> (shared while bound) and its object
    /// <paramref name="obj"/> (both empty to unbind).
    /// </summary>
    virtual int32_t Init(std::shared_ptr<MCAppearanceType> tree = nullptr, std::weak_ptr<MCGameObject> obj = {})
    {
        Owner = std::move(obj);
        Visible = false;
        return 0;
    }

    /// <summary>Advances animation. Returns nonzero when it wants to be drawn.</summary>
    virtual int32_t Update() { return 0; }

    /// <summary>Adds the appearance's elements to the draw list at <paramref name="depthFixup"/>.</summary>
    virtual int32_t Render(int32_t depthFixup = 0) { return 0; }

    /// <summary>The appearance's type (none for the base).</summary>
    virtual std::shared_ptr<MCAppearanceType> GetAppearanceType() { return nullptr; }

    /// <summary>Draws the damage/status bars over the object (nothing for the base).</summary>
    virtual void DrawBars() {}

    /// <summary>
    /// Recomputes the screen position and bounds against <paramref name="view"/> (none during a scenario's load:
    /// only the shape's own bounds are taken). Returns nonzero when on screen.
    /// </summary>
    virtual int RecalcBounds(MCOptionalRef<MCCamera> view)
    {
        UpperLeft.X = 0.0f;
        UpperLeft.Y = 0.0f;
        LowerRight.X = 0.0f;
        LowerRight.Y = 0.0f;
        return 0;
    }

    /// <summary>Starts gesture <paramref name="gesture"/> (actors that animate by gesture).</summary>
    virtual int32_t SetGesture(uint32_t gesture) { return 0; }

    /// <summary>Sets the gesture the actor animates toward.</summary>
    virtual int32_t SetGestureGoal(int32_t gestureGoal) { return 0; }

    /// <summary>Scales the walk/move animation speed.</summary>
    virtual void SetVelocityPercentage(float percent) {}

    /// <summary>The current animation frame.</summary>
    virtual int32_t GetFrameNumber() { return 0; }

    /// <summary>The appearance's class (the top byte of its type id); 0 for the base.</summary>
    virtual uint32_t GetAppearanceClass() { return 0; }

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): whether the bounds RecalcBounds last found for
    /// <paramref name="view"/> lie within half the view's size past its edges.
    /// </summary>
    bool NearView(const MCCamera& view) const;

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): <see cref="ReadAhead"/> while HD art draws numbered
    /// folders and the appearance is <paramref name="shown"/> in <paramref name="view"/> (RecalcBounds found it on
    /// screen) or near it (NearView), which a pan or its move brings it into. Its object's on-screen check calls it.
    /// </summary>
    void ReadAheadNear(const MCCamera& view, bool shown);

    /// <summary>
    /// Port-only (SPEC-first-use.md: the camera's surroundings): has HD art's readers read the packets the appearance
    /// draws now (as its Render finds them) whole, ahead of their frames' first draws, each once. Nothing for the base.
    /// </summary>
    virtual void ReadAhead() {}

    /// <summary>Where the owner is on screen through <paramref name="cam"/>.</summary>
    MCVector2D GetScreenPos(const MCCamera& cam) const;

    /// <summary>The cached <see cref="ScreenPos"/>.</summary>
    MCVector2D GetScreenPos() const { return ScreenPos; }

    /// <summary>Draws corner marks around the appearance's bounds in <paramref name="color"/>.</summary>
    void DrawSelectBox(uint8_t color);

    /// <summary>Draws brackets above and below the appearance's bounds in <paramref name="color"/>.</summary>
    void DrawSelectBrackets(uint8_t color);

    /// <summary>Whether the object is to be drawn this frame (set by the object's update).</summary>
    bool Visible = false;
    /// <summary>Set by <c>RecalcBounds</c> once the current shape's bounds are known.</summary>
    bool InView = false;
    /// <summary>
    /// The object the appearance draws (a back-reference: the object owns its appearance). Its methods
    /// <c>lock()</c> it for the call; the owner is alive while its appearance runs.
    /// </summary>
    std::weak_ptr<MCGameObject> Owner;
    /// <summary>The owner's position on screen (the camera's last projection).</summary>
    MCVector2D ScreenPos;
    /// <summary>Seconds left of the highlight box drawn when the object first becomes a target.</summary>
    float HighlightTime = 0.0f;
    /// <summary>Whether the highlight box is showing.</summary>
    bool Highlighting = false;
    /// <summary>Whether the highlight has been shown (it shows only once).</summary>
    bool Highlighted = false;
    /// <summary>The screen bounds' top-left corner.</summary>
    MCVector2D UpperLeft;
    /// <summary>The screen bounds' bottom-right corner.</summary>
    MCVector2D LowerRight;
};
