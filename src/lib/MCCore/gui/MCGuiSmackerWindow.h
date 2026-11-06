#pragma once

#include "gui/MCGuiObject.h"
#include "platform/MCSmacker.h"
#include "sprite/MCHdMovie.h"

/// <summary>
/// A window playing a Smacker movie into a pane of its own, copied to the screen each frame.
/// </summary>
/// <remarks>Original source: <c>gui\awindow.cpp</c> and <c>gui\awindow.h</c> (<c>aSmackerWindow</c>).</remarks>
class MCGuiSmackerWindow : public MCGuiObject
{
public:
    /// <summary>Does nothing: the movie sets the size.</summary>
    void Resize(int32_t, int32_t) override {}
    /// <summary>
    /// Places the window at <paramref name="area"/>'s corner (moved by <paramref name="position"/> when given); the
    /// rectangle's right and bottom are the width and height.
    /// </summary>
    int32_t Init(const tagRECT& area, std::optional<tagPOINT> position);
    /// <summary>
    /// Starts <paramref name="movie"/> (the window owns it from then on): full screen (<paramref name="fullScreen"/>) puts up the
    /// movie's own palette, otherwise its colours are remapped to the game palette. The movie decodes into a pane of
    /// the window's size.
    /// </summary>
    /// <returns>0, or <see cref="NoPane"/> when the window has no area.</returns>
    int32_t StartSmackerMovie(std::unique_ptr<MCSmackTag> movie, bool fullScreen);
    /// <summary>Closes the movie and frees the pane.</summary>
    void OnDestroy() override;
    /// <summary>
    /// Closes the movie, sets <see cref="MovieOver"/> and takes the window off its parent, which destroys it.
    /// </summary>
    virtual void EndSmackerMovie();
    void CheckSmackerPalette() override;
    /// <summary>Decodes the next frame into <see cref="MoviePane"/> and shows it; the movie's end ends it.</summary>
    void Display() override;
    /// <summary>Port: copies the movie frame in <see cref="MoviePane"/> to the window.</summary>
    void Draw() override;
    /// <summary>Port: the window draws itself each frame (see <see cref="Draw"/>).</summary>
    bool DrawsLive() override { return true; }
    /// <summary>
    /// Decodes the current frame into <paramref name="buffer"/> (<paramref name="pitch"/> bytes a row, at most
    /// <paramref name="height"/> rows) and steps on.
    /// </summary>
    /// <returns>False on the last frame.</returns>
    bool NextFrame(std::span<uint8_t> buffer, int32_t pitch, int32_t height) const;
    /// <summary>Never hit: returns null.</summary>
    std::shared_ptr<MCGuiObject> FindObject(int32_t xPos, int32_t yPos) override;

    /// <summary>What <see cref="StartSmackerMovie"/> returns when the window has no area.</summary>
    static constexpr int32_t NoPane = static_cast<int32_t>(0xd4d40000);

    /// <summary>The movie.</summary>
    std::unique_ptr<MCSmackTag> Movie;
    /// <summary>Set until the first frame: the display clears the pane (or the screen) before decoding it.</summary>
    bool FirstFrame = true;
    /// <summary>
    /// The pane the movie decodes into: at the window's screen position in <see cref="MovieWindow"/>, a buffer reaching
    /// from the screen's corner to the window's. (The original had one for windowed play only.)
    /// </summary>
    std::unique_ptr<MCPane> MoviePane;
    /// <summary>The window the movie decodes into, with its pixels and its streamed texture.</summary>
    std::shared_ptr<MCWindow> MovieWindow;
    /// <summary>Whether the movie plays full screen (its own palette).</summary>
    bool FullScreen = false;
    /// <summary>
    /// Port-only: the HD art pack's frames for the movie (<see cref="MCHdArt::FindMovie"/>), when it has as many as the
    /// movie, its size scaled by a whole number, and the screen is sharp; each frame the movie decodes is shown from
    /// the next one. Null once they fail (the movie plays on as it is).
    /// </summary>
    std::unique_ptr<MCHdMovie> HdMovie;
    /// <summary>
    /// Port-only: <see cref="HdMovie"/> once dropped, closed with the window: closing it waits for the frame it's
    /// decoding, which is long on a machine it fell behind on.
    /// </summary>
    std::unique_ptr<MCHdMovie> HdDropped;

private:
    /// <summary>Port-only: opens the HD art pack's frames for the movie (<see cref="HdMovie"/>), from the first.</summary>
    void OpenHdMovie();
    /// <summary>
    /// Port-only: shows the movie's frame <paramref name="shown"/>, just decoded into <paramref name="rect"/> of
    /// <paramref name="texture"/>, from its HD frame.
    /// </summary>
    void ShowHdFrame(MCTexture& texture, const MCRect& rect, uint32_t shown);
    /// <summary>Port-only: stops showing HD frames, saying why.</summary>
    void DropHdMovie(MCTexture& texture, std::string_view why);

    /// <summary>The movie's frame the next HD frame is for.</summary>
    uint32_t HdNextFrame = 0;
    /// <summary>The original's frame under the HD one (its indices), reused frame to frame.</summary>
    std::vector<uint8_t> HdOriginal;
};

/// <summary>
/// Set when a Smacker movie has played to its end (or been ended); the briefing screen also sets it to -1 for no
/// movie at all.
/// </summary>
extern int MovieOver;
