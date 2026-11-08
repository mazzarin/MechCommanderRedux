#include "stdafx.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCMissionResultsScreen.h"
#include "lib/MCFatal.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogPort.h"
#include "logistics/MCLogRows.h"
#include "main/MCLogistics.h"
#include "color/MCPalette.h"
#include "gui/MCGuiButton.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiPort.h"
#include "gui/MCGuiScrollTextObject.h"
#include "gui/MCUpdateDisplay.h"
#include "iface/MCFriendlyMechIcon.h"
#include "iface/MCTacticalInterface.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGameStrings.h"
#include "mission/MCMission.h"
#include "mission/MCRematchLetters.h"
#include "mission/MCScenario.h"
#include "network/MCMultiPlayer.h"
#include "network/MCRoomEnd.h"
#include "vfx/MCVfxFunctions.h"
#include "object/MCForces.h"
#include "object/MCMechWarrior.h"
#include "object/MCMover.h"
#include "sound/MCSoundSystem.h"
#include "terrain/MCTacticalMap.h"
#include "vfx/MCVfxFunctions.h"

uint32_t ResultsStepTicks = 20;
int EventsToMissionResultsScreen = 0;

namespace
{
    /// <summary>Warrior status of a dead pilot.</summary>
    constexpr int32_t PilotDead = 4;
    /// <summary>Warrior status of a pilot whose icon shows the dead image.</summary>
    constexpr int32_t PilotMissing = 3;
    /// <summary>The kinds of kill a warrior counts.</summary>
    constexpr int32_t KillKinds = 7;
    /// <summary>GUI event: a mouse button went down.</summary>
    constexpr int32_t MouseDownEvent = 1;
    /// <summary>GUI event: a mouse button went up.</summary>
    constexpr int32_t MouseUpEvent = 4;
    /// <summary>GUI event: a key.</summary>
    constexpr int32_t KeyEvent = 10;
    /// <summary>GUI event: a timer fired.</summary>
    constexpr int32_t TimerEvent = 0x13;
    /// <summary>The scroll buttons' first repeat timer.</summary>
    constexpr int16_t ScrollDelayTimer = 4;
    /// <summary>The scroll buttons' fast repeat timer.</summary>
    constexpr int16_t ScrollRepeatTimer = 5;
    /// <summary>The multiplayer screen's timeout.</summary>
    constexpr int16_t TimeoutTimer = 10;

    /// <summary>The frame round a mech bar icon that a pilot line leaves out, in pixels each side.</summary>
    constexpr int32_t IconFrame = 2;

    /// <summary>The mech bar icon of <paramref name="warrior"/>'s mover, or null.</summary>
    std::shared_ptr<MCFriendlyMechIcon> IconOf(const MCMechWarrior& warrior)
    {
        const std::shared_ptr<MCGameObject> vehicle = LockObject(warrior.Vehicle);
        return vehicle != nullptr ? TacticalInterface()->GetMechIconFromID(vehicle->PartId) : nullptr;
    }

    /// <summary>
    /// <paramref name="warrior"/>'s icon for a pilot line, its model brought up to date, with the pilot's own portrait
    /// for a pilot whose icon shows the dead image. (The original redrew the icon's own picture then, the portrait
    /// swapped in, moved its pane's edges in by 2 and copied it onto the window.)
    /// </summary>
    /// <returns>The icon, or none when the pilot's mover has no icon.</returns>
    std::optional<MCResultsIcon> TakeIcon(MCMechWarrior& warrior)
    {
        const std::shared_ptr<MCFriendlyMechIcon> icon = IconOf(warrior);

        if (icon == nullptr)
        {
            return std::nullopt;
        }

        icon->UpdateModel();
        MCResultsIcon taken{.Warrior = warrior.weak_from_this()};

        if (warrior.Status == PilotMissing)
        {
            taken.Portrait = std::make_unique<MCGuiPort>();
            taken.Portrait->Init(std::string_view(warrior.Picture));
        }

        return taken;
    }

    /// <summary>
    /// Draws an icon from <see cref="TakeIcon"/> at (<paramref name="x"/>, <paramref name="y"/>) of
    /// <paramref name="target"/>, without its frame: the icon draws itself in place, through a view cut to its inside.
    /// </summary>
    void DrawPilotIcon(std::optional<MCResultsIcon>& taken, const MCPane& target, int32_t x, int32_t y)
    {
        const std::shared_ptr<MCMechWarrior> warrior = taken.has_value() ? taken->Warrior.lock() : nullptr;
        const std::shared_ptr<MCFriendlyMechIcon> icon = warrior != nullptr ? IconOf(*warrior) : nullptr;

        if (icon == nullptr)
        {
            return;
        }

        const int32_t width = icon->Port().Width();
        const int32_t height = icon->Port().Height();
        MCPane inside = target;
        inside.X0 = target.X0 + x;
        inside.Y0 = target.Y0 + y;
        inside.X1 = std::min(target.X1, inside.X0 + width - 2 * IconFrame - 1);
        inside.Y1 = std::min(target.Y1, inside.Y0 + height - 2 * IconFrame - 1);
        MCLogBlockPort view(inside, -IconFrame, -IconFrame, width, height, false);
        // The pilot's portrait lent to the icon for the draw.
        std::unique_ptr<MCGuiPort>& portrait = taken->Portrait;
        const bool lent = portrait != nullptr;

        if (lent)
        {
            std::swap(icon->PilotImage, portrait);
        }

        icon->DrawIcon(view);

        if (lent)
        {
            std::swap(icon->PilotImage, portrait);
        }
    }

    /// <summary>The left and top of single-player pilot line <paramref name="index"/>'s box.</summary>
    void PilotBox(int32_t index, int32_t& left, int32_t& top)
    {
        if (index < 6)
        {
            left = 0xe8;
            top = index * 0x42 + 0x2c;
        }
        else
        {
            left = 0x18a;
            top = index * 0x42 - 0x160;
        }
    }

    /// <summary>The length in pixels of a skill bar for <paramref name="skill"/> (55 across the skill range).</summary>
    int32_t SkillBarLength(int32_t skill)
    {
        const int32_t aboveMinimum = static_cast<int32_t>(skill - static_cast<double>(MinPilotSkill));
        return static_cast<int32_t>(static_cast<double>(aboveMinimum * 55) /
                                    (static_cast<double>(MaxPilotSkill) - MinPilotSkill));
    }

