#include "stdafx.h"
#include "logistics/MCCampaignMenu.h"
#include "MCPort.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCBriefingText.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCMissionNames.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCSaves.h"
#include "logistics/MCSavesMenu.h"
#include "logistics/MCSmuti.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGameStrings.h"
#include "main/MCLogMech.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "sound/MCSoundSystem.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlModel.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/SystemInterface.h>

namespace
{
    constexpr const char* ScreenDocument = "campaign.rml";
    constexpr const char* ScreenModel = "campaign";
    constexpr const char* TabsId = "tabs";
    constexpr const char* MapId = "map";
    constexpr const char* DropId = "drop";
    constexpr std::string_view ReadyPrefix = "ready";
    constexpr std::string_view SlotPrefix = "slot";

    /// <summary>1998's sounds: a unit picked up and put down (MCMechBriefBlock's), BEGIN MISSION's, the units' limit's.</summary>
    constexpr uint32_t PickedUpSound = 0x35;
    constexpr uint32_t PlacedSound = 0x34;
    constexpr uint32_t BeginSound = 0x3a;
    constexpr uint32_t TooManySound = 0x33;
    /// <summary>Betty's lines: a unit too heavy for the drop (1998's TooHeavy), BEGIN MISSION refused.</summary>
    constexpr uint32_t TooHeavyBetty = 0;
    constexpr uint32_t BeginRefusedBetty = 0x1b;
    /// <summary>A game string's buffer, as the 1998 screens read theirs.</summary>
    constexpr int StringBuffer = 0xfe;

    /// <summary>The weight's ramp: its segments, from a third of its height to all of it.</summary>
    constexpr size_t Segments = 60;
    constexpr float LowestSegment = 1.0f / 3.0f;
    constexpr float WholePercent = 100.0f;
    /// <summary>
    /// The ramp's motion (SPEC-menus.md's DEPLOY): a unit's tons grow in cyan over a fifth of a second; dropped, they turn
    /// cream left to right in under half a second; taken out, its share empties right to left.
    /// </summary>
    constexpr float GrowSeconds = 0.2f;
    constexpr float FillSeconds = 0.45f;
    /// <summary>A refused send's reason stays under its card this long.</summary>
    constexpr double WhySeconds = 2.0;
    /// <summary>A card carried by the keys sits this far (units) over its slot's corner.</summary>
    constexpr float CarriedOffset = 6.0f;
    /// <summary>The pilot's health pips: six, as 1998's six points.</summary>
    constexpr float HealthPips = 6.0f;
    /// <summary>The condition bar's shades, as 1998's status bar: green at half or more, yellow above a fifth, red under.</summary>
    constexpr float WornBelow = 0.5f;
    constexpr float BadFrom = 0.2f;
    /// <summary>The mouse's buttons in RmlUi's events.</summary>
    constexpr int LeftButton = 0;
    constexpr int RightButton = 1;
    /// <summary>The document's events the screen listens to: the mouse's for the drag, the keys' for DEPLOY's keys.</summary>
    constexpr std::array ListenedEvents = {Rml::EventId::Mousedown, Rml::EventId::Mouseup, Rml::EventId::Mousemove,
                                           Rml::EventId::Keydown};

    double RmlNow()
    {
        Rml::SystemInterface* system = Rml::GetSystemInterface();
        return system != nullptr ? system->GetElapsedTime() : 0.0;
    }

    MCGuiHandle<MCCampaignScreen>& TheScreen()
    {
        static MCGuiHandle<MCCampaignScreen> screen;
        return screen;
    }

    /// <summary>The tab the screen was left on for the 1998 PURCHASING or 'MECH BAY, and whether the next show comes back to it.</summary>
    MCCampaignScreen::MCTab LeftOn = MCCampaignScreen::MCTab::Briefing;
    bool ComingBack = false;

    /// <summary>A picture as a decorator, kept whole in its place (the frames' <c>contain</c>).</summary>
    std::string Pictured(std::string_view source)
    {
        return source.empty() ? std::string(MCCampaignScreen::NoPicture)
                              : std::format("image({} contain center bottom)", source);
    }

    /// <summary>The art: source of a pilot's picture (its file under ArtPath, as warrior->Picture names it).</summary>
    std::string PilotSource(std::string_view picture)
    {
        constexpr std::string_view Tga = ".tga";
        std::string name(picture);

        if (name.size() > Tga.size() && MCMissionNames::Lowered(name.substr(name.size() - Tga.size())) == Tga)
        {
            name.resize(name.size() - Tga.size());
        }

        std::ranges::replace(name, '\\', '/');
        return name.empty() ? std::string() : std::string(MCRmlArt::Scheme) + name;
    }

    int32_t Tons(float tons)
    {
        return static_cast<int32_t>(std::lround(tons));
    }

    /// <summary>How many of the ramp's segments <paramref name="tons"/> of the drop's limit light.</summary>
    float SegmentsOf(float tons)
    {
        const auto limit = static_cast<float>(MaxDeployTonnage);
        return limit > 0 ? std::clamp(std::round(tons / limit * Segments), 0.0f, static_cast<float>(Segments)) : 0.0f;
    }

    /// <summary><paramref name="shown"/> moved toward <paramref name="target"/> by at most <paramref name="step"/>.</summary>
    float Toward(float shown, float target, float step)
    {
        return shown < target ? std::min(shown + step, target) : std::max(shown - step, target);
    }

    /// <summary>The scenario the mission logistics has now (its mission file's name).</summary>
    std::string Scenario()
    {
        const std::shared_ptr<MCLogistics> logistics = GlobalLog();

        if (MultiPlayer() != nullptr)
        {
            return logistics != nullptr ? logistics->MpMissionName : std::string();
        }

        const std::shared_ptr<MCMission> mission = Mission();

        if (mission == nullptr || mission->CurrentScenario < 0 ||
            static_cast<size_t>(mission->CurrentScenario) >= mission->Scenarios.size())
        {
            return {};
        }

        return mission->Scenarios[static_cast<size_t>(mission->CurrentScenario)];
    }

