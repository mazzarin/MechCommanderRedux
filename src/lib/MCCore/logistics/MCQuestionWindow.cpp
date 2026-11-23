#include "stdafx.h"
#include "logistics/MCQuestionWindow.h"
#include "ui/MCRmlLayer.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using Rml::StringUtilities::EncodeRml;

    constexpr const char* AskDocument = "dialog_ask.rml";
    constexpr const char* HeadingId = "heading";
    constexpr const char* EyebrowId = "eyebrow";
    constexpr const char* HiddenClass = "hidden";
    constexpr const char* YesId = "yes";
    constexpr const char* NoId = "no";
}

MCQuestionWindow::MCQuestionWindow(std::string heading, std::string act, std::function<void()> yes)
    : _Heading(std::move(heading)), _Act(std::move(act)), _Yes(std::move(yes))
{
    SetDocument(AskDocument);
    Modal = true;
    Keys = {{"Enter", _Act}, {"Esc", "Cancel"}};
}

MCGuiHandle<MCQuestionWindow> MCQuestionWindow::Ask(MCRmlScreen& over, std::string heading, std::string act,
                                                    std::function<void()> yes, std::function<void()> closed,
                                                    std::string eyebrow)
{
    const MCGuiHandle<MCQuestionWindow> window = over.Layer().CreateChild<MCQuestionWindow>(
        [&closed, &eyebrow](MCQuestionWindow& made)
        {
            made._Closed = std::move(closed);
            made._Eyebrow = std::move(eyebrow);
            made.Back = [&made] { made.Answer({}); };
        },
        std::move(heading), std::move(act), std::move(yes));

    if (const std::expected<void, std::string> opened = window->Open(); !opened)
    {
        SDL_Log("MCQuestionWindow: cannot open %s: %s", AskDocument, opened.error().c_str());
        window->Close();
        return {};
    }

    return window;
}

void MCQuestionWindow::OnLoaded(Rml::ElementDocument& document)
{
    if (Rml::Element* heading = document.GetElementById(HeadingId); heading != nullptr)
    {
        heading->SetInnerRML(EncodeRml(_Heading));
    }

    if (Rml::Element* eyebrow = document.GetElementById(EyebrowId); eyebrow != nullptr)
    {
        eyebrow->SetInnerRML(EncodeRml(_Eyebrow));
        eyebrow->SetClass(HiddenClass, _Eyebrow.empty());
    }

    if (Rml::Element* yes = document.GetElementById(YesId); yes != nullptr)
    {
        yes->SetInnerRML(EncodeRml(_Act));
    }

    OnClick(YesId, [this] { Answer(_Yes); });
    OnClick(NoId, [this] { Answer({}); });
}

void MCQuestionWindow::Answer(const std::function<void()>& then)
{
    // The work outlives the window, which closes first (its screen under it shows the answer's result).
    const std::function<void()> work = then;
    const std::function<void()> closed = _Closed;
    Close();

    if (work)
    {
        work();
    }

    if (closed)
    {
        closed();
    }
}
