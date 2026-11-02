#include "stdafx.h"
#include "logistics/MCLogOptionList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiGlobals.h"
#include "main/MCHdSettings.h"
#include "platform/MCPresenter.h"

namespace
{
    /// <summary>The height of a line: a row is two, its name's and what it does.</summary>
    constexpr int32_t LineHeight = MCLogOptionList::RowHeight / 2;

    /// <summary>
    /// What it does (grey); an option that can't show: its name and setting (dim), and what it needs (amber).
    /// </summary>
    constexpr uint8_t DescriptionColor = 0x19;
    constexpr uint8_t DimColor = 0x16;
    constexpr uint8_t NeedsColor = 0x33;

    /// <summary>A setting box's width, and the gap before it.</summary>
    constexpr int32_t SettingWidth = 96;
    constexpr int32_t SettingGap = 8;

    /// <summary>The column titles.</summary>
    constexpr std::string_view OptionTitle = "OPTION";
    constexpr std::string_view SettingTitle = "SETTING";

    /// <summary>The status line's hint, and what it says with the SOFTWARE renderer chosen.</summary>
    constexpr std::string_view Hint = "CLICK AN OPTION TO CHANGE IT. RIGHT-CLICK TO STEP BACK.";
    constexpr std::string_view SoftwareHint = "THE SOFTWARE RENDERER DRAWS NONE OF THESE (HD AUDIO STILL PLAYS).";

    /// <summary>The sample a change plays.</summary>
    constexpr uint32_t ChangedSound = 15;

    /// <summary>Whether <paramref name="choice"/> is offered now.</summary>
    bool IsOffered(const MCLogOptionList::Choice& choice)
    {
        return choice.Offered == nullptr || choice.Offered();
    }
}

auto MCLogOptionList::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::span<const Row> rows) -> void
{
    MCLogObject::Init(xPos, yPos, width, height);
    _Rows = rows;
    Reset();
}

auto MCLogOptionList::Reset() -> void
{
    ResetScroll();
    _Hovered.reset();
    _News.clear();
}

auto MCLogOptionList::SetNews(std::string_view news) -> void
{
    _News = news;
}

auto MCLogOptionList::Status() const -> std::string_view
{
    if (!_News.empty())
    {
        return _News;
    }

    return GRendererPreference == static_cast<int>(MCRendererKind::Software) ? SoftwareHint : Hint;
}

auto MCLogOptionList::Setting(int32_t row) const -> int&
{
    return *MCHdSettings::Find(_Rows[static_cast<size_t>(row)].Key).Value;
}

auto MCLogOptionList::RowOf(std::string_view key) const -> std::optional<int32_t>
{
    for (size_t row = 0; row < _Rows.size(); row++)
    {
        if (!_Rows[row].IsHeading() && _Rows[row].Key == key)
        {
            return static_cast<int32_t>(row);
        }
    }

    return std::nullopt;
}

auto MCLogOptionList::ChoiceName(int32_t row) const -> std::string_view
{
    const int value = Setting(row);

    for (const Choice& choice : _Rows[static_cast<size_t>(row)].Choices)
    {
        if (choice.Value == value)
        {
            return choice.Name;
        }
    }

    return "?";
}

auto MCLogOptionList::CanShow(int32_t row) const -> bool
{
    const Row& option = _Rows[static_cast<size_t>(row)];
    return option.Needs.empty() || *MCHdSettings::Find(option.Needs).Value != 0;
}

auto MCLogOptionList::Step(int32_t row, int32_t steps) -> void
{
    const Row& option = _Rows[static_cast<size_t>(row)];
    std::vector<int32_t> offered;

    for (const Choice& choice : option.Choices)
    {
        if (IsOffered(choice))
        {
            offered.push_back(choice.Value);
        }
    }

    if (offered.empty())
    {
        return;
    }

    // A value not among those offered (ORCHESTRAL without the pack's orchestral music) steps from the first.
    int& setting = Setting(row);
    const auto found = std::ranges::find(offered, setting);
    const ptrdiff_t at = found != offered.end() ? std::distance(offered.begin(), found) : 0;
    const ptrdiff_t count = std::ssize(offered);
    setting = offered[static_cast<size_t>(((at + steps) % count + count) % count)];
    _News.clear();
    PlayLogSound(ChangedSound);

    if (option.Changed != nullptr)
    {
        option.Changed(setting);
    }
}

