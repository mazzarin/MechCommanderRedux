#include "stdafx.h"
#include "logistics/MCChooseMapWindow.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

namespace
{
    constexpr const char* WindowDocument = "choose_map.rml";
    constexpr const char* ModelName = "choose";
    /// <summary>The list of maps and its rows (choose_map.rml's).</summary>
    constexpr const char* ListId = "list";
    constexpr const char* RowClass = "list-row";
    /// <summary>The keys' focus ring (the kit's pseudo-class).</summary>
    constexpr const char* FocusVisible = "focus-visible";
    constexpr std::string_view FactsFormat = "{} RP a team. {}";
    constexpr std::string_view TwoTeamsFormat = "{} and {} RP.";
    /// <summary>The list's points when the teams' differ.</summary>
    constexpr std::string_view BothTeamsFormat = "{} / {}";
    constexpr std::string_view TimeLimitFormat = "Time limit {}.";
    constexpr std::string_view NoTimeLimit = "No time limit.";
    constexpr std::string_view TimeFormat = "{}:{:02}";
    constexpr uint32_t SecondsAMinute = 60;

    MCGuiHandle<MCChooseMapWindow>& Made()
    {
        static MCGuiHandle<MCChooseMapWindow> made;
        return made;
    }

    /// <summary>A time limit as the list writes it ("15:00"); empty for none.</summary>
    std::string TimeOf(const MCMatchMap& map)
    {
        return map.Seconds.has_value() && *map.Seconds > 0
                   ? std::format(TimeFormat, *map.Seconds / SecondsAMinute, *map.Seconds % SecondsAMinute)
                   : std::string();
    }

    /// <summary>The points each team starts with as the list writes them: one figure, or each team's.</summary>
    std::string PointsOf(const MCMatchMap& map)
    {
        return map.Team1Points == map.Team2Points
                   ? MCGroupedPoints(map.Team1Points)
                   : std::format(BothTeamsFormat, MCGroupedPoints(map.Team1Points), MCGroupedPoints(map.Team2Points));
    }
}

MCChooseMapWindow::MCChooseMapWindow()
{
    SetDocument(WindowDocument);
    Modal = true;
    Keys = {{"ENTER", "Choose"}, {"ESC", "Cancel"}};
    Back = [this] { Hide(); };
}

MCChooseMapWindow::~MCChooseMapWindow() = default;

std::string MCChooseMapWindow::FactsOf(const MCMatchMap& map)
{
    const std::string time = TimeOf(map);
    const std::string limit = time.empty() ? std::string(NoTimeLimit) : std::format(TimeLimitFormat, time);

    if (map.Team1Points != map.Team2Points)
    {
        return std::format(
            "{} {}", std::format(TwoTeamsFormat, MCGroupedPoints(map.Team1Points), MCGroupedPoints(map.Team2Points)),
            limit);
    }

    return std::format(FactsFormat, MCGroupedPoints(map.Team1Points), limit);
}

void MCChooseMapWindow::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ModelName);
    MCRmlModel& model = *_Model;
    model.List("maps", _Rows, std::pair{"name", &MCRow::Name}, std::pair{"file", &MCRow::File},
               std::pair{"points", &MCRow::Points}, std::pair{"time", &MCRow::Time});
    model.Value("picked", _Picked);
    model.Value("pick_name", _PickName);
    model.Value("pick_pic", _PickPic);
    model.Value("pick_facts", _PickFacts);
    model.List("pick_lines", _PickLines, std::pair{"text", &MCLine::Text});
    const auto at = [](const Rml::VariantList& arguments) { return arguments.empty() ? -1 : arguments[0].Get<int>(); };
    model.Event("pick", [this, at](const Rml::VariantList& arguments) { Pick(at(arguments)); });
    model.Event("choose",
                [this, at](const Rml::VariantList& arguments)
                {
                    Pick(at(arguments));
                    PressChoose();
                });
}

void MCChooseMapWindow::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick("cancel", [this] { PressCancel(); });
    OnClick("pick-map", [this] { PressChoose(); });
}

bool MCChooseMapWindow::OnKey(int key, int /*modifiers*/)
{
    // Enter chooses the map picked, wherever the keys' focus is.
    if (key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER)
    {
        PressChoose();
        return true;
    }

    return false;
}

void MCChooseMapWindow::OnFrame()
{
    if (_FocusPicked)
    {
        FocusPicked();
    }
}

