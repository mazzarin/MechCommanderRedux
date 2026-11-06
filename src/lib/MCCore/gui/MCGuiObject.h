#pragma once

#include "gui/MCGuiEvent.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiAnimation.h"
#include "gui/MCGuiRef.h"

class MCCamera;
class MCGuiObject;

/// <summary>A parent's entry for one of its children (<see cref="MCGuiObject::ChildList"/>).</summary>
struct MCGuiChild
{
    /// <summary>The child (found by its control block, also while it is being destroyed).</summary>
    std::weak_ptr<MCGuiObject> Object;
    /// <summary>The parent's ownership of the child, made on it (<see cref="MCGuiObject::CreateChild"/>).</summary>
    MCGuiRef<MCGuiObject> Owner;

    /// <summary>A pin on the child, or null when it is gone.</summary>
    std::shared_ptr<MCGuiObject> Lock() const { return Object.lock(); }
};

/// <summary>The side an <see cref="MCGuiObject"/> slides out to when it hides (<see cref="MCGuiObject::HideMe"/>).</summary>
enum class MCDirection : uint8_t
{
    Left = 0,
    Up = 1,
    Right = 2,
    Down = 3
};

/// <summary>A window's display state (<see cref="MCGuiObject::WinState"/>).</summary>
enum class MCGuiWindowState : int32_t
{
    Normal = 0,
    Maximized = 1,
    Iconized = 2
};

/// <summary>
/// The base of every GUI element: a rectangle at (x, y) relative to its parent, with a port it draws into, a pane on
/// the screen it is copied to, an optional background picture, icon and animation, children, and optional paint and
/// event routines. It can be maximized, iconized, dragged, and slid off the screen (hidden).
/// </summary>
/// <remarks>
/// <para>Original source: <c>gui\asystem.cpp</c> and <c>gui\asystem.h</c> (<c>aObject</c>).</para>
/// <para>The tree owns its objects: an object is made on its parent (<see cref="CreateChild"/>), which owns it and
/// hands out an <see cref="MCGuiHandle"/>; only the root is owned from outside (an <see cref="MCGuiRef"/>). Destroy is
/// an event on a live object, as the original's and Windows' <c>WM_DESTROY</c>: <see cref="Destroy"/> runs the class's
/// own cleanup (<see cref="OnDestroy"/>), destroys and releases the children, then the base's cleanup, and takes the
/// object off its parent. It runs once, when the owner lets go (a child taken off its parent, its parent destroyed, the
/// root's owner gone) or when it is called.</para>
/// </remarks>
class MCGuiObject : public std::enable_shared_from_this<MCGuiObject>
{
public:
    MCGuiObject() = default;
    /// <summary>Runs the base's <see cref="Destroy"/> if nothing did.</summary>
    virtual ~MCGuiObject();
    MCGuiObject(const MCGuiObject&) = delete;
    MCGuiObject& operator=(const MCGuiObject&) = delete;