auto MCLogOptionList::SettingLeft() -> int32_t
{
    return ScrollBarLeft() - SettingGap - SettingWidth;
}

auto MCLogOptionList::OptionAt(int32_t xPos, int32_t yPos) -> std::optional<int32_t>
{
    const std::optional<int32_t> row = RowAt(yPos);
    const int32_t localX = xPos - GlobalX();

    if (!row.has_value() || _Rows[static_cast<size_t>(*row)].IsHeading() || localX < 1 || localX >= ScrollBarLeft())
    {
        return std::nullopt;
    }

    return row;
}

auto MCLogOptionList::SettingCentre(int32_t row) -> std::optional<std::pair<int32_t, int32_t>>
{
    const std::optional<int32_t> top = RowTop(row);

    if (!top.has_value())
    {
        return std::nullopt;
    }

    return std::pair{GlobalX() + SettingLeft() + SettingWidth / 2, GlobalY() + *top + LineHeight / 2};
}

auto MCLogOptionList::HandleEvent(MCGuiEvent& event) -> void
{
    if (HandleScrollMouse(event))
    {
        return;
    }

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        case MCGuiEventType::RightButtonDown:
        {
            if (const std::optional<int32_t> row = OptionAt(event.X, event.Y); row.has_value())
            {
                Step(*row, event.Type == MCGuiEventType::LeftButtonDown ? 1 : -1);
            }
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            _Hovered = OptionAt(event.X, event.Y);
            break;
        }
        case MCGuiEventType::KeyDown:
        {
            if (!ScrollByKey(event.Key))
            {
                PassToScreen(event);
            }
            break;
        }
        default:
            break;
    }
}

auto MCLogOptionList::Leave() -> void
{
    _Hovered.reset();
}

auto MCLogOptionList::DrawOption(int32_t row, int32_t top) -> void
{
    const Row& option = _Rows[static_cast<size_t>(row)];
    const bool shows = CanShow(row);
    const int32_t settingLeft = SettingLeft();
    const int32_t nameWidth = settingLeft - SettingGap - NameLeft;
    Write(NameLeft, top + TextOffset, option.Label, shows ? NameColor : DimColor, nameWidth);

    // What it does; when it can't show, what it needs first.
    int32_t left = NameLeft;

    if (!shows)
    {
        left += Write(left, top + LineHeight + TextOffset, std::format("NEEDS {}. ", option.NeedsLabel), NeedsColor,
                      nameWidth);
    }

    Write(left, top + LineHeight + TextOffset, option.Description, DescriptionColor, NameLeft + nameWidth - left);

    // The setting, in its box.
    const int32_t right = settingLeft + SettingWidth - 1;
    const int32_t bottom = top + LineHeight - 2;
    DrawOutline(settingLeft, top, right, bottom, _Hovered == row ? HoverLineColor : LineColor);
    Write(settingLeft + BoxTextLeft, top + 1, ChoiceName(row), shows ? BoxTextColor : DimColor,
          SettingWidth - 2 * BoxTextLeft);
}

auto MCLogOptionList::Draw() -> void
{
    if (!_Port->ViewOpen())
    {
        return;
    }

    DrawFrame(Status(), !_News.empty());

    // The column titles.
    Write(HeadingLeft, TitleTop, OptionTitle, TitleColor, SettingLeft() - HeadingLeft);
    Write(SettingLeft() + BoxTextLeft, TitleTop, SettingTitle, TitleColor, SettingWidth);

    // The rows shown: a heading on its second line, a space above it.
    const int32_t rows = std::min(VisibleRows(), RowCount() - FirstRow());

    for (int32_t shown = 0; shown < rows; shown++)
    {
        const int32_t row = FirstRow() + shown;
        const int32_t top = RowsTop + shown * RowHeight;

        if (_Rows[static_cast<size_t>(row)].IsHeading())
        {
            Write(HeadingLeft, top + LineHeight + TextOffset, _Rows[static_cast<size_t>(row)].Label, HeadingColor,
                  ScrollBarLeft() - HeadingLeft);
            continue;
        }

        DrawOption(row, top);
    }
}