    /// <summary>The kills of <paramref name="warrior"/>, all kinds together.</summary>
    int32_t TotalKills(const MCMechWarrior& warrior)
    {
        int32_t kills = 0;

        for (int32_t kind = 0; kind < KillKinds; kind++)
        {
            kills += warrior.NumKilled[kind][1];
        }

        return kills;
    }

    /// <summary>Writes <paramref name="text"/> with <paramref name="font"/> at (<paramref name="x"/>, <paramref name="y"/>).</summary>
    void WriteText(MCGuiFont& font, MCPane& pane, int32_t x, int32_t y, std::string text, int32_t width = -1)
    {
        font.WriteString(pane, x, y, text.data(), width);
    }

    /// <summary>The move-on button's event routine: on release, closes the results screen.</summary>
    void MoveOnButtonHandleEvent(MCGuiObject&, MCGuiEvent& event)
    {
        // Port-only (Redux HD): a rematch under way closes them itself (network/MCRoomEnd.h).
        if (event.Type == MouseUpEvent && !MCRoomEnd::Rematching())
        {
            Mission()->CloseResultsScreen();
        }
    }

    /// <summary>Port-only (Redux HD): the move-on button's faces, which REMATCH is made from.</summary>
    constexpr std::string_view MoveOnUpFace = "mr_ms01.tga";
    constexpr std::string_view MoveOnDownFace = "mr_ms02.tga";
    /// <summary>The move-on button's place and size, and REMATCH's left of it.</summary>
    constexpr int16_t MoveOnX = 0x1bc;
    constexpr int16_t MoveOnY = 6;
    constexpr int16_t MoveOnWidth = 0x6b;
    constexpr int16_t MoveOnHeight = 0x12;
    constexpr int16_t ButtonGap = 6;
    constexpr int16_t RematchX = MoveOnX - MoveOnWidth - ButtonGap;
    /// <summary>
    /// The face's lettering lies within these columns and rows (inside its bevel); the face column left of it gives each
    /// row's colour (the face's shading and its two lines).
    /// </summary>
    constexpr int32_t FaceColumn = 3;
    constexpr int32_t LetteringRight = 0x67;
    constexpr int32_t LetteringTop = 2;
    constexpr int32_t LetteringBottom = 0xf;
    /// <summary>Port-only (Redux HD): the palette's grey ramp, black to white, the faces' and their words' greys.</summary>
    constexpr uint8_t GreyRampFirst = 0x10;
    constexpr uint8_t GreyRampLast = 0x1f;

    /// <summary>Port-only (Redux HD): how a face's word is inked: darker than the face (up) or lighter (held).</summary>
    enum class MCInk
    {
        Dark,
        Light
    };

    /// <summary>Port-only (Redux HD): a colour's red, green and blue.</summary>
    constexpr std::array<int64_t, 3> Channels(const SDL_Color& color)
    {
        return {color.r, color.g, color.b};
    }

    /// <summary>Port-only (Redux HD): how bright a grey is (its channels' sum).</summary>
    constexpr int32_t Brightness(const SDL_Color& color)
    {
        return color.r + color.g + color.b;
    }

    /// <summary>
    /// Port-only (Redux HD): of <paramref name="face"/>, <paramref name="ink"/> and the grey ramp, the entry of
    /// <paramref name="colors"/> nearest <paramref name="face"/>'s colour blended with <paramref name="ink"/>'s by
    /// <paramref name="coverage"/> (of MCRematchLetters::Full); the first of equals. In whole numbers, so every machine
    /// picks the same.
    /// </summary>
    uint8_t NearestBlend(const MCTgaColors& colors, uint8_t face, uint8_t ink, int32_t coverage)
    {
        const std::array<int64_t, 3> from = Channels(colors[face]);
        const std::array<int64_t, 3> to = Channels(colors[ink]);
        std::array<int64_t, 3> target{};

        for (size_t channel = 0; channel < target.size(); channel++)
        {
            target[channel] = from[channel] * (MCRematchLetters::Full - coverage) + to[channel] * coverage;
        }

        uint8_t nearest = face;
        int64_t distance = std::numeric_limits<int64_t>::max();
        const auto weigh = [&](uint8_t candidate)
        {
            const std::array<int64_t, 3> channels = Channels(colors[candidate]);
            int64_t away = 0;

            for (size_t channel = 0; channel < target.size(); channel++)
            {
                const int64_t difference = channels[channel] * MCRematchLetters::Full - target[channel];
                away += difference * difference;
            }

            if (away < distance)
            {
                distance = away;
                nearest = candidate;
            }
        };

        weigh(face);
        weigh(ink);

        for (int32_t grey = GreyRampFirst; grey <= GreyRampLast; grey++)
        {
            weigh(static_cast<uint8_t>(grey));
        }

        return nearest;
    }

