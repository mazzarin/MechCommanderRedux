#include "stdafx.h"
#include "ui/MCRmlScreen.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Input.h>

namespace
{
    /// <summary>A document's head's end, and the body put after it for a document of its head alone.</summary>
    constexpr std::string_view HeadEnd = "</head>";
    constexpr std::string_view EmptyBody = "<body/></rml>";

    /// <summary>The hint line's part for the screen's keys (ui/look.rcss).</summary>
    constexpr const char* KeysClass = "keys";
    /// <summary>An element the focus goes to first when the screen shows (the look's convention).</summary>
    constexpr const char* FirstFocusAttribute = "data-first";
    /// <summary>The arrow keys, which the hint line boxes one by one, narrower (the look's <c>.key.updown</c>).</summary>
    constexpr std::array<std::string_view, 4> ArrowKeys = {"↑", "↓", "←", "→"};
    constexpr char KeySeparator = ' ';

    /// <summary>
    /// A hint's key boxed for the hint line: arrows each in a box of their own (<c>↑ ↓</c>), as the frames draw them;
    /// any other key in one (<c>Q E</c>).
    /// </summary>
    std::string KeyBoxes(std::string_view key)
    {
        std::string arrows;

        for (const auto part : std::views::split(key, KeySeparator))
        {
            const std::string_view arrow(part.begin(), part.end());

            if (std::ranges::find(ArrowKeys, arrow) == ArrowKeys.end())
            {
                return std::format("<span class=\"key\">{}</span>", key);
            }

            arrows += std::format("<span class=\"key updown\">{}</span>", arrow);
        }

        return arrows;
    }
}

/// <summary>
/// One of a screen's listeners: a function called with the event. A click can close its own screen (BACK, CANCEL):
/// the action and its screen are pinned while it runs, so the screen's Destroy (its listeners let go) frees them only
/// once the event is done with them.
/// </summary>
class MCRmlScreen::MCAction final
    : public Rml::EventListener
    , public std::enable_shared_from_this<MCAction>
{
public:
    MCAction(std::weak_ptr<MCRmlScreen> screen, std::function<void(Rml::Event&)> act)
        : _Screen(std::move(screen)), _Act(std::move(act))
    {
    }

    void ProcessEvent(Rml::Event& event) override
    {
        // Let go of in reverse: the screen first (which may free it), then the action.
        const std::shared_ptr<MCAction> action = shared_from_this();
        const std::shared_ptr<MCRmlScreen> screen = _Screen.lock();
        _Act(event);
    }

private:
    std::weak_ptr<MCRmlScreen> _Screen;
    std::function<void(Rml::Event&)> _Act;
};

// Out of line: the listeners' actions are only known here.
MCRmlScreen::MCRmlScreen() = default;

MCRmlScreen::~MCRmlScreen()
{
    Destroy();
}

MCRmlLayer& MCRmlScreen::Layer() const
{
    SDL_assert(_Layer.has_value());
    return _Layer->get();
}

MCOptionalRef<Rml::ElementDocument> MCRmlScreen::Document() const
{
    // Only a document is watched (Read).
    return MCOptionalOf(static_cast<Rml::ElementDocument*>(_Document.get()));
}

MCOptionalRef<Rml::Element> MCRmlScreen::ElementById(const std::string& id) const
{
    return _Document ? MCOptionalOf(_Document->GetElementById(id)) : std::nullopt;
}

void MCRmlScreen::ReadStyle()
{
    if (_Destroyed || _Document || !_Error.empty())
    {
        return;
    }

    // The document read, and its style sheets into RmlUi's cache, which its read then finds: its head alone read as a
    // document of its own, closed at once (a quarter of a large screen's first read: the briefing's four sheets, 2.9 of
    // 12.3 ms on the M4 Pro).
    Rml::FileInterface* const files = Rml::GetFileInterface();
    const Rml::FileHandle file = files != nullptr ? files->Open(_Path) : 0;

    if (file == 0)
    {
        Unreadable();
        return;
    }

    _Text.assign(files->Length(file), '\0');
    _Text.resize(files->Read(_Text.data(), _Text.size(), file));
    files->Close(file);

    if (const size_t head = _Text.find(HeadEnd); head != std::string::npos)
    {
        std::string text = _Text.substr(0, head + HeadEnd.size());
        text += EmptyBody;

        if (Rml::ElementDocument* const style = Layer().Context().LoadDocumentFromMemory(text, _Path); style != nullptr)
        {
            style->Close();
        }
    }
}

void MCRmlScreen::Read()
{
    if (_Destroyed || _Document || !_Error.empty())
    {
        return;
    }

    Rml::Context& context = Layer().Context();
    MakeModels(context);
    Rml::ElementDocument* const document =
        _Text.empty() ? context.LoadDocument(_Path) : context.LoadDocumentFromMemory(_Text, _Path);
    _Text.clear();
    _Text.shrink_to_fit();

    if (document == nullptr)
    {
        Unreadable();
        return;
    }

    // Hidden until shown: it is styled and laid out, and its first frame recorded, while the screen before shows.
    _Document = document->GetObserverPtr();
    document->Hide();
    Layer().Kit().Attach(*document);
    OnLoaded(*document);
}

