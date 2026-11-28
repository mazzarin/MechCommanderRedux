#include "stdafx.h"
#include "logistics/MCCampaignMenu.h"
#include "MCPort.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCScreenChange.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCBriefingText.h"
#include "logistics/MCChatView.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogisticsTabs.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCMissionNames.h"
#include "logistics/MCOperationVideo.h"
#include "logistics/MCSaves.h"
#include "logistics/MCSmuti.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCGameStrings.h"
#include "main/MCLogMech.h"
#include "main/MCLogVehicle.h"
#include "main/MCLogWarrior.h"
#include "main/MCLogWarriorList.h"
#include "main/MCLogistics.h"
#include "main/MCMPPlayerLights.h"
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
#include "logistics/MCDeploy.h"
#include "main/MCPrefs.h"

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
    /// <summary>
    /// A match's chat's places (BRIEFING's under the orders, DEPLOY's under the groups, each tab's column at 21:9, and
    /// PURCHASING's and the 'MECH BAY's band under their middle and right columns): its line's boxes and TEAM/ALL
    /// switches, kept as one.
    /// </summary>
    constexpr std::array<const char*, 5> SayIds = {"say-brief", "say-brief-side", "say-deploy", "say-deploy-side",
                                                   "say-logistics"};
    constexpr std::array<const char*, 5> ToIds = {"to-brief", "to-brief-side", "to-deploy", "to-deploy-side",
                                                  "to-logistics"};
    /// <summary>The chat's switch: TEAM, then ALL (the room's order).</summary>
    constexpr int TeamChoice = 0;
    constexpr int AllChoice = 1;
    /// <summary>A player's light once they've pressed BEGIN MISSION (MCMPPlayerLights' status 2, 1998's blinking green).</summary>
    constexpr int32_t ReadyLight = 2;
    constexpr const char* TabsId = "tabs";
    constexpr const char* MapId = "map";
    /// <summary>DEPLOY's briefing map, at READY AREA's foot.</summary>
    constexpr const char* MiniMapId = "mini-map";
    /// <summary>A briefing map's picture drawn to fit its box with its own shape, at its left and top.</summary>
    constexpr std::string_view MapPictureFormat = "image({} contain left top)";
    /// <summary>DEPLOY's key for its briefing map, and what the hint line says it does.</summary>
    constexpr std::string_view MapKey = "M";
    constexpr std::string_view ShowMapWords = "Show map";
    constexpr std::string_view HideMapWords = "Hide map";
    /// <summary>The modifiers that make a letter a shortcut (Cmd+M minimizes), never DEPLOY's map key.</summary>
    constexpr int ShortcutModifiers = Rml::Input::KM_CTRL | Rml::Input::KM_ALT | Rml::Input::KM_META;
    constexpr const char* DropId = "drop";
    constexpr std::string_view ReadyPrefix = "ready";
    constexpr std::string_view SlotPrefix = "slot";
    /// <summary>What a vehicle's card says it is, under its name (a 'Mech without a MechWarrior's says NO PILOT).</summary>
    constexpr std::string_view VehicleWord = "Vehicle";

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
    /// <summary>The document's events the screen listens to: the mouse's for the drag (its keys: TakeKeysFirst).</summary>
    constexpr std::array ListenedEvents = {Rml::EventId::Mousedown, Rml::EventId::Mouseup, Rml::EventId::Mousemove};

    double RmlNow()
    {
        Rml::SystemInterface* system = Rml::GetSystemInterface();
        return system != nullptr ? system->GetElapsedTime() : 0.0;
    }

    /// <summary>What the log calls the screen.</summary>
    constexpr std::string_view BriefingName = "the briefing";

    /// <summary>
    /// A drop zone tag's picture (tools/upscale/zone_tags.py, its zone_tags.json): the corner its tail leaves from, as
    /// the files name it, and its box's size and the tail's tip in 1998 map pixels (art/hd's pictures are 4x that,
    /// art's 1x). F1 to F3 share each corner's.
    /// </summary>
    struct MCTagShape
    {
        MCCampaignMenu::MCTail Tail;
        std::string_view Corner;
        float Width;
        float Height;
        float TipX;
        float TipY;
    };

    /// <summary>The tags' corners in the order they're tried: 1998's flags' bottom right first, then its mirrors.</summary>
    constexpr std::array<MCTagShape, 4> ZoneTagShapes = {{
        {MCCampaignMenu::MCTail::BottomRight, "br", 39.0f, 19.0f, 37.25f, 17.25f},
        {MCCampaignMenu::MCTail::BottomLeft, "bl", 39.0f, 19.0f, 0.5f, 17.25f},
        {MCCampaignMenu::MCTail::TopRight, "tr", 39.0f, 19.0f, 37.25f, 0.25f},
        {MCCampaignMenu::MCTail::TopLeft, "tl", 39.0f, 19.0f, 0.5f, 0.25f},
    }};
    /// <summary>A team's drop zones: one a group (DEPLOY's three), F1 to F3.</summary>
    constexpr int ZoneGroups = 3;
    constexpr std::string_view ZoneTagFormat = "art/zone_tag_F{}_{}.png";
    constexpr std::string_view ZoneTagPicture = "image({})";
    /// <summary>A length in whole pixels, as RmlUi reads it.</summary>
    constexpr std::string_view PixelsFormat = "{:.0f}px";
    // The shapes in MCTail's order, so a tail finds its own.
    static_assert(std::ranges::all_of(std::views::iota(size_t{0}, ZoneTagShapes.size()), [](size_t i)
                                      { return ZoneTagShapes[i].Tail == static_cast<MCCampaignMenu::MCTail>(i); }));

    const MCTagShape& ShapeOf(MCCampaignMenu::MCTail tail)
    {
        return ZoneTagShapes[static_cast<size_t>(tail)];
    }

    MCGuiHandle<MCCampaignScreen>& TheScreen()
    {
        static MCGuiHandle<MCCampaignScreen> screen;
        return screen;
    }

    /// <summary>
    /// The tab the screen was left on (for the 1998 PURCHASING or 'MECH BAY, the main menu), and whether the next show
    /// comes back to it.
    /// </summary>
    MCCampaignScreen::MCTab LeftOn = MCCampaignScreen::MCTab::Briefing;
    bool ComingBack = false;

    using MCCampaignMenu::NumberAfter;
    using MCCampaignMenu::Pictured;
    using MCCampaignMenu::PilotSource;

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

    /// <summary>Whether player <paramref name="id"/>'s light is green: BEGIN MISSION pressed.</summary>
    bool LightReady(const MCLogistics& logistics, uint32_t id)
    {
        if (logistics.PlayerLights == nullptr)
        {
            return false;
        }

        const MCMPPlayerLights& lights = *logistics.PlayerLights;
        const auto count = static_cast<size_t>(std::clamp(lights.NumPlayers, 0, MCMPPlayerLights::MaxPlayers));

        for (size_t light = 0; light < count; ++light)
        {
            if (lights.PlayerIDs[light] == id)
            {
                return lights.PlayerStatus[light] == ReadyLight;
            }
        }

        return false;
    }
}

