#pragma once

#include "ui/MCRmlScreen.h"

/// <summary>
/// Port-only (SPEC-menus.md, "Dialogs"; stage 6): a question over a remade screen (<c>ui/dialog_ask.rml</c>), modal:
/// its heading the question ("Remove the HD pack?"), CANCEL and its act ("Remove"), the act the focus's. CANCEL,
/// Escape and the act each close it; the act's work runs as it closes.
/// </summary>
class MCQuestionWindow final : public MCRmlScreen
{
public:
    MCQuestionWindow(std::string heading, std::string act, std::function<void()> yes);

    /// <summary>
    /// Opens the question over the top of <paramref name="over"/>'s layer; <paramref name="closed"/> runs after either
    /// answer (the screen under it shows the setting as it is then). False when its document can't be read.
    /// </summary>
    static bool Ask(MCRmlScreen& over, std::string heading, std::string act, std::function<void()> yes,
                    std::function<void()> closed);

protected:
    void OnLoaded(Rml::ElementDocument& document) override;

private:
    /// <summary>Closes the window, then runs <paramref name="then"/> and the closed's.</summary>
    void Answer(const std::function<void()>& then);

    std::string _Heading;
    std::string _Act;
    std::function<void()> _Yes;
    std::function<void()> _Closed;
};