    /// <summary>The element <paramref name="id"/> of the screen's document; null with none.</summary>
    Rml::Element* ElementOf(const MCOptionalRef<Rml::ElementDocument>& document, const std::string& id)
    {
        return document.has_value() ? document->get().GetElementById(id) : nullptr;
    }

    /// <summary>The number after <paramref name="prefix"/> in <paramref name="id"/> ("slot7": 7); none for another id.</summary>
    std::optional<size_t> NumberAfter(std::string_view id, std::string_view prefix)
    {
        if (!id.starts_with(prefix) || id.size() == prefix.size())
        {
            return std::nullopt;
        }

        size_t number = 0;
        const std::string_view digits = id.substr(prefix.size());
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        return error == std::errc() && end == digits.data() + digits.size() ? std::optional(number) : std::nullopt;
    }
}

/// <summary>The document's mouse: presses, releases and moves over DEPLOY's cards.</summary>
class MCCampaignScreen::MCEvents final : public Rml::EventListener
{
public:
    explicit MCEvents(MCCampaignScreen& screen) : _Screen(screen) {}

    void ProcessEvent(Rml::Event& event) override
    {
        if (event.GetId() == Rml::EventId::Keydown)
        {
            _Screen.OnKeyDown(event);
        }
        else
        {
            _Screen.OnMouse(event);
        }
    }

private:
    MCCampaignScreen& _Screen;
};

MCCampaignScreen::MCCampaignScreen()
{
    SetDocument(ScreenDocument);
    Back = [this]
    {
        // Escape puts a carried unit back; else the campaign's way back (the main menu, or a match's question).
        if (_Carry.has_value())
        {
            PutBack();
            return;
        }

        MCScreenFlow::Back();
    };

    for (size_t segment = 0; segment < Segments; ++segment)
    {
        const float height = LowestSegment + (1.0f - LowestSegment) * static_cast<float>(segment) / (Segments - 1);
        _Ramp.push_back(MCSegment{.Height = std::format("{:.1f}%", height * WholePercent)});
    }
}

MCCampaignScreen::~MCCampaignScreen() = default;

void MCCampaignScreen::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, ScreenModel);
    MCRmlModel& model = *_Model;
    model.Value("tab", _Tab);
    model.Value("mp", _Multiplayer);
    model.Value("rp", _Points);
    model.Value("clock", _Clock);
    model.Value("eyebrow", _Eyebrow);
    model.Value("title", _Title);
    model.Value("fire", _Fire);
    model.Value("warningsHead", _WarningsHead);
    model.Value("carriedLeft", _CarriedLeft);
    model.Value("carriedTop", _CarriedTop);
    model.Value("weightNow", _WeightNow);
    model.Value("weightOf", _WeightOf);
    model.Value("weightFree", _WeightFree);
    model.Value("weightMore", _WeightMore);
    model.Value("weightOver", _WeightOver);
    model.Value("canBegin", _CanBegin);
    model.Value("keysCarry", _KeysCarry);
    model.List("objectives", _Objectives, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number});
    model.List("orders", _Orders, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number});
    model.List("warnings", _Warnings, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number});
    // The cards' list first: the groups' rows hold their cards (RmlUi's register then knows a list of cards).
    model.List("ready", _Ready, std::pair{"kind", &MCCard::Kind}, std::pair{"who", &MCCard::Who},
               std::pair{"what", &MCCard::What}, std::pair{"tons", &MCCard::Tons},
               std::pair{"outline", &MCCard::Outline}, std::pair{"pilot", &MCCard::Pilot},
               std::pair{"piloted", &MCCard::Piloted}, std::pair{"condition", &MCCard::Condition},
               std::pair{"shade", &MCCard::Shade}, std::pair{"health", &MCCard::Health}, std::pair{"off", &MCCard::Off},
               std::pair{"target", &MCCard::Target}, std::pair{"refuse", &MCCard::Refuse},
               std::pair{"why", &MCCard::Why}, std::pair{"slot", &MCCard::Slot});
    model.List("carried", _Carried, std::pair{"kind", &MCCard::Kind});
    model.List("groups", _Groups, std::pair{"name", &MCGroup::Name}, std::pair{"tons", &MCGroup::Tons},
               std::pair{"closed", &MCGroup::Closed}, std::pair{"cards", &MCGroup::Cards});
    model.List("ramp", _Ramp, std::pair{"height", &MCSegment::Height}, std::pair{"state", &MCSegment::State});
}

void MCCampaignScreen::OnLoaded(Rml::ElementDocument& document)
{
    OnClick("mainmenu", [] { MCMenuScreens::LeaveCampaign(); });
    OnClick("begin", [this] { PressBegin(); });
    OnChange(TabsId, [this](int tab) { ChooseTab(static_cast<MCTab>(tab)); });
    _Events = std::make_unique<MCEvents>(*this);

    for (const Rml::EventId event : ListenedEvents)
    {
        document.AddEventListener(event, _Events.get());
    }
}