    /// <summary>
    /// Places the object at (<paramref name="xPos"/>, <paramref name="yPos"/>) with the given size, makes its port
    /// and its screen pane, and resets its state. <paramref name="name"/> is unused by the base.
    /// </summary>
    /// <returns>0, or the port's error.</returns>
    virtual int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name);

    /// <summary>
    /// Destroys the object, once: the class's own cleanup (<see cref="OnDestroy"/>, its children still on it), then
    /// each child is destroyed and released, then the base frees the port,
    /// pane, background, icon, animation and drop targets, removes the object from its timers and its parent, and lets
    /// go of every system reference to it (grab, text focus, modal, current object).
    /// </summary>
    /// <remarks>Not virtual: a class adds its own cleanup in <see cref="OnDestroy"/>.</remarks>
    void Destroy();

    /// <summary>
    /// The class's own cleanup, run by <see cref="Destroy"/> first, while the object, its parent and its children are
    /// alive. An override does its part, then calls its base's. Nothing in the base.
    /// </summary>
    virtual void OnDestroy() {}

    /// <summary>Whether <see cref="Destroy"/> has run (a handle to the object is empty from then on).</summary>
    bool WasDestroyed() const { return _Destroyed; }

    /// <summary>
    /// Makes a <typeparamref name="T"/> from <paramref name="args"/>, sets it up with <paramref name="setup"/> (its
    /// <c>Init</c>), and adds it as a child this object owns.
    /// </summary>
    /// <returns>A handle to the child: the parent is its only owner.</returns>
    template <typename T, typename Setup, typename... Args>
        requires std::derived_from<T, MCGuiObject> && std::invocable<Setup&, T&>
    MCGuiHandle<T> CreateChild(Setup&& setup, Args&&... args)
    {
        MCGuiRef<T> made = MCGuiRef<T>::Make(std::forward<Args>(args)...);
        setup(*made);
        MCGuiHandle<T> handle = made.Handle();
        AddOwnedChild(std::move(made));
        return handle;
    }

    /// <summary>
    /// Like <see cref="CreateChild"/>, for a <paramref name="setup"/> that can fail: it returns 0, or an error code,
    /// and a child that failed is not added (it is destroyed at once).
    /// </summary>
    /// <returns>A handle to the child, or <paramref name="setup"/>'s error.</returns>
    template <typename T, typename Setup, typename... Args>
        requires std::derived_from<T, MCGuiObject> && std::is_invocable_r_v<int32_t, Setup&, T&>
    std::expected<MCGuiHandle<T>, int32_t> TryCreateChild(Setup&& setup, Args&&... args)
    {
        MCGuiRef<T> made = MCGuiRef<T>::Make(std::forward<Args>(args)...);

        if (const int32_t result = setup(*made); result != 0)
        {
            return std::unexpected(result);
        }

        MCGuiHandle<T> handle = made.Handle();
        AddOwnedChild(std::move(made));
        return handle;
    }

    /// <summary>Takes the object off its parent, which destroys it (nothing for the root).</summary>
    void Close();

    virtual int32_t Width();
    virtual int32_t Height();
    /// <summary>The x position, relative to the parent.</summary>
    virtual int32_t X();
    virtual int32_t Y();

    /// <summary>
    /// Moves the object to (<paramref name="xPos"/>, <paramref name="yPos"/>) relative to its parent, moves its pane
    /// and its children's. Unless <paramref name="temporary"/>, the position also becomes its home (where
    /// <see cref="HideMe"/> slides back to).
    /// </summary>
    virtual void MoveTo(int32_t xPos, int32_t yPos, bool temporary = false);

    /// <summary>
    /// Puts the pane at the object's position on <paramref name="base"/>, as if it were <paramref name="base"/>'s
    /// child, for an object that <paramref name="base"/> keeps and displays itself (the file pane's column headers).
    /// </summary>
    void PlaceOver(MCGuiObject& base);

    /// <summary>Resizes the object and its port (snapped to the 40-pixel grid when <see cref="GridAligned"/>).</summary>
    virtual void Resize(int32_t newWidth, int32_t newHeight);

    /// <summary>The object's pane on the screen (made by <see cref="Init"/>).</summary>
    virtual MCPane& Frame();

    /// <summary>
    /// Takes <paramref name="child"/> off, which destroys it (it is still on its parent while its cleanup runs; the
    /// entry is found by its control block). Nothing for an object that isn't a child of this one.
    /// </summary>
    virtual void RemoveChild(MCGuiObject& child);
    virtual void SetParent(std::weak_ptr<MCGuiObject> newParent);
    /// <summary>The parent, or null; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCGuiObject> ParentObject() const { return Parent.lock(); }
    /// <summary>The object as a <typeparamref name="T"/> that shares its ownership (it must be one).</summary>
    template <typename T> std::shared_ptr<T> SharedAs() { return std::static_pointer_cast<T>(shared_from_this()); }
    /// <summary>A reference to the object as a <typeparamref name="T"/> (empty when no shared_ptr owns it).</summary>
    template <typename T> std::weak_ptr<T> WeakAs() { return std::static_pointer_cast<T>(weak_from_this().lock()); }
    virtual int32_t NumberOfChildren();
    /// <summary>The children there are, in depth order, pinned for a walk.</summary>
    std::vector<std::shared_ptr<MCGuiObject>> Children() const;
    /// <summary>The x position on the screen (the sum of the parents' positions).</summary>
    virtual int32_t GlobalX();
    virtual int32_t GlobalY();

    /// <summary>
    /// Draws the object into its port: the background (or the icon when iconized), the animation, the paint routine,
    /// then the children.
    /// </summary>
    virtual void Draw();

    /// <summary>
    /// The front-most visible object at (<paramref name="xPos"/>, <paramref name="yPos"/>): a child or this, or null;
    /// the caller keeps it as its pin.
    /// </summary>
    virtual std::shared_ptr<MCGuiObject> FindObject(int32_t xPos, int32_t yPos);

    /// <summary>Whether the screen point is inside the pane (its right and bottom edges included).</summary>
    virtual bool PointInside(int32_t xPos, int32_t yPos);
    /// <summary>Whether <paramref name="area"/> overlaps the pane.</summary>
    virtual bool RectIntersect(tagRECT area);
    /// <summary>Whether the rectangle overlaps the pane.</summary>
    virtual bool RectIntersect(int32_t left, int32_t top, int32_t right, int32_t bottom);

    /// <summary>
    /// The default handling: a click brings the window to the front; an iconized window is dragged by the mouse;
    /// the close event destroys it; the screen-resized event goes to every child. Then the event routine, if any.
    /// </summary>
    virtual void HandleEvent(MCGuiEvent& event);

    /// <summary>Loads background picture <paramref name="artPacket"/> of the art file.</summary>
    virtual int32_t SetBackground(int32_t artPacket);
    /// <summary>Loads background picture <paramref name="fileName"/> (a TGA under <c>ArtPath</c>).</summary>
    virtual int32_t SetBackground(std::string_view fileName);

    /// <summary>Loads the animation (a VFX shape file under <c>ArtPath</c>).</summary>
    virtual int32_t SetAnimation(std::string_view fileName);
    virtual void StartAnimation();
    virtual void StopAnimation();
    /// <summary>The background picture, or null.</summary>
    virtual std::shared_ptr<MCGuiPort> Background();
    /// <summary>The animation, or null.</summary>
    virtual std::shared_ptr<MCGuiAnimation> Animation();
    /// <summary>The icon, or null.</summary>
    virtual std::shared_ptr<MCGuiAnimation> Icon();

    /// <summary>
    /// Changes the depth (takes the object off its parent and adds it back so it sorts again; one its parent owns keeps
    /// its owner throughout).
    /// </summary>
    virtual void SetDepth(int32_t newDepth);

    /// <summary>
    /// Moves the object to the front of its parent's children of the same depth (the child list is kept sorted by
    /// depth), its parent to the front of its own, and so on up. Without <paramref name="noShuffle"/>, grid-aligned
    /// siblings on the same 40-pixel cell are nudged aside.
    /// </summary>
    virtual void BringToFront(bool noShuffle);

    /// <summary>The front-most child of depth <paramref name="atDepth"/>, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiObject> ForemostChild(int32_t atDepth);
    virtual int32_t Depth();
    /// <summary>Child <paramref name="index"/>, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiObject> Child(int32_t index);
    virtual int32_t DragStartX();
    virtual int32_t DragStartY();
    virtual bool Dragging();
    virtual void StartDrag(int32_t xPos, int32_t yPos);
    virtual void StopDrag();

    /// <summary>The mouse came over the object. Does nothing in the base.</summary>
    virtual void Enter() {}
    /// <summary>The mouse left the object. Does nothing in the base.</summary>
    virtual void Leave() {}

    virtual int32_t BackColor();
    /// <summary>The colour the port is wiped to (0xff: none).</summary>
    virtual void SetBackColor(int32_t color);
    /// <summary>Loads the icon (a VFX shape file); the iconized size becomes the icon's.</summary>
    virtual int32_t SetIcon(std::string_view fileName);
    /// <summary>
    /// Switches between normal, maximized and iconized (<see cref="MCGuiWindowState"/>), saving the current placement
    /// and restoring the new state's. (The startup window uses the slot for its own states.)
    /// </summary>
    virtual void SetState(int32_t newState);
    virtual void Iconize();
    virtual void Normalize();
    virtual void Maximize();

    /// <summary>
    /// Shows the object: steps a pending slide (<see cref="HideMe"/>), copies the port to the pane (transparently
    /// when <see cref="Transparent"/>), then displays the children.
    /// </summary>
    virtual void Display();

    virtual void SetPaintRoutine(std::function<void(MCGuiObject&)> routine);
    virtual void SetEventRoutine(std::function<void(MCGuiObject&, MCGuiEvent&)> routine);
    /// <summary>Runs the paint routine, if any.</summary>
    virtual void Paint();
    virtual void StartModal();
    virtual void StopModal();
    virtual MCGuiWindowState State();
    /// <summary>The port the object draws into (made by <see cref="Init"/>; see <see cref="DisplayPort"/>).</summary>
    virtual MCGuiPort& Port();
    /// <summary>Makes the pane (and the children's) show <paramref name="newPort"/>'s bitmap.</summary>
    virtual void SetDisplayPort(MCGuiPort& newPort);

    /// <summary>
    /// Draws a 3-line bevelled frame around the port (sunken when <paramref name="pushed"/>), after wiping it to the
    /// back colour when <paramref name="fill"/>.
    /// </summary>
    virtual void DrawFramed(bool pushed, bool fill);
    /// <summary>Draws the outline of <paramref name="area"/>.</summary>
    virtual void DrawBox(uint8_t color, tagRECT area);
    /// <summary>Draws a rectangle outline in the port; -1 for a side means the port's edge.</summary>
    virtual void DrawBox(uint8_t color, int32_t left, int32_t top, int32_t right, int32_t bottom);

    /// <summary>Shows or hides the window (a hidden one isn't drawn or found).</summary>
    virtual void ShowGuiWindow(bool show) { ShowWindow = show; }
    /// <summary>The camera shown in the window (null for the base); the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCCamera> GetCamera() { return nullptr; }
    virtual bool IsShowing() { return ShowWindow; }
    /// <summary>
    /// Starts sliding the object off the screen in its <see cref="HideDirection"/> (<paramref name="hide"/>), or back
    /// to its home position.
    /// </summary>
    virtual void HideMe(bool hide);
    virtual bool IsHidden() { return Hidden; }
    virtual void SetHideDirection(MCDirection direction) { HideDirection = direction; }
    /// <summary>Whether the port is copied to the screen with colour 0 transparent.</summary>
    virtual void SetTransparent(bool on) { Transparent = on; }
    virtual int32_t Left() { return X(); }
    virtual int32_t Top() { return Y(); }
    virtual int32_t Right() { return X() + Width(); }
    virtual int32_t Bottom() { return Y() + Height(); }
    /// <summary>Lets a movie window apply the movie's palette. Does nothing in the base.</summary>
    virtual void CheckSmackerPalette() {}

    /// <summary>
    /// Port-only (the original had no wheel): scrolls by <paramref name="steps"/> arrow clicks, negative up, positive
    /// down. The wheel is offered to the object under the mouse (at <paramref name="xPos"/>, <paramref name="yPos"/>),
    /// then to its parents, until one takes it. The position lets an object that draws a hidden child itself (the
    /// logistics weapon lists) pass the wheel on to that child.
    /// </summary>
    /// <returns>Whether the object scrolls (whether or not it could move); false passes the wheel to the parent.</returns>
    virtual bool MouseWheel([[maybe_unused]] int32_t steps, [[maybe_unused]] int32_t xPos,
                            [[maybe_unused]] int32_t yPos)
    {
        return false;
    }

    /// <summary>
    /// Port-only: whether the object draws itself in the frame pass instead of keeping a picture. Its port is then a
    /// view (<see cref="MCGuiPort::InitView"/>): each frame <see cref="Display"/> opens the view's scissor (its
    /// rectangle on the screen, cut to a clipping ancestor's: <see cref="ClipsChildren"/>), calls <see cref="Draw"/>,
    /// which renders the object from its state, and shuts it again; draws at any other time do nothing. A class decides
    /// for itself; a plain object draws itself once its owner calls <see cref="SetDrawsLive"/>.
    /// </summary>
    virtual bool DrawsLive() { return LiveDraw; }

    /// <summary>
    /// Port-only: makes this object draw itself in the frame pass (see <see cref="DrawsLive"/>): for an object whose
    /// picture holds only what <see cref="Draw"/> puts there (its background, animation and paint routine). Its port
    /// becomes a view now, or when <see cref="Init"/> makes it.
    /// </summary>
    void SetDrawsLive();

    /// <summary>
    /// Port-only: whether the children that draw themselves are cut to this object's rectangle. Not by default: the
    /// original copied each picture to the screen on its own, and children often lie outside their parent (a scroll
    /// thumb beside its text, a combo box's list below it). A pane that scrolls its children opts in.
    /// </summary>
    virtual bool ClipsChildren() { return false; }

    /// <summary>Wipes a rectangle of the port to <paramref name="color"/>.</summary>
    void FillBox(int16_t left, int16_t top, int16_t right, int16_t bottom, uint8_t color) const;

    /// <summary>Sets one pixel of the port.</summary>
    void SetBit(int32_t xPos, int32_t yPos, uint8_t color);

    /// <summary>Whether the object snaps to the 40-pixel grid when resized or raised.</summary>
    bool GridAligned = false;
    /// <summary>The object's pane on the screen window (made by <see cref="Init"/>; shared with
    /// <see cref="MCOverlay"/> while a camera renders through this view).</summary>
    std::shared_ptr<MCPane> FramePane;
    int32_t WinWidth = 0;
    int32_t WinHeight = 0;
    /// <summary>The position relative to the parent.</summary>
    int32_t WinX = 0;
    int32_t WinY = 0;
    /// <summary>The depth: children are kept sorted by it (front-most last).</summary>
    int32_t WinDepth = 0;
    /// <summary>The placement while maximized (width, height, x, y).</summary>
    int32_t MaxWidth = 0;
    int32_t MaxHeight = 0;
    int32_t MaxX = 0;
    int32_t MaxY = 0;
    /// <summary>The placement while normal (width, height, x, y).</summary>
    int32_t NormalWidth = 0;
    int32_t NormalHeight = 0;
    int32_t NormalX = 0;
    int32_t NormalY = 0;
    /// <summary>The placement while iconized (the icon's size, and x, y).</summary>
    int32_t IconWidth = 0;
    int32_t IconHeight = 0;
    int32_t IconX = 0;
    int32_t IconY = 0;
    MCGuiWindowState WinState = MCGuiWindowState::Normal;
    /// <summary>Whether the object is shown and can be found (<see cref="IsShowing"/>); set by <see cref="Init"/>.</summary>
    bool ShowWindow = false;
    /// <summary>What kind of object this is, for the interface's hit tests: -1 by default, 1 the screen window, 4 a
    /// camera view window, 7 a floating help.</summary>
    int16_t ObjectType = 0;
    /// <summary>Whether hidden (slid off the screen).</summary>
    bool Hidden = false;
    /// <summary>How far the object still has to slide (signed); 0 when not moving.</summary>
    int32_t HideOffset = 0;
    /// <summary>The side it slides out to (down after <see cref="Init"/>).</summary>
    MCDirection HideDirection = MCDirection::Left;
    /// <summary>The home position the slide returns to.</summary>
    int32_t HomeX = 0;
    int32_t HomeY = 0;
    /// <summary>Whether the port is copied transparently.</summary>
    bool Transparent = false;
    /// <summary>The children, sorted by depth; the ones made on this object are owned by it.</summary>
    std::vector<MCGuiChild> ChildList;
    /// <summary>The parent (a back-reference).</summary>
    std::weak_ptr<MCGuiObject> Parent;
    /// <summary>The background picture, copied into the port by <see cref="Draw"/>.</summary>
    std::shared_ptr<MCGuiPort> BackgroundPort;
    /// <summary>The icon drawn while iconized.</summary>
    std::shared_ptr<MCGuiAnimation> IconAnimation;
    /// <summary>The animation drawn over the background while <see cref="Animating"/>.</summary>
    std::shared_ptr<MCGuiAnimation> WindowAnimation;
    /// <summary>Where a drag started.</summary>
    int32_t DragX = 0;
    int32_t DragY = 0;
    bool DragOn = false;
    bool Animating = false;
    /// <summary>The colour the port is wiped to; 0xff for none.</summary>
    int32_t BackgroundColor = 0;
    /// <summary>The port the object draws into.</summary>
    std::unique_ptr<MCGuiPort> DisplayPort;
    std::function<void(MCGuiObject&)> PaintRoutine;
    std::function<void(MCGuiObject&, MCGuiEvent&)> EventRoutine;
    /// <summary>The screen rectangles dragged items can be dropped on.</summary>
    std::vector<tagRECT> DropTargets;
    /// <summary>Port: set by <see cref="SetDrawsLive"/>.</summary>
    bool LiveDraw = false;

