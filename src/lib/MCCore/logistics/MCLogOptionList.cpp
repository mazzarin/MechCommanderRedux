#include "stdafx.h"
#include "logistics/MCLogOptionList.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "main/MCHdSettings.h"
#include "platform/MCPresenter.h"

namespace
{
    /// <summary>The lines in a row: its name's and what it does.</summary>
    constexpr int32_t LinesPerRow = 2;
    /// <summary>The height of a line.</summary>
    constexpr int32_t LineHeight = MCLogOptionList::RowHeight / LinesPerRow;

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

    /// <summary>
    /// What the status line says while the CPU draws the game (else nothing, but what a button did): with the SOFTWARE
    /// renderer chosen, and when the GPU failed to start. The options only the GPU draws say they need VULKAN.
    /// </summary>
    const std::string& SoftwareHint()
    {
        static const std::string hint =
            std::format("THE SOFTWARE RENDERER DRAWS NO OPTION THAT NEEDS {}.", MCGpuRendererChoice);
        return hint;
    }

    const std::string& FallbackHint()
    {
        static const std::string hint =
            std::format("{} DIDN'T START ON THIS COMPUTER: THE CPU DRAWS THE GAME.", MCGpuRendererChoice);
        return hint;
    }

    /// <summary>
    /// Whether the CPU draws the game as the settings stand, which the page describes: SOFTWARE chosen (it draws from
    /// the next start if not now), or VULKAN chosen and this run's GPU failed to start (as it will again). VULKAN chosen
    /// on a run the player started on SOFTWARE counts as the GPU's: it is tried at the next start.
    /// </summary>
    bool CpuDrawsAsSet()
    {
        return GRendererPreference == static_cast<int>(MCRendererKind::Software) || GpuFellBack();
    }

    /// <summary>The sample a change plays.</summary>
    constexpr uint32_t ChangedSound = 15;

    /// <summary>Whether <paramref name="choice"/> is offered now.</summary>
    bool IsOffered(const MCLogOptionList::Choice& choice)
    {
        return choice.Offered == nullptr || choice.Offered();
    }

    /// <summary>
    /// The lines <paramref name="text"/> wraps over at <paramref name="width"/> pixels in the white font: broken at
    /// spaces, a word wider than a line cut where it fills one.
    /// </summary>
    std::vector<std::string> WrapLines(std::string_view text, int32_t width)
    {
        std::vector<std::string> lines;

        while (!text.empty())
        {
            int32_t fits = WhiteFont->CharactersToWidth(text, width, true);

            if (fits <= 0)
            {
                // At least a letter a line, so the wrap ends even in a line too narrow for one.
                fits = std::max(WhiteFont->CharactersToWidth(text, width, false), 1);
            }

            lines.emplace_back(text.substr(0, static_cast<size_t>(fits)));
            text.remove_prefix(static_cast<size_t>(fits));

            while (text.starts_with(' '))
            {
                text.remove_prefix(1);
            }
        }

        return lines;
    }
}

MCLogOptionList::MCLogOptionList() : MCLogScrollList(RowHeight), _SettingWidth(SettingWidth)
{
}

