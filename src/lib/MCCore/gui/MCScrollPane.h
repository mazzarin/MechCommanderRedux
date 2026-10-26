#pragma once

#include "lib/MCOptionalRef.h"
#include "logistics/MCLogObject.h"

/// <summary>
/// A logistics-screen scrolling pane: its content is drawn into a tall port (<see cref="ContentPort"/>), of which the
/// pane shows a window, and a 13-pixel slider column on the right (art <c>scroll.tga</c>, with the up and down
/// arrows <c>supbup.tga</c> / <c>sdnbup.tga</c>) moves it. The children are moved with the content.
/// </summary>
/// <remarks>
/// Original source: <c>gui\scrlpane.cpp</c> (<c>ScrollPane</c>). The scroll position is a percentage of the content's
/// height; the slider is placed from it.
/// </remarks>
class MCScrollPane : public MCLogObject
{
public:
    /// <summary>The slider column's width.</summary>
    static constexpr int32_t SliderWidth = 13;
    /// <summary>The height of each arrow at the ends of the column (the slider travels between them).</summary>
    static constexpr int32_t ArrowHeight = 0x10;
    /// <summary>Port: how far above the column's bottom its down arrow's art goes.</summary>
    static constexpr int32_t ArrowArtHeight = 15;

    /// <summary>Which arrow of the column is held down.</summary>
    enum class Arrow : uint8_t
    {
        None = 0,
        Up = 1,
        Down = 2
    };

    ~MCScrollPane() override;

    /// <summary>
    /// Loads <paramref name="name"/> (when given) as the background image and inits the pane with it. Unlike the
    /// base's init, the size comes first: (width, height, xPos, yPos).
    /// </summary>
    /// <returns>0.</returns>
    int32_t Init(int32_t width, int32_t height, int32_t xPos, int32_t yPos, std::string_view name) override;
    /// <summary>
    /// Makes the content port (<paramref name="width"/> - 13 wide, owned by the pane), copies
    /// <paramref name="background"/> (when given) into a background port, inits the object at
    /// (<paramref name="xPos"/>, <paramref name="yPos"/>) drawing into the content port, and builds the slider column
    /// from the art files.
    /// </summary>
    void Init(int32_t width, int32_t height, int32_t xPos, int32_t yPos, MCOptionalRef<const MCLogPort> background);
    void Destroy() override;

