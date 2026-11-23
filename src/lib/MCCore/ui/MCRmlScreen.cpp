#include "stdafx.h"
#include "ui/MCRmlScreen.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>

namespace
{
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

/// <summary>One of a screen's listeners: a function called with the event.</summary>
class MCRmlScreen::MCAction final : public Rml::EventListener
{
public:
    explicit MCAction(std::function<void(Rml::Event&)> act) : _Act(std::move(act)) {}
    void ProcessEvent(Rml::Event& event) override { _Act(event); }

private:
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
    return _Document != nullptr ? MCOptionalRef<Rml::ElementDocument>(*_Document) : std::nullopt;
}

void MCRmlScreen::Read()
{
    if (_Destroyed || _Document != nullptr || !_Error.empty())
    {
        return;
    }

    Rml::Context& context = Layer().Context();
    MakeModels(context);
    _Document = context.LoadDocument(_Path);

    if (_Document == nullptr)
    {
        _Error = std::format("the menus' document {} can't be read (the log says why)", _Path);
        return;
    }

    // Hidden until shown: it is styled and laid out, and its first frame recorded, while the screen before shows.
    _Document->Hide();
    Layer().Kit().Attach(*_Document);
    OnLoaded(*_Document);
}

void MCRmlScreen::LayOut()
{
    if (_Document != nullptr)
    {
        _Document->UpdateDocument();
    }
}

void MCRmlScreen::Record()
{
    // A hidden document draws nothing, so its first draw (its letters' textures and its pictures' reads) is made by
    // its first shown frame; here what can be made without showing it: its layout, done.
    _Recorded = _Document != nullptr;
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
    if (_Document == nullptr || _Destroyed)
    {
        return;
    }

    OnShow();
    ShowKeys();
    _Document->Show(Modal ? Rml::ModalFlag::Modal : Rml::ModalFlag::None, Rml::FocusFlag::Document);

    // The focus on what the player most likely wants, without its ring until a key is pressed.
    if (const MCOptionalRef<Rml::Element> first = FirstFocus(*_Document); first.has_value())
    {
        first->get().Focus(false);
    }

    _Shown = true;
    Layer().Shown(*this);
}

void MCRmlScreen::ShowKeys()
{
    if (_Document == nullptr)
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
    if (_Document != nullptr && _Shown)
    {
        _Document->Hide();
    }

    if (_Shown)
    {
        _Shown = false;

        if (_Layer.has_value())
        {
            Layer().Hidden(*this);
        }

        OnHide();
    }
}

bool MCRmlScreen::OnClick(const std::string& id, std::function<void()> act)
{
    Rml::Element* element = _Document != nullptr ? _Document->GetElementById(id) : nullptr;

    if (element == nullptr)
    {
        SDL_Log("Menus: %s has no element '%s' to click", _Path.c_str(), id.c_str());
        return false;
    }

    auto action = std::make_unique<MCAction>([act = std::move(act)](Rml::Event&) { act(); });
    element->AddEventListener(Rml::EventId::Click, action.get());
    _Listening.push_back(
        MCListening{.Element = element, .Event = static_cast<int>(Rml::EventId::Click), .Action = std::move(action)});
    return true;
}

bool MCRmlScreen::OnChange(const std::string& id, std::function<void(int value)> act)
{
    Rml::Element* element = _Document != nullptr ? _Document->GetElementById(id) : nullptr;

    if (element == nullptr)
    {
        SDL_Log("Menus: %s has no element '%s' to change", _Path.c_str(), id.c_str());
        return false;
    }

    auto action = std::make_unique<MCAction>([act = std::move(act)](Rml::Event& event)
                                             { act(event.GetParameter<int>("value", 0)); });
    element->AddEventListener(Rml::EventId::Change, action.get());
    _Listening.push_back(
        MCListening{.Element = element, .Event = static_cast<int>(Rml::EventId::Change), .Action = std::move(action)});
    return true;
}

bool MCRmlScreen::OnKey(int /*key*/, int /*modifiers*/)
{
    return false;
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

    if (_Document != nullptr)
    {
        // The listeners off their elements first: RmlUi destroys a closed document's elements a frame later.
        for (MCListening& listening : _Listening)
        {
            listening.Element->RemoveEventListener(static_cast<Rml::EventId>(listening.Event), listening.Action.get());
        }

        _Listening.clear();

        if (_Layer.has_value())
        {
            Layer().Kit().Detach(*_Document);
        }

        _Document->Close();
        _Document = nullptr;
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