auto MCLogOptionList::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::span<const Row> rows) -> void
{
    MCLogObject::Init(xPos, yPos, width, height);
    _Rows = rows;
    _Held.assign(rows.size(), std::string());
    _HeldLines.assign(rows.size(), {});
    Relayout();
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

auto MCLogOptionList::SetNarrow(int32_t settingWidth) -> void
{
    _SettingWidth = settingWidth;
    _WideDescriptions = true;
}

auto MCLogOptionList::SetLocked(bool locked, std::string_view hint) -> void
{
    _Locked = locked;
    _LockedHint = hint;

    if (locked)
    {
        _Hovered.reset();
    }
}

auto MCLogOptionList::SetHeld(int32_t row, std::string_view reason) -> void
{
    std::string& held = _Held[static_cast<size_t>(row)];

    if (held == reason)
    {
        return;
    }

    held = reason;
    _HeldLines[static_cast<size_t>(row)] = WrapLines(reason, LineWidth());
    Relayout();
}

auto MCLogOptionList::Relayout() -> void
{
    _Slots.clear();

    for (size_t row = 0; row < _Rows.size(); row++)
    {
        const auto option = static_cast<int32_t>(row);
        _Slots.push_back(Slot{.Row = option});
        // The first line is the row's own; the rest go two to a slot under it.
        const auto further = std::max(static_cast<int32_t>(_HeldLines[row].size()) - 1, 0);

        for (int32_t part = 1; part <= (further + LinesPerRow - 1) / LinesPerRow; part++)
        {
            _Slots.push_back(Slot{.Row = option, .Part = part});
        }
    }

    ScrollTo(FirstRow());
}

auto MCLogOptionList::SlotOf(int32_t row) const -> int32_t
{
    const auto found =
        std::ranges::find_if(_Slots, [row](const Slot& slot) { return slot.Row == row && slot.Part == 0; });
    return static_cast<int32_t>(std::distance(_Slots.begin(), found));
}

auto MCLogOptionList::HeldReason(int32_t row) const -> std::string_view
{
    return _Held[static_cast<size_t>(row)];
}

auto MCLogOptionList::Description(int32_t row) const -> std::string
{
    const Row& option = _Rows[static_cast<size_t>(row)];
    return option.Line != nullptr ? option.Line() : std::string(option.Description);
}

auto MCLogOptionList::SetRendererHints(bool shown) -> void
{
    _RendererHints = shown;
}

auto MCLogOptionList::SetHeadingColor(uint8_t color) -> void
{
    _HeadingColor = color;
}

auto MCLogOptionList::Status() const -> std::string_view
{
    if (!_News.empty())
    {
        return _News;
    }

    if (_Locked)
    {
        return _LockedHint;
    }

    if (!_RendererHints)
    {
        return {};
    }

    if (GRendererPreference == static_cast<int>(MCRendererKind::Software))
    {
        return SoftwareHint();
    }

    return GpuFellBack() ? std::string_view(FallbackHint()) : std::string_view();
}

auto MCLogOptionList::Setting(int32_t row) const -> int&
{
    const Row& option = _Rows[static_cast<size_t>(row)];
    return option.Value != nullptr ? *option.Value : *MCHdSettings::Find(option.Key).Value;
}

auto MCLogOptionList::RowOf(std::string_view key) const -> std::optional<int32_t>
{
    for (size_t row = 0; row < _Rows.size(); row++)
    {
        if (!_Rows[row].IsHeading() && !key.empty() && _Rows[row].Key == key)
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

auto MCLogOptionList::Lacks(int32_t row) const -> std::string_view
{
    const Row& option = _Rows[static_cast<size_t>(row)];

    if (option.Gpu && CpuDrawsAsSet())
    {
        return MCGpuRendererChoice;
    }

    if (!option.Needs.empty() && *MCHdSettings::Find(option.Needs).Value == 0)
    {
        return option.NeedsLabel;
    }

    return {};
}

auto MCLogOptionList::CanShow(int32_t row) const -> bool
{
    return Lacks(row).empty();
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
    return ScrollBarLeft() - SettingGap - _SettingWidth;
}

auto MCLogOptionList::LineWidth() -> int32_t
{
    return _WideDescriptions ? ScrollBarLeft() - SettingGap / 2 - NameLeft : SettingLeft() - SettingGap - NameLeft;
}

auto MCLogOptionList::OptionAt(int32_t xPos, int32_t yPos) -> std::optional<int32_t>
{
    const std::optional<int32_t> slot = RowAt(yPos);
    const int32_t localX = xPos - GlobalX();

    if (!slot.has_value())
    {
        return std::nullopt;
    }

    const int32_t row = _Slots[static_cast<size_t>(*slot)].Row;

    if (_Rows[static_cast<size_t>(row)].IsHeading() || localX < 1 || localX >= ScrollBarLeft())
    {
        return std::nullopt;
    }

    return row;
}

auto MCLogOptionList::ChangeableAt(int32_t xPos, int32_t yPos) -> std::optional<int32_t>
{
    const std::optional<int32_t> row = OptionAt(xPos, yPos);
    return row.has_value() && !_Locked && HeldReason(*row).empty() && _Rows[static_cast<size_t>(*row)].Place == nullptr
               ? row
               : std::nullopt;
}

auto MCLogOptionList::SettingCentre(int32_t row) -> std::optional<std::pair<int32_t, int32_t>>
{
    const std::optional<int32_t> top = RowTop(SlotOf(row));

    if (!top.has_value())
    {
        return std::nullopt;
    }

    return std::pair{GlobalX() + SettingLeft() + _SettingWidth / 2, GlobalY() + *top + LineHeight / 2};
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
            if (const std::optional<int32_t> row = ChangeableAt(event.X, event.Y); row.has_value())
            {
                Step(*row, event.Type == MCGuiEventType::LeftButtonDown ? 1 : -1);
            }
            break;
        }
        case MCGuiEventType::MouseMove:
        {
            _Hovered = ChangeableAt(event.X, event.Y);
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
    const std::string_view held = HeldReason(row);
    const std::string_view lacks = Lacks(row);
    const bool shows = lacks.empty() && held.empty();
    const int32_t settingLeft = SettingLeft();
    const int32_t nameWidth = settingLeft - SettingGap - NameLeft;
    Write(NameLeft, top + TextOffset, option.Label, shows ? NameColor : DimColor, nameWidth);

    // What it does; when it can't show, what it needs first (VULKAN, or another setting); when held, why instead (in the
    // headings' amber: a held row is the session screen's, whose palette has none at NeedsColor).
    int32_t left = NameLeft;
    const int32_t descriptionRight = NameLeft + LineWidth();

    if (!held.empty())
    {
        Write(left, top + LineHeight + TextOffset, HeldLines(row).front(), _HeadingColor, descriptionRight - left);
    }
    else
    {
        if (!lacks.empty())
        {
            left += Write(left, top + LineHeight + TextOffset, std::format("NEEDS {}. ", lacks), NeedsColor, nameWidth);
        }

        Write(left, top + LineHeight + TextOffset, Description(row), DescriptionColor, descriptionRight - left);
    }

    // An action's button is the page's own (Row::Place).
    if (option.Place != nullptr)
    {
        return;
    }

    // The setting, in its box.
    const int32_t right = settingLeft + _SettingWidth - 1;
    const int32_t bottom = top + LineHeight - 2;
    DrawOutline(settingLeft, top, right, bottom, _Hovered == row ? HoverLineColor : LineColor);
    Write(settingLeft + BoxTextLeft, top + 1, ChoiceName(row), shows ? BoxTextColor : DimColor,
          _SettingWidth - 2 * BoxTextLeft);
}

auto MCLogOptionList::DrawHeldMore(const Slot& slot, int32_t top) -> void
{
    const std::span<const std::string> lines = HeldLines(slot.Row);
    // Part 1 holds the reason's second and third lines (its first is on the row), and so on.
    const int32_t first = (slot.Part - 1) * LinesPerRow + 1;

    for (int32_t line = 0; line < LinesPerRow && first + line < static_cast<int32_t>(lines.size()); line++)
    {
        Write(NameLeft, top + line * LineHeight + TextOffset, lines[static_cast<size_t>(first + line)], _HeadingColor,
              LineWidth());
    }
}

auto MCLogOptionList::Draw() -> void
{
    if (!_Port->ViewOpen())
    {
        return;
    }

    // Rows held by how things stand now are held, or let go, before they're drawn.
    for (size_t row = 0; row < _Rows.size(); row++)
    {
        if (_Rows[row].HeldNow != nullptr)
        {
            SetHeld(static_cast<int32_t>(row), _Rows[row].HeldNow());
        }
    }

    DrawFrame(Status(), !_News.empty());

    // The column titles.
    Write(HeadingLeft, TitleTop, OptionTitle, TitleColor, SettingLeft() - HeadingLeft);
    Write(SettingLeft() + BoxTextLeft, TitleTop, SettingTitle, TitleColor, _SettingWidth);

    // The rows shown: a heading on its second line, a space above it; a held reason's further lines under its row.
    const int32_t slots = std::min(VisibleRows(), RowCount() - FirstRow());

    for (int32_t shown = 0; shown < slots; shown++)
    {
        const Slot& slot = _Slots[static_cast<size_t>(FirstRow() + shown)];
        const int32_t row = slot.Row;
        const int32_t top = RowsTop + shown * RowHeight;

        if (slot.Part > 0)
        {
            DrawHeldMore(slot, top);
            continue;
        }

        if (_Rows[static_cast<size_t>(row)].IsHeading())
        {
            Write(HeadingLeft, top + LineHeight + TextOffset, _Rows[static_cast<size_t>(row)].Label, _HeadingColor,
                  ScrollBarLeft() - HeadingLeft);
            continue;
        }

        DrawOption(row, top);
    }

    // An action's button stands where its setting's box would be, in line with the other rows' boxes, while it shows.
    for (size_t row = 0; row < _Rows.size(); row++)
    {
        if (_Rows[row].Place != nullptr)
        {
            const auto option = static_cast<int32_t>(row);
            _Rows[row].Place(HeldReason(option).empty() ? SettingCentre(option) : std::nullopt);
        }
    }
}