void MCChooseMapWindow::FocusPicked()
{
    const MCOptionalRef<Rml::ElementDocument> document = Document();
    Rml::Element* list = document.has_value() ? document->get().GetElementById(ListId) : nullptr;

    if (list == nullptr || _Picked < 0)
    {
        _FocusPicked = false;
        return;
    }

    Rml::ElementList rows;
    list->GetElementsByClassName(rows, RowClass);
    std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });

    if (static_cast<size_t>(_Picked) >= rows.size())
    {
        return;
    }

    _FocusPicked = false;
    Rml::Context* context = document->get().GetContext();
    Rml::Element* focused = context != nullptr ? context->GetFocusElement() : nullptr;
    Rml::Element& row = *rows[static_cast<size_t>(_Picked)];
    // The ring as it was: a keys player sees it on the map, a mouse player sees none.
    row.Focus(focused != nullptr && focused->IsPseudoClassSet(FocusVisible));
    row.ScrollIntoView(Rml::ScrollAlignment::Nearest);
}

void MCChooseMapWindow::OnDestroy()
{
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCChooseMapWindow::OpenOver(std::string_view roomsMap)
{
    _Maps = MCMatchMaps();
    // Port-only (the menus' playtest, 24 Nov): 1998's NA maps left out, the match options' ARTILLERY OFF and PROBES OFF
    // in their place. Only the list: a room whose host picked one (another build's) loads and plays it as before.
    const size_t read = _Maps.size();
    std::erase_if(_Maps, [](const MCMatchMap& map) { return map.NoArtillery; });
    SDL_Log("CHOOSE MAP: %zu maps (%zu NA maps left out)", _Maps.size(), read - _Maps.size());
    const auto found = std::ranges::find_if(
        _Maps,
        [roomsMap](const MCMatchMap& map)
        {
            return std::ranges::equal(
                map.File, roomsMap, [](char a, char b)
                { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        });
    _Picked = _Maps.empty() ? -1 : found != _Maps.end() ? static_cast<int>(found - _Maps.begin()) : 0;
    _FocusPicked = true;
    ShowMaps();

    if (!Document().has_value())
    {
        if (const auto opened = Open(); !opened)
        {
            SDL_Log("Menus: CHOOSE MAP's window can't open: %s", opened.error().c_str());
        }

        return;
    }

    Show();
}

void MCChooseMapWindow::ShowMaps()
{
    _Rows.clear();

    for (const MCMatchMap& map : _Maps)
    {
        _Rows.push_back(MCRow{.Name = map.Name, .File = map.File, .Points = PointsOf(map), .Time = TimeOf(map)});
    }

    if (_Model != nullptr)
    {
        _Model->Changed("maps");
    }

    ShowPick();
}

void MCChooseMapWindow::ShowPick()
{
    const bool any = _Picked >= 0 && static_cast<size_t>(_Picked) < _Maps.size();
    _PickName.clear();
    _PickPic.clear();
    _PickFacts.clear();
    _PickLines.clear();

    if (any)
    {
        const MCMatchMap& map = _Maps[static_cast<size_t>(_Picked)];
        _PickName = map.Name;
        _PickPic = std::format("{}{}", MCRmlArt::TerrainScheme, map.MapFile);
        _PickFacts = FactsOf(map);

        for (std::string& paragraph : MCParagraphs(map.Description))
        {
            _PickLines.push_back(MCLine{std::move(paragraph)});
        }
    }

    if (_Model != nullptr)
    {
        for (const char* name : {"picked", "pick_name", "pick_pic", "pick_facts", "pick_lines"})
        {
            _Model->Changed(name);
        }
    }
}

void MCChooseMapWindow::Pick(int at)
{
    if (at < 0 || static_cast<size_t>(at) >= _Maps.size() || at == _Picked)
    {
        return;
    }

    _Picked = at;
    ShowPick();
}

void MCChooseMapWindow::PressChoose()
{
    if (_Picked < 0 || static_cast<size_t>(_Picked) >= _Maps.size())
    {
        return;
    }

    const std::string file = _Maps[static_cast<size_t>(_Picked)].File;
    SDL_Log("CHOOSE MAP: %s chosen", file.c_str());
    Hide();

    if (Choosing)
    {
        Choosing(file);
    }
}

void MCChooseMapWindow::PressCancel()
{
    if (Back)
    {
        Back();
    }
}

bool ShowChooseMapWindow(std::string_view roomsMap, std::function<void(const std::string& file)> choosing)
{
    if (!Made())
    {
        const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

        if (!menus.has_value())
        {
            return false;
        }

        Made() = menus->get().Menus().CreateChild<MCChooseMapWindow>([](MCChooseMapWindow&) {});
    }

    Made()->Choosing = std::move(choosing);
    Made()->OpenOver(roomsMap);

    if (!Made()->Error().empty())
    {
        SDL_Log("CHOOSE MAP: the window can't show: %s", Made()->Error().c_str());
        return false;
    }

    return true;
}

MCGuiHandle<MCChooseMapWindow> ChooseMapWindow()
{
    return Made();
}

bool ChooseMapWindowShown()
{
    return Made() && Made()->Shown();
}