void MCCampaignScreen::OnDestroy()
{
    if (const MCOptionalRef<Rml::ElementDocument> document = Document(); document.has_value() && _Events != nullptr)
    {
        for (const Rml::EventId event : ListenedEvents)
        {
            document->get().RemoveEventListener(event, _Events.get());
        }
    }

    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCCampaignScreen::OnShow()
{
    _LastFrame = RmlNow();
}

void MCCampaignScreen::OnHide()
{
    // Nothing carried is kept past the screen (a mission's teardown takes the units).
    if (_Carry.has_value())
    {
        PutBack();
    }

    _ReadyUnits.clear();
}

MCOptionalRef<Rml::Element> MCCampaignScreen::FirstFocus(Rml::ElementDocument& document)
{
    // DEPLOY: READY AREA's first card, to send or carry.
    if (Tab() == MCTab::Deploy)
    {
        if (Rml::Element* first = document.GetElementById(std::format("{}0", ReadyPrefix)); first != nullptr)
        {
            return *first;
        }
    }

    return MCRmlScreen::FirstFocus(document);
}

void MCCampaignScreen::SetUp(MCTab tab)
{
    _Carry.reset();
    _WhyCard.reset();
    _Tab = static_cast<int>(tab == MCTab::Deploy ? MCTab::Deploy : MCTab::Briefing);
    _Multiplayer = MultiPlayer() != nullptr;
    ReadBriefing();
    FillBar();
    FillCards();
    FillWeight();
    _ShownOn = SegmentsOf(Weighed().Base);
    _ShownMore = _ShownOn;
    SetKeys();
    MarkTab();
    Changed();
}

void MCCampaignScreen::MarkTab()
{
    if (Rml::Element* tabs = ElementOf(Document(), TabsId); tabs != nullptr && MCRmlKit::Choice(*tabs) != _Tab)
    {
        MCRmlKit::SetChoice(*tabs, _Tab);
    }
}

void MCCampaignScreen::ReadBriefing()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::string path = logistics != nullptr ? logistics->BriefingTextPath : std::string();
    const std::string text = path.empty() ? std::string() : MCSmuti::ReadFile(path);
    const MCMissionNames::MCWritten written = MCMissionNames::FromBriefing(text);
    const MCBriefingText::MCRead read = MCBriefingText::Read(text);
    // The game's paths are written with backslashes (data\missions\brf0101.txt).
    const std::string briefingFile = path.substr(path.find_last_of("\\/") + 1);
    _Title = MCMissionNames::Mission(briefingFile, written.Mission);
    _Eyebrow.clear();

    // A campaign's mission: its operation and number in it, as SAVE GAME names it; a solo mission its operation;
    // a match's side nothing (its COMMAND DESIGNATION names no map).
    if (!_Multiplayer && !Solo && logistics != nullptr)
    {
        const MCSaves::MCSave save =
            MCSaves::Unsaved(CurPlanet == 0 ? MCSaves::MCCampaign::Original : MCSaves::MCCampaign::Expansion,
                             logistics->CurrentMission, 0);

        if (save.Read)
        {
            _Eyebrow = std::format(MCCampaignMenu::EyebrowFormat, save.OperationName, save.Mission);
        }
    }
    else if (!_Multiplayer && !written.Operation.empty())
    {
        _Eyebrow = MCMissionNames::Operation(written.Operation);
    }

    _Objectives.clear();

    for (size_t at = 0; at < read.Objectives.size(); ++at)
    {
        _Objectives.push_back(MCLine{.Text = read.Objectives[at], .Number = static_cast<int>(at + 1)});
    }

    _Fire = read.FireSupport.has_value() ? MCBriefingText::FireSupportText(*read.FireSupport) : std::string();
    _Orders.clear();

    for (const std::string& paragraph : read.Orders)
    {
        _Orders.push_back(MCLine{.Text = paragraph});
    }

    _WarningsHead = read.WarningsHead;
    _Warnings.clear();

    for (const std::string& warning : read.Warnings)
    {
        _Warnings.push_back(MCLine{.Text = warning});
    }

    const std::string scenario = Scenario();
    _MapSource = scenario.empty() ? std::string() : std::string(MCRmlArt::MapScheme) + scenario;

    if (Rml::Element* map = ElementOf(Document(), MapId); map != nullptr)
    {
        if (_MapSource.empty())
        {
            map->RemoveProperty("decorator");
        }
        else
        {
            map->SetProperty("decorator", std::format("image({} contain left top)", _MapSource));
        }
    }

    if (text.empty())
    {
        SDL_Log("Menus: the briefing's text %s can't be read", path.c_str());
    }
}

bool MCCampaignScreen::FillBar()
{
    const std::string points = ResourceDisplayState == 0
                                   ? MCSavesMenu::PointsText(static_cast<uint32_t>(std::max(ResourcePoints, 0)))
                                   : std::string();
    const std::string clock = MCPort::StrTime();

    if (points == _Points && clock == _Clock)
    {
        return false;
    }

    _Points = points;
    _Clock = clock;
    return true;
}

MCCampaignScreen::MCCard MCCampaignScreen::UnitCard(const MCLogPart& part, int slot) const
{
    MCCard card{.Kind = static_cast<int>(MCCardKind::Unit), .Slot = slot};
    card.Tons = MCCampaignMenu::TonsText(Tons(part.CurTonnage));
    card.Outline = Pictured(MCRmlArt::UnitSource(part));

    if (part.PartType != 1)
    {
        // A vehicle: its name, as 1998's tile writes it; no pilot.
        card.Who = part.FileName;
        return card;
    }

    const auto& mech = static_cast<const MCLogMech&>(part);
    std::shared_ptr<MCLogWarrior> warrior;

    if (mech.LocalPart == 0)
    {
        warrior = mech.NetworkPilot.lock();
    }
    else if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr)
    {
        logistics->AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, warrior);
    }

    card.Who = warrior != nullptr ? warrior->Callsign : std::string();
    // The name the store and the 'MECH BAY give it (the string table's), not its file's short one (COM-A).
    card.What = mech.FileName;
    card.Pilot = warrior != nullptr ? Pictured(PilotSource(warrior->Picture)) : std::string(NoPicture);
    card.Piloted = warrior != nullptr;
    card.Health = warrior != nullptr ? static_cast<int>(std::clamp(std::round(warrior->Health), 0.0f, HealthPips)) : 0;
    card.Condition = static_cast<int>(std::lround(std::clamp(mech.StatusValue, 0.0f, 1.0f) * WholePercent));
    card.Shade = mech.StatusValue == 0.0f          ? std::string()
                 : !(mech.StatusValue < WornBelow) ? std::string("ok")
                 : mech.StatusValue > BadFrom      ? std::string("worn")
                                                   : std::string("bad");
    return card;
}