    /// <summary>
    /// Port-only (Redux HD): REMATCH's face made from the move-on button's <paramref name="file"/>, its word inked as
    /// <paramref name="ink"/> says (see RematchFaces); null when the face can't be read.
    /// </summary>
    std::shared_ptr<MCGuiPort> RematchFace(std::string_view file, MCInk ink)
    {
        MCTgaColors colors{};
        auto face = std::make_shared<MCGuiPort>();

        if (face->Init(file, colors) != 0 || face->Width() <= LetteringRight || face->Height() <= LetteringBottom ||
            face->Width() < MCRematchLetters::Left + MCRematchLetters::Width ||
            face->Height() < MCRematchLetters::Top + MCRematchLetters::Rows)
        {
            SDL_Log("Results: REMATCH has no face: %.*s didn't load as NEXT MISSION's (%dx%d)",
                    static_cast<int>(file.size()), file.data(), face->Width(), face->Height());
            return nullptr;
        }

        const int32_t width = face->Width();
        std::vector<uint8_t> pixels(face->Pixels().begin(), face->Pixels().end());
        const auto at = [&](int32_t x, int32_t y) -> uint8_t&
        { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)]; };

        // The art's ink (its word's darkest pixel on the up face, its brightest on the held one), and the word wiped:
        // each row the face column's colour.
        uint8_t inkColor = at(FaceColumn, LetteringTop);

        for (int32_t y = LetteringTop; y <= LetteringBottom; y++)
        {
            const uint8_t faceColor = at(FaceColumn, y);

            for (int32_t x = FaceColumn + 1; x <= LetteringRight; x++)
            {
                const int32_t brightness = Brightness(colors[at(x, y)]);
                const int32_t inkBrightness = Brightness(colors[inkColor]);

                if (ink == MCInk::Dark ? brightness < inkBrightness : brightness > inkBrightness)
                {
                    inkColor = at(x, y);
                }

                at(x, y) = faceColor;
            }
        }

        // REMATCH laid on.
        for (int32_t row = 0; row < MCRematchLetters::Rows; row++)
        {
            for (int32_t column = 0; column < MCRematchLetters::Width; column++)
            {
                const int32_t coverage =
                    MCRematchLetters::Coverage[static_cast<size_t>(row)][static_cast<size_t>(column)];

                if (coverage != 0)
                {
                    uint8_t& pixel = at(MCRematchLetters::Left + column, MCRematchLetters::Top + row);
                    pixel = NearestBlend(colors, pixel, inkColor, coverage);
                }
            }
        }

        const std::shared_ptr<MCTexture> texture = face->Bitmap().Texture.lock();

        if (texture == nullptr)
        {
            SDL_Log("Results: REMATCH has no face: %.*s's picture has no texture", static_cast<int>(file.size()),
                    file.data());
            return nullptr;
        }

        std::ranges::copy(pixels, MCRenderer::LockTexture(*texture).begin());
        MCRenderer::UnlockTexture(*texture);
        return face;
    }

    /// <summary>Port-only (Redux HD): REMATCH's event routine: on release, the rematch begins, and the button goes.</summary>
    void RematchButtonHandleEvent(MCGuiObject& object, MCGuiEvent& event)
    {
        if (event.Type == MouseUpEvent)
        {
            object.ShowGuiWindow(false);
            MCRoomEnd::Rematch();
        }
    }

    /// <summary>The pilot switch button's event routine: shows the multiplayer pilot list for the button's state.</summary>
    void PilotSwitchHandleEvent(MCGuiObject& object, MCGuiEvent& event)
    {
        if (event.Type == MouseDownEvent)
        {
            Mission()->ResultsScreen->ShowMPPilots(static_cast<MCGuiToolButton&>(object).Pushed == 0);
        }
    }
}

MCMissionResultsScreen::~MCMissionResultsScreen()
{
    Destroy();
}

auto MCMissionResultsScreen::Init() -> int32_t
{
    int32_t result = MCGuiObject::Init(0x28, 0xf, 0x230, 0x1bc, {});
    Assert(result == 0, static_cast<uint32_t>(result), " error initializing mission results screen display elements ");
    // (The original loaded the art into the window's own picture.)
    result = SetBackground(const_cast<char*>(MultiPlayer() == nullptr ? "mr_bkgd.tga" : "mrm_bkgd.tga"));
    Assert(result == 0, static_cast<uint32_t>(result), " error initializing mission results screen display elements ");

    // The original drops this init's result.
    _MoveOnPort = std::make_unique<MCGuiPort>();
    _MoveOnPort->Init(const_cast<char*>("mr_ms00.tga"));

    _MoveOnButton = CreateChild<MCGuiButton>(
        [](MCGuiButton& button)
        {
            button.Init(MoveOnX, MoveOnY, MoveOnWidth, MoveOnHeight, {});
            button.SetUpPicture(MoveOnUpFace);
            button.SetDownPicture(MoveOnDownFace);
        });
    _MoveOnButton->SetEventRoutine(MoveOnButtonHandleEvent);
    _MoveOnButton->SetTransparent(true);

    // Port-only (Redux HD): the host's REMATCH, shown when the results come (network/MCRoomEnd.h).
    if (MultiPlayer() != nullptr)
    {
        _RematchButton = AddRematchButton(*this, RematchX, MoveOnY);
    }

    if (MultiPlayer() != nullptr)
    {
        _PilotSwitchButton = CreateChild<MCGuiToolButton>(
            [](MCGuiToolButton& button)
            {
                button.Init(0xe4, 0x1e, 0x148, 0xb, {});
                button.SetUpPicture(const_cast<char*>("mrm_bkgd00.tga"));
                button.SetDownPicture(const_cast<char*>("mrm_bkgd01.tga"));
                button.Framed = false;
                button.Draw();
                button.Draw();
            });
        _PilotSwitchButton->SetEventRoutine(PilotSwitchHandleEvent);
    }

    _SuccessPort = std::make_unique<MCGuiPort>();
    _FailurePort = std::make_unique<MCGuiPort>();
    result = _SuccessPort->Init(const_cast<char*>("guimr08.tga"));
    Assert(result == 0, static_cast<uint32_t>(result), " error initializing mission results screen display elements ");
    result = _FailurePort->Init(const_cast<char*>("guimr07.tga"));
    Assert(result == 0, static_cast<uint32_t>(result), " error initializing mission results screen display elements ");
    _NextDrawTime = 0;

    if (MultiPlayer() == nullptr)
    {
        // (The original loaded the pictures into the objects' own ports.)
        const auto makeScrollButton = [this](std::string picture, int32_t x, int32_t y)
        {
            MCGuiHandle<MCGuiObject> button = CreateChild<MCGuiObject>(
                [&picture](MCGuiObject& made)
                {
                    made.SetDrawsLive();
                    made.Init(0, 0, 0xb, 0xb, {});
                    made.SetBackground(picture.data());
                    made.ShowGuiWindow(false);
                });
            button->MoveTo(x, y, false);
            return button;
        };

        _ScrollUpButton = makeScrollButton("mfddbg04.tga", 0xcf, 0x15b);
        _ScrollDownButton = makeScrollButton("mfddbg05.tga", 0xcf, 0x1a7);
        _ScrollUpRect = {0xcf, 0x15b, 0xda, 0x166};
        _ScrollDownRect = {0xcf, 0x1a7, 0xda, 0x1b2};
        _ScrollBarRect = {0xcf, 0x169, 0xda, 0x1a4};
    }

    return result;
}

auto MCMissionResultsScreen::OnDestroy() -> void
{
    EventsToMissionResultsScreen = 0;
    _Results = {};
    _SuccessPort.reset();
    _FailurePort.reset();
    _MoveOnPort.reset();
    _BestPilotIcon.reset();
    _PilotIcons.clear();
    _CommanderLines.clear();
    MCGuiObject::OnDestroy();
}

auto MCMissionResultsScreen::MouseWheel(int32_t steps, int32_t xPos, int32_t yPos) -> bool
{
    const std::shared_ptr<MCGuiScrollTextObject> text = _TextObject.Lock();

    if (MultiPlayer() != nullptr || text == nullptr)
    {
        return false;
    }

    return text->MouseWheel(steps, xPos, yPos);
}

