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
        /// with a way out (a launcher's CANCEL). It stands: see <see cref="OnLost"/>.
        /// </summary>
        bool Wait = false;
        /// <summary>
        /// Port-only (SPEC-menus.md's stage 4): an old dialog's say (MCReusableDialog), which stands as a wait does until
        /// answered or ended: a change of screens hides it, and it says itself again over the next remade screen
        /// (MCRmlDialogs::Tend); with none to say itself over, this shows it the old way instead.
        /// </summary>
        std::function<void()> OnLost;

        /// <summary>Whether a change of screens leaves it standing (a wait, or an old dialog's say).</summary>
        bool Stands() const { return Wait || OnLost != nullptr; }
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
};

namespace MCRmlDialogs
{
    /// <summary>
    /// The dialog on the layer <paramref name="on"/> names (the menus', or Port-only (stage 9) the mission's over the
    /// battlefield at its INTERFACE SIZE), made once there; none when RmlUi can't start.
    /// </summary>
    MCGuiHandle<MCRmlDialog> Dialog(MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus);

    /// <summary>
    /// Says <paramref name="say"/> on the layer <paramref name="on"/> names, over whatever shows there (Port-only,
    /// stage 9: a remade screen under it or none, as the 1998 box went over any screen); false only when the menus'
    /// RmlUi can't start (the 1998 box's turn).
    /// </summary>
    bool Show(MCRmlDialog::MCSay say, MCRmlFlow::MCOn on = MCRmlFlow::MCOn::Menus);

    /// <summary>
    /// What the dialogs belong to when they don't say (the remade screen shown: "Internet", "LAN"), above their sentence.
    /// </summary>
    void SetPlace(std::string place);

    /// <summary>The dialog closed (a job's wait ended, the screen under it gone).</summary>
    void Close();

    /// <summary>Whether it shows (on either layer).</summary>
    bool Shown();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 9): a 1998 dialog box shown (MCReusableDialog's, MCPurchaseDlg's, the version
    /// box) with the words <paramref name="text"/>: logged, and counted while the menus' RmlUi runs, where none should
    /// show (the tests read <see cref="OldBoxes"/>).
    /// </summary>
    void NoteOldBox(std::string_view text);
    size_t OldBoxes();

    /// <summary>A wait shown by <see cref="Waiting"/> ended (the 1998 dialog's Deactivate): the dialog closes.</summary>
    void EndWait();

    /// <summary>
    /// Port-only (SPEC-menus.md's stage 4): whether a say that stands (MCRmlDialog::MCSay::Stands) was hidden by a change
    /// of screens and says itself again at <see cref="Tend"/>; with <paramref name="text"/>, whether that say is it.
    /// </summary>
    bool Returning(std::optional<std::string_view> text = std::nullopt);

    /// <summary>
    /// Each frame, after the screens': a say a change of screens hid says itself again once the change is done (over the
    /// screen it shows), or, where the menus can't start, its old dialog shows it (MCRmlDialog::MCSay::OnLost).
    /// </summary>
    void Tend();

    /// <summary>
    /// An old message (ShowMenuMessage, in the 1998 dialog's capitals) said over the screens, in sentence case; OK runs
    /// <paramref name="then"/>. False when the menus can't start.
    /// </summary>
    bool Message(std::string_view text, std::function<void()> then = {});

    /// <summary>
    /// An old message that asks for a key (the LIGHTS key's) said over the remade screen shown: <paramref name="take"/>
    /// offered each key (MCRmlDialog::MCSay::TakesKey); OK, Escape or a key taken runs <paramref name="then"/>. False
    /// when the menus can't start.
    /// </summary>
    bool AskKey(std::string_view text, std::function<std::optional<std::string>(uint32_t key)> take,
                std::function<void()> then);

    /// <summary>A wait with no way out (ShowWaitingDialog: "Opening your room again...") over the screens.</summary>
    bool Waiting(std::string_view text);

    /// <summary>The 1998 dialog's capitals in sentence case: "BOB CLOSED THE GAME." to "Bob closed the game.".</summary>
    std::string SentenceCase(std::string_view text);

    /// <summary>Text in the 1998 dialog's capitals (no small letter) in sentence case; a sentence already, as it is.</summary>
    std::string AsSentence(std::string_view text);
}