void MCCampaignScreen::FillCards()
{
    const std::shared_ptr<MCLogPart> carried = Carried();
    std::vector<std::shared_ptr<MCLogPart>> units = MCDeploy::ReadyUnits();

    // A unit carried from READY AREA keeps its card there, dim, as the frames show it lifted off its list.
    if (carried != nullptr && _Carry->FromSlot == std::nullopt && std::ranges::find(units, carried) == units.end())
    {
        units.insert(units.begin() + static_cast<ptrdiff_t>(std::min(_Carry->FromReady, units.size())), carried);
    }

    const bool whyShows = _WhyCard.has_value() && RmlNow() < _WhyUntil;
    _ReadyUnits.clear();
    _Ready.clear();

    for (size_t at = 0; at < units.size(); ++at)
    {
        MCCard card = UnitCard(*units[at], -1);
        card.Off = units[at] == carried;

        if (whyShows && *_WhyCard == at)
        {
            card.Refuse = true;
            card.Why = _WhyText;
        }

        _ReadyUnits.push_back(units[at]);
        _Ready.push_back(std::move(card));
    }

    const MCWeight weight = Weighed();
    const std::optional<size_t> over = _Carry.has_value() ? _Carry->Over : std::nullopt;
    _Groups.clear();

    for (size_t group = 0; group < MCDeploy::Groups; ++group)
    {
        MCGroup shown{.Name = std::format("Group {}", group + 1)};
        bool open = false;

        for (size_t place = 0; place < MCDeploy::GroupSlots; ++place)
        {
            const size_t slot = group * MCDeploy::GroupSlots + place;
            const std::shared_ptr<MCLogPart> unit = MCDeploy::SlotUnit(slot);
            MCCard card{.Kind = static_cast<int>(MCCardKind::Closed), .Slot = static_cast<int>(slot)};

            // A slot the player can't fill (the mission's closed, a teammate's) striped, saying nothing of who fills it.
            if (MCDeploy::Open(slot))
            {
                open = true;
                card = unit != nullptr
                           ? UnitCard(*unit, static_cast<int>(slot))
                           : MCCard{.Kind = static_cast<int>(MCCardKind::Empty), .Slot = static_cast<int>(slot)};
                card.Target = over == slot;
                card.Refuse = over == slot && weight.Refused;
            }

            shown.Cards.push_back(std::move(card));
        }

        shown.Closed = !open;
        shown.Tons = open ? MCCampaignMenu::TonsText(MCDeploy::GroupTons(group)) : std::string();
        _Groups.push_back(std::move(shown));
    }

    _Carried.clear();

    if (carried != nullptr)
    {
        MCCard card = UnitCard(*carried, -1);
        card.Refuse = weight.Refused;
        card.Why =
            weight.Refused
                ? std::format(MCCampaignMenu::TooHeavyFormat,
                              MCCampaignMenu::TonsText(Tons(weight.Total - static_cast<float>(MaxDeployTonnage))))
                : std::string();
        _Carried.push_back(std::move(card));
    }
}

MCCampaignScreen::MCWeight MCCampaignScreen::Weighed() const
{
    const auto now = static_cast<float>(CurDeployTonnage);
    MCWeight weight{.Base = now, .Total = now};
    const std::shared_ptr<MCLogPart> carried = Carried();

    if (carried == nullptr || !_Carry->Over.has_value() || !MCDeploy::Open(*_Carry->Over))
    {
        return weight;
    }

    // Over a filled slot it swaps: the one there leaves first, and the carried one must fit what is free then (the
    // limit's lifting not asked, as 1998's); over an empty one, what is free now (MCDeploy::Fits).
    if (const std::shared_ptr<MCLogPart> occupant = MCDeploy::SlotUnit(*_Carry->Over); occupant != nullptr)
    {
        weight.Base = now - occupant->CurTonnage;
        weight.Total = weight.Base + carried->CurTonnage;
        weight.Refused = static_cast<float>(MaxDeployTonnage) < weight.Total;
    }
    else
    {
        weight.Total = now + carried->CurTonnage;
        weight.Refused = !MCDeploy::Fits(*carried) && MCDeploy::LimitHolds();
    }

    weight.Adds = !weight.Refused;
    return weight;
}

void MCCampaignScreen::FillWeight()
{
    const MCWeight weight = Weighed();
    const auto limit = static_cast<float>(MaxDeployTonnage);
    const float over = weight.Total - limit;
    _WeightNow = std::to_string(Tons(weight.Total));
    _WeightOf = std::format(MCCampaignMenu::OfFormat, MCCampaignMenu::TonsText(MaxDeployTonnage));
    _WeightFree = over > 0 ? std::format(MCCampaignMenu::OverFormat, MCCampaignMenu::TonsText(Tons(over)))
                           : std::format(MCCampaignMenu::FreeFormat, MCCampaignMenu::TonsText(Tons(-over)));
    _WeightMore = weight.Adds && weight.Total != weight.Base;
    _WeightOver = weight.Refused || over > 0;
    _CanBegin = MCDeploy::CanBegin();
}