void MCRmlScreen::Unreadable()
{
    _Error = std::format("the menus' document {} can't be read (the log says why)", _Path);
}

void MCRmlScreen::LayOut()
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        document->get().UpdateDocument();
    }
}

void MCRmlScreen::Record()
{
    // A hidden document draws nothing, so its first draw (its letters' textures and its pictures' reads) is made by
    // its first shown frame; here what can be made without showing it: its layout, done.
    _Recorded = static_cast<bool>(_Document);
}

std::vector<MCScreenChange::MCPiece> MCRmlScreen::Prepare()
{
    const std::weak_ptr<MCRmlScreen> self = weak_from_this();
    const auto step = [self](void (MCRmlScreen::*work)())
    {
        return [self, work]
        {
            if (const std::shared_ptr<MCRmlScreen> screen = self.lock(); screen != nullptr && !screen->_Destroyed)
            {
                ((*screen).*work)();
            }
        };
    };

    return {
        MCScreenChange::MCPiece{.Part = "menus.style", .Run = step(&MCRmlScreen::ReadStyle)},
        MCScreenChange::MCPiece{.Part = "menus.read", .Run = step(&MCRmlScreen::Read)},
        MCScreenChange::MCPiece{.Part = "menus.layout", .Run = step(&MCRmlScreen::LayOut)},
        MCScreenChange::MCPiece{.Part = "menus.record", .Run = step(&MCRmlScreen::Record)},
    };
}

std::expected<void, std::string> MCRmlScreen::Open()
{
    for (const MCScreenChange::MCPiece& piece : Prepare())
    {
        piece.Run();
    }

    if (!_Error.empty())
    {
        return std::unexpected(_Error);
    }

    Show();
    return {};
}

bool MCRmlScreen::Ready() const
{
    return _Recorded && _Error.empty();
}

MCOptionalRef<Rml::Element> MCRmlScreen::FirstFocus(Rml::ElementDocument& document)
{
    Rml::Element* first = document.QuerySelector(std::format("[{}]", FirstFocusAttribute));
    return first != nullptr ? MCOptionalRef<Rml::Element>(*first) : std::nullopt;
}

void MCRmlScreen::Show()
{
    if (!_Document || _Destroyed)
    {
        return;
    }

    OnShow();
    ShowKeys();
    Rml::ElementDocument& document = Document()->get();
    document.Show(Modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None, Rml::FocusFlag::Document);

    // The focus on what the player most likely wants, without its ring until a key is pressed.
    if (const MCOptionalRef<Rml::Element> first = FirstFocus(document); first.has_value())
    {
        first->get().Focus(false);
    }

    _Shown = true;
    Layer().Shown(*this);
}

void MCRmlScreen::ShowKeys()
{
    if (!_Document)
    {
        return;
    }

    // The hint line's right: the screen's own keys, each boxed before what it does.
    std::string keys;

    for (const MCRmlKeyHint& hint : Keys)
    {
        keys += std::format("<span class=\"hint\">{}{}</span>", KeyBoxes(hint.Key), hint.Does);
    }

    Rml::ElementList parts;
    _Document->GetElementsByClassName(parts, KeysClass);

    for (Rml::Element* part : parts)
    {
        part->SetInnerRML(keys);
    }
}

void MCRmlScreen::Hide()
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value() && _Shown)
    {
        document->get().Hide();
    }

    if (_Shown)
    {
        _Shown = false;

        if (_Layer.has_value())
        {
            Layer().Hidden(*this);
            FocusBelow();
        }

        OnHide();
    }
}

void MCRmlScreen::FocusBelow()
{
    // RmlUi gives the keys' focus back to the document focused before, but only one in its focus history, and a kept
    // screen shown again with the focus still in it (nothing shown since it hid) isn't put back in the history: a window
    // over it then hid with the focus left in the window (SAVE GAME's NAME took no keys after the overwrite's question,
    // LOAD GAME's list none after DELETE's). So the screen now on top takes it: where it had it, or where it opens with it.
    Rml::Element* focus = Layer().Context().GetFocusElement();
    const MCOptionalRef<MCRmlScreen> top = Layer().Top();

    if (!_Document || focus == nullptr || focus->GetOwnerDocument() != _Document.get() || !top.has_value() ||
        !top->get()._Document)
    {
        return;
    }

    Rml::ElementDocument& below = top->get().Document()->get();

    if (Rml::Element* leaf = below.GetFocusLeafNode(); leaf != &below && leaf->IsVisible(true))
    {
        leaf->Focus(false);
    }
    else if (const MCOptionalRef<Rml::Element> first = top->get().FirstFocus(below); first.has_value())
    {
        first->get().Focus(false);
    }
    else
    {
        below.Focus(false);
    }
}

