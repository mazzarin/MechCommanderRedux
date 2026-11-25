#pragma once

#include "ui/MCRmlFlow.h"
#include "ui/MCRmlScreen.h"

#include <optional>

/// <summary>
/// Port-only (SPEC-menus.md's stage 3, "When something goes wrong"): a dialog over the remade screens
/// (<c>ui/dialog.rml</c>): what it belongs to above in cyan, its title when it has one, one short sentence under it,
/// the way back (CANCEL) and the answer (OK, RETRY, UPDATE). Escape is its way back, or its answer when it has only one;
/// one with neither (a wait no one can call off) takes no keys.
/// </summary>
class MCRmlDialog final : public MCRmlScreen
{
public:
    /// <summary>What a dialog says, and what its buttons do.</summary>
    struct MCSay
    {
        /// <summary>What it belongs to: the screen ("Internet"), or the game it names.</summary>
        std::string Eyebrow;
        /// <summary>Its title, when the sentence needs one ("Needs version 0.7.2"); empty for none.</summary>
        std::string Heading;
        /// <summary>The sentence.</summary>
        std::string Text;
        /// <summary>The answer's word ("OK", "Retry", "Update"); empty for none.</summary>
        std::string Main = "OK";
        /// <summary>The way back's word ("Cancel"); empty for none.</summary>
        std::string Other;
        std::function<void()> OnMain;
        std::function<void()> OnOther;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 8): its answer leaves (a match): it opens on its way back, as the spec's
        /// windows that delete or leave do.
        /// </summary>
        bool Leaves = false;
        /// <summary>
        /// A key it asks for, in the game's key code (the LIGHTS key question): each key pressed but its buttons' (Enter,
        /// Escape) and a modifier alone is offered; none returned takes it (the dialog closes, as its answer does),
        /// words say why not (the dialog says them in its sentence's place). Empty: it takes no keys of its own.
        /// </summary>
        std::function<std::optional<std::string>(uint32_t key)> TakesKey;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 4): a wait (MCRmlDialogs::Waiting), which MCRmlDialogs::EndWait ends, even
        /// with a way out (a launcher's CANCEL). It stands: see <see cref="UntilAnswered"/>.
        /// </summary>
        bool Wait = false;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 4): an old dialog's say (MCMenuDialog), which stands as a wait does until
        /// answered or ended: a change of screens hides it, and it says itself again over the next remade screen
        /// (MCRmlDialogs::Tend).
        /// </summary>
        bool UntilAnswered = false;

        /// <summary>Whether a change of screens leaves it standing (a wait, or an old dialog's say).</summary>
        bool Stands() const { return Wait || UntilAnswered; }
    };

    MCRmlDialog();

    /// <summary>Says <paramref name="say"/>, shown over the screens (the one shown before replaced).</summary>
    void Say(MCSay say);

    /// <summary>What it says now (the tests read it).</summary>
    const MCSay& Said() const { return _Say; }

    /// <summary>Presses its answer, or its way back, as a click would (and the tests).</summary>
    void PressMain();
    void PressOther();

    /// <summary>Hidden as answered or ended (MCRmlDialogs::Close, EndWait): it won't say itself again.</summary>
    void End();

    /// <summary>Port-only (stage 9): the layer it was made on (MCRmlDialogs::Dialog), where a say a change hid comes back.</summary>
    void SetLayer(MCRmlFlow::MCOn on) { _On = on; }

    bool TakesKeys() const override;
    bool OnRawKey(const SDL_KeyboardEvent& key) override;

    /// <summary>Text made safe to put in a document (its markup's characters as entities).</summary>
    static std::string Escape(std::string_view text);

protected:
    void OnLoaded(Rml::ElementDocument& document) override;
    MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document) override;
    void OnHide() override;

private:
    /// <summary>The words put in the document, and its buttons shown or not.</summary>
    void Fill();

    MCSay _Say;
    /// <summary>Shown, and not yet answered or ended: hidden now, a change of screens hid it.</summary>
    bool _Standing = false;
    /// <summary>Port-only (stage 9): its layer (MCRmlDialogs::Dialog), where a say a change hid comes back.</summary>
    MCRmlFlow::MCOn _On = MCRmlFlow::MCOn::Menus;
};

namespace MCRmlDialogs
{
    /// <summary>
    /// The dialog on the layer <paramref name="on"/> names (the menus', or Port-only (stage 9) the mission's over the
    /// battlefield at its INTERFACE SIZE), made once there; none when it can't be made.
    /// </summary>
    MCGuiHandle<MCRmlDialog> Dialog(MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus);

    /// <summary>
    /// Says <paramref name="say"/> on the layer <paramref name="on"/> names, over whatever shows there (Port-only,
    /// stage 9: a remade screen under it or none, as the 1998 box went over any screen).
    /// </summary>
    void Show(MCRmlDialog::MCSay say, MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus);

    /// <summary>
    /// What the dialogs belong to when they don't say (the remade screen shown: "Internet", "LAN"), above their sentence.
    /// </summary>
    void SetPlace(std::string place);

    /// <summary>The dialog closed (a job's wait ended, the screen under it gone).</summary>
    void Close();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): the dialog on <paramref name="on"/> closed unanswered, with any say of that
    /// layer's waiting to come back (<see cref="Tend"/>): the way into the battle takes the menus' says with logistics,
    /// as the original's box went with the screens it was said over, and the menus' layer takes no event over the
    /// battlefield.
    /// </summary>
    void Drop(MCRmlFlow::MCOn on);

    /// <summary>Whether it shows (on either layer).</summary>
    bool Shown();

    /// <summary>A wait shown by <see cref="Waiting"/> ended (a menu dialog's Deactivate): the dialog closes.</summary>
    void EndWait();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 4): whether a say that stands (MCRmlDialog::MCSay::Stands) was hidden by a change
    /// of screens and says itself again at <see cref="Tend"/>; with <paramref name="text"/>, whether that say is it.
    /// </summary>
    bool Returning(std::optional<std::string_view> text = std::nullopt);

    /// <summary>
    /// Each frame, after the screens': a say a change of screens hid says itself again once the change is done (over the
    /// screen it shows).
    /// </summary>
    void Tend();

    /// <summary>
    /// An old message (ShowMenuMessage, in the 1998 dialog's capitals) said over the screens, in sentence case; OK runs
    /// <paramref name="then"/>.
    /// </summary>
    void Message(std::string_view text, std::function<void()> then = {});

    /// <summary>
    /// An old message that asks for a key (the LIGHTS key's) said over the remade screen shown: <paramref name="take"/>
    /// offered each key (MCRmlDialog::MCSay::TakesKey); OK, Escape or a key taken runs <paramref name="then"/>.
    /// </summary>
    void AskKey(std::string_view text, std::function<std::optional<std::string>(uint32_t key)> take,
                std::function<void()> then);

    /// <summary>A wait with no way out (ShowWaitingDialog: "Opening your room again...") over the screens.</summary>
    void Waiting(std::string_view text);

    /// <summary>The 1998 dialog's capitals in sentence case: "BOB CLOSED THE GAME." to "Bob closed the game.".</summary>
    std::string SentenceCase(std::string_view text);

    /// <summary>Text in the 1998 dialog's capitals (no small letter) in sentence case; a sentence already, as it is.</summary>
    std::string AsSentence(std::string_view text);
}