auto MCMissionResultsScreen::HandleEvent(MCGuiEvent& event) -> void
{
    MCGuiObject::HandleEvent(event);

    switch (event.Type)
    {
        case MouseDownEvent:
        {
            if (MultiPlayer() != nullptr)
            {
                break;
            }

            const POINT point{event.X - GlobalX(), event.Y - GlobalY()};

            if (PtInRect(_ScrollUpRect, point))
            {
                GuiSystem().Grab(weak_from_this());
                _ScrollUpButton->ShowGuiWindow(true);
                GuiSystem().AddTimer(weak_from_this(), ScrollDelayTimer, TacticalInterface()->ScrollStart, 0, 0, false);
                _TextObject.Lock()->ReceiveClick(-1, 0);
                return;
            }

            if (PtInRect(_ScrollDownRect, point))
            {
                GuiSystem().Grab(weak_from_this());
                _ScrollDownButton->ShowGuiWindow(true);
                GuiSystem().AddTimer(weak_from_this(), ScrollDelayTimer, TacticalInterface()->ScrollStart, 0, 0, false);
                _TextObject.Lock()->ReceiveClick(1, 0);
                return;
            }

            if (PtInRect(_ScrollBarRect, point))
            {
                // Original behaviour (OB-057): the line is picked from the cursor's screen y, not its window y, so the
                // click lands 15 pixels (the window's y) further down the text.
                _TextObject.Lock()->ReceiveClick(0, event.Y - _ScrollBarRect.top);
                return;
            }
            break;
        }
        case MouseUpEvent:
        {
            GuiSystem().RemoveTimer(*this, ScrollDelayTimer);
            GuiSystem().RemoveTimer(*this, ScrollRepeatTimer);
            GuiSystem().Release();

            if (_ScrollUpButton != nullptr)
            {
                _ScrollUpButton->ShowGuiWindow(false);
            }

            if (_ScrollDownButton != nullptr)
            {
                _ScrollDownButton->ShowGuiWindow(false);
            }
            break;
        }
        case KeyEvent:
        {
            if (event.Key == 0x1b)
            {
                _SkipAnimation = true;
                return;
            }
            break;
        }
        case TimerEvent:
        {
            const int32_t timerId = event.Data;

            if (timerId == ScrollDelayTimer)
            {
                // The first repeat delay has passed: repeat five times as fast.
                GuiSystem().RemoveTimer(*this, ScrollDelayTimer);
                GuiSystem().AddTimer(weak_from_this(), ScrollRepeatTimer, TacticalInterface()->ScrollStart / 5, 0, 0,
                                     false);
            }
            else if (timerId != ScrollRepeatTimer)
            {
                if (timerId == TimeoutTimer)
                {
                    // The multiplayer timeout: close the screen (this object is deleted here).
                    Mission()->CloseResultsScreen();
                }

                return;
            }

            const POINT point{event.X - GlobalX(), event.Y - GlobalY()};

            if (PtInRect(_ScrollUpRect, point))
            {
                _TextObject.Lock()->ReceiveClick(-1, 0);
                return;
            }

            if (PtInRect(_ScrollDownRect, point))
            {
                _TextObject.Lock()->ReceiveClick(1, 0);
                return;
            }
            break;
        }

        default:
            break;
    }
}

auto MCMissionResultsScreen::Display() -> void
{
    // Port: the screen is on the screen window, hidden, from the scenario's start (the original added it when it
    // showed it), so it draws nothing until shown.
    if (!ShowWindow)
    {
        return;
    }

    const std::optional<MCColorTable> hazePalette = GamePalette().GetHazePalette(-7);
    MCScreenVertex vertices[4] = {};
    vertices[1].X = GuiSystem().Width() - 1;
    vertices[2].X = GuiSystem().Width() - 1;
    vertices[2].Y = GuiSystem().Height() - 1;
    vertices[3].Y = GuiSystem().Height() - 1;
    VfxTranslatePolygon(ScreenPort()->Frame(), std::span(vertices, 4), *hazePalette);

    if (MultiPlayer() == nullptr)
    {
        while (_NextDrawTime != 0 && (_NextDrawTime <= MouseTicks || _SkipAnimation))
        {
            switch (_Step)
            {
                case MCResultsStep::ResourcePoints:
                {
                    if (ScenarioResult < 4 || Solo != 0)
                    {
                        _Step = MCResultsStep::Statistics;
                    }
                    else
                    {
                        StepResourcePoints();
                    }
                    break;
                }
                case MCResultsStep::Statistics:
                    StepStatistics();
                    break;
                case MCResultsStep::PrimaryObjectives:
                case MCResultsStep::SecondaryObjectives:
                    StepObjectives();
                    break;
                case MCResultsStep::Pilots:
                    StepPilots();
                    break;
                case MCResultsStep::Debriefing:
                    StepDebriefing();
                    break;

                default:
                    break;
            }
        }
    }

    // (The original copied the window's picture to the screen and displayed the children.)
    if (DisplayPort != nullptr)
    {
        DrawInFramePass(*DisplayPort);
    }
}

auto MCMissionResultsScreen::Draw() -> void
{
    MCGuiObject::Draw();

    if (_MoveOnPort != nullptr)
    {
        _MoveOnPort->CopyTo(Port().Frame(), 4, 4, true);
    }

    if (MultiPlayer() == nullptr)
    {
        DrawResourcePoints();
        DrawStatistics();
        DrawObjectiveList();
        // The pilots the steps have reached.
        const auto lines = static_cast<int32_t>(_Results.Pilots.size());
        const int32_t pilots =
            _Step == MCResultsStep::Pilots ? _StepIndex : (_Step > MCResultsStep::Pilots ? lines : 0);

        for (int32_t i = 0; i < pilots; i++)
        {
            DrawPilot(i);
        }
    }
    else
    {
        DrawMPSummary();
        DrawMPPilotList();
        DrawMPObjectives();
    }
}

auto MCMissionResultsScreen::AdvanceStep() -> void
{
    _StepIndex = 0;
    _Step = static_cast<MCResultsStep>(std::to_underlying(_Step) + 1);
}