bool MCCampaignScreen::StepRamp(double now)
{
    const auto seconds = static_cast<float>(std::max(now - _LastFrame, 0.0));
    _LastFrame = now;
    const MCWeight weight = Weighed();
    const float on = SegmentsOf(weight.Base);
    const float total = weight.Adds ? SegmentsOf(weight.Total) : on;
    const float fill = Segments / FillSeconds * seconds;
    _ShownOn = Toward(_ShownOn, on, fill);
    // What a unit would add grows quickly; what goes out empties at the fill's pace.
    _ShownMore =
        Toward(_ShownMore, std::max(total, _ShownOn), total > _ShownMore ? Segments / GrowSeconds * seconds : fill);
    _ShownMore = std::max(_ShownMore, _ShownOn);
    bool changed = false;

    for (size_t segment = 0; segment < Segments; ++segment)
    {
        const auto at = static_cast<float>(segment);
        MCLit lit = MCLit::Dark;

        if (weight.Refused && at >= on)
        {
            lit = MCLit::Over;
        }
        else if (at < std::round(_ShownOn))
        {
            lit = MCLit::On;
        }
        else if (at < std::round(_ShownMore))
        {
            lit = MCLit::More;
        }

        if (_Ramp[segment].State != static_cast<int>(lit))
        {
            _Ramp[segment].State = static_cast<int>(lit);
            changed = true;
        }
    }

    return changed;
}

void MCCampaignScreen::SetKeys()
{
    if (_Carry.has_value() && _Carry->ByKeys)
    {
        Keys = {{"← → ↑ ↓", "Move to a slot"}, {"Space", "Put down"}, {"Esc", "Put back"}, {"Q E", "Change tab"}};
    }
    else if (Tab() == MCTab::Deploy)
    {
        Keys = {{"Enter", "Into the first open slot"}, {"Space", "Carry"}, {"Q E", "Change tab"}};
    }
    else
    {
        Keys = {{"Q E", "Change tab"}};
    }

    ShowKeys();
}

void MCCampaignScreen::Changed()
{
    _KeysCarry = _Carry.has_value() && _Carry->ByKeys;

    if (_Model != nullptr)
    {
        _Model->Changed();
    }
}

void MCCampaignScreen::Sound(uint32_t sample)
{
    SoundSystem()->PlayInterfaceSample(sample);
}

void MCCampaignScreen::ChooseTab(MCTab tab)
{
    if (_Carry.has_value())
    {
        PutBack();
    }

    if (tab == MCTab::Briefing || tab == MCTab::Deploy)
    {
        _Tab = static_cast<int>(tab);
        SetKeys();
        MarkTab();
        Changed();
        return;
    }

    // PURCHASING and the 'MECH BAY: the 1998 screens until step 2, through a change; their BRIEFING button comes back
    // to the tab left on. The tabs show the one left on meanwhile.
    LeftOn = Tab();
    MarkTab();

    const bool purchasing = tab == MCTab::Purchasing;
    MCScreenFlow::Change(purchasing ? "purchasing" : "mech bay",
                         [purchasing]
                         {
                             MCCampaignMenu::Hide();

                             if (purchasing)
                             {
                                 GlobalLog()->SetUpPurchaseScreen(false);
                             }
                             else
                             {
                                 GlobalLog()->SetUpRepairScreen(false);
                             }
                         });
}

void MCCampaignScreen::Answer(MCDeploy::MCRefusal refusal, const MCLogPart& part, std::optional<size_t> card)
{
    switch (refusal)
    {
        case MCDeploy::MCRefusal::None:
        {
            Sound(PlacedSound);
            return;
        }
        case MCDeploy::MCRefusal::TooHeavy:
        {
            // Betty's warning, as 1998's, and the reason under the card (1998 said it in a box after the drop).
            SoundSystem()->PlayBettySample(TooHeavyBetty);
            Layer().Kit().Refuse();
            const float over =
                static_cast<float>(CurDeployTonnage) + part.CurTonnage - static_cast<float>(MaxDeployTonnage);
            _WhyText = std::format(MCCampaignMenu::TooHeavyFormat, MCCampaignMenu::TonsText(Tons(over)));
            break;
        }
        case MCDeploy::MCRefusal::NoSlot:
        {
            Layer().Kit().Refuse();
            _WhyText = MCCampaignMenu::NoSlotText;
            break;
        }
        case MCDeploy::MCRefusal::EngineDamaged:
        case MCDeploy::MCRefusal::LocationDestroyed:
        {
            // 1998's own words, in the remade window.
            if (const std::optional<uint32_t> string = MCDeploy::RefusalString(refusal); string.has_value())
            {
                MCRmlDialogs::Message(LoadGameString(*string, StringBuffer));
            }

            return;
        }
        case MCDeploy::MCRefusal::NotOwn:
        case MCDeploy::MCRefusal::Locked:
            return;
    }

    _WhyCard = card;
    _WhyUntil = RmlNow() + WhySeconds;
}

