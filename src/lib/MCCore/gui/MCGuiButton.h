#pragma once

#include "gui/MCGuiCallback.h"
#include "gui/MCGuiObject.h"

/// <summary>
/// A push button: up, down (while held) and gray (disabled) pictures, and two callbacks run when the left or right
/// button is released over it. Pictures given as a number are packets of the art file.
/// </summary>
/// <remarks>Original source: <c>gui\abutton.cpp</c> and <c>gui\abutton.h</c> (<c>aButton</c>).</remarks>
class MCGuiButton : public MCGuiObject
{
public:
    ~MCGuiButton() override;

    /// <summary>Like the base's <see cref="MCGuiObject::Init"/>; makes the two callbacks and wipes the port to colour 0.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Frees the pictures and the callbacks.</summary>
    void OnDestroy() override;
    /// <summary>
    /// Draws the gray, down (while grabbed) or up picture, or wipes to the back colour; the frame first when
    /// <see cref="Framed"/>.
    /// </summary>
    void Draw() override;
    /// <summary>
    /// Unless disabled: grabs the mouse on a press, and on a release over the button runs <see cref="LeftCallback"/>
    /// (left) or <see cref="RightButtonCallback"/> (right). Then the event routine.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;
    /// <summary>
    /// Port: buttons (and every derived button) draw themselves each frame from their state (pictures, disabled,
    /// grabbed, pushed).
    /// </summary>
    bool DrawsLive() override { return true; }

    /// <summary>The up picture, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiPort> GetUpPicture() { return UpPicture; }
    /// <summary>The down picture, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiPort> GetDownPicture() { return DownPicture; }
    /// <summary>The gray picture, or null; the caller keeps it as its pin.</summary>
    virtual std::shared_ptr<MCGuiPort> GetGrayPicture() { return GrayPicture; }
    /// <summary>The callback run by a left click (made by <see cref="Init"/>).</summary>
    virtual MCGuiCallback& Callback() { return *LeftCallback; }
    /// <summary>The callback run by a right click (made by <see cref="Init"/>).</summary>
    virtual MCGuiCallback& RightCallback() { return *RightButtonCallback; }
    /// <summary>Runs the left click's callback (see <see cref="MCGuiCallback::Run"/>).</summary>
    void RunCallback() const { MCGuiCallback::Run(LeftCallback); }

    /// <summary>Loads the up picture from a file and sizes the button to it; the back colour becomes 0xff (none).</summary>
    void SetUpPicture(std::string_view fileName);
    void SetGrayPicture(std::string_view fileName);
    /// <summary>Loads the down picture (the size is left alone).</summary>
    void SetDownPicture(std::string_view fileName);
    /// <summary>Loads the up picture from art packet <paramref name="artPacket"/> and sizes the button to it.</summary>
    void SetUpPicture(int32_t artPacket);
    void SetGrayPicture(int32_t artPacket);
    void SetDownPicture(int32_t artPacket);

    std::shared_ptr<MCGuiPort> UpPicture;
    std::shared_ptr<MCGuiPort> DownPicture;
    std::shared_ptr<MCGuiPort> GrayPicture;
    /// <summary>The left click's callback (shared: it runs unpinned, see <see cref="MCGuiCallback::Run"/>).</summary>
    std::shared_ptr<MCGuiCallback> LeftCallback;
    std::shared_ptr<MCGuiCallback> RightButtonCallback;
    /// <summary>Whether disabled (gray, ignores the mouse).</summary>
    bool Disabled = false;
    /// <summary>Whether the bevelled frame (<see cref="MCGuiObject::DrawFramed"/>) is drawn; on after init.</summary>
    bool Framed = false;

protected:
    /// <summary>
    /// Whether a mouse event lies on the button (its rectangle in its parent's coordinates, right and bottom edges
    /// out).
    /// </summary>
    bool EventOnButton(const MCGuiEvent& event);

private:
    /// <summary>
    /// Replaces <paramref name="picture"/> with one loaded from <paramref name="source"/> (a file name or an art
    /// packet). With <paramref name="sizeButton"/> the button takes the picture's size and loses its back colour
    /// (0xff); a picture that fails to load is dropped.
    /// </summary>
    template <typename Source> void LoadPicture(std::shared_ptr<MCGuiPort>& picture, Source source, bool sizeButton);
};

/// <summary>A window's close button: like a button, but a left release over it runs only the callback.</summary>
/// <remarks>Original source: <c>gui\abutton.cpp</c> (<c>aCloseButton</c>).</remarks>
class MCGuiCloseButton : public MCGuiButton
{
public:
    void HandleEvent(MCGuiEvent& event) override;
};

/// <summary>A toggle button: each click flips <see cref="Pushed"/> and runs the callback.</summary>
/// <remarks>Original source: <c>gui\abutton.cpp</c> (<c>aToolButton</c>).</remarks>
class MCGuiToolButton : public MCGuiButton
{
public:
    /// <summary>Clears <see cref="Pushed"/>, then the button's init.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;
    /// <summary>Draws the down picture while pushed, the up one otherwise (transparently), then the frame.</summary>
    void Draw() override;
    /// <summary>A left press toggles <see cref="Pushed"/> and runs the callback; the rest as a button.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    bool Pushed = false;
};