auto MCMissionResultsScreen::StepResourcePoints() -> void
{
    const int32_t shown = _StepIndex * 1000;

    if (_Results.ResourcePointsEarned < shown)
    {
        _ShownResourcePoints = _Results.ResourcePointsEarned;
        AdvanceStep();
        _NextDrawTime = ResultsStepTicks + MouseTicks;

        if (!_SkipAnimation)
        {
            SoundSystem()->PlayInterfaceSample(0x43);
        }
    }
    else
    {
        _ShownResourcePoints = shown;
        _NextDrawTime = ResultsStepTicks / 20 + MouseTicks;

        if (!_SkipAnimation)
        {
            SoundSystem()->PlayInterfaceSample(0x42);
        }

        _StepIndex++;
    }
}

auto MCMissionResultsScreen::StepStatistics() -> void
{
    _NextDrawTime = ResultsStepTicks / 2 + MouseTicks;

    if (_StepIndex < 4)
    {
        _StepIndex++;
    }
    else if (_StepIndex == 4)
    {
        AdvanceStep();
        _NextDrawTime = ResultsStepTicks + MouseTicks;
    }

    if (!_SkipAnimation)
    {
        SoundSystem()->PlayInterfaceSample(0x47);
    }
}

auto MCMissionResultsScreen::ShowsTonnageBonus() -> bool
{
    std::shared_ptr<const MCScenario> scenario = Scenario();
    const MCScenarioObjective& bonus = scenario->Objectives[scenario->Objectives.Count()];
    return MultiPlayer() == nullptr && bonus.Type == MCScenarioObjective::TonnageBonus && bonus.Points > 0 &&
           ScenarioResult > 3 && Solo == 0;
}

auto MCMissionResultsScreen::StepObjectives() -> void
{
    // The primary step lists the type 0 objectives, the secondary step the type 1 ones.
    const uint32_t wantedType =
        _Step != MCResultsStep::PrimaryObjectives ? MCScenarioObjective::Secondary : MCScenarioObjective::Primary;
    const MCObjectiveList& objectives = Scenario()->Objectives;
    bool drew = objectives[_StepIndex].Type == wantedType;

    if (_StepIndex == objectives.Count())
    {
        // After the secondary objectives: the tonnage bonus, when it was earned.
        if (_Step == MCResultsStep::SecondaryObjectives && ShowsTonnageBonus())
        {
            SoundSystem()->PlayBettySample(10);
            drew = true;
        }

        AdvanceStep();
    }
    else
    {
        _StepIndex++;
    }

    if (drew && MultiPlayer() == nullptr)
    {
        if (!_SkipAnimation)
        {
            SoundSystem()->PlayInterfaceSample(0x47);
        }

        _NextDrawTime = ResultsStepTicks + MouseTicks;
    }
}

auto MCMissionResultsScreen::StepPilots() -> void
{
    const int32_t index = _StepIndex;
    _NextDrawTime = ResultsStepTicks + MouseTicks;

    if (index == static_cast<int32_t>(_Results.Pilots.size()))
    {
        AdvanceStep();
        return;
    }

    if (const std::shared_ptr<MCMechWarrior> warrior = _Results.Pilots[static_cast<size_t>(index)].Warrior.lock();
        warrior != nullptr)
    {
        _PilotIcons[static_cast<size_t>(index)] = TakeIcon(*warrior);
        _StepIndex++;
    }

    if (!_SkipAnimation)
    {
        SoundSystem()->PlayInterfaceSample(0x10);
    }
}

auto MCMissionResultsScreen::StepDebriefing() -> void
{
    const std::shared_ptr<MCGuiScrollTextObject> text = _TextObject.Lock();
    text->ShowGuiWindow(true);

    if (text->TextBuffer.empty())
    {
        std::string line = LoadGameString(0x361, 0xfe);
        text->FontIndex = 1;
        text->Print(line.data(), 0x1f);
    }

    text->Draw();
    _NextDrawTime = 0;
    _Step = MCResultsStep::Done;

    if (!_SkipAnimation)
    {
        SoundSystem()->PlayInterfaceSample(0x32);
    }
}

auto MCMissionResultsScreen::DrawResourcePoints() -> void
{
    if (_ShownResourcePoints < 0)
    {
        return;
    }

    FillBox(0x149, 9, 0x185, 0x14, 0x10);
    WriteText(*LgWhiteFont, Port().Frame(), 0x14a, 10, std::to_string(_ShownResourcePoints));
}

auto MCMissionResultsScreen::DrawStatistics() -> void
{
    static constexpr std::array<int32_t, 5> statisticY = {0x33, 0x40, 0x4d, 0x5a, 0x67};
    const std::array<int32_t, 5> values = _Results.Statistics.Values();
    // The statistics the steps have reached: each step shows one.
    const int32_t shown = _Step == MCResultsStep::Statistics ? _StepIndex : (_Step > MCResultsStep::Statistics ? 5 : 0);

    for (int32_t i = 0; i < shown; i++)
    {
        WriteText(*LgWhiteFont, Port().Frame(), 0xcc, statisticY[static_cast<size_t>(i)],
                  std::to_string(values[static_cast<size_t>(i)]));
    }
}