void MCCampaignScreen::SendReady(size_t ready, std::optional<size_t> group)
{
    const std::shared_ptr<MCLogPart> part = ready < _ReadyUnits.size() ? _ReadyUnits[ready].lock() : nullptr;

    if (part == nullptr || _Carry.has_value())
    {
        return;
    }

    const MCDeploy::MCRefusal lifted = MCDeploy::Lift(*part);

    if (lifted != MCDeploy::MCRefusal::None)
    {
        Answer(lifted, *part, ready);
        return;
    }

    // Its tons as the refusal says them, before it's put back.
    const MCDeploy::MCRefusal sent = MCDeploy::Send(*part, group);

    if (sent != MCDeploy::MCRefusal::None)
    {
        MCDeploy::Unlift(*part);
    }

    Answer(sent, *part, ready);
    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::SendBack(size_t slot)
{
    if (_Carry.has_value() || GlobalLog()->BriefingScreen->ButtonsLocked)
    {
        return;
    }

    if (MCDeploy::TakeOut(slot))
    {
        Sound(PlacedSound);
        FillCards();
        FillWeight();
        Changed();
    }
}

void MCCampaignScreen::PickUpReady(size_t ready, bool byKeys)
{
    const std::shared_ptr<MCLogPart> part = ready < _ReadyUnits.size() ? _ReadyUnits[ready].lock() : nullptr;

    if (part == nullptr || _Carry.has_value())
    {
        return;
    }

    const MCDeploy::MCRefusal lifted = MCDeploy::Lift(*part);

    if (lifted == MCDeploy::MCRefusal::NotOwn || lifted == MCDeploy::MCRefusal::Locked)
    {
        return;
    }

    Sound(PickedUpSound);

    if (lifted != MCDeploy::MCRefusal::None)
    {
        Answer(lifted, *part, ready);
        return;
    }

    _Carry = MCCarry{.Part = part, .FromReady = ready, .ByKeys = byKeys};
    SetKeys();
    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::PickUpSlot(size_t slot, bool byKeys)
{
    const std::shared_ptr<MCLogPart> part = MCDeploy::SlotUnit(slot);

    if (part == nullptr || _Carry.has_value() || !MCDeploy::Open(slot) || GlobalLog()->BriefingScreen->ButtonsLocked)
    {
        return;
    }

    // Out of its slot as it's lifted (its tons leave the drop), as 1998's.
    if (!MCDeploy::TakeOut(slot))
    {
        return;
    }

    Sound(PickedUpSound);
    _Carry =
        MCCarry{.Part = part, .FromSlot = slot, .ByKeys = byKeys, .Over = byKeys ? std::optional(slot) : std::nullopt};
    SetKeys();
    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::HoldOver(std::optional<size_t> slot)
{
    if (!_Carry.has_value() || _Carry->Over == slot)
    {
        return;
    }

    _Carry->Over = slot;
    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::PutDown()
{
    const std::shared_ptr<MCLogPart> part = Carried();

    if (!_Carry.has_value())
    {
        return;
    }

    const MCCarry carry = *_Carry;
    _Carry.reset();
    SetKeys();

    if (part == nullptr)
    {
        FillCards();
        Changed();
        return;
    }

    MCDeploy::MCRefusal placed = MCDeploy::MCRefusal::NoSlot;

    if (carry.Over.has_value() && MCDeploy::Open(*carry.Over))
    {
        placed = MCDeploy::Place(*part, *carry.Over);
    }

    if (placed == MCDeploy::MCRefusal::None)
    {
        Sound(PlacedSound);
    }
    else
    {
        // Nowhere to go: back in READY AREA (from a slot, out of the drop already), a refusal said.
        MCDeploy::Unlift(*part);

        if (placed == MCDeploy::MCRefusal::TooHeavy)
        {
            const std::vector<std::shared_ptr<MCLogPart>> units = MCDeploy::ReadyUnits();
            const auto at = std::ranges::find(units, part);
            Answer(placed, *part,
                   at != units.end() ? std::optional(static_cast<size_t>(at - units.begin())) : std::nullopt);
        }
        else
        {
            Sound(PlacedSound);
        }
    }

    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::PutBack()
{
    const std::shared_ptr<MCLogPart> part = Carried();

    if (!_Carry.has_value())
    {
        return;
    }

    const MCCarry carry = *_Carry;
    _Carry.reset();
    SetKeys();

    if (part != nullptr)
    {
        // Where it was: READY AREA, or its slot again (its tons were taken off it, so it fits).
        if (carry.FromSlot.has_value() && MCDeploy::Place(*part, *carry.FromSlot) == MCDeploy::MCRefusal::None)
        {
            Sound(PlacedSound);
        }
        else
        {
            MCDeploy::Unlift(*part);
            Sound(PlacedSound);
        }
    }

    FillCards();
    FillWeight();
    Changed();
}

void MCCampaignScreen::PressBegin()
{
    if (!MCDeploy::CanBegin())
    {
        SoundSystem()->PlayBettySample(BeginRefusedBetty);
        Layer().Kit().Refuse();
        return;
    }

    switch (MCDeploy::Begin())
    {
        case MCDeploy::MCBegin::Ready:
        {
            if (MultiPlayer() == nullptr)
            {
                Sound(BeginSound);
            }

            break;
        }
        case MCDeploy::MCBegin::TooManyUnits:
        {
            Sound(TooManySound);
            MCRmlDialogs::Message(MCDeploy::TooManyUnitsText());
            break;
        }
        case MCDeploy::MCBegin::Refused:
            break;
    }
}

std::optional<std::pair<bool, size_t>> MCCampaignScreen::CardOf(const Rml::Element* element)
{
    for (const Rml::Element* at = element; at != nullptr; at = at->GetParentNode())
    {
        const std::string& id = at->GetId();

        if (const std::optional<size_t> ready = NumberAfter(id, ReadyPrefix); ready.has_value())
        {
            return std::pair{true, *ready};
        }

        if (const std::optional<size_t> slot = NumberAfter(id, SlotPrefix); slot.has_value() && *slot < MCDeploy::Slots)
        {
            return std::pair{false, *slot};
        }
    }

    return std::nullopt;
}

std::optional<size_t> MCCampaignScreen::SlotAt(float x, float y) const
{
    for (size_t slot = 0; slot < MCDeploy::Slots; ++slot)
    {
        if (const Rml::Element* card = ElementOf(Document(), std::format("{}{}", SlotPrefix, slot));
            card != nullptr && const_cast<Rml::Element*>(card)->IsPointWithinElement(Rml::Vector2f(x, y)))
        {
            return slot;
        }
    }

    return std::nullopt;
}

std::optional<size_t> MCCampaignScreen::FocusedSlot() const
{
    const std::optional<std::pair<bool, size_t>> card = CardOf(Layer().Context().GetFocusElement());
    return card.has_value() && !card->first ? std::optional(card->second) : std::nullopt;
}

std::optional<size_t> MCCampaignScreen::SlotToward(std::optional<size_t> from, int key)
{
    if (!from.has_value())
    {
        for (size_t slot = 0; slot < MCDeploy::Slots; ++slot)
        {
            if (MCDeploy::Open(slot))
            {
                return slot;
            }
        }

        return std::nullopt;
    }

    const auto group = static_cast<int>(*from / MCDeploy::GroupSlots);
    const auto row = static_cast<int>(*from % MCDeploy::GroupSlots);
    const auto slotAt = [](int inGroup, int atRow)
    { return static_cast<size_t>(inGroup) * MCDeploy::GroupSlots + static_cast<size_t>(atRow); };
    constexpr auto Rows = static_cast<int>(MCDeploy::GroupSlots);
    constexpr auto Groups = static_cast<int>(MCDeploy::Groups);

    if (key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN)
    {
        const int step = key == Rml::Input::KI_UP ? -1 : 1;

        for (int at = row + step; at >= 0 && at < Rows; at += step)
        {
            if (MCDeploy::Open(slotAt(group, at)))
            {
                return slotAt(group, at);
            }
        }

        return std::nullopt;
    }

    const int step = key == Rml::Input::KI_LEFT ? -1 : 1;

    for (int across = group + step; across >= 0 && across < Groups; across += step)
    {
        // The same row if it's open, else the nearest open one (above first).
        for (int reach = 0; reach < Rows; ++reach)
        {
            for (const int at : {row - reach, row + reach})
            {
                if (at >= 0 && at < Rows && MCDeploy::Open(slotAt(across, at)))
                {
                    return slotAt(across, at);
                }
            }
        }
    }

    return std::nullopt;
}

void MCCampaignScreen::PlaceCarried()
{
    Rml::Element* drop = ElementOf(Document(), DropId);

    if (!_Carry.has_value() || drop == nullptr)
    {
        return;
    }

    const Rml::Vector2f corner = drop->GetAbsoluteOffset(Rml::BoxArea::Border);
    Rml::Vector2f at(_Carry->MouseX - _Carry->GrabX, _Carry->MouseY - _Carry->GrabY);

    // By the keys: over the slot (or the card) with the focus.
    if (_Carry->ByKeys)
    {
        if (Rml::Element* focus = Layer().Context().GetFocusElement(); focus != nullptr)
        {
            at = focus->GetAbsoluteOffset(Rml::BoxArea::Border) +
                 Rml::Vector2f(CarriedOffset, CarriedOffset) * Layer().Unit();
        }
    }

    const std::string left = std::format("{:.0f}px", at.x - corner.x);
    const std::string top = std::format("{:.0f}px", at.y - corner.y);

    if (left != _CarriedLeft || top != _CarriedTop)
    {
        _CarriedLeft = left;
        _CarriedTop = top;

        if (_Model != nullptr)
        {
            _Model->Changed("carriedLeft");
            _Model->Changed("carriedTop");
        }
    }
}

void MCCampaignScreen::OnMouse(Rml::Event& event)
{
    const auto x = event.GetParameter<float>("mouse_x", 0.0f);
    const auto y = event.GetParameter<float>("mouse_y", 0.0f);
    const int button = event.GetParameter<int>("button", LeftButton);

    if (event.GetId() == Rml::EventId::Mousemove)
    {
        if (_Carry.has_value() && !_Carry->ByKeys)
        {
            _Carry->MouseX = x;
            _Carry->MouseY = y;
            HoldOver(SlotAt(x, y));
            PlaceCarried();
        }

        return;
    }

    if (Tab() != MCTab::Deploy)
    {
        return;
    }

    if (event.GetId() == Rml::EventId::Mouseup)
    {
        if (button == LeftButton && _Carry.has_value() && !_Carry->ByKeys)
        {
            HoldOver(SlotAt(x, y));
            PutDown();
        }

        return;
    }

    // A press: a carry by the keys is put back first.
    if (_Carry.has_value())
    {
        if (!_Carry->ByKeys)
        {
            return;
        }

        PutBack();
    }

    Rml::Element* target = event.GetTargetElement();
    const std::optional<std::pair<bool, size_t>> card = CardOf(target);

    if (!card.has_value())
    {
        return;
    }

    const auto [ready, index] = *card;

    if (button == RightButton)
    {
        if (ready)
        {
            SendReady(index, MCDeploy::HeldGroup());
        }
        else
        {
            SendBack(index);
        }

        return;
    }

    if (button != LeftButton)
    {
        return;
    }

    // Taken where the mouse took it.
    Rml::Vector2f corner(x, y);

    for (Rml::Element* at = target; at != nullptr; at = at->GetParentNode())
    {
        if (CardOf(at) == card && at->GetId().starts_with(ready ? ReadyPrefix : SlotPrefix))
        {
            corner = at->GetAbsoluteOffset(Rml::BoxArea::Border);
            break;
        }
    }

    if (ready)
    {
        PickUpReady(index, false);
    }
    else
    {
        PickUpSlot(index, false);
    }

    if (_Carry.has_value())
    {
        _Carry->GrabX = x - corner.x;
        _Carry->GrabY = y - corner.y;
        _Carry->MouseX = x;
        _Carry->MouseY = y;
        HoldOver(SlotAt(x, y));
        PlaceCarried();
    }
}

bool MCCampaignScreen::OnKey(int key, int /*modifiers*/)
{
    if (Tab() != MCTab::Deploy)
    {
        return false;
    }

    const std::optional<std::pair<bool, size_t>> card = CardOf(Layer().Context().GetFocusElement());

    switch (key)
    {
        case Rml::Input::KI_SPACE:
        {
            // Space picks up the card with the focus, and puts it down where the focus is.
            if (_Carry.has_value())
            {
                HoldOver(FocusedSlot());
                PutDown();
            }
            else if (card.has_value())
            {
                card->first ? PickUpReady(card->second, true) : PickUpSlot(card->second, true);
                PlaceCarried();
            }

            // Anything else with the focus (BEGIN MISSION, a tab) is RmlUi's to press.
            return _Carry.has_value() || card.has_value();
        }
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        {
            // Enter does the right button's send.
            if (card.has_value() && !_Carry.has_value())
            {
                card->first ? SendReady(card->second, std::nullopt) : SendBack(card->second);
            }

            return card.has_value();
        }
        case Rml::Input::KI_LEFT:
        case Rml::Input::KI_RIGHT:
        case Rml::Input::KI_UP:
        case Rml::Input::KI_DOWN:
        {
            if (!_Carry.has_value() || !_Carry->ByKeys)
            {
                return false;
            }

            // The carry's arrows are the screen's, the focus following the unit over the slots.
            if (const std::optional<size_t> next = SlotToward(_Carry->Over, key); next.has_value())
            {
                if (Rml::Element* slot = ElementOf(Document(), std::format("{}{}", SlotPrefix, *next)); slot != nullptr)
                {
                    slot->Focus(true);
                }

                HoldOver(next);
                PlaceCarried();
            }

            return true;
        }
        case Rml::Input::KI_1:
        case Rml::Input::KI_2:
        case Rml::Input::KI_3:
        {
            if (card.has_value() && card->first && !_Carry.has_value())
            {
                SendReady(card->second, static_cast<size_t>(key - Rml::Input::KI_1));
            }

            return card.has_value();
        }
        default:
            return false;
    }
}

void MCCampaignScreen::OnKeyDown(Rml::Event& event)
{
    const int key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const int modifiers = event.GetParameter<int>("modifiers", 0);

    if (OnKey(key, modifiers))
    {
        event.StopPropagation();
    }
}

void MCCampaignScreen::OnFrame()
{
    if (!Shown() || GlobalLog() == nullptr)
    {
        return;
    }

    bool changed = FillBar();
    const double now = RmlNow();
    // The deploy data as it stands: the moves made here, and a teammate's or a cheat's made elsewhere.
    uint64_t revision = MCDeploy::Revision() ^ static_cast<uint64_t>(CurDeployTonnage) ^
                        (static_cast<uint64_t>(MaxDeployTonnage) << 16);

    for (size_t slot = 0; slot < MCDeploy::Slots; ++slot)
    {
        revision = revision * 31 + reinterpret_cast<uintptr_t>(MCDeploy::SlotUnit(slot).get()) +
                   (MCDeploy::Open(slot) ? 1 : 0);
    }

    revision = revision * 31 + MCDeploy::ReadyUnits().size();

    if (revision != _Revision || (_WhyCard.has_value() && now >= _WhyUntil))
    {
        if (_WhyCard.has_value() && now >= _WhyUntil)
        {
            _WhyCard.reset();
        }

        _Revision = revision;

        // A carried unit gone (sold, a match's end): nothing carried.
        if (_Carry.has_value() && Carried() == nullptr)
        {
            _Carry.reset();
            SetKeys();
        }

        FillCards();
        FillWeight();
        changed = true;
    }

    if (_Carry.has_value() && _Carry->ByKeys)
    {
        HoldOver(FocusedSlot());
        PlaceCarried();
    }

    changed = StepRamp(now) || changed;

    if (changed && _Model != nullptr)
    {
        _Model->Changed();
    }
}

// MCCampaignMenu

bool& MCCampaignMenu::Remade()
{
    static bool remade = true;
    return remade;
}

bool MCCampaignMenu::Wanted()
{
    return Remade() && MCRmlFlow::Menus().has_value();
}

void MCCampaignMenu::ComeBack()
{
    ComingBack = true;
}

bool MCCampaignMenu::Show()
{
    const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();
    const MCCampaignScreen::MCTab tab = ComingBack ? LeftOn : MCCampaignScreen::MCTab::Briefing;
    ComingBack = false;

    if (!Remade() || !menus.has_value())
    {
        return false;
    }

    if (const std::shared_ptr<MCCampaignScreen> screen = TheScreen().Lock();
        screen == nullptr || screen->WasDestroyed())
    {
        TheScreen() = menus->get().Menus().CreateChild<MCCampaignScreen>([](MCCampaignScreen&) {});
    }

    MCCampaignScreen& screen = *TheScreen();

    if (!screen.Document().has_value())
    {
        for (const MCScreenChange::MCPiece& piece : screen.Prepare())
        {
            piece.Run();
        }
    }

    if (!screen.Error().empty())
    {
        SDL_Log("Menus: the briefing can't show: %s", screen.Error().c_str());
        return false;
    }

    screen.SetUp(tab);
    screen.Layer().Context().Update();
    screen.Show();
    return true;
}

void MCCampaignMenu::Hide()
{
    if (const MCOptionalRef<MCCampaignScreen> screen = Screen(); screen.has_value() && screen->get().Shown())
    {
        screen->get().Hide();
    }
}

MCOptionalRef<MCCampaignScreen> MCCampaignMenu::Screen()
{
    const std::shared_ptr<MCCampaignScreen> screen = TheScreen().Lock();
    return screen != nullptr && !screen->WasDestroyed() ? MCOptionalRef<MCCampaignScreen>(*screen) : std::nullopt;
}

std::string MCCampaignMenu::TonsText(int32_t tons)
{
    return tons == 1 ? std::format("{} ton", tons) : std::format("{} tons", tons);
}

bool MCCampaignMenu::Shown()
{
    const MCOptionalRef<MCCampaignScreen> screen = Screen();
    return screen.has_value() && screen->get().Shown();
}

bool MCCampaignMenu::Ready()
{
    const MCOptionalRef<MCCampaignScreen> screen = Screen();
    return !screen.has_value() || !screen->get().Shown() || screen->get().Ready();
}