protected:
    /// <summary>
    /// The placement part of <see cref="Init"/>, shared with the classes that make no port of their own: the size and
    /// position (also as the normal, maximized and iconized placement and the home), a fresh pane on the screen, and
    /// the state reset (shown, not hidden, no children, routines or animations).
    /// </summary>
    void Place(int32_t xPos, int32_t yPos, int32_t width, int32_t height);

    /// <summary>
    /// Port: steps a pending slide (<see cref="HideMe"/>) by its whole offset, stopping it once the object is off the
    /// screen or back home. Part of <see cref="Display"/>.
    /// </summary>
    void SlideStep();

    /// <summary>
    /// Port: the frame pass of an object that <see cref="DrawsLive"/>: draws it into <paramref name="port"/>'s view
    /// over its pane (the view's pixel (0, <paramref name="scrollY"/>) at the pane's corner, for a port taller than
    /// the object), then displays the children unless <paramref name="displayChildren"/> is false. Without
    /// <paramref name="wipe"/>, an opaque object leaves what is under the parts it doesn't draw (the original copied
    /// pieces to the screen, not a whole picture).
    /// </summary>
    void DrawInFramePass(MCGuiPort& port, int32_t scrollY = 0, bool wipe = true, bool displayChildren = true);

    /// <summary>
    /// Port: whether <see cref="Draw"/> draws <paramref name="child"/> too: only a child with a picture, and not while
    /// this object draws in the frame pass (the children display themselves after it).
    /// </summary>
    bool DrawsChild(MCGuiObject& child);

    /// <summary>
    /// Port: the children part of a <see cref="Draw"/>: nothing in the frame pass (the children display themselves
    /// after it); otherwise, as the original's paint, each child with a picture draws into it (a child that draws
    /// itself is drawn in the frame pass only).
    /// </summary>
    void DrawChild(MCGuiObject& child);

    /// <summary>Displays every child (in depth order; a child may add or remove children as it goes).</summary>
    void DisplayChildren();

    /// <summary>
    /// Takes the parent's entry of this object away and forgets the parent (part of destroying it; the entry is found
    /// by its control block, so it works while the last share is going).
    /// </summary>
    void LeaveParent();