    /// <summary>Scrolls to <paramref name="position"/> (clamped to 0..<see cref="MaxScroll"/>) and moves the slider.</summary>
    void SetScrollPos(float position);
    /// <summary>Moves the slider to <paramref name="position"/> pixels (clamped) and scrolls to match.</summary>
    void SetSliderPos(int32_t position);
    /// <summary>Moves every child by the change in the scroll offset since the last call.</summary>
    void SetChildren();
    /// <summary>
    /// Port: draws the background, the visible part of the content and the slider column (the original's draw did
    /// nothing; its display copied these pictures to the screen).
    /// </summary>
    void Draw() override;
    /// <summary>Draws the pane in the frame pass (see <see cref="Draw"/>); the children aren't shown.</summary>
    void Display() override;
    /// <summary>Port: the pane draws itself from its state each frame.</summary>
    bool DrawsLive() override { return true; }
    /// <summary>
    /// Port: draws the content into <see cref="ContentPort"/> when that is a view: the pane opens it scrolled into
    /// place (0xff draws nothing, as the original's keyed copy of the content picture) for this call. A content port
    /// that is a picture is copied instead, as the original did. By default the content port's own
    /// <see cref="MCGuiPort::DrawContent"/> draws it.
    /// </summary>
    virtual void DrawContent();
    /// <summary>
    /// Port: draws the content as the pane shows it, scrolled, into <paramref name="target"/> with its top left at
    /// (<paramref name="xPos"/>, <paramref name="yPos"/>), opaque (a transition's picture of the pane).
    /// </summary>
    void DrawContentTo(const MCPane& target, int32_t xPos, int32_t yPos);
    /// <summary>Sizes the slider to the content (none when it fits) and builds its picture.</summary>
    void SetUpSlider();
    /// <summary>The colour of the slider's outer edges (the file panes of the splash screens use their own).</summary>
    virtual uint8_t SliderEdgeColor() const { return 0x35; }
    /// <summary>
    /// Makes <paramref name="port"/> the content (the pane shares it; none clears the content), rescales the scroll
    /// range to its height and rebuilds the slider. Unless <paramref name="resetPosition"/>, a port at least as tall
    /// as the pane keeps the old scroll offset (as a percentage of the new height).
    /// </summary>
    void SetDisplayPort(std::shared_ptr<MCLogPort> port, bool resetPosition);
    /// <summary>Leaves the pane without content.</summary>
    void ClearDisplayPort();
    /// <summary>The content port, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogPort> Lport() const;
    /// <summary>
    /// Dragging the slider, the arrows (pressed art, then repeating on a 200 ms timer, id 6) and the track (a page per
    /// click), and passing the rest to the children. The arrows step one row of the first child's height, or a
    /// <c>BlackFont</c> line of slider when there are no children.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>
    /// Port-only: the mouse wheel steps a row per notch, as the arrows do (a <c>BlackFont</c> line of slider when
    /// there are no children). Not taken when the content fits (no slider).
    /// </summary>
    bool MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) override;
    /// <summary>The first content row shown.</summary>
    int32_t GetScrollOffset() const;
    /// <summary>The content row just below the shown part.</summary>
    int32_t GetScrollBottom();

    /// <summary>The slider's height in pixels, or 0 when the content fits (no slider).</summary>
    int32_t SliderHeight = -1;
    /// <summary>The slider's top, in pixels from the top of the column (16 = below the up arrow).</summary>
    int32_t SliderPos = 0;
    /// <summary>The largest <see cref="SliderPos"/>.</summary>
    int32_t SliderMax = 0;
    /// <summary>The slider's picture (13 x <see cref="SliderHeight"/>), with its texture.</summary>
    std::shared_ptr<MCWindow> SliderWindow;
    /// <summary>The scroll offset the children were last placed for.</summary>
    int32_t LastScrollOffset = 0;
    /// <summary>A copy of the background passed to <see cref="Init"/>, drawn under the content.</summary>
    std::shared_ptr<MCLogPort> BackgroundCopy;
    /// <summary>
    /// The content: as wide as the pane less the slider, as tall as the content. Shared with its maker when the maker
    /// keeps it (the logistics store keeps its views and resizes them).
    /// </summary>
    std::shared_ptr<MCLogPort> ContentPort;
    /// <summary>Port: the art of a slider column: its track tile and its two arrows (logistics art names).</summary>
    struct ColumnArt
    {
        std::string Track;
        std::string UpArrow;
        std::string DownArrow;
    };

    /// <summary>
    /// Port: the slider column's art (13 wide), each over the one before: the track tile repeated down the column below
    /// its first row, and the arrows at the ends. The original copied them into a picture of the column once; they are
    /// drawn in place (<see cref="DrawSliderColumn"/>), so with HD art on they are drawn from the pack.
    /// </summary>
    std::vector<ColumnArt> ColumnArts;
    /// <summary>Port: the slider column's height (the pane's when <see cref="Init"/> made it).</summary>
    int32_t ColumnHeight = 0;
    /// <summary>Content pixels per scroll unit: 1% of the content's height (set by <see cref="SetDisplayPort"/>).</summary>
    float ScrollUnit = 0.0f;
    /// <summary>The largest scroll position.</summary>
    float MaxScroll = 0.0f;
    /// <summary>The scroll position, in percent of the content's height (0..<see cref="MaxScroll"/>).</summary>
    float ScrollPos = 0.0f;

    /// <summary>Port: the pane's view (the original copied its pictures straight to the frame).</summary>
    std::shared_ptr<MCLogPort> PanePort;
    /// <summary>Port: the texture of <see cref="SliderWindow"/>.</summary>
    std::weak_ptr<MCTexture> SliderTexture;

    /// <summary>
    /// Port: draws the slider column from the state into <paramref name="target"/> with its corner at
    /// (<paramref name="xPos"/>, <paramref name="yPos"/>), with 0xff as a colour key when <paramref name="keyed"/>:
    /// the track and its arrows, an arrow held down in its pressed art (<see cref="PressedArrowArt"/>), and the
    /// slider at <see cref="SliderPos"/> unless the content fits.
    /// </summary>
    void DrawSliderColumn(const MCPane& target, int32_t xPos, int32_t yPos, bool keyed);

    /// <summary>
    /// Port: the art an arrow shows while it is held down (<paramref name="down"/>: the bottom one), or null to show
    /// the column's own. The original copied it into the track on the press and put the normal art back on the
    /// release. The caller keeps it as its pin.
    /// </summary>
    virtual std::shared_ptr<MCLogPort> PressedArrowArt(bool down);

    /// <summary>Port: which arrow of this pane is held down.</summary>
    Arrow HeldArrow() const;

private:
    /// <summary>Port: makes <see cref="SliderTexture"/> for a new <see cref="SliderWindow"/> (destroying the old one).</summary>
    void MakeSliderTexture();
    /// <summary>Frees the slider's picture and its texture.</summary>
    void FreeSlider();
    /// <summary>Points the pane at <paramref name="port"/> and rescales the scroll range (the body of <see cref="SetDisplayPort"/>).</summary>
    void ShowContent(std::shared_ptr<MCLogPort> port, bool resetPosition);
    /// <summary>Scrolls to the top of content row <paramref name="row"/> (rows the first child's height).</summary>
    void ScrollToRow(int32_t row);
    /// <summary>The row below the one at the top of the view (an arrow's step down; rows the first child's height).</summary>
    int32_t NextRow();
    /// <summary>Passes a mouse event to the first child under it, unless the mouse is on the slider column.</summary>
    void ForwardToChildren(MCGuiEvent& event);

    /// <summary>The mouse y the slider drag last moved to; -1 when not dragging.</summary>
    int32_t _DragY = -1;
    /// <summary>Whether the slider is being dragged.</summary>
    bool _DraggingSlider = false;
    /// <summary>The arrow held down.</summary>
    Arrow _ArrowPressed = Arrow::None;
};