/// <summary>The document's mouse: presses, releases and moves over DEPLOY's cards.</summary>
class MCCampaignScreen::MCEvents final : public Rml::EventListener
{
public:
    explicit MCEvents(MCCampaignScreen& screen) : _Screen(screen) {}

    void ProcessEvent(Rml::Event& event) override { _Screen.OnMouse(event); }

private:
    MCCampaignScreen& _Screen;
};

MCCampaignScreen::MCCampaignScreen()
{
    SetDocument(ScreenDocument);
    _Logistics = std::make_unique<MCLogisticsTabs>(*this);
    Back = [this]
    {
        // Escape puts a carried unit back, or closes PURCHASING's deal window; else the campaign's way back (the main
        // menu, or a match's question).
        if (_Carry.has_value())
        {
            PutBack();
            return;
        }

        if (_Logistics->Back())
        {
            return;
        }

        MCScreenFlow::Back();
    };

    for (size_t segment = 0; segment < Segments; ++segment)
    {
        const float height = LowestSegment + (1.0f - LowestSegment) * static_cast<float>(segment) / (Segments - 1);
        _Ramp.push_back(MCSegment{.Height = std::format("{:.1f}%", height * WholePercent)});
    }

    // DEPLOY's ring hidden at the map's corner until a group is in view (its style read all the same).
    _RingLeft = std::format(PixelsFormat, 0.0f);
    _RingTop = _RingLeft;
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
    model.List("rpFigures", _PointsFigures, std::pair{"text", &MCFigureGlyph::Text},
               std::pair{"digit", &MCFigureGlyph::Digit});
    model.List("clockFigures", _ClockFigures, std::pair{"text", &MCFigureGlyph::Text},
               std::pair{"digit", &MCFigureGlyph::Digit});
    model.Value("eyebrow", _Eyebrow);
    model.Value("videoDocked", _VideoDocked);
    model.Value("title", _Title);
    model.Value("timeLimit", _TimeLimit);
    model.Value("dropWeight", _DropWeight);
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
    model.Value("hasMap", _HasMap);
    model.Value("deployMap", _DeployMap);
    model.Value("ringShown", _RingShown);
    model.Value("ringLeft", _RingLeft);
    model.Value("ringTop", _RingTop);
    model.List("objectives", _Objectives, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.List("fire", _Fire, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.List("orders", _Orders, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.List("warnings", _Warnings, std::pair{"text", &MCLine::Text}, std::pair{"num", &MCLine::Number},
               std::pair{"limit", &MCLine::Limit});
    model.List("zones", _Zones, std::pair{"left", &MCZoneShown::Left}, std::pair{"top", &MCZoneShown::Top},
               std::pair{"width", &MCZoneShown::Width}, std::pair{"height", &MCZoneShown::Height},
               std::pair{"picture", &MCZoneShown::Picture});
    model.List("miniZones", _MiniZones, std::pair{"left", &MCZoneShown::Left}, std::pair{"top", &MCZoneShown::Top},
               std::pair{"width", &MCZoneShown::Width}, std::pair{"height", &MCZoneShown::Height},
               std::pair{"picture", &MCZoneShown::Picture});
    // The cards' list first: the groups' rows hold their cards (RmlUi's register then knows a list of cards).
    model.List("ready", _Ready, std::pair{"kind", &MCCard::Kind}, std::pair{"who", &MCCard::Who},
               std::pair{"what", &MCCard::What}, std::pair{"letter", &MCCard::Letter}, std::pair{"tons", &MCCard::Tons},
               std::pair{"outline", &MCCard::Outline}, std::pair{"pilot", &MCCard::Pilot},
               std::pair{"piloted", &MCCard::Piloted}, std::pair{"unpiloted", &MCCard::Unpiloted},
               std::pair{"condition", &MCCard::Condition}, std::pair{"shade", &MCCard::Shade},
               std::pair{"health", &MCCard::Health}, std::pair{"off", &MCCard::Off},
               std::pair{"picked", &MCCard::Picked}, std::pair{"target", &MCCard::Target},
               std::pair{"refuse", &MCCard::Refuse}, std::pair{"why", &MCCard::Why}, std::pair{"slot", &MCCard::Slot},
               std::pair{"long", &MCCard::Long}, std::pair{"longer", &MCCard::Longer},
               std::pair{"vehicle", &MCCard::Vehicle});
    model.List("carried", _Carried, std::pair{"kind", &MCCard::Kind});
    model.List("groups", _Groups, std::pair{"name", &MCGroup::Name}, std::pair{"tons", &MCGroup::Tons},
               std::pair{"closed", &MCGroup::Closed}, std::pair{"cards", &MCGroup::Cards});
    model.List("ramp", _Ramp, std::pair{"height", &MCSegment::Height}, std::pair{"state", &MCSegment::State});
    model.List("ours", _Ours, std::pair{"name", &MCBarPlayer::Name}, std::pair{"colour", &MCBarPlayer::Colour},
               std::pair{"ready", &MCBarPlayer::Ready});
    model.List("theirs", _Theirs, std::pair{"name", &MCBarPlayer::Name}, std::pair{"colour", &MCBarPlayer::Colour},
               std::pair{"ready", &MCBarPlayer::Ready});
    model.Value("readyCount", _ReadyCount);
    model.Value("playerCount", _PlayerCount);
    _Chat.Bind(model, "chat");
    model.Value("say", _Say);
    _Logistics->Bind(model);
}

void MCCampaignScreen::OnLoaded(Rml::ElementDocument& document)
{
    OnClick("mainmenu", [] { MCMenuScreens::LeaveCampaign(); });
    OnClick("begin", [this] { PressBegin(); });
    OnClick("show-map", [this] { ToggleMap(); });
    OnClick("hide-map", [this] { ToggleMap(); });
    OnChange(TabsId, [this](int tab) { ChooseTab(static_cast<MCTab>(tab)); });
    // PURCHASING's and the 'MECH BAY's controls.
    OnChange("kinds", [this](int kind) { _Logistics->ChooseKind(static_cast<MCLogisticsTabs::MCKind>(kind)); });
    OnChange("stock-kinds",
             [this](int stock) { _Logistics->ChooseStock(static_cast<MCLogisticsTabs::MCStock>(stock)); });
    OnChange("deal-count", [this](int choice) { _Logistics->DealCount(choice + 1); });
    OnChange("armor", [this](int place) { _Logistics->MoveSlider(MCMechBay::MCKnob::Armor, place); });
    OnChange("structure", [this](int place) { _Logistics->MoveSlider(MCMechBay::MCKnob::Internal, place); });
    OnChange("engine", [this](int level) { _Logistics->MoveSlider(MCMechBay::MCKnob::Engine, level); });
    OnClick("deal", [this] { _Logistics->PressDeal(); });
    OnClick("deal-cancel", [this] { _Logistics->DealCancel(); });
    OnClick("deal-confirm", [this] { _Logistics->DealConfirm(); });
    OnClick("repair-all", [this] { _Logistics->RepairAll(); });
    OnClick("refit-all", [this] { _Logistics->RefitAll(); });
    OnClick("repair-force", [this] { _Logistics->RepairForce(); });
    OnClick("refit-force", [this] { _Logistics->RefitForce(); });
    OnClick("into-force", [this] { _Logistics->IntoTheForce(); });

    // A match's chat: each place's line sent on Enter, its switch followed by the others.
    for (const char* id : SayIds)
    {
        OnText(id,
               [this](const std::string& typed, bool enter)
               {
                   if (enter)
                   {
                       SayTyped(typed);
                   }
               });
    }

    for (const char* id : ToIds)
    {
        OnChange(id, [this](int choice) { ChooseWhoReads(choice == TeamChoice); });
    }

    _Events = std::make_unique<MCEvents>(*this);
    // DEPLOY's keys on a card before RmlUi's (Enter and Space click what has the focus); BRIEFING's Space plays the video.
    TakeKeysFirst();

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

    // The tabs (made with the screen, kept until its memory goes) let go of the model before it goes.
    _Logistics->Unbind();
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCCampaignScreen::OnShow()
{
    _LastFrame = RmlNow();
    // The chat read afresh: it may have lines from the room, or from while a 1998 screen showed.
    _Chat.Forget();

    // A dialog over it belongs to the mission ("Beachhead · Mission 1", "Solo"), not the main menu it came from; a
    // match's to its room, which named them as it showed.
    if (!_Multiplayer)
    {
        MCRmlDialogs::SetPlace(_Eyebrow);
    }
}

void MCCampaignScreen::OnHide()
{
    // However it hides (logistics' screen changing hides the remade ones), the way back comes to this tab.
    LeftOn = Tab();
    // The operation's video goes with the briefing (a mission begun, the main menu); a deal or a drag with its tab.
    MCVideoMenu::Close();
    _Logistics->Leave();

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
    _Tab = static_cast<int>(tab);
    _Multiplayer = MultiPlayer() != nullptr;
    _DeployMap = DeployMapShown;
    ReadBriefing();
    FillBar();
    FillPlayers();
    ChooseWhoReads(true);
    FillCards();
    FillWeight();
    _ShownOn = SegmentsOf(Weighed().Base);
    _ShownMore = _ShownOn;

    if (tab == MCTab::Purchasing || tab == MCTab::MechBay)
    {
        _Logistics->Show(tab);
    }

    SetKeys();
    MarkTab();
    Changed();
}

void MCCampaignScreen::MarkTab()
{
    if (const MCOptionalRef<Rml::Element> tabs = ElementById(TabsId);
        tabs.has_value() && MCRmlKit::Choice(*tabs) != _Tab)
    {
        MCRmlKit::SetChoice(*tabs, _Tab);
    }
}

void MCCampaignScreen::ReadBriefing()
{
    const MCCampaignMenu::MCMissionRead mission = MCCampaignMenu::ReadMission();
    const MCBriefingText::MCRead& read = mission.Briefing;
    _Title = mission.Title;
    _Eyebrow = mission.Mission > 0 ? std::format(MCCampaignMenu::EyebrowFormat, mission.OperationName, mission.Mission)
                                   : mission.OperationName;
    _Objectives.clear();

    for (size_t at = 0; at < read.Objectives.size(); ++at)
    {
        _Objectives.push_back(MCLine{.Text = read.Objectives[at].Text,
                                     .Number = static_cast<int>(at + 1),
                                     .Limit = read.Objectives[at].TimeLimit});
    }

    _TimeLimit = read.TimeLimit;

    _Fire.clear();

    if (read.FireSupport.has_value())
    {
        for (std::string& line : MCBriefingText::FireSupportLines(*read.FireSupport))
        {
            _Fire.push_back(MCLine{.Text = std::move(line)});
        }
    }

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

    _MapSource = mission.MapSource;
    // The operation's video: a campaign mission's (a match has none: 1998's video tab was its chat).
    _Video = !_Multiplayer && GlobalLog() != nullptr && !GlobalLog()->OperationCinema.empty();
    _VideoEyebrow = mission.Mission > 0
                        ? std::format(MCCampaignMenu::VideoEyebrowFormat, mission.Operation, mission.OperationName)
                        : mission.OperationName;

    _HasMap = !_MapSource.empty();
    const std::string picture = _HasMap ? std::format(MapPictureFormat, _MapSource) : std::string();

    // BRIEFING's map and DEPLOY's, the same picture.
    for (const char* id : {MapId, MiniMapId})
    {
        const MCOptionalRef<Rml::Element> map = ElementById(id);

        if (!map.has_value())
        {
            continue;
        }

        if (picture.empty())
        {
            map->get().RemoveProperty("decorator");
        }
        else
        {
            map->get().SetProperty("decorator", picture);
        }
    }

    // A match's map has no flags painted in: the team's drop zones are tagged over it (a campaign's has its own), on
    // BRIEFING's map and DEPLOY's.
    _ZoneMap = _Multiplayer ? MCCampaignMenu::ZoneTags() : MCCampaignMenu::MCZoneMap{};
    _DropZones = MCCampaignMenu::DropZones();
    _Zones.clear();
    _ZoneBox = {};
    _MiniZones.clear();
    _MiniBox = {};
    _RingShown = false;
    _RingLeft = std::format(PixelsFormat, 0.0f);
    _RingTop = _RingLeft;
    PlaceZoneTags(MapId, _ZoneBox, _Zones);
    PlaceZoneTags(MiniMapId, _MiniBox, _MiniZones);
}

bool MCCampaignScreen::PlaceZoneTags(const char* id, SDL_FPoint& placed, std::vector<MCZoneShown>& shown) const
{
    const MCOptionalRef<Rml::Element> map = ElementById(id);
    const Rml::Vector2f box =
        map.has_value() ? map->get().GetBox().GetSize(Rml::BoxArea::Content) : Rml::Vector2f(0, 0);

    // The map not laid out (another tab shows, DEPLOY's hidden): the tags stay as they were placed; nor moved, they stay.
    if (box.x <= 0 || box.y <= 0 || (box.x == placed.x && box.y == placed.y))
    {
        return false;
    }

    placed = SDL_FPoint{box.x, box.y};
    std::vector<MCZoneShown> tags;

    // The picture drawn to fit the box with its own shape (contain), at its left and top; each tag at its scale, on
    // whole pixels, its tail's tip on the zone.
    if (_ZoneMap.Width > 0 && _ZoneMap.Height > 0)
    {
        const float scale = std::min(box.x / _ZoneMap.Width, box.y / _ZoneMap.Height);

        for (const MCCampaignMenu::MCZoneTag& tag : _ZoneMap.Tags)
        {
            const float width = std::round(tag.Width * scale);
            const float height = std::round(tag.Height * scale);
            const float left = std::round(tag.X * scale - (tag.X - tag.Left) * width / tag.Width);
            const float top = std::round(tag.Y * scale - (tag.Y - tag.Top) * height / tag.Height);
            tags.push_back(MCZoneShown{.Left = std::format(PixelsFormat, left),
                                       .Top = std::format(PixelsFormat, top),
                                       .Width = std::format(PixelsFormat, width),
                                       .Height = std::format(PixelsFormat, height),
                                       .Picture = std::format(ZoneTagPicture, MCCampaignMenu::ZoneTagSource(tag))});
        }
    }

    if (tags == shown)
    {
        return false;
    }

    shown = std::move(tags);
    return true;
}

std::optional<size_t> MCCampaignScreen::GroupInView() const
{
    if (_Carry.has_value())
    {
        return _Carry->Over.has_value() ? std::optional(*_Carry->Over / MCDeploy::GroupSlots) : std::nullopt;
    }

    const std::optional<size_t> slot = FocusedSlot();
    return slot.has_value() ? std::optional(*slot / MCDeploy::GroupSlots) : std::nullopt;
}

bool MCCampaignScreen::PlaceRing()
{
    // Hidden, at the map's corner (its style read all the same).
    bool shown = false;
    std::string left = std::format(PixelsFormat, 0.0f);
    std::string top = left;
    const MCOptionalRef<Rml::Element> map = ElementById(MiniMapId);
    const Rml::Vector2f box =
        map.has_value() ? map->get().GetBox().GetSize(Rml::BoxArea::Content) : Rml::Vector2f(0, 0);
    const std::optional<size_t> group = GroupInView();

    // The zone of the group in view, at the map's scale as its picture is drawn (contain, at its left and top).
    if (MapShown() && group.has_value() && box.x > 0 && box.y > 0 && _DropZones.Width > 0 && _DropZones.Height > 0)
    {
        const float scale = std::min(box.x / _DropZones.Width, box.y / _DropZones.Height);

        for (const MCCampaignMenu::MCZoneTag& zone : _DropZones.Tags)
        {
            if (static_cast<size_t>(zone.Group - 1) == *group)
            {
                shown = true;
                left = std::format(PixelsFormat, std::round(zone.X * scale));
                top = std::format(PixelsFormat, std::round(zone.Y * scale));
            }
        }
    }

    if (shown == _RingShown && left == _RingLeft && top == _RingTop)
    {
        return false;
    }

    _RingShown = shown;
    _RingLeft = std::move(left);
    _RingTop = std::move(top);
    return true;
}

void MCCampaignScreen::ToggleMap()
{
    if (!_HasMap)
    {
        return;
    }

    _DeployMap = !_DeployMap;
    DeployMapShown = _DeployMap;
    SavePrefs();
    // Laid out afresh as it shows (the window may have changed while it was hidden).
    _MiniBox = {};
    SetKeys();
    Changed();
}

bool MCCampaignScreen::FillBar()
{
    const std::string points = ResourceDisplayState == 0
                                   ? MCSaves::PointsText(static_cast<uint32_t>(std::max(ResourcePoints, 0)))
                                   : std::string();
    const std::string clock = MCPort::StrTime();

    if (points == _Points && clock == _Clock)
    {
        return false;
    }

    _Points = points;
    _Clock = clock;
    _PointsFigures = MCHeldFigures(_Points);
    _ClockFigures = MCHeldFigures(_Clock);
    return true;
}

bool MCCampaignScreen::FillPlayers()
{
    std::vector<MCBarPlayer> ours;
    std::vector<MCBarPlayer> theirs;
    size_t ready = 0;
    const std::vector<MCCampaignMenu::MCMatchPlayer> players = MCCampaignMenu::MatchPlayers();

    for (const MCCampaignMenu::MCMatchPlayer& player : players)
    {
        (player.Ours ? ours : theirs)
            .push_back(MCBarPlayer{.Name = player.Name, .Colour = player.Colour, .Ready = player.Ready});
        ready += player.Ready ? 1 : 0;
    }

    std::string readyCount = std::to_string(ready);
    std::string playerCount = std::to_string(players.size());

    if (ours == _Ours && theirs == _Theirs && readyCount == _ReadyCount && playerCount == _PlayerCount)
    {
        return false;
    }

    _Ours = std::move(ours);
    _Theirs = std::move(theirs);
    _ReadyCount = std::move(readyCount);
    _PlayerCount = std::move(playerCount);
    return true;
}

void MCCampaignScreen::ChooseWhoReads(bool toTeam)
{
    _ToTeam = toTeam;
    const int choice = toTeam ? TeamChoice : AllChoice;

    // One place of the chat shows at a time (a tab's, or its column at 21:9): the others follow it.
    for (const char* id : ToIds)
    {
        if (const MCOptionalRef<Rml::Element> to = ElementById(id); to.has_value() && MCRmlKit::Choice(*to) != choice)
        {
            MCRmlKit::SetChoice(*to, choice);
        }
    }
}

void MCCampaignScreen::SayTyped(const std::string& typed)
{
    // As the room's chat says it: to the team or to all, sent to the room and put in the chat.
    MCChatView::Say(typed, _ToTeam);
    _Say.clear();

    if (_Model != nullptr)
    {
        _Model->Changed("say");
    }
}

MCCampaignScreen::MCCard MCCampaignScreen::UnitCard(const MCLogPart& part, int slot) const
{
    MCCard card{.Kind = static_cast<int>(MCCardKind::Unit), .Slot = slot};
    card.Tons = MCCampaignMenu::TonsText(Tons(part.CurTonnage));
    card.Outline = Pictured(MCRmlArt::UnitSource(part));

    if (part.PartType != 1)
    {
        // A vehicle: its name, as 1998's tile writes it, in its own case over VEHICLE, as a 'Mech without a MechWarrior's
        // card (the user's, 26 Nov: REFIT TRUCK in the callsign's capitals overflowed its card); no pilot.
        card.Who = part.FileName;
        card.What = std::string(VehicleWord);
        card.Vehicle = true;
        card.Long = card.Who.size() > UnitNameLetters;
        card.Longer = card.Who.size() > SmallNameLetters;
        return card;
    }

    const auto& mech = static_cast<const MCLogMech&>(part);
    std::shared_ptr<MCLogWarrior> warrior;

    if (mech.LocalPart == 0)
    {
        warrior = mech.NetworkPilot.lock();
    }
    else if (const std::shared_ptr<MCLogistics> logistics = GlobalLog(); logistics != nullptr && mech.PilotIndex >= 0)
    {
        // As 1998's blocks: none for a 'Mech without one (the list gives its first for a negative index).
        logistics->AssignedWarriorList->GetWarriorInfo(mech.PilotIndex, warrior);
    }

    // The name the store and the 'MECH BAY give it (the string table's), not its file's short one (COM-A): its chassis,
    // the variant's letter after it as plain text ("Raven A"; the user's, 26 Nov: the chip's box taken off).
    MCCampaignMenu::MCVariantName name = MCCampaignMenu::VariantName(mech.FileName);
    card.Letter = std::move(name.Letter);

    const size_t letter = card.Letter.empty() ? 0 : LetterLetters;

    if (warrior != nullptr)
    {
        // Under the callsign (its line at the details' size already), a long name in Fine.
        card.Who = warrior->Callsign;
        card.What = std::move(name.Chassis);
        card.Long = card.What.size() + letter > SmallNameLetters;
    }
    else
    {
        // Without a MechWarrior: its name first, in its own case, NO PILOT under it; a long one smaller.
        card.Who = std::move(name.Chassis);
        card.What = std::string(NoPilotWord);
        card.Long = card.Who.size() + letter > UnitNameLetters;
        card.Longer = card.Who.size() + letter > SmallNameLetters;
    }

    card.Pilot = warrior != nullptr ? Pictured(PilotSource(warrior->Picture)) : std::string(NoPicture);
    card.Piloted = warrior != nullptr;
    card.Unpiloted = warrior == nullptr;
    card.Health = warrior != nullptr ? static_cast<int>(std::clamp(std::round(warrior->Health), 0.0f, HealthPips)) : 0;
    // Its condition as it stands (a repair, a refit or a wound shows at once); without a pilot, the 'Mech's own (1998's
    // status reads none), as the logistics' cards show it.
    const float status = warrior != nullptr ? mech.CalcStatus() : mech.CalcCondition();
    card.Condition = static_cast<int>(std::lround(std::clamp(status, 0.0f, 1.0f) * WholePercent));
    card.Shade = status == 0.0f          ? std::string()
                 : !(status < WornBelow) ? std::string("ok")
                 : status > BadFrom      ? std::string("worn")
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
    // BRIEFING's line of it: the limit DEPLOY weighs against (a match's player's share of the team's), none without one.
    _DropWeight = MaxDeployTonnage > 0 ? MCCampaignMenu::TonsText(MaxDeployTonnage) : std::string();
    _WeightFree = over > 0 ? std::format(MCCampaignMenu::OverFormat, MCCampaignMenu::TonsText(Tons(over)))
                           : std::format(MCCampaignMenu::FreeFormat, MCCampaignMenu::TonsText(Tons(-over)));
    _WeightMore = weight.Adds && weight.Total != weight.Base;
    _WeightOver = weight.Refused || over > 0;
    _CanBegin = MCDeploy::CanBegin() && !MCDeploy::Locked();
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
    else if (Tab() == MCTab::Deploy && _HasMap)
    {
        Keys = {{"Enter", "Into the first open slot"},
                {"Space", "Carry"},
                {std::string(MapKey), std::string(_DeployMap ? HideMapWords : ShowMapWords)},
                {"Q E", "Change tab"}};
    }
    else if (Tab() == MCTab::Deploy)
    {
        Keys = {{"Enter", "Into the first open slot"}, {"Space", "Carry"}, {"Q E", "Change tab"}};
    }
    else if (Tab() == MCTab::Purchasing || Tab() == MCTab::MechBay)
    {
        Keys = _Logistics->Keys();
    }
    else if (_Video)
    {
        Keys = {{"Space", "Play operation briefing video"}, {"Q E", "Change tab"}};
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

    // PURCHASING and the 'MECH BAY read the logistics data afresh as they show; a deal or a drag goes with its tab.
    _Logistics->Leave();
    _Tab = static_cast<int>(tab);

    if (tab == MCTab::Purchasing || tab == MCTab::MechBay)
    {
        _Logistics->Show(tab);
    }
    else if (tab == MCTab::Deploy)
    {
        // DEPLOY's cards made from the units as they stand: a repair or a refit on the 'MECH BAY changes how a unit looks,
        // not the deploy data the frame watches.
        FillCards();
    }

    SetKeys();
    MarkTab();
    Changed();
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
    if (_Carry.has_value() || MCDeploy::Locked())
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

    if (part == nullptr || _Carry.has_value() || !MCDeploy::Open(slot) || MCDeploy::Locked())
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
    // A match's BEGIN MISSION once pressed is said: 1998's button stayed down and took no more presses
    // (MCBriefingScreen::HandleClick's !MCDeploy::Locked()). Another would send the ready word and its line again.
    if (MCDeploy::Locked())
    {
        return;
    }

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
            else
            {
                // Dark from now on, as 1998's stayed down.
                FillWeight();
                Changed();
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

std::optional<std::pair<bool, size_t>> MCCampaignScreen::CardOf(MCOptionalRef<const Rml::Element> element)
{
    // Up RmlUi's tree from the element.
    for (const Rml::Element* at = element.has_value() ? &element->get() : nullptr; at != nullptr;
         at = at->GetParentNode())
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
        if (const MCOptionalRef<Rml::Element> card = ElementById(std::format("{}{}", SlotPrefix, slot));
            card.has_value() && card->get().IsPointWithinElement(Rml::Vector2f(x, y)))
        {
            return slot;
        }
    }

    return std::nullopt;
}

std::optional<size_t> MCCampaignScreen::FocusedSlot() const
{
    const std::optional<std::pair<bool, size_t>> card = CardOf(MCOptionalOf(Layer().Context().GetFocusElement()));
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
    const MCOptionalRef<Rml::Element> drop = ElementById(DropId);

    if (!_Carry.has_value() || !drop.has_value())
    {
        return;
    }

    const Rml::Vector2f corner = drop->get().GetAbsoluteOffset(Rml::BoxArea::Border);
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
    if (Tab() == MCTab::Purchasing || Tab() == MCTab::MechBay)
    {
        _Logistics->OnMouse(event);
        return;
    }

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
    const std::optional<std::pair<bool, size_t>> card = CardOf(MCOptionalOf(target));

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
        if (CardOf(*at) == card && at->GetId().starts_with(ready ? ReadyPrefix : SlotPrefix))
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

void MCCampaignScreen::PlayVideo()
{
    // OB-191 (fixed): asked for again while the operation's video shows (Space, its AutoPlay) it does nothing, so it plays
    // once, from its start. The original's tab stopped the movie on a quick second click, and MISSION BRIEFING just after
    // it flipped back to it.
    if (!_Video || MCVideoMenu::Shown())
    {
        return;
    }

    MCVideoMenu::Open(GlobalLog()->OperationCinema, _VideoEyebrow);
}

bool MCCampaignScreen::OnKey(int key, int modifiers)
{
    // BRIEFING: Space plays the operation's video, wherever the focus is.
    if (Tab() == MCTab::Briefing)
    {
        if (key == Rml::Input::KI_SPACE && _Video)
        {
            PlayVideo();
            return true;
        }

        return false;
    }

    if (Tab() == MCTab::Purchasing || Tab() == MCTab::MechBay)
    {
        return _Logistics->OnKey(key, MCOptionalOf(Layer().Context().GetFocusElement()));
    }

    if (Tab() != MCTab::Deploy)
    {
        return false;
    }

    const std::optional<std::pair<bool, size_t>> card = CardOf(MCOptionalOf(Layer().Context().GetFocusElement()));

    switch (key)
    {
        case Rml::Input::KI_M:
        {
            // M shows or hides the briefing map, as its buttons do: not a letter typed in a match's chat, nor a shortcut.
            if (Layer().Typing() || (modifiers & ShortcutModifiers) != 0 || !_HasMap)
            {
                return false;
            }

            ToggleMap();
            return true;
        }
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
                if (const MCOptionalRef<Rml::Element> slot = ElementById(std::format("{}{}", SlotPrefix, *next));
                    slot.has_value())
                {
                    slot->get().Focus(true);
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

void MCCampaignScreen::OnFrame()
{
    if (!Shown() || GlobalLog() == nullptr)
    {
        return;
    }

    // 1998's AutoPlay (the campaign file's, on each operation's first mission): the operation's video plays by itself
    // once BRIEFING shows whole (the change's fade in over, nothing said over it), once: the flag cleared as it starts,
    // as 1998's SetUpOperation cleared it, so BRIEFING shown again (a tab, RETURN TO GAME) doesn't play it again. The
    // campaign file sets it again each time a briefing is read (a save loaded at that mission, REPLAY MISSION), as 1998's.
    if (MCCampaignMenu::AutoPlaysVideo && GlobalLog()->AutoPlayMovie && _Video && Tab() == MCTab::Briefing &&
        !MCScreenChange::Running() && !MCRmlDialogs::Shown() && !MCVideoMenu::Shown())
    {
        GlobalLog()->AutoPlayMovie = false;
        PlayVideo();
    }

    bool changed = FillBar();

    // The operation's video docked at BRIEFING's orders' foot, the briefing around it as it was: it goes with BRIEFING
    // (another tab chosen), and the orders end above it while it shows.
    if (MCVideoMenu::Shown() && Tab() != MCTab::Briefing)
    {
        MCVideoMenu::Close();
    }

    if (const bool docked = MCVideoMenu::Shown(); docked != _VideoDocked)
    {
        _VideoDocked = docked;

        if (_Model != nullptr)
        {
            _Model->Changed("videoDocked");
        }
    }

    if (_Multiplayer)
    {
        changed = FillPlayers() || changed;
        // The maps' drop zone tags follow their boxes (a window resized, BRIEFING shown again, DEPLOY's map shown).
        changed = PlaceZoneTags(MapId, _ZoneBox, _Zones) || changed;
        changed = PlaceZoneTags(MiniMapId, _MiniBox, _MiniZones) || changed;

        if (_Model != nullptr)
        {
            _Chat.Show(*_Model);
        }
    }

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

    if (Tab() == MCTab::Deploy)
    {
        changed = PlaceRing() || changed;
    }

    changed = StepRamp(now) || changed;

    if (Tab() == MCTab::Purchasing || Tab() == MCTab::MechBay)
    {
        changed = _Logistics->OnFrame() || changed;
    }

    if (changed && _Model != nullptr)
    {
        _Model->Changed();
    }
}

// MCCampaignMenu

std::vector<MCCampaignMenu::MCMatchPlayer> MCCampaignMenu::MatchPlayers()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (logistics == nullptr || MultiPlayer() == nullptr)
    {
        return {};
    }

    std::vector<MCRoomModel::MCPlayer> players = logistics->Room.Players();
    const auto me = std::ranges::find(players, true, &MCRoomModel::MCPlayer::Me);
    const int8_t team = me != players.end() ? me->Team : MCRoomModel::NoTeam;
    // This game's team first, then the other, each by its slots (a player on no team last).
    std::ranges::stable_sort(
        players, {}, [team](const MCRoomModel::MCPlayer& player)
        { return std::tuple(player.Team != team, player.Team == MCRoomModel::NoTeam, player.Team, player.Slot); });
    std::vector<MCMatchPlayer> shown;

    for (const MCRoomModel::MCPlayer& player : players)
    {
        shown.push_back(MCMatchPlayer{.Id = player.Id,
                                      .Name = player.Name,
                                      .Colour = MCChatView::ColourOf(player.Number),
                                      .Me = player.Me,
                                      .Ours = player.Team == team,
                                      .Ready = LightReady(*logistics, player.Id)});
    }

    return shown;
}

std::vector<MCScreenChange::MCPiece> MCCampaignMenu::Ahead()
{
    return MCRmlFlow::ReadAhead(TheScreen(), BriefingName);
}

void MCCampaignMenu::ComeBack()
{
    ComingBack = true;
}

namespace
{
    /// <summary>
    /// <see cref="MCCampaignMenu::Show"/>'s first part: the screen made if it isn't and set up for the mission now, on
    /// BRIEFING or the tab it comes back to. Port-only (stage 9): where it can't be read the game stops
    /// (MCRmlFlow::Broken): there is no other briefing to show.
    /// </summary>
    MCCampaignScreen& SetUpToShow()
    {
        const bool comingBack = ComingBack;
        const MCCampaignScreen::MCTab tab = comingBack ? LeftOn : MCCampaignScreen::MCTab::Briefing;
        ComingBack = false;
        MCCampaignScreen& made = MCRmlFlow::Needed(TheScreen(), BriefingName);

        // A mission's briefing afresh: PURCHASING's and the 'MECH BAY's INVENTORY at MECHS (the user's, 27 Nov: "Every
        // mission, it should start at mechs"); back from a save, as it was.
        if (!comingBack)
        {
            made.Logistics().StartAtMechs();
        }

        made.SetUp(tab);
        return made;
    }

    /// <summary>The rest: the screen set up laid out, then shown.</summary>
    void LayOut(MCCampaignScreen& screen)
    {
        screen.Layer().Context().Update();
    }
}

void MCCampaignMenu::Show()
{
    MCCampaignScreen& made = SetUpToShow();
    LayOut(made);
    made.Show();
}

std::vector<MCScreenChange::MCPiece> MCCampaignMenu::ShowPieces()
{
    // Whether the first piece set the screen up: the others lay out and show only a screen it set up. A piece each, as a
    // briefing's first set-up (its text read, 2.5 ms on the M4 Pro) and layout (4.6) would hold a dark frame together.
    const auto setUp = std::make_shared<bool>(false);
    const auto ifSetUp = [setUp](void (*then)(MCCampaignScreen&))
    {
        return [setUp, then]
        {
            if (const MCOptionalRef<MCCampaignScreen> screen = Screen(); *setUp && screen.has_value())
            {
                then(screen->get());
            }
        };
    };

    return {{.Part = "change.campaign screen",
             .Run =
                 [setUp]
             {
                 SetUpToShow();
                 *setUp = true;
             }},
            {.Part = "change.campaign layout", .Run = ifSetUp(LayOut)},
            {.Part = "change.campaign shown", .Run = ifSetUp([](MCCampaignScreen& screen) { screen.Show(); })}};
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
    return MCRmlFlow::Alive(TheScreen());
}

MCCampaignMenu::MCMissionRead MCCampaignMenu::ReadMission()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const std::string scenario = Scenario();
    // Port-only (SPEC-menus.md's stage 9): with no logistics before the load (-mission's game segment, the benchmark,
    // the staged battle), the briefing its mission file names, as logistics would have read it.
    const std::string path = logistics != nullptr ? logistics->BriefingTextPath
                                                  : MCLogistics::BriefingTextFile(scenario).value_or(std::string());
    const std::string text = path.empty() ? std::string() : MCSmuti::ReadFile(path);

    if (text.empty())
    {
        SDL_Log("Menus: the briefing's text %s can't be read", path.c_str());
    }

    const MCMissionNames::MCWritten written = MCMissionNames::FromBriefing(text);
    // The game's paths are written with backslashes (data\missions\brf0101.txt).
    const std::string briefingFile = path.substr(path.find_last_of("\\/") + 1);
    MCMissionRead read{.Title = MCMissionNames::Mission(briefingFile, written.Mission),
                       .Briefing = MCBriefingText::Read(text)};
    const bool multiplayer = MultiPlayer() != nullptr;

    // A match's side briefing: its starred title in sentence case (the app's names are the campaigns').
    if (multiplayer)
    {
        read.Title = MCRmlDialogs::AsSentence(written.Mission);
    }

    // A campaign's mission: its operation and number in it, as SAVE GAME names it (with no logistics, the game
    // segment's scenario: the campaign's mission is its scenario, as BackToLogistics keeps them).
    const std::shared_ptr<MCMission> game = Mission();

    if (!multiplayer && !Solo && (logistics != nullptr || game != nullptr))
    {
        const MCSaves::MCSave save =
            MCSaves::Unsaved(CurPlanet == 0 ? MCSaves::MCCampaign::Original : MCSaves::MCCampaign::Expansion,
                             logistics != nullptr ? logistics->CurrentMission : game->CurrentScenario, 0);

        if (save.Read)
        {
            read.OperationName = save.OperationName;
            read.Operation = save.Operation;
            read.Mission = save.Mission;
        }
    }
    else if (!multiplayer && !written.Operation.empty())
    {
        read.OperationName = MCMissionNames::Operation(written.Operation);
    }

    read.MapSource = scenario.empty() ? std::string() : std::string(MCRmlArt::MapScheme) + scenario;
    return read;
}

auto MCCampaignMenu::MapPointOf(float worldX, float worldY, float scale) -> SDL_FPoint
{
    // The original's sums, in its mix of float and double.
    constexpr float r = 0.70710677f;
    constexpr double AcrossMiddle = 490.0;
    constexpr double DownMiddle = 141.0;
    constexpr double DownMargin = 31.0;
    const float yR = worldY * r;
    const auto diff = static_cast<float>(yR - static_cast<double>(worldX) * r);
    const double across = (static_cast<double>(worldX) * r + yR) / scale;
    const auto down = static_cast<float>(diff / scale);
    return SDL_FPoint{static_cast<float>(across + AcrossMiddle), static_cast<float>(DownMiddle - down + DownMargin)};
}

MCCampaignMenu::MCZoneTag MCCampaignMenu::ZoneTag(int group, float x, float y, float width, float height)
{
    std::optional<MCZoneTag> best;
    float bestOutside = 0;

    // The first corner least outside the picture: one inside, or the bottom right's 1998 flag where none is.
    for (const MCTagShape& shape : ZoneTagShapes)
    {
        const MCZoneTag tag{.Group = group,
                            .Tail = shape.Tail,
                            .X = x,
                            .Y = y,
                            .Left = x - shape.TipX,
                            .Top = y - shape.TipY,
                            .Width = shape.Width,
                            .Height = shape.Height};
        const float outside = std::max(0.0f, -tag.Left) + std::max(0.0f, -tag.Top) +
                              std::max(0.0f, tag.Left + tag.Width - width) +
                              std::max(0.0f, tag.Top + tag.Height - height);

        if (!best.has_value() || outside < bestOutside)
        {
            best = tag;
            bestOutside = outside;
        }
    }

    return *best;
}

MCCampaignMenu::MCZoneMap MCCampaignMenu::ZoneTags()
{
    return MultiPlayer() != nullptr ? DropZones() : MCZoneMap{};
}

MCCampaignMenu::MCZoneMap MCCampaignMenu::DropZones()
{
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();

    if (logistics == nullptr)
    {
        return {};
    }

    // 1998's marks, worked out as logistics read the mission (MCLogistics::ZoneMarks): a match's team's first zone (the
    // Clans' the last three; a campaign's the first), the world units to a map pixel, the map picture's size.
    const MCLogistics::MCZoneMarks& marks = logistics->ZoneMarks;
    const bool match = MultiPlayer() != nullptr;
    const int32_t first = match ? marks.Zone : 0;

    if (first < 0 || !(marks.Scale > 0))
    {
        return {};
    }

    MCZoneMap map{.Width = static_cast<float>(marks.MapWidth), .Height = static_cast<float>(marks.MapHeight)};

    if (map.Width <= 0 || map.Height <= 0)
    {
        return {};
    }

    for (int group = 1; group <= ZoneGroups; ++group)
    {
        const auto zone = static_cast<size_t>(first + group - 1);

        if (zone >= logistics->DropZonePositions.size() || zone >= marks.Zones)
        {
            break;
        }

        // A campaign mission's zones are the groups it opens slots in (a zone a group); a match's team has all three.
        if (!match && !MCDeploy::Open((static_cast<size_t>(group) - 1) * MCDeploy::GroupSlots))
        {
            continue;
        }

        // Where 1998 marked it on its screen, in the picture it stretched over the map area.
        const MCLogistics::DropZonePosition& at = logistics->DropZonePositions[zone];
        const SDL_FPoint marked = MapPointOf(at.X, at.Y, marks.Scale);
        const float x = (marked.x - static_cast<float>(MapLeft)) / MapAreaWidth * map.Width;
        const float y = (marked.y - static_cast<float>(MapTop)) / MapAreaHeight * map.Height;
        map.Tags.push_back(ZoneTag(group, x, y, map.Width, map.Height));
    }

    return map;
}

std::string MCCampaignMenu::ZoneTagSource(const MCZoneTag& tag)
{
    return std::format(ZoneTagFormat, tag.Group, ShapeOf(tag.Tail).Corner);
}

std::string MCCampaignMenu::Pictured(std::string_view source)
{
    return source.empty() ? std::string(MCCampaignScreen::NoPicture)
                          : std::format("image({} contain center bottom)", source);
}

std::string MCCampaignMenu::Portrait(std::string_view source)
{
    return source.empty() ? std::string(MCCampaignScreen::NoPicture)
                          : std::format("image({} contain left top)", source);
}

std::string MCCampaignMenu::PilotSource(std::string_view picture)
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

std::optional<size_t> MCCampaignMenu::NumberAfter(std::string_view id, std::string_view prefix)
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

std::string MCCampaignMenu::TonsText(int32_t tons)
{
    return tons == 1 ? std::format("{} ton", tons) : std::format("{} tons", tons);
}

MCCampaignMenu::MCVariantName MCCampaignMenu::VariantName(std::string_view name)
{
    // The letter: one capital after the name's last space ("Commando A"; "Hunchback IIc J").
    constexpr size_t LetterWord = 2;
    const size_t space = name.rfind(' ');

    if (space == std::string_view::npos || space == 0 || name.size() - space != LetterWord ||
        std::isupper(static_cast<unsigned char>(name.back())) == 0)
    {
        return {.Chassis = std::string(name)};
    }

    return {.Chassis = std::string(name.substr(0, space)), .Letter = std::string(name.substr(space + 1))};
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