private:
    /// <summary>The base's cleanup in <see cref="Destroy"/>, after the children.</summary>
    void DestroyObject();

    /// <summary>Adds <paramref name="child"/>, made for this object, as a child it owns.</summary>
    void AddOwnedChild(MCGuiRef<MCGuiObject> child);
    /// <summary>Whether <paramref name="child"/> is one of this object's children (each is owned by it).</summary>
    /// <summary>Whether <paramref name="child"/> is on the list as a child this object owns.</summary>
    bool Owns(const MCGuiObject& child) const;

    /// <summary>
    /// Takes <paramref name="child"/>'s entry out of the list, with this object's ownership of it if it has it; none
    /// when the child isn't on the list. The child's parent is left as it is.
    /// </summary>
    std::optional<MCGuiChild> TakeEntry(const MCGuiObject& child);

    /// <summary>Puts <paramref name="entry"/> on the list as the front-most of its depth and places the child.</summary>
    void InsertEntry(MCGuiChild entry);

    /// <summary>Empties the child list: each child loses its parent and is destroyed and released.</summary>
    void ReleaseChildren();

    /// <summary>Whether <see cref="Destroy"/> has run.</summary>
    bool _Destroyed = false;
};

/// <summary>
/// Takes the object <paramref name="object"/> refers to off its parent (<see cref="MCGuiObject::Close"/>), which
/// destroys it; nothing once it is gone.
/// </summary>
template <typename T> void CloseGuiObject(const MCGuiHandle<T>& object)
{
    if (const std::shared_ptr<T> closed = object.Lock(); closed != nullptr)
    {
        closed->Close();
    }
}
