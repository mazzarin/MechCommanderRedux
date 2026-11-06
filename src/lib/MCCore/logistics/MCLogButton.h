#pragma once

#include "gui/MCGuiCallback.h"
#include "logistics/MCLogObject.h"

/// <summary>
/// A logistics-screen push button: up/over/down/gray pictures, a press sound, an enter (hover) sound and a callback
/// run when it is clicked.
/// </summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>lButton</c>).</remarks>
class MCLogButton : public MCLogObject
{
public:
    /// <summary>The sample played on a click of a disabled button.</summary>
    static constexpr uint32_t DisabledSound = 0x33;

    /// <summary>Destroys the button if nothing did (its cleanup lets go of the held button).</summary>
    ~MCLogButton() override;

    /// <summary>Places the button, clears its callback and pictures, and makes its port.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>Frees the four pictures and clears the callback.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Shows the picture for the button's state: gray while disabled, down while <see cref="Pressed"/> or held (grabbed
    /// with the mouse over it), over while the mouse is over it, else up; the back colour when that picture is
    /// missing. Port: drawn each frame from the state.
    /// </summary>
    void Draw() override;

    /// <summary>Port: the button draws itself each frame from its state.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// On a click: plays the press sound and runs the callback (or the "disabled" sound when grayed), then passes the
    /// event to the event routine. Port: a release ends the press shown.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The mouse came over the button: highlights it and plays the enter sound.</summary>
    void Enter() override;

    /// <summary>The mouse left the button: drops the highlight (and a press still shown).</summary>
    void Leave() override;

    /// <summary>Port: the button shows pressed (a click on it); any other button's press ends.</summary>
    void Press();

    /// <summary>
    /// Port: the press shown ends (the release, the mouse leaving or coming back, or a screen shown afresh: the
    /// original's next paint showed the button up).
    /// </summary>
    static void LetGoPress();

    std::shared_ptr<MCLogPort> GetUpPicture() const { return UpPicture; }
    std::shared_ptr<MCLogPort> GetOverPicture() const { return OverPicture; }
    std::shared_ptr<MCLogPort> GetDownPicture() const { return DownPicture; }
    std::shared_ptr<MCLogPort> GetGrayPicture() const { return GrayPicture; }
    /// <summary>The callback run on a click (set its function with <see cref="MCGuiCallback::SetExec"/>).</summary>
    MCGuiCallback& Callback() { return *ButtonCallback; }
    /// <summary>Runs the click's callback (see <see cref="MCGuiCallback::Run"/>).</summary>
    void RunCallback() const { MCGuiCallback::Run(ButtonCallback); }

    /// <summary>Loads the picture shown normally; the button takes its size.</summary>
    void SetUpPicture(std::string_view fileName);
    /// <summary>Loads the picture shown under the mouse.</summary>
    void SetOverPicture(std::string_view fileName);
    /// <summary>Loads the picture shown while disabled.</summary>
    void SetGrayPicture(std::string_view fileName);
    /// <summary>Loads the picture shown while pressed.</summary>
    void SetDownPicture(std::string_view fileName);

    /// <summary>
    /// Set by a click: shows the down picture. Port: until the release or the mouse leaving (the original's next
    /// paint showed it up again).
    /// </summary>
    bool Pressed = false;
    std::shared_ptr<MCLogPort> UpPicture;
    std::shared_ptr<MCLogPort> DownPicture;
    std::shared_ptr<MCLogPort> GrayPicture;
    std::shared_ptr<MCLogPort> OverPicture;
    std::shared_ptr<MCGuiCallback> ButtonCallback = std::make_shared<MCGuiCallback>();
    /// <summary>The button is grayed out and ignores clicks.</summary>
    bool Disabled = false;
    /// <summary>The mouse is over the button.</summary>
    bool OverState = false;
    /// <summary>The digital sample played on a click (0xf by default; the ini's PressSFX).</summary>
    uint32_t PressSound = 0xf;
    /// <summary>The digital sample played when the mouse comes over the button (the ini's OverSFX; -1 = none).</summary>
    uint32_t OverSound = 0xffffffff;

    /// <summary>Port: the button shown pressed (one mouse: one press at a time), if any.</summary>
    static inline std::weak_ptr<MCLogButton> HeldButton;

protected:
    /// <summary>
    /// Port: draws <paramref name="picture"/> as the button's face (with 0xff as a colour key when
    /// <paramref name="keyed"/>), or fills the button with its back colour when there is none, then the background and
    /// children as an <see cref="MCLogObject"/>. Only in the frame pass.
    /// </summary>
    void DrawFace(MCOptionalRef<const MCLogPort> picture, bool keyed);
};