bool MCRmlScreen::Listen(const std::string& id, Rml::EventId event, std::function<void(Rml::Event&)> act)
{
    const MCOptionalRef<Rml::Element> element = ElementById(id);

    if (!element.has_value())
    {
        SDL_Log("Menus: %s has no element '%s' to listen to", _Path.c_str(), id.c_str());
        return false;
    }

    auto action = std::make_shared<MCAction>(weak_from_this(), std::move(act));
    element->get().AddEventListener(event, action.get());
    _Listening.push_back(MCListening{
        .Element = element->get().GetObserverPtr(), .Event = static_cast<int>(event), .Action = std::move(action)});
    return true;
}

bool MCRmlScreen::OnClick(const std::string& id, std::function<void()> act)
{
    return Listen(id, Rml::EventId::Click, [act = std::move(act)](Rml::Event&) { act(); });
}

bool MCRmlScreen::OnChange(const std::string& id, std::function<void(int value)> act)
{
    return Listen(id, Rml::EventId::Change,
                  [act = std::move(act)](Rml::Event& event) { act(event.GetParameter<int>("value", 0)); });
}

bool MCRmlScreen::OnText(const std::string& id, std::function<void(const std::string& text, bool enter)> act)
{
    const MCOptionalRef<Rml::Element> element = ElementById(id);

    if (!element.has_value())
    {
        SDL_Log("Menus: %s has no text box '%s'", _Path.c_str(), id.c_str());
        return false;
    }

    // RmlUi's text boxes say ENTER as a change with its "linebreak".
    auto action = std::make_shared<MCAction>(
        weak_from_this(), [act = std::move(act)](Rml::Event& event)
        { act(event.GetParameter<Rml::String>("value", ""), event.GetParameter<bool>("linebreak", false)); });
    element->get().AddEventListener(Rml::EventId::Change, action.get());
    _Listening.push_back(MCListening{.Element = element->get().GetObserverPtr(),
                                     .Event = static_cast<int>(Rml::EventId::Change),
                                     .Action = std::move(action)});
    return true;
}

bool MCRmlScreen::OnChangeIn(const std::string& id, std::function<void(const std::string& control, int value)> act)
{
    const MCOptionalRef<Rml::Element> element = ElementById(id);

    if (!element.has_value())
    {
        SDL_Log("Menus: %s has no element '%s' to change in", _Path.c_str(), id.c_str());
        return false;
    }

    // A control's change rises to it (RmlUi's change event bubbles), from its target.
    auto action = std::make_shared<MCAction>(weak_from_this(),
                                             [act = std::move(act)](Rml::Event& event)
                                             {
                                                 const Rml::Element* target = event.GetTargetElement();
                                                 act(target != nullptr ? target->GetId() : Rml::String(),
                                                     event.GetParameter<int>("value", 0));
                                             });
    element->get().AddEventListener(Rml::EventId::Change, action.get());
    _Listening.push_back(MCListening{.Element = element->get().GetObserverPtr(),
                                     .Event = static_cast<int>(Rml::EventId::Change),
                                     .Action = std::move(action)});
    return true;
}

bool MCRmlScreen::OnKey(int /*key*/, int /*modifiers*/)
{
    return false;
}

void MCRmlScreen::TakeKeysFirst()
{
    if (!_Document)
    {
        SDL_Log("Menus: %s has no document to take its keys from", _Path.c_str());
        return;
    }

    auto action = std::make_shared<MCAction>(weak_from_this(),
                                             [this](Rml::Event& event)
                                             {
                                                 const int key =
                                                     event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);

                                                 if (OnKey(key, event.GetParameter<int>("modifiers", 0)))
                                                 {
                                                     event.StopPropagation();
                                                 }
                                             });
    _Document->AddEventListener(Rml::EventId::Keydown, action.get());
    _Listening.push_back(MCListening{
        .Element = _Document, .Event = static_cast<int>(Rml::EventId::Keydown), .Action = std::move(action)});
}

void MCRmlScreen::Destroy()
{
    if (_Destroyed)
    {
        return;
    }

    OnDestroy();
    _Destroyed = true;
    Hide();

    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value())
    {
        // The listeners off their elements first: RmlUi destroys a closed document's elements a frame later. One whose
        // element is gone already (rows put in again) went with it.
        for (MCListening& listening : _Listening)
        {
            if (listening.Element)
            {
                listening.Element->RemoveEventListener(static_cast<Rml::EventId>(listening.Event),
                                                       listening.Action.get());
            }
        }

        _Listening.clear();

        if (_Layer.has_value())
        {
            Layer().Kit().Detach(*document);
        }

        document->get().Close();
        _Document.reset();
    }

    if (_Layer.has_value())
    {
        Layer().Remove(*this);
    }
}

void MCRmlScreen::Close()
{
    if (_Layer.has_value())
    {
        Layer().Remove(*this);
    }
}
