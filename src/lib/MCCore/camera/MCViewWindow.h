#pragma once

#include "gui/MCGuiSystem.h"
#include "gui/MCGuiTitleWindow.h"
#include "lib/MCVector2D.h"

class MCCamera;
struct MCPane;

/// <summary>
/// A camera pane: an <see cref="MCGuiTitleWindow"/> that renders the scenario through its <see cref="MCCamera"/>, and
/// passes events to the interface when it is the main view.
/// </summary>
/// <remarks>
/// Port: the world surface and the zoom. The camera draws the world into a surface of its own, at 1x (camera scale
/// 100), as tall as the zoom asks: ZoomHeight game pixels, 480 closest to 2160 furthest, whatever the window's size.
/// The surface has the view's aspect, is the screen's underlay over the view's rectangle (the display scales it
/// there), and the view's rectangle of the screen is left in the key colour for it to show through. Unit overlays
/// (health bars, selection marks) are drawn on the screen at its own scale.
/// </remarks>
class MCViewWindow : public MCGuiTitleWindow
{
public:
    ~MCViewWindow() override;
    /// <summary>aObject::init, clears the fields; depth 5.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Lets go of the world surface, then aObject::destroy.</summary>
    void OnDestroy() override;
    /// <summary>
    /// Keeps a copy of the event; handles close (deactivates the camera), resize (the global pane), zoom (toggles
    /// the zoom) and follow-off; hands the rest to the interface when <see cref="InterfaceWindow"/> is set; a click
    /// swaps this camera's view with the active pane's.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>Snaps a titled window's size to multiples of 40, then resizes the camera's view.</summary>
    void Resize(int32_t w, int32_t h) override;
    /// <summary>Renders the scenario through the camera, the children, and the drag-selection box.</summary>
    void Display() override;
    void Leave() override;
    /// <summary>Draws a one-pixel frame (-1 edges mean the window's).</summary>
    void DrawBox(uint8_t color, int32_t left, int32_t top, int32_t right, int32_t bottom) override;
    using MCGuiTitleWindow::DrawBox;
    std::shared_ptr<MCCamera> GetCamera() override { return Camera.lock(); }
    /// <summary>Sets the camera (null: none); the background colour follows its id (1: 0xef, 2: 0xf8).</summary>
    void SetWindowCamera(std::shared_ptr<MCCamera> newCamera);

    /// <summary>The closest zoom: the view shows 480 lines of the world (the original's 640x480 at camera scale 100).</summary>
    static constexpr float ZoomClosest = 480.0f;
    /// <summary>The furthest zoom: 2160 lines of the world.</summary>
    static constexpr float ZoomFurthest = 2160.0f;

    /// <summary>The world surface's pane (what the camera draws into), sized for the zoom first.</summary>
    MCPane& WorldFrame();
    /// <summary><see cref="WorldFrame"/> shared, for a holder that follows the surface (<see cref="GlobalPane"/>).</summary>
    std::shared_ptr<MCPane> SharedWorldFrame();
    /// <summary>
    /// Sizes the world surface from <see cref="ZoomHeight"/> (the view's height until the zoom starts) and the view's
    /// aspect; it is cleared when its size changes.
    /// </summary>
    void UpdateWorldSurface();
    /// <summary>The world surface's width and height.</summary>
    int32_t WorldWidth() const { return WorldWindow->XMax + 1; }
    int32_t WorldHeight() const { return WorldWindow->YMax + 1; }
    /// <summary>The closest and furthest zoom for this view: 480 and 2160, or less where the terrain grid can't cover
    /// a surface that large (a very wide view).</summary>
    void ZoomLimits(float& closest, float& furthest);
    /// <summary>Moves the zoom toward <paramref name="height"/> (eased over a few frames), within the limits.</summary>
    /// <returns>Whether the target moved.</returns>
    bool ZoomTo(float height);
    /// <summary>Zooms out by <paramref name="factor"/> (in when below 1) from the current target.</summary>
    bool ZoomBy(float factor);
    /// <summary>
    /// The zoom the toggle (and a view's start) zooms in to: one world pixel per screen pixel, within the limits, or
    /// the closest zoom when that is already the furthest.
    /// </summary>
    float ZoomInHeight();
    /// <summary>Whether the target is nearer the furthest zoom than <see cref="ZoomInHeight"/> (the original's
    /// zoomed-out camera scale 1).</summary>
    bool ZoomedOut();
    /// <summary>The zoom toggle (the original flipped the camera between scales 100 and 1): to
    /// <see cref="ZoomInHeight"/> when zoomed out, else to the furthest zoom.</summary>
    void ToggleZoom();
    /// <summary>Eases <see cref="ZoomHeight"/> toward the target: called once a frame by MCCamera::Update, before the
    /// objects place themselves on screen.</summary>
    void EaseZoom();
    /// <summary>Sets the zoom the first time it is needed: one world pixel per screen pixel, within the limits.</summary>
    void StartZoom();
    /// <summary>Screen pixels per world surface pixel, across and down.</summary>
    float WorldScaleX();
    float WorldScaleY();
    /// <summary>The point of the world surface under screen point (<paramref name="screenX"/>, <paramref name="screenY"/>).</summary>
    MCVector2D ScreenToWorld(int32_t screenX, int32_t screenY);
    /// <summary>The window point (relative to this view) a point of the world surface is shown at.</summary>
    MCVector2D WorldToWindow(MCVector2D point);
    /// <summary>The screen point a point of the world surface is shown at.</summary>
    MCVector2D WorldToScreen(MCVector2D point);