auto MCMissionResultsScreen::DrawObjectiveList() -> void
{
    const std::string pointsName = LoadGameString(0x363, 0xfe);
    const std::string bonusHeader = LoadGameString(0x360, 0xfe);
    const std::string secondaryHeader = LoadGameString(0x362, 0xfe);
    const MCObjectiveList& objectives = Scenario()->Objectives;
    int32_t y = 0x92;
    bool headerDrawn = false;

    // Each step (primary or secondary, objective 0 .. count) the steps have passed, laid out as the original drew them.
    for (const MCResultsStep step : {MCResultsStep::PrimaryObjectives, MCResultsStep::SecondaryObjectives})
    {
        const uint32_t wantedType =
            step != MCResultsStep::PrimaryObjectives ? MCScenarioObjective::Secondary : MCScenarioObjective::Primary;

        for (int32_t index = 0; index <= objectives.Count(); index++)
        {
            if (step > _Step || (step == _Step && index >= _StepIndex))
            {
                return;
            }

            const MCScenarioObjective& objective = objectives[index];

            if (objective.Type == wantedType)
            {
                if (wantedType == MCScenarioObjective::Secondary && !headerDrawn)
                {
                    WriteText(*MedBlueFont, Port().Frame(), 0xf, y, secondaryHeader);
                    headerDrawn = true;
                    y += 0xe;
                }

                std::shared_ptr<MCGuiFont> font = GreyFont;
                bool listed = true;

                if (objective.Status == MCScenarioObjective::Succeeded)
                {
                    _SuccessPort->CopyTo(Port().Frame(), 0xb, y - 1, false);
                    font = GreenFont;
                }
                else if (objective.Status == MCScenarioObjective::Failed)
                {
                    _FailurePort->CopyTo(Port().Frame(), 0xb, y - 1, false);
                    font = RedFont;
                }
                else if (objective.Status != MCScenarioObjective::Pending)
                {
                    listed = false;
                }

                if (listed && font != nullptr)
                {
                    WriteText(*font, Port().Frame(), 0x17, y, objective.Name);
                    const int32_t nameY = y;
                    y = nameY + 10;

                    if (MultiPlayer() == nullptr && objective.Points != 0)
                    {
                        WriteText(*font, Port().Frame(), 0x1d, nameY + 10,
                                  std::format("{} {}", objective.Points, pointsName));
                        y += 10;
                    }
                    else
                    {
                        y = nameY + 0xd;
                    }
                }
            }

            if (index == objectives.Count() && step == MCResultsStep::SecondaryObjectives && ShowsTonnageBonus())
            {
                const MCScenarioObjective& bonus = objectives[objectives.Count()];
                WriteText(*BlueFont, Port().Frame(), 0xf, y, bonusHeader);
                const int32_t markY = y + 10;
                y += 0xb;
                _SuccessPort->CopyTo(Port().Frame(), 0xb, markY, false);
                WriteText(*GreenFont, Port().Frame(), 0x17, y, bonus.Name);
                const int32_t nameY = y;
                y = nameY + 10;
                WriteText(*GreenFont, Port().Frame(), 0x1d, nameY + 10, std::format("{} {}", bonus.Points, pointsName));
                y += 0xb;
            }
        }
    }
}

