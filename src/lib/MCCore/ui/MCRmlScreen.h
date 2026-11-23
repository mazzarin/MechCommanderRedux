#pragma once

#include "gui/MCGuiRef.h"
#include "gui/MCScreenChange.h"
#include "lib/MCOptionalRef.h"

namespace Rml
{
    class Context;
    class Element;
    class ElementDocument;
}

class MCRmlLayer;

/// <summary>A key the hint line names, with what it does on the screen (SPEC-menus.md, "Titles and bars").</summary>
struct MCRmlKeyHint
{
    /// <summary>The key's name as boxed (<c>ESC</c>, <c>Q E</c>, <c>ENTER</c>).</summary>
    std::string Key;
    /// <summary>What it does, in Fine capitals (<c>BACK</c>, <c>CHANGE TAB</c>).</summary>
    std::string Does;
};

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built" and "PR #59's rules on RmlUi"): a remade screen, made on its layer
/// (<see cref="MCRmlLayer::CreateChild"/>), which owns it and hands out an <see cref="MCGuiHandle"/>; it owns its RML
/// document, read from the app's <c>ui</c> folder. Opening it is the screen flow's (MCScreenFlow): its
/// <see cref="Prepare"/> pieces run live from the press (the document read hidden, styled and laid out, its first frame
/// recorded), then <see cref="Show"/> once the screen before is dark, polled <see cref="Ready"/> until its first frame
/// is whole; <see cref="Hide"/> when another shows. <see cref="Open"/> does it all at once (tools, tests).
/// </summary>
/// <remarks>
/// Destroy is an event on a live screen, as an MCGuiObject's: <see cref="OnDestroy"/> (an override does its part, then
/// calls its base's) while its document and models are still there, then the base closes the document. RmlUi's
/// pointers stay at its interface: the screen hands out references.
/// </remarks>
class MCRmlScreen : public std::enable_shared_from_this<MCRmlScreen>
{
public:
    MCRmlScreen();
    /// <summary>Runs <see cref="Destroy"/> if nothing did.</summary>
    virtual ~MCRmlScreen();
    MCRmlScreen(const MCRmlScreen&) = delete;
    MCRmlScreen& operator=(const MCRmlScreen&) = delete;

    /// <summary>The document the screen shows (a path under the app's <c>ui</c> folder), set before it's prepared.</summary>
    void SetDocument(std::string path) { _Path = std::move(path); }
    const std::string& DocumentPath() const { return _Path; }

    /// <summary>
    /// The work that makes the screen, a piece a frame (the screen flow's Prepare): its models made and its document
    /// read, hidden; styled and laid out; its first frame recorded, which sets its letters and asks for its pictures.
    /// A piece that fails leaves <see cref="Error"/> set, and the pieces after it do nothing.
    /// </summary>
    std::vector<MCScreenChange::MCPiece> Prepare();

    /// <summary>Runs <see cref="Prepare"/>'s pieces now and shows the screen; why not when its document can't be read.</summary>
    std::expected<void, std::string> Open();

    /// <summary>Shows the prepared screen, over the layer's others, its focus where the player most likely wants it.</summary>
    void Show();
    /// <summary>Hides the screen, kept as it is (shown again by <see cref="Show"/>).</summary>
    void Hide();
    bool Shown() const { return _Shown; }
    /// <summary>Whether the screen's first frame is whole: prepared, its pictures read.</summary>
    bool Ready() const;
    /// <summary>What leaving the screen does before the one under it shows (the screen flow's Leave); nothing here.</summary>
    virtual void Leave() {}

    /// <summary>
    /// What Escape and the screen's way-back button do (the screen flow's Back, set by whoever opens the screen); with
    /// none set, the screen hides.
    /// </summary>
    std::function<void()> Back;

    /// <summary>The keys the hint line names at its right (SPEC-menus.md, "Without a mouse").</summary>
    std::vector<MCRmlKeyHint> Keys;

    /// <summary>A window over a screen (shown modal: the screens under it take no input while it shows).</summary>
    bool Modal = false;

    /// <summary>Why the document couldn't be read or made; empty when it was.</summary>
    const std::string& Error() const { return _Error; }

    /// <summary>The screen's document (none before it is read, or once the screen is destroyed).</summary>
    MCOptionalRef<Rml::ElementDocument> Document() const;

    /// <summary>The layer the screen is on.</summary>
    MCRmlLayer& Layer() const;

    /// <summary>
    /// Destroys the screen, once: its own cleanup (<see cref="OnDestroy"/>), then its document closed and taken off its
    /// layer. Run when its layer lets go of it, or when called.
    /// </summary>
    void Destroy();
    /// <summary>The class's own cleanup, run first by <see cref="Destroy"/>; nothing in the base.</summary>
    virtual void OnDestroy() {}
    bool WasDestroyed() const { return _Destroyed; }
    /// <summary>Takes the screen off its layer, which destroys it.</summary>
    void Close();

    /// <summary>
    /// A key the kit and RmlUi left to the screen (<see cref="MCRmlLayer"/>: Delete on a save, Space on a video);
    /// whether the screen used it.
    /// </summary>
    virtual bool OnKey(int key, int modifiers);

protected:
    /// <summary>The screen's models, made in its layer's context before its document is read (RmlUi binds them then).</summary>
    virtual void MakeModels(Rml::Context& /*context*/) {}
    /// <summary>The document read, before its first layout: the screen finds its elements and listens to them.</summary>
    virtual void OnLoaded(Rml::ElementDocument& /*document*/) {}
    /// <summary>
    /// The element the focus goes to as the screen shows: the one marked <c>data-first</c> (the look's convention);
    /// none: the document's own.
    /// </summary>
    virtual MCOptionalRef<Rml::Element> FirstFocus(Rml::ElementDocument& document);

    /// <summary>
    /// Calls <paramref name="act"/> when the element <paramref name="id"/> is clicked (or pressed by Enter); false
    /// without one. Taken off the element before the document closes.
    /// </summary>
    bool OnClick(const std::string& id, std::function<void()> act);
    /// <summary>Calls <paramref name="act"/> with the element <paramref name="id"/>'s change event's value.</summary>
    bool OnChange(const std::string& id, std::function<void(int value)> act);

private:
    friend class MCRmlLayer;

    /// <summary>Set by the layer as it makes the screen.</summary>
    void Attach(MCRmlLayer& layer) { _Layer = layer; }
    /// <summary>The screen's steps (<see cref="Prepare"/>).</summary>
    void Read();
    void LayOut();
    void Record();

    std::string _Path;
    std::string _Error;
    /// <summary>The layer, which outlives its screens (it destroys them first).</summary>
    MCOptionalRef<MCRmlLayer> _Layer;
    /// <summary>RmlUi's (its context owns the document; the screen closes it).</summary>
    Rml::ElementDocument* _Document = nullptr;
    /// <summary>The screen's listeners on its elements (an Rml::EventListener each), and what they listen to.</summary>
    class MCAction;
    struct MCListening
    {
        Rml::Element* Element = nullptr;
        int Event = 0;
        std::unique_ptr<MCAction> Action;
    };

    std::vector<MCListening> _Listening;
    bool _Recorded = false;
    bool _Shown = false;
    bool _Destroyed = false;
};