    /// <summary>The drag-selection box's corners (x0 y0 where the drag started, x1 y1 where the mouse is), zeroed
    /// by init and when the drag ends; display draws it unless x1 and y1 are both 0.</summary>
    /// <remarks>Port: in the view's own (screen) coordinates, not the world surface's.</remarks>
    std::array<float, 4> SelectionBox{};
    /// <summary>The camera shown (the camera owns its view: a back-reference).</summary>
    std::weak_ptr<MCCamera> Camera;
    /// <summary>Set for the main view: events go to the interface first.</summary>
    bool InterfaceWindow = false;
    /// <summary>A copy of the last event handled.</summary>
    MCGuiEvent LastEvent;

    /// <summary>Port: how many lines of the world the view shows now (0 until the first frame sets it).</summary>
    float ZoomHeight = 0.0f;
    /// <summary>Port: the zoom <see cref="ZoomHeight"/> eases toward.</summary>
    float ZoomTarget = 0.0f;
    /// <summary>Port: when the zoom last eased (MCPort::Milliseconds).</summary>
    uint32_t ZoomClock = 0;
    /// <summary>Port: the view starts at the furthest zoom (its camera's "CameraScale" was 1, zoomed out).</summary>
    bool ZoomStartsOut = false;
    /// <summary>Port: the view has been shown; until then a zoom change takes effect at once, not eased.</summary>
    bool ZoomShown = false;
    /// <summary>Port: the world surface's pixels, window and pane.</summary>
    std::shared_ptr<MCWindow> WorldWindow = std::make_shared<MCWindow>();
    std::shared_ptr<MCPane> WorldPane = std::make_shared<MCPane>();
};

/// <summary>
/// Port: the unit overlays. Health bars, selection marks and strike timers (the elements at depth -50000 and -40000)
/// are drawn on the screen, over the view's rectangle, at the screen's scale: their positions follow the world through
/// the zoom, their sizes don't. While a camera renders, this is the view's pane on the screen and the scale from the
/// camera's world surface to it.
/// </summary>
struct MCOverlayTarget
{
    /// <summary>The pane the overlays draw into (null outside MCCamera::Render).</summary>
    std::shared_ptr<MCPane> Pane;
    /// <summary>Screen pixels per world surface pixel.</summary>
    float ScaleX = 1.0f;
    float ScaleY = 1.0f;
};

/// <summary>Port: where the overlays of the camera rendering go.</summary>
extern MCOverlayTarget MCOverlay;

/// <summary>
/// Port-only (tests): when above 0, every view's zoom is fixed at this many lines of the world (both limits), so a run
/// shows, and updates, the same area whatever the window.
/// </summary>
extern float MCFixedZoomHeight;

/// <summary>Port: whether an element at <paramref name="depth"/> is a unit overlay (-50000 or -40000).</summary>
bool MCIsOverlayDepth(float depth);
/// <summary>Port: a point of the rendering camera's world surface, in the overlays' pane.</summary>
MCVector2D MCOverlayPoint(MCVector2D point);
/// <summary>Port: <see cref="MCOverlayPoint"/> of one coordinate.</summary>
float MCOverlayX(float x);
float MCOverlayY(float y);
/// <summary>Port: the view of the main camera (camera 1), whose screen positions objects keep at index 0, or null.</summary>
std::shared_ptr<MCViewWindow> MCMainView();
/// <summary>
/// Port: screen point (<paramref name="screenX"/>, <paramref name="screenY"/>) in <paramref name="window"/>'s
/// coordinates: for a camera's view, the point of its world surface under it (through the zoom), as the camera's
/// projections take it; for other windows, relative to the window's corner.
/// </summary>
MCVector2D MCWindowPoint(MCGuiObject& window, int32_t screenX, int32_t screenY);