auto MCMissionResultsScreen::DrawPilot(int32_t index) -> void
{
    const MCMissionPilotResult& result = _Results.Pilots[static_cast<size_t>(index)];
    const std::shared_ptr<MCMechWarrior> warrior = result.Warrior.lock();

    if (warrior == nullptr)
    {
        return;
    }

    const auto line = [this](int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
    { VfxLineDraw(Port().Frame(), x0, y0, x1, y1, color); };
    const auto pixel = [this](int32_t x, int32_t y) { AGPixelWrite(Port().Frame(), x, y, 0x10); };

    int32_t left;
    int32_t top;
    PilotBox(index, left, top);
    uint32_t stringId;

    if (warrior->Status == PilotDead)
    {
        stringId = 0x356;
    }
    else if (warrior->Wounds <= 4.0f)
    {
        stringId = 0x358;
    }
    else
    {
        stringId = 0x357;
    }

    std::string text = LoadGameString(stringId, 0xfe);
    const int32_t textY = top + 3;
    WriteText(*WhiteFont, Port().Frame(), left + 3, textY, text);

    // The rank; a rank outside 0..3 leaves the status text in place.
    const int32_t rank = static_cast<int8_t>(warrior->Rank);

    if (rank >= 0 && rank <= 3)
    {
        text = LoadGameString(0x70 + static_cast<uint32_t>(rank), 0xfe);
    }

    std::shared_ptr<MCGuiFont> rankFont = result.OldRank < rank ? YellowFont : WhiteFont;
    WriteText(*rankFont, Port().Frame(), left + 0x39, textY, text);
    WriteText(*WhiteFont, Port().Frame(), left + 0x8c, textY, std::to_string(TotalKills(*warrior)));

    // A bar per skill: the old value, and the gain in another colour.
    int32_t barY = top + 0x18;

    for (const int32_t skill : ResultsSkillOrder)
    {
        const int32_t oldLength = SkillBarLength(result.Skills[static_cast<size_t>(skill)]);
        const int32_t newLength = SkillBarLength(static_cast<int32_t>(warrior->SkillRank[skill]));
        const int32_t barLeft = left + 0x62;
        line(barLeft, barY - 1, barLeft, barY, 0xe3);

        if (oldLength == newLength)
        {
            const int32_t end = oldLength + barLeft;
            const int32_t fillEnd = end - 2;
            line(left + 99, barY - 2, fillEnd, barY - 2, 0xe3);
            pixel(end - 1, barY - 2);
            line(end, barY - 2, end, barY + 1, 0x10);
            pixel(end - 1, barY + 1);
            line(end - 1, barY - 1, end - 1, barY, 0xe3);
            line(left + 99, barY + 1, fillEnd, barY + 1, 0xe5);
            line(left + 99, barY - 1, fillEnd, barY - 1, 0xe4);
            line(left + 99, barY, fillEnd, barY, 0xe4);
        }
        else
        {
            const int32_t oldEnd = oldLength + barLeft;
            line(left + 99, barY - 2, oldEnd, barY - 2, 0xe3);
            line(left + 99, barY - 1, oldEnd, barY - 1, 0xe4);
            line(left + 99, barY, oldEnd, barY, 0xe4);
            line(left + 99, barY + 1, oldEnd, barY + 1, 0xe5);
            const int32_t end = barLeft + newLength;
            const int32_t gainStart = oldEnd + 1;
            line(gainStart, barY - 2, end - 2, barY - 2, 0xa3);
            line(gainStart, barY - 1, end - 2, barY - 1, 0xf2);
            line(gainStart, barY, end - 2, barY, 0xf2);
            line(gainStart, barY + 1, end - 2, barY + 1, 0xf1);
            pixel(end - 1, barY - 2);
            line(end, barY - 2, end, barY + 1, 0x10);
            pixel(end - 1, barY + 1);
            line(end - 1, barY - 2, end - 1, barY + 1, 0xf1);
        }

        barY += 9;
    }

    DrawPilotIcon(_PilotIcons[static_cast<size_t>(index)], Port().Frame(), left + 4, top + 0x10);
}

auto MCMissionResultsScreen::DrawMPPilotList() -> void
{
    const auto line = [this](int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t color)
    { VfxLineDraw(Port().Frame(), x0, y0, x1, y1, color); };
    const auto pixel = [this](int32_t x, int32_t y) { AGPixelWrite(Port().Frame(), x, y, 0x10); };

    // Clear the twelve pilot boxes (six per column) and their empty skill bars.
    for (int32_t row = -0x160; row <= 0x1b7; row += 0x42)
    {
        const int32_t left = row < 0x2c ? 0xe8 : 0x18a;
        const int32_t top = row < 0x2c ? row + 0x18c : row;
        const auto boxLeft = static_cast<int16_t>(left);
        const auto boxTop = static_cast<int16_t>(top);
        FillBox(boxLeft + 1, boxTop + 1, boxLeft + 0x70, boxTop + 10, 0x12);
        FillBox(boxLeft + 0x8c, boxTop + 1, boxLeft + 0x9a, boxTop + 10, 0x12);
        FillBox(boxLeft + 4, boxTop + 0x10, boxLeft + 0x33, boxTop + 0x39, 0x10);
        int32_t barY = top + 0x18;

        for (int32_t bar = 0; bar < 4; bar++)
        {
            line(left + 99, barY - 2, left + 0x97, barY - 2, 0x32);
            line(left + 99, barY - 1, left + 0x97, barY - 1, 0x12);
            line(left + 99, barY, left + 0x97, barY, 0x12);
            line(left + 99, barY + 1, left + 0x97, barY + 1, 0x14);
            line(left + 0x62, barY - 1, left + 0x62, barY, 0x32);
            line(left + 0x98, barY - 1, left + 0x98, barY, 0x14);
            barY += 9;
        }
    }

    int32_t rowBase = -0x160;

    for (size_t i = 0; i < _Results.Pilots.size(); i++)
    {
        const std::shared_ptr<MCMechWarrior> warrior = _Results.Pilots[i].Warrior.lock();

        if (warrior == nullptr || (warrior->Alignment == HomeTeam()->Alignment) != _MpShowHomeSide)
        {
            continue;
        }

        const int32_t left = rowBase < 0x2c ? 0xe8 : 0x18a;
        const int32_t top = rowBase < 0x2c ? rowBase + 0x18c : rowBase;
        const auto mover = std::static_pointer_cast<MCMover>(LockObject(warrior->Vehicle));
        WriteText(*WhiteFont, Port().Frame(), left + 3, top + 2, mover->NetName.data());
        WriteText(*WhiteFont, Port().Frame(), left + 0x8c, top + 3, std::to_string(TotalKills(*warrior)));
        int32_t barY = top + 0x17;

        for (const int32_t skill : ResultsSkillOrder)
        {
            const int32_t length = SkillBarLength(static_cast<int32_t>(warrior->SkillRank[skill]));
            line(left + 0x62, barY, left + 0x62, barY + 1, 0xe3);
            const int32_t end = left + 0x62 + length;
            const int32_t fillEnd = end - 2;
            line(left + 99, barY - 1, fillEnd, barY - 1, 0xe3);
            pixel(end - 1, barY - 1);
            line(end, barY - 1, end, barY + 2, 0x10);
            pixel(end - 1, barY + 2);
            line(end - 1, barY, end - 1, barY + 1, 0xe3);
            line(left + 99, barY + 2, fillEnd, barY + 2, 0xe5);
            line(left + 99, barY, fillEnd, barY, 0xe4);
            line(left + 99, barY + 1, fillEnd, barY + 1, 0xe4);
            barY += 9;
        }

        DrawPilotIcon(_PilotIcons[i], Port().Frame(), left + 4, top + 0x10);
        rowBase += 0x42;
    }
}

auto MCMissionResultsScreen::DrawMPSummary() -> void
{
    // Port fix: MCX.EXE reads the best pilot without checking there is one.
    if (!_Results.Pilots.empty())
    {
        // The best pilot.
        const std::shared_ptr<MCMechWarrior> best = _Results.Pilots[0].Warrior.lock();
        const auto bestMover = std::static_pointer_cast<MCMover>(LockObject(best->Vehicle));
        WriteText(*MedWhiteFont, Port().Frame(), 0x70, 0x40, bestMover->NetName.data(), 0x68);
        WriteText(*MedWhiteFont, Port().Frame(), 0x70, 0x4d,
                  LoadGameString(best->Alignment == HomeTeam()->Alignment ? 0xb6 : 0xb7, 0xfe));
        WriteText(*MedWhiteFont, Port().Frame(), 0xca, 0x4d, std::to_string(_Results.Pilots[0].OldRank));
        DrawPilotIcon(_BestPilotIcon, Port().Frame(), 0xb, 0x2f);
    }

    static constexpr std::array<int32_t, 5> statisticY = {100, 0x70, 0x7c, 0x88, 0x94};
    const std::array<int32_t, 5> statistics = _Results.Statistics.Values();

    for (size_t i = 0; i < statistics.size(); i++)
    {
        WriteText(*MedWhiteFont, Port().Frame(), 0xcc, statisticY[i], std::to_string(statistics[i]));
    }

    WriteText(*MedWhiteFont, Port().Frame(), 0xb8, 0xa0, _TimeText);

    // The commanders by kills.
    int32_t row = 0;

    for (const CommanderLine& commander : _CommanderLines)
    {
        const int32_t rowY = static_cast<int16_t>(row) * 0xc + 0xbe;
        WriteText(*MedBlueFont, Port().Frame(), 0x1a, rowY, std::format("{}.", commander.Place));
        WriteText(*MedWhiteFont, Port().Frame(), 0x28, rowY, commander.Name, 0x68);
        WriteText(*MedWhiteFont, Port().Frame(), 0xcc, rowY, std::to_string(commander.Score));
        row++;
    }
}

auto MCMissionResultsScreen::DrawMPObjectives() -> void
{
    // Only the primary objectives of the home team are listed (the original's loop over the types stops after the
    // first, so its secondary header is never drawn).
    const MCObjectiveList& objectives = Scenario()->Objectives;
    int32_t y = 0x11d;
    const int32_t first = HomeTeam()->FirstObjective;
    const int32_t end = first + static_cast<int32_t>(HomeTeam()->NumObjectives);

    for (int32_t i = first; i < end; i++)
    {
        const MCScenarioObjective& objective = objectives[i];

        if (objective.Type != MCScenarioObjective::Primary)
        {
            continue;
        }

        std::shared_ptr<MCGuiFont> font = GreyFont;

        if (objective.Status == MCScenarioObjective::Succeeded)
        {
            _SuccessPort->CopyTo(Port().Frame(), 0xb, y - 1, false);
            font = GreenFont;
        }
        else if (objective.Status == MCScenarioObjective::Failed)
        {
            _FailurePort->CopyTo(Port().Frame(), 0xb, y - 1, false);
            font = RedFont;
        }
        else if (objective.Status != MCScenarioObjective::Pending)
        {
            continue;
        }

        if (font != nullptr)
        {
            WriteText(*font, Port().Frame(), 0x17, y, objective.Name);
            y += 0xd;
        }
    }
}

auto MCMissionResultsScreen::Activate() -> int32_t
{
    EventsToMissionResultsScreen = 1;
    _SkipAnimation = false;
    Mission()->StopScenarioCallbacks();
    GuiSystem().CursorHidden = false;
    GuiSystem().SetCurrentCursor(static_cast<MCCursorType>(0));
    GuiSystem().CursorHidden = true;
    GuiSystem().Release();

    _StepIndex = 0;
    _Step = MCResultsStep::ResourcePoints;
    _NextDrawTime = MouseTicks;
    // Shown in front (the original added it to the screen here).
    ShowGuiWindow(true);
    SetDepth(0x5f);

    // The move-on button's label: "mission failed" when the scenario (or the home side) lost.
    if (MultiPlayer() == nullptr)
    {
        if (ScenarioResult < 3)
        {
            _MoveOnPort = std::make_unique<MCGuiPort>();
            _MoveOnPort->Init(const_cast<char*>("mr_mf00.tga"));
            _MoveOnButton->SetUpPicture(const_cast<char*>("mr_mf01.tga"));
            _MoveOnButton->SetDownPicture(const_cast<char*>("mr_mf02.tga"));
        }
    }
    else
    {
        _MoveOnPort = std::make_unique<MCGuiPort>();
        _MoveOnPort->Init(
            const_cast<char*>(HomeSideLost(ScenarioResult, HomeTeam()->Alignment) ? "mr_mf00.tga" : "mrm_ms00.tga"));
    }

    // (The original copied the label onto the window here and freed it; Draw shows it.)
    _MoveOnButton->Draw();
    const MCScenario& scenario = *Scenario();

    if (MultiPlayer() == nullptr)
    {
        _Results = GatherSinglePlayerResults(scenario, ScenarioResult);
        _PilotIcons.clear();
        _PilotIcons.resize(_Results.Pilots.size());
        _ShownResourcePoints = -1;

        // The debriefing shows the tactical map's salvage text. The original moved the map's box onto this screen; the
        // screen makes its own box like it, with the map's text, and the map's box is hidden (it left the map).
        const std::shared_ptr<MCTacticalMap> tacMap = TacticalMap();
        const std::shared_ptr<MCGuiScrollTextObject> salvage = tacMap->SalvageText.Lock();
        _TextObject = CreateChild<MCGuiScrollTextObject>(
            [&salvage](MCGuiScrollTextObject& made)
            {
                made.Init(salvage->X(), salvage->Y(), salvage->Width(), salvage->Height(), {});
                made.CopyTextFrom(*salvage);
            });
        salvage->ShowGuiWindow(false);
        const std::shared_ptr<MCGuiScrollTextObject> text = _TextObject.Lock();
        text->MoveTo(10, 0x15b, false);
        text->Resize(0xc4, 0x59);
        text->ShowGuiWindow(false);

        SoundSystem()->PlayBettySample(ScenarioResult < 4 ? 0xb : 0x12);
    }
    else
    {
        if (IsMPlayerGame != 0)
        {
            GuiSystem().AddTimer(weak_from_this(), TimeoutTimer, 90000, 0, 0, false);
        }

        _Results = GatherMultiplayerResults(scenario);

        // (The original drew the summary, the home side's pilots and the objectives into the window's picture here;
        // Draw shows them each frame from what is kept below.)
        // Port fix: MCX.EXE reads the best pilot without checking there is one.
        if (!_Results.Pilots.empty())
        {
            _BestPilotIcon = TakeIcon(*_Results.Pilots[0].Warrior.lock());
        }

        _PilotIcons.clear();

        for (const MCMissionPilotResult& pilot : _Results.Pilots)
        {
            const std::shared_ptr<MCMechWarrior> warrior = pilot.Warrior.lock();
            _PilotIcons.push_back(warrior != nullptr ? TakeIcon(*warrior) : std::nullopt);
        }

        const int32_t seconds = static_cast<int32_t>(std::fmod(static_cast<double>(ActualTime), 60.0));
        _TimeText = std::format("{:02}:{:02}", static_cast<int32_t>(ActualTime) / 60, seconds);

        // The commanders by kills.
        _CommanderLines.clear();

        for (int32_t i = 0; i < static_cast<int32_t>(_Results.Commanders.size()); i++)
        {
            const MCMissionCommanderScore& score = _Results.Commanders[static_cast<size_t>(i)];
            auto player = score.Score < 0 ? nullptr : MultiPlayer()->SessionManager->GetPlayerNumber(score.CommanderId);

            if (player != nullptr)
            {
                _CommanderLines.push_back({i + 1, player->Name, score.Score});
            }
        }

        ShowMPPilots(true);
        SoundSystem()->PlayBettySample(HomeSideLost(ScenarioResult, HomeTeam()->Alignment) ? 0xb : 0x12);
    }

    // Port-only (Redux HD): a battle over, this game's leave ends its session for everyone, as the original's sessions
    // ended with each battle; a game that quit a battle still going on hands its session on (MCSessionManager::HandsOn).
    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
    {
        MultiPlayer()->SessionManager->HandsOn = Mission()->EndScenarioRequested < 0;
    }

    if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr && Mission()->EndScenarioRequested != 0)
    {
        MultiPlayer()->LeaveSession();
        MultiPlayer()->InMission = true;
    }

    // Port-only (Redux HD): the host's REMATCH, when a guest still in the session can have it (network/MCRoomEnd.h).
    if (_RematchButton != nullptr)
    {
        _RematchButton->ShowGuiWindow(MCRoomEnd::OffersRematch());
    }

    SomethingOnFire = 0;
    return 0;
}

MCRematchFaces RematchFaces()
{
    return {.Up = RematchFace(MoveOnUpFace, MCInk::Dark), .Down = RematchFace(MoveOnDownFace, MCInk::Light)};
}

MCGuiHandle<MCGuiButton> AddRematchButton(MCGuiObject& parent, int32_t xPos, int32_t yPos)
{
    MCGuiHandle<MCGuiButton> button = parent.CreateChild<MCGuiButton>(
        [&](MCGuiButton& made)
        {
            made.Init(xPos, yPos, MoveOnWidth, MoveOnHeight, {});
            const MCRematchFaces faces = RematchFaces();
            made.UpPicture = faces.Up;
            made.DownPicture = faces.Down;
        });
    button->SetEventRoutine(RematchButtonHandleEvent);
    button->SetTransparent(true);
    button->ShowGuiWindow(false);
    return button;
}
