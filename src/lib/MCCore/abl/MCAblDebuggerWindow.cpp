#include "stdafx.h"
#include "abl/MCAblDebuggerWindow.h"
#include "gui/MCGuiTextObject.h"

auto MCAblDebuggerWindow::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name)
    -> int32_t
{
    int32_t err = MCGuiTitleWindow::Init(xPos, yPos, width, height, name);

    if (err != 0)
    {
        return err;
    }

    SetBackColor(10);

    const std::expected<MCGuiHandle<MCScrollingTextWindow>, int32_t> output = TryCreateChild<MCScrollingTextWindow>(
        [&](MCScrollingTextWindow& made) { return made.Init(0, 0, width, height - 40, "ABL Out"); });

    if (!output.has_value())
    {
        return output.error();
    }

    _Output = *output;
    const std::expected<MCGuiHandle<MCGuiTextObject>, int32_t> input = TryCreateChild<MCGuiTextObject>(
        [this](MCGuiTextObject& made)
        {
            const int32_t result = made.Init(0, Height() - 36, 260, 36, {});

            if (result == 0)
            {
                made.SetText(const_cast<char*>("\"?\" for help"));
            }

            return result;
        });

    if (!input.has_value())
    {
        return input.error();
    }

    _Input = *input;
    Draw();
    return 0;
}

auto MCAblDebuggerWindow::Resize(int32_t width, int32_t height) -> void
{
    _Output->Resize(width, height - 40);
    _Input->Resize(width, 36);
    _Input->MoveTo(0, height - 36);
    MCGuiTitleWindow::Resize(width, height);
    Draw();
}
