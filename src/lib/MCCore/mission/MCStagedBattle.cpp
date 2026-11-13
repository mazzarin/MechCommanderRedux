#include "stdafx.h"
#include "mission/MCStagedBattle.h"
#include "ai/MCScenarioMap.h"
#include "ai/MCTacticalOrder.h"
#include "camera/MCCamera.h"
#include "camera/MCCameraList.h"
#include "camera/MCViewWindow.h"
#include "iface/MCMechBar.h"
#include "iface/MCTacticalInterface.h"
#include "lib/MCDice.h"
#include "lib/MCOptionalRef.h"
#include "main/MCGameContext.h"
#include "main/MCMissionGlobals.h"
#include "mission/MCScenario.h"
#include "object/MCArtillery.h"
#include "object/MCBattleMech.h"
#include "object/MCCommander.h"
#include "object/MCExplosion.h"
#include "object/MCForces.h"
#include "object/MCMasterComponent.h"
#include "object/MCMechGameSystem.h"
#include "object/MCMechWarrior.h"
#include "object/MCMiscTerrainObject.h"
#include "object/MCMover.h"
#include "object/MCObjectSystem.h"
#include "object/MCSensorSystem.h"
#include "object/MCTeam.h"
#include "sprite/MCWeather.h"
#include "sprite/MCWeatherPace.h"
#include "terrain/MCTerrain.h"

namespace
{
    using MCStaging::FaceMechTo;
    using MCStaging::NearestEnemy;
    using MCStaging::PlayerMechPartId;
    using MCStaging::SendLanceAt;

    /// <summary>The names a battle's spec has (MCBattleSpec::Read's).</summary>
    constexpr std::array<std::string_view, 30> SpecNames = {
        "mission", "force",   "frames",       "zoom",   "jump",     "strike",      "snow-least", "incoming",
        "walk",    "contact", "contact-step", "answer", "kill",     "watch",       "watch-zoom", "zoom-ease",
        "jump-at", "camera",  "dfa",          "mines",  "alpha",    "destroy",     "run",        "close",
        "volley",  "loss",    "smoke",        "wear",   "converge", "close-names",
    };

    /// <summary>
    /// The trailer's battle (video/hype/lines-canyon.json's staging, as `spec.py battle` prints it): mission 16's canyon
    /// with the trailer's company, its 2680 frames (89 seconds), the view at 540 lines.
    /// </summary>
    constexpr std::string_view CanyonSpec =
        R"spec(# The Redux HD trailer's battle: mission 16's canyon, the trailer's company of ten.
force mis0304
frames 2680
zoom 540
mission 16
converge Masakari,Thor
answer 860
walk 860
contact 620
contact-step 260
watch 620,900
watch-zoom 900
close-names Masakari,Thor
camera 900:3,1000:1,1135:4,1210:0,1275:6,1300:0,1350:4,1380:5,1437:2,1540:6,1599:5,1690:0,1749:6,1820:7,1895:5,1945:4,2020:7,2043:4,2100:0,2135:4,2172:6,2200:0,2217:3,2250:8,2291:0,2325:8,2360:9,2410:0,2427:3,2470:0,2519:4,2551:5
wear 900:3:90,1590:5:90,1740:6:90,1950:9:430,2040:8:300,2190:4:341,2200:0:95,2300:3:140
incoming 1215:0,1440:2,2005:0
dfa 1385:5,2140:4
mines 1545:6,1560:6,1575:6,1900:5,1915:5,1930:5
loss 1610:5:hit,1760:6:hit,2190:6:die,2236:7:die,2285:1:die,2306:0:leg,2332:8:die,2384:9:die,2395:2:die,2438:3:arm,2447:3:die,2481:0:arm,2490:0:arm,2498:0:die,2531:4:die,2610:5:eject
alpha 1695:0,1825:7,1950:4,2025:7,2222:3
destroy 1722:0,1866,2058:4,2228:3
close 2172:6:3@50,2204:0:0
run 2255:8
volley 2545:5
)spec";

    /// <summary>A whole number, all of <paramref name="text"/>.</summary>
    std::optional<int32_t> WholeNumber(std::string_view text)
    {
        int32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc() && end == text.data() + text.size() ? std::optional(value) : std::nullopt;
    }

    constexpr int32_t FramesPerSecond = MCBattleSpec::FramesPerSecond;
    constexpr int32_t RetargetFrames = 150;
    constexpr int32_t LargeStrike = 1;
    constexpr int32_t StrikeDelaySeconds = 1;
    // A kill keeps 30% of its internal structure and goes in a volley or two.
    constexpr float KillStructure = 0.3f;
    // A mech running goes this far from the nearest enemy mech, ordered again every second, for three.
    constexpr float RetreatDistance = 300.0f;
    constexpr int32_t RetreatFrames = 30;
    constexpr int32_t RunFrames = 90;
    // Staged damage lands with a hit: whatever reaches its mech fires at it from FireLeadFrames before it is due, in a
    // volley every LossVolleyFrames until it lands (a shot's damage comes when its effect gets there: a beam's when it
    // ends, half a second on, a slug's or missile's when it arrives), and the first hit from HitWindowFrames before
    // takes the part (with none by HitGraceFrames after, it goes anyway).
    constexpr int32_t FireLeadFrames = 24;
    // A death's fire starts DieLeadFrames before it is due instead (from those not held for it until FireLeadFrames),
    // so its hits wear the mech's bar down first.
    constexpr int32_t DieLeadFrames = 90;
    constexpr int32_t LossVolleyFrames = 8;
    constexpr int32_t HitWindowFrames = 8;
    constexpr int32_t HitGraceFrames = 30;
    constexpr int32_t LossGapFrames = 10;
    // A held mech of the force has its full armor raised to the most a location takes, so no one frame's fire (a
    // volley's missiles bursting all around it) goes through to its structure and breaks its engine, ammunition or head
    // before its staged end. What the game's own hits take off it comes off the armor the mech really has, and the
    // raised armor is held at the share of that left, so its bar (the share of its armor its locations have) falls as
    // it is hit; no location's last WornFloor share goes before its staged end.
    constexpr uint8_t ArmorHeld = 255;
    constexpr float WornFloor = 0.2f;
    // A mech worn down (--showcase-wear frame:mech[:frames]) has WearShooters enemy mechs (the nearest, none sent at one
    // worn sooner) kept at it for that many frames or until it falls, and while its bar is above one falling from full
    // to WearEndBar over them: one of those further than all its weapons reach (by WearSlack) set down where they do
    // once neither is on screen, and from WearLeadFrames on, what reaches it fired at it as for a staged loss (the
    // WearReadied units nearest readied), a volley every LossVolleyFrames. The game's own shots wear its armor.
    constexpr size_t WearShooters = 3;
    constexpr int32_t WearFrames = 30;
    constexpr int32_t WearLeadFrames = 30;
    constexpr float WearSlack = 1.25f;
    constexpr float WearEndBar = 0.3f;
    constexpr int32_t WearReadied = 8;
    constexpr int32_t ApproachFrames = 90;
    constexpr size_t ApproachingMechs = 3;
    // Out of turn, only the weapons of this many units nearest a target are readied, a unit no more often than this.
    constexpr int32_t ReadiedShooters = 4;
    constexpr float ShooterRestSeconds = 2.0f;
    // The force sees as far as its tiles' heights let it (the sight its mechs give is not used), its sensors further.
    // The fog's edge is the first spot ahead of the force it does not see between frames (spots tried this much
    // further along the line from the first mech at a time, world units); a unit there is seen about a fifth further
    // out. The contact is set down this many times the edge's distance from the force's nearest mech, walks in to stand
    // this many (a blip on the sensors, in the fog), and steps into sight, this many, MCBattleSpec::ContactStepFrames before the
    // walk's end (it walks about 8 m a second; --showcase-contact-step gives a ground whose sight reaches further more).
    constexpr float ContactTry = 10.0f;
    constexpr float ContactFrom = 1.55f;
    constexpr float ContactOutside = 1.4f;
    constexpr float ContactStop = 0.75f;
    // The force halts this long before the contact, so what it sees has settled.
    constexpr int32_t HaltLeadFrames = 30;
    // Until the force attacks, the rest of the other side is kept this many of the sensors' reaches from it, so the
    // contact is the first; then those converging and further off are set down this many times the edge's distance
    // from it, unseen, to come in.
    constexpr float SensorClear = 1.1f;
    constexpr float ConvergeFrom = 1.2f;
    // The watch's camera holds this share of the way from the force's mech nearest where the contact stands to there:
    // the fog's edge, the force's front on one side, the blip coming in on the other.
    constexpr float WatchShare = 0.45f;
    // Once the fight starts it looks this share of the way from the force's nearest mech to the contact.
    constexpr float FightShare = 0.7f;
    // The other side's strikes are called this far from their mech, toward its nearest enemy, to land this long after
    // (their marks show from 4 seconds out; under 3 the strike's own time is used).
    constexpr float IncomingOffset = 120.0f;
    constexpr int32_t IncomingSeconds = 3;
    // It is then moved onto the force, on open ground level with the mech, looked for this far around (meters).
    constexpr float IncomingSearchMeters = 40.0f;
    // A volley: the weapons of this many enemy units nearest the mech readied at once, and fired as they reach it, for a
    // second.
    constexpr int32_t VolleyFrames = 30;
    constexpr int32_t VolleyShooters = 6;
    // An alpha strike: the mech fires every weapon as it comes to bear, for a second and a half, its target brought
    // within reach the frame before (staged on a cut to the mech, so the camera is elsewhere then). The camera, on the
    // mech since before, looks this share of the way from it to the target.
    constexpr int32_t AlphaFrames = 45;
    constexpr int32_t AlphaBringFrames = 1;
    // Before it, the mech holds the fire of the weapons that come ready this long, so they go together, and the enemy
    // mech nearest it (not sent in for another) runs to within its reach this long before (of its own accord, where the
    // camera may see), ordered on again this often.
    constexpr int32_t AlphaHoldFrames = 240;
    constexpr int32_t AlphaApproachFrames = 150;
    constexpr int32_t AlphaRunFrames = 15;
    constexpr float AlphaShare = 0.5f;
    // An enemy being finished near the mech followed is framed with it (AlphaShare between them) this long from the
    // fire on it.
    constexpr int32_t KillFrames = 90;
    // A mech in smoke has the SmokeTrees forests nearest it within SmokeTreeMeters burning this long.
    constexpr size_t SmokeTrees = 12;
    constexpr float SmokeTreeMeters = 90.0f;
    constexpr float SmokeSeconds = 15.0f;
    // Those closing in are ordered again every two seconds; at the close's frame its what (default Brought) of them are
    // set down behind the mech first (CloseIn). The camera on the mech they close on frames it with them (AlphaShare
    // between it and their middle) this long from then, so they are seen coming.
    constexpr int32_t CloseFrames = 60;
    constexpr int32_t Brought = 4;
    constexpr int32_t BroughtFrames = 75;
    // A strike's or a kill's target is the most worn of this many nearest.
    constexpr size_t WornChoices = 3;
    // The enemy mechs in the fight: those this near one of the force (the medium band).
    constexpr float FightMeters = 140.0f;

    // A minefield is the other side's, laid before its line: before its first mine (MineWalkFrames, or its what's
    // frames) its mech steps out toward its enemy as far as it walks by then at MineWalkPace and MineBeyondMeters more
    // (its own way round what's between; the first of MineWalkShares of that with such ground) to where the way from
    // MineSlackMeters short of the first mine's place (at most MineSlackShare of the way there) on is open ground level with it that the force has not crossed
    // (none of it within FreshMeters; the force's places are kept every TrailFrames), turned as little from the line to
    // its enemy as there is such ground (up to MineTurnDegrees either side, MineTurnStepDegrees apart). Each mine goes
    // off MineAheadMeters ahead of it, MineSideMeters left and right in turn; MineSettleFrames after the last it backs
    // out the way it came, and keeps out of the force's orders until it is back.
    constexpr int32_t MineWalkFrames = 60;
    constexpr float MineWalkPace = 10.0f;
    constexpr float MineBeyondMeters = 15.0f;
    constexpr float MineSlackMeters = 15.0f;
    constexpr float MineSlackShare = 0.5f;
    constexpr std::array MineWalkShares = {1.0f, 0.85f, 1.15f, 0.7f};
    constexpr float FreshMeters = 10.0f;
    constexpr float WayStep = 0.05f;
    constexpr int32_t TrailFrames = 15;
    constexpr float MineTurnDegrees = 90.0f;
    constexpr float MineTurnStepDegrees = 10.0f;
    constexpr float MineAheadMeters = 4.0f;
    constexpr float MineSideMeters = 3.0f;
    constexpr int32_t MineSettleFrames = 45;

    /// <summary><paramref name="mover"/> ordered (by the player, unless <paramref name="origin"/> says otherwise) to
    /// attack <paramref name="target"/> from <paramref name="range"/> (a Mover::getFireRange selector: -1 its optimal
    /// range, 0 the short band's).</summary>
    void OrderAttack(MCMover& mover, const std::shared_ptr<MCMover>& target,
                     MCOrderOrigin origin = MCOrderOrigin::Player, int32_t range = -1)
    {
        MCTacticalOrder order;
        order.Reset(origin, MCTacticalOrderCode::AttackObject, 0);
        order.Target = target;
        order.AttackParams.Type = 1;
        order.AttackParams.Method = 0;
        order.AttackParams.Range = range;
        order.AttackParams.Pursue = -1;
        mover.HandleTacticalOrder(order, 1, 0);
    }

    /// <summary>The most mechs a player fields (part ids from <see cref="PlayerMechPartId"/>).</summary>
    constexpr int32_t PlayerMechs = 12;

    /// <summary>The player's mech in place <paramref name="place"/> of the force (fallen or not), or none.</summary>
    std::shared_ptr<MCMover> ForceMech(int32_t place)
    {
        return place >= 0 && place < PlayerMechs ? GetMoverFromPartId(PlayerMechPartId + place) : nullptr;
    }

    /// <summary>The player's mechs still fighting, in part order.</summary>
    std::vector<std::shared_ptr<MCMover>> LiveLance()
    {
        std::vector<std::shared_ptr<MCMover>> lance;

        for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + PlayerMechs; ++partId)
        {
            if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                lance.push_back(std::move(mover));
            }
        }

        return lance;
    }

    /// <summary><paramref name="mover"/>'s armor gone and its internal structure cut to <paramref name="share"/> of
    /// full, so the next hits take it apart.</summary>
    void Weaken(MCMover& mover, float share)
    {
        for (MCArmorLocation& armor : mover.Armor)
        {
            armor.CurArmor = 0.0f;
        }

        for (MCBodyLocation& body : mover.Body)
        {
            body.CurInternalStructure = std::min(body.CurInternalStructure, body.MaxInternalStructure * share);
        }
    }

    /// <summary>Whether <paramref name="mover"/>'s name holds one of <paramref name="names"/> (comma-separated parts of
    /// names).</summary>
    bool Named(MCMover& mover, std::string_view names)
    {
        const std::string_view name = mover.GetIfaceName();
        bool named = false;

        for (const auto part : std::views::split(names, ','))
        {
            named = named || name.find(std::string_view(part.begin(), part.end())) != std::string_view::npos;
        }

        return named;
    }

    /// <summary>The live enemy mechs (or, with <paramref name="anyKind"/>, units), nearest <paramref name="from"/>
    /// first; with <paramref name="names"/>, those so named (<see cref="Named"/>).</summary>
    std::vector<std::shared_ptr<MCMover>> EnemyMechsByDistance(MCMover& from, bool anyKind = false,
                                                               std::string_view names = {})
    {
        const std::shared_ptr<MCTeam> team = from.Team.lock();
        const MCVector3D at = from.GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> found;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0 ||
                (!anyKind && mover->ObjectClass != MCObjectClass::BattleMech) ||
                (!names.empty() && !Named(*mover, names)))
            {
                continue;
            }

            const MCVector3D to = mover->GetPosition();
            found.emplace_back((to.X - at.X) * (to.X - at.X) + (to.Y - at.Y) * (to.Y - at.Y), mover);
        }

        std::ranges::stable_sort(found, {}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        std::vector<std::shared_ptr<MCMover>> nearestFirst;

        for (auto& [distance, mover] : found)
        {
            nearestFirst.push_back(std::move(mover));
        }

        return nearestFirst;
    }

    /// <summary>The live enemy mech (or unit) nearest <paramref name="from"/>, or none (see
    /// <see cref="EnemyMechsByDistance"/>).</summary>
    std::shared_ptr<MCMover> NearestEnemyMech(MCMover& from, bool anyKind = false, std::string_view names = {})
    {
        const std::vector<std::shared_ptr<MCMover>> nearestFirst = EnemyMechsByDistance(from, anyKind, names);
        return nearestFirst.empty() ? nullptr : nearestFirst.front();
    }

    /// <summary>Whether <paramref name="mover"/> is a mech in the air.</summary>
    bool Jumping(const MCMover& mover)
    {
        return mover.ObjectClass == MCObjectClass::BattleMech && static_cast<const MCBattleMech&>(mover).InJump;
    }

    /// <summary>
    /// <paramref name="mover"/> ordered to jump at <paramref name="target"/> when it can and the target is not too near:
    /// landing <see cref="JumpLanding"/> of the way there, or as far as its jets take it. Returns whether it jumps.
    /// </summary>
    bool JumpAt(MCMover& mover, MCMover& target)
    {
        // A jump lands three quarters of the way to the target; one shorter than a third of the range isn't worth it.
        constexpr double JumpLanding = 0.75;
        constexpr float ShortestJump = 1.0f / 3.0f;
        const float range = mover.GetJumpRange();

        if (mover.CanJump() == 0 || Jumping(mover) || range <= 0.0f)
        {
            return false;
        }

        const MCVector3D from = mover.GetPosition();
        const MCVector3D to = target.GetPosition();
        const double dx = static_cast<double>(to.X) - from.X;
        const double dy = static_cast<double>(to.Y) - from.Y;
        const double distance = std::sqrt(dx * dx + dy * dy);

        if (distance < range * ShortestJump)
        {
            return false;
        }

        const double share = std::min(JumpLanding, range / distance);
        const MCWayPathPoint point{
            MCVector3D(static_cast<float>(from.X + dx * share), static_cast<float>(from.Y + dy * share), to.Z), false};
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::JumpToPoint, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
        return true;
    }

    /// <summary>How far apart the two sides' lines stand (meters): inside most weapons' reach, in one frame.</summary>
    constexpr float LineMeters = 45.0f;
    /// <summary>How near those closing in on a mech come (meters): the noose tightened, not a pile.</summary>
    constexpr float NooseMeters = 30.0f;
    /// <summary>How far behind a mech a second line is set down to walk in on it (meters), unless a close says.</summary>
    constexpr float BehindMeters = 110.0f;

    /// <summary><paramref name="mover"/> ordered by <paramref name="origin"/> to walk (or <paramref name="run"/>) to
    /// <paramref name="point"/>.</summary>
    void WalkTo(MCMover& mover, const MCVector3D& point, MCOrderOrigin origin = MCOrderOrigin::Player, bool run = false)
    {
        const MCWayPathPoint way{point, run};
        MCTacticalOrder order;
        order.Reset(origin, MCTacticalOrderCode::MoveToPoint, 0);
        order.InitWayPath(std::span(&way, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
    }

    /// <summary>
    /// <paramref name="mover"/> walked to <paramref name="meters"/> from <paramref name="target"/> along the line between
    /// them (where it is, if nearer: backing off would turn it away), its pilot's target set to it, so it fires as it goes
    /// and once there (a pilot fires on his last target under a move order while it is within his attack radius). An
    /// attack order keeps only coarse distances (its range bands, 60, 140 and 200 m, or the pilot's optimal range, which
    /// for a mech that outranges its target is its longest weapon's), so the two sides would stand far off or pile up.
    /// </summary>
    void Engage(MCMover& mover, const std::shared_ptr<MCMover>& target, float meters, MCOrderOrigin origin)
    {
        const MCVector3D from = target->GetPosition();
        const MCVector3D at = mover.GetPosition();
        const float length = std::max(std::hypot(at.X - from.X, at.Y - from.Y), 1.0f);
        const float away = std::min(meters * WorldUnitsPerMeter, length);
        WalkTo(mover,
               MCVector3D(from.X + (at.X - from.X) / length * away, from.Y + (at.Y - from.Y) / length * away, at.Z),
               origin);

        if (const std::shared_ptr<MCMechWarrior> pilot = mover.GetPilot(); pilot != nullptr)
        {
            pilot->SetLastTarget(target);
        }
    }

    /// <summary>
    /// A target of <paramref name="targets"/> for each of <paramref name="attackers"/> (in their order; none if there
    /// are no targets): the nearest, each attacker already given one counting SpreadPenalty further (world units), the
    /// attackers nearest a target choosing first, so fire spreads along the other side's line.
    /// </summary>
    std::vector<std::shared_ptr<MCMover>> SpreadTargets(const std::vector<std::shared_ptr<MCMover>>& attackers,
                                                        const std::vector<std::shared_ptr<MCMover>>& targets)
    {
        constexpr float SpreadPenalty = 150.0f;
        std::vector<std::shared_ptr<MCMover>> chosen(attackers.size());

        if (targets.empty())
        {
            return chosen;
        }

        const auto Distance = [](MCMover& one, MCMover& other)
        {
            const MCVector3D a = one.GetPosition();
            const MCVector3D b = other.GetPosition();
            return std::hypot(a.X - b.X, a.Y - b.Y);
        };

        std::vector<std::pair<float, size_t>> order;

        for (size_t i = 0; i < attackers.size(); ++i)
        {
            float nearest = std::numeric_limits<float>::max();

            for (const std::shared_ptr<MCMover>& target : targets)
            {
                nearest = std::min(nearest, Distance(*attackers[i], *target));
            }

            order.emplace_back(nearest, i);
        }

        std::ranges::stable_sort(order, {}, &std::pair<float, size_t>::first);
        std::vector<int32_t> load(targets.size(), 0);

        for (const auto& [nearest, i] : order)
        {
            size_t best = 0;
            float bestCost = std::numeric_limits<float>::max();

            for (size_t t = 0; t < targets.size(); ++t)
            {
                if (const float cost =
                        Distance(*attackers[i], *targets[t]) + SpreadPenalty * static_cast<float>(load[t]);
                    cost < bestCost)
                {
                    best = t;
                    bestCost = cost;
                }
            }

            ++load[best];
            chosen[i] = targets[best];
        }

        return chosen;
    }

    /// <summary>
    /// The other sides' units whose names hold one of <paramref name="names"/> (comma-separated) ordered by their
    /// commander at a mech of <paramref name="lance"/> to <see cref="LineMeters"/> from it (<see cref="Engage"/>), spread
    /// along it (<see cref="SpreadTargets"/>), but those of <paramref name="busy"/> (part ids). Returns how many.
    /// </summary>
    int32_t Converge(std::string_view names, const std::vector<std::shared_ptr<MCMover>>& lance,
                     const std::set<int32_t>& busy = {})
    {
        if (lance.empty())
        {
            return 0;
        }

        const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
        std::vector<std::shared_ptr<MCMover>> movers;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0 || !Named(*mover, names) ||
                busy.contains(partId))
            {
                continue;
            }

            movers.push_back(mover);
        }

        const std::vector<std::shared_ptr<MCMover>> targets = SpreadTargets(movers, lance);

        for (size_t i = 0; i < movers.size(); ++i)
        {
            Engage(*movers[i], targets[i], LineMeters, MCOrderOrigin::Commander);
        }

        return static_cast<int32_t>(movers.size());
    }

    /// <summary>
    /// <paramref name="mover"/> ordered to run <paramref name="distance"/> away from the live enemy mech nearest it.
    /// Returns whether there was one to run from.
    /// </summary>
    bool RetreatFrom(MCMover& mover, float distance)
    {
        const std::shared_ptr<MCMover> enemy = NearestEnemyMech(mover);

        if (enemy == nullptr)
        {
            return false;
        }

        const MCVector3D at = mover.GetPosition();
        const MCVector3D from = enemy->GetPosition();
        const float dx = at.X - from.X;
        const float dy = at.Y - from.Y;
        const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        const MCWayPathPoint point{MCVector3D(at.X + dx / length * distance, at.Y + dy / length * distance, at.Z),
                                   true};
        MCTacticalOrder order;
        order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::MoveToPoint, 0);
        order.InitWayPath(std::span(&point, 1));
        order.MoveParams.Wait = 0;
        mover.HandleTacticalOrder(order, 1, 0);
        return true;
    }

    /// <summary>
    /// Whether <paramref name="point"/> shows in the main camera's view, or within a sprite's reach of its edge (a mech
    /// standing just outside it still shows its top), as MCBigGameObject::OnScreen projects.
    /// </summary>
    bool PointOnScreen(const MCVector3D& point)
    {
        constexpr float SpriteReach = 120.0f;
        const std::shared_ptr<MCCamera> camera = CameraList()->FindCameraFromIDNumber(1);

        if (camera == nullptr || camera->Active == 0)
        {
            return false;
        }

        const double dx = point.X - camera->Position.X;
        const double dy = point.Y - camera->Position.Y;
        const double dz = point.Z - camera->Position.Z;
        const double x = dy * camera->CosAngle + dx * camera->CosAngle + camera->HalfWidth;
        const double y = dx * camera->SinAngle + camera->HalfHeight - dy * camera->SinAngle - dz;
        return x >= -SpriteReach && y >= -SpriteReach && x <= camera->ViewWidth + SpriteReach &&
               y <= camera->ViewHeight + SpriteReach;
    }

    /// <summary>Whether a mech could stand at <paramref name="spot"/>: open ground, within LevelMeters of the elevation
    /// <paramref name="level"/> (a terrace's, not the slope below it).</summary>
    bool OpenAndLevel(const MCVector3D& spot, float level)
    {
        constexpr float LevelMeters = 5.0f;
        return GameMap().CellPassable(spot) &&
               std::abs(GameMap().GetTerrainElevation(spot) - level) <= LevelMeters * WorldUnitsPerMeter;
    }

    /// <summary>
    /// The spot nearest <paramref name="point"/> (itself first, then rings RingMeters apart out to
    /// <paramref name="meters"/>) that is open ground level with <paramref name="level"/>; the point itself if none.
    /// </summary>
    MCVector3D NearestOpenAndLevel(const MCVector3D& point, float level, float meters)
    {
        constexpr float RingMeters = 5.0f;
        constexpr int32_t RingSpots = 24;

        if (OpenAndLevel(point, level))
        {
            return point;
        }

        for (float ring = RingMeters; ring <= meters; ring += RingMeters)
        {
            for (int32_t i = 0; i < RingSpots; ++i)
            {
                const float turn = static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / RingSpots;
                const MCVector3D spot(point.X + std::cos(turn) * ring * WorldUnitsPerMeter,
                                      point.Y + std::sin(turn) * ring * WorldUnitsPerMeter, point.Z);

                if (OpenAndLevel(spot, level))
                {
                    return spot;
                }
            }
        }

        return point;
    }

    /// <summary>
    /// Up to <paramref name="count"/> spots <paramref name="meters"/> behind <paramref name="at"/> (along the unit
    /// vector (<paramref name="backX"/>, <paramref name="backY"/>)), abreast AbreastMeters apart, each where a mech can
    /// stand and walk in from: open ground, level with <paramref name="at"/> (a terrace, not the slope below it), off
    /// screen. Where an abreast spot is not, the nearest that is of those fanned out behind (FanDegrees to either side,
    /// up to FanDepthMeters further back), AbreastMeters from those already chosen. In abreast order. On a camera
    /// <paramref name="cut"/> what the camera showed doesn't matter: that view ends.
    /// </summary>
    std::vector<MCVector3D> SpotsBehind(const MCVector3D& at, float backX, float backY, float meters, int32_t count,
                                        bool cut)
    {
        constexpr float AbreastMeters = 20.0f;
        constexpr float FanDegrees = 75.0f;
        constexpr float FanStepDegrees = 5.0f;
        constexpr float FanDepthMeters = 60.0f;
        constexpr float FanStepMeters = 10.0f;
        const float level = GameMap().GetTerrainElevation(at);
        const float apart = AbreastMeters * WorldUnitsPerMeter;
        const auto open = [&](const MCVector3D& spot)
        { return OpenAndLevel(spot, level) && (cut || !PointOnScreen(spot)); };

        std::vector<MCVector3D> fan;

        for (float depth = meters; depth <= meters + FanDepthMeters; depth += FanStepMeters)
        {
            for (float degrees = -FanDegrees; degrees <= FanDegrees; degrees += FanStepDegrees)
            {
                const float turn = degrees * std::numbers::pi_v<float> / 180.0f;
                const float x = backX * std::cos(turn) - backY * std::sin(turn);
                const float y = backX * std::sin(turn) + backY * std::cos(turn);
                const MCVector3D spot(at.X + x * depth * WorldUnitsPerMeter, at.Y + y * depth * WorldUnitsPerMeter,
                                      at.Z);

                if (open(spot))
                {
                    fan.push_back(spot);
                }
            }
        }

        std::vector<MCVector3D> spots;

        for (int32_t place = 0; place < count; ++place)
        {
            const float side = (static_cast<float>(place) - static_cast<float>(count - 1) * 0.5f) * apart;
            const float back = meters * WorldUnitsPerMeter;
            MCVector3D ideal(at.X + backX * back - backY * side, at.Y + backY * back + backX * side, at.Z);
            const auto clear = [&](const MCVector3D& spot)
            {
                return std::ranges::all_of(spots, [&](const MCVector3D& other)
                                           { return std::hypot(other.X - spot.X, other.Y - spot.Y) >= apart; });
            };

            if (open(ideal) && clear(ideal))
            {
                spots.push_back(ideal);
                continue;
            }

            MCOptionalRef<const MCVector3D> nearest;

            for (const MCVector3D& spot : fan)
            {
                if (clear(spot) && (!nearest || std::hypot(spot.X - ideal.X, spot.Y - ideal.Y) <
                                                    std::hypot(nearest->get().X - ideal.X, nearest->get().Y - ideal.Y)))
                {
                    nearest = spot;
                }
            }

            if (nearest)
            {
                spots.push_back(nearest->get());
            }
        }

        return spots;
    }

    /// <summary>
    /// The other sides' units named in <paramref name="names"/> (comma-separated parts of names) closing in on
    /// <paramref name="mover"/>, each sent to <see cref="NooseMeters"/> from it (Engage: the noose tightens); the <paramref name="bring"/>
    /// of them furthest from it first set down <paramref name="behind"/> meters behind it (on the side away from the
    /// rest of them, abreast, SpotsBehind), each where neither it nor its spot is on screen (on a camera
    /// <paramref name="cut"/>, anywhere: the view they were in ends, and the next finds them there), facing it, and
    /// only those that can walk: a second line that walks in on it from behind, the force caught between, added to
    /// <paramref name="brought"/>. Those of
    /// <paramref name="busy"/> (part ids) are left be. Returns how many close in.
    /// </summary>
    int32_t CloseIn(const MCBattleLog& log, std::string_view names, const std::shared_ptr<MCMover>& mover,
                    int32_t bring = 0, const std::set<int32_t>& busy = {}, float behind = BehindMeters,
                    MCOptionalRef<std::vector<std::shared_ptr<MCMover>>> brought = std::nullopt, bool cut = false)
    {
        // The second line walks in on a player's orders: under fire a pilot keeps to his alarm's over his
        // commander's, and would stand where he was set down.
        constexpr MCOrderOrigin SecondOrders = MCOrderOrigin::Player;
        const auto Second = [&](const std::shared_ptr<MCMover>& enemy)
        { return brought && std::ranges::find(brought->get(), enemy) != brought->get().end(); };
        const std::shared_ptr<MCTeam> team = mover->Team.lock();
        const MCVector3D at = mover->GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> named;
        double sumX = 0.0;
        double sumY = 0.0;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

            if (enemy == nullptr || enemy->Team.lock() == team || enemy->IsDisabled() != 0 || !Named(*enemy, names) ||
                busy.contains(partId))
            {
                continue;
            }

            const MCVector3D from = enemy->GetPosition();
            named.emplace_back(std::hypot(from.X - at.X, from.Y - at.Y), enemy);
            sumX += from.X;
            sumY += from.Y;
            Engage(*enemy, mover, NooseMeters, Second(enemy) ? SecondOrders : MCOrderOrigin::Commander);
        }

        if (named.empty() || bring <= 0)
        {
            return static_cast<int32_t>(named.size());
        }

        // Behind: away from where the rest stand.
        const auto count = static_cast<double>(named.size());
        const auto awayX = static_cast<float>(at.X - sumX / count);
        const auto awayY = static_cast<float>(at.Y - sumY / count);
        const float length = std::max(std::hypot(awayX, awayY), 1.0f);
        const float backX = awayX / length;
        const float backY = awayY / length;
        std::ranges::stable_sort(named, std::ranges::greater{}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        std::vector<MCVector3D> spots = SpotsBehind(at, backX, backY, behind, bring, cut);
        size_t placed = 0;

        for (const auto& [distance, enemy] : named)
        {
            if (placed == spots.size())
            {
                break;
            }

            if (!cut && PointOnScreen(enemy->GetPosition()))
            {
                continue;
            }

            // A part the mission's script hasn't brought into play yet (its Exists 0), asleep or powered down stands
            // where it is set down, unseen or still.
            constexpr int32_t PoweredUp = 0;

            if (enemy->GetExistsAndAwake() == 0 || enemy->Status != PoweredUp)
            {
                log(MCBattleLine::Event,
                    std::format("{} (part {:#x}) left be: it can't walk (exists {}, awake {}, status {})",
                                enemy->GetIfaceName(), enemy->PartId, enemy->GetExists(), enemy->GetAwake(),
                                enemy->Status));
                continue;
            }

            MCVector3D& place = spots[placed++];
            enemy->SetPosition(place);

            // Facing it, torso and arms with the legs: a line walking in on it, not one with its back to it.
            if (enemy->ObjectClass == MCObjectClass::BattleMech)
            {
                auto& mech = static_cast<MCBattleMech&>(*enemy);
                FaceMechTo(mech, at);
                mech.TorsoRotation = 0.0f;
                mech.LeftArmRotation = 0.0f;
                mech.RightArmRotation = 0.0f;
            }

            Engage(*enemy, mover, NooseMeters, SecondOrders);

            if (brought)
            {
                brought->get().push_back(enemy);
            }

            log(MCBattleLine::Event,
                std::format("{} (part {:#x}) set down {:.0f} m behind {} at {:.0f} {:.0f} ({:+.1f} m up)",
                            enemy->GetIfaceName(), enemy->PartId,
                            std::hypot(place.X - at.X, place.Y - at.Y) / WorldUnitsPerMeter, mover->GetIfaceName(),
                            place.X, place.Y,
                            (GameMap().GetTerrainElevation(place) - GameMap().GetTerrainElevation(at)) /
                                WorldUnitsPerMeter));
        }

        return static_cast<int32_t>(named.size());
    }

    /// <summary><paramref name="mover"/>'s armor and internal structure left, every location's together: less after a
    /// frame than before it, it was hit.</summary>
    float Toughness(const MCMover& mover)
    {
        float left = 0.0f;

        for (const MCArmorLocation& armor : mover.Armor)
        {
            left += armor.CurArmor;
        }

        for (const MCBodyLocation& body : mover.Body)
        {
            left += body.CurInternalStructure;
        }

        return left;
    }

    /// <summary>Whether either is on screen (or near enough its edge to show), where a unit set down would be seen to
    /// jump.</summary>
    bool EitherOnScreen(MCMover& one, MCMover& other)
    {
        return PointOnScreen(one.GetPosition()) || PointOnScreen(other.GetPosition());
    }

    /// <summary>When units' weapons were last readied out of turn (mission seconds, by part id).</summary>
    using MCReadiedAt = std::map<int32_t, float>;

    /// <summary>
    /// A weapon readied out of turn is one this share of the way through its recycle since it last fired: so none ever
    /// fires again much sooner than it would.
    /// </summary>
    constexpr float RecycledShare = 0.65f;

    /// <summary>Whether the weapon has done RecycledShare of its recycle since it last fired (or never has).</summary>
    bool Recycled(const MCInventoryItem& item)
    {
        return item.ReadyTime - ScenarioTime <= MasterComponentList[item.MasterID].RecycleTime * (1.0f - RecycledShare);
    }

    /// <summary>
    /// Whether the player sees <paramref name="unit"/>: one of the home team's, or in its sight. The game draws another
    /// side's unit only then (on the sensors alone, or unseen, it isn't drawn).
    /// </summary>
    bool PlayerSees(MCMover& unit)
    {
        const std::shared_ptr<MCTeam> home = HomeTeam();
        return home == nullptr || unit.Team.lock() == home ||
               static_cast<MCContactStatus>(unit.GetContactType(home->Id)) == MCContactStatus::Visual;
    }

    /// <summary>
    /// <paramref name="shooter"/>'s weapon <paramref name="weaponIndex"/> fired at <paramref name="target"/> as the AI
    /// fires it (FireWeapon's answer: 0 fired), but staged to land: the dice are rolled, each dropped, until the next
    /// d100 (FireWeapon's first roll, its hit roll) comes up 0, so it hits at any chance; and nothing on the tiles under
    /// its line (units, trees, buildings: their cells' blocks-fire bits) blocks it. The dice are as they were if it
    /// didn't fire, the tiles as they were after. Not fired (-1) where the shooter or the target is a unit the player
    /// doesn't see, on screen.
    /// </summary>
    int32_t StagedShot(MCMover& shooter, const std::shared_ptr<MCMover>& target, int32_t weaponIndex)
    {
        // Never out of, or into, empty ground the camera shows: a unit the game doesn't draw (PlayerSees), on screen.
        constexpr int32_t NotStaged = -1;

        if (const auto hidden = [](MCMover& unit) { return !PlayerSees(unit) && PointOnScreen(unit.GetPosition()); };
            hidden(shooter) || hidden(*target))
        {
            return NotStaged;
        }

        constexpr int32_t HitRollRange = 100;
        const uint32_t unloaded = MCPort::RandState();
        uint32_t loaded = unloaded;

        while (RandomNumber(HitRollRange) != 0)
        {
            loaded = MCPort::RandState();
        }

        MCPort::SeedRand(loaded);
        // The tiles LineOfFire walks (a third of a cell at a time), each with its nine cells' bits (15 + 2c) set clear.
        constexpr int32_t CellsPerTile = 9;
        constexpr uint32_t ClearBit = 0x8000u;
        uint32_t clear = 0;

        for (int32_t cell = 0; cell < CellsPerTile; ++cell)
        {
            clear |= ClearBit << (cell * 2);
        }

        std::map<std::pair<int32_t, int32_t>, uint32_t> opened;
        const MCVector3D from = shooter.GetPosition();
        const MCVector3D to = target->GetPosition();
        const float length = std::hypot(to.X - from.X, to.Y - from.Y);
        constexpr float StepShare = 0.33f;
        const float step = MCTerrain::MetersPerVertexDivMapcellDim * StepShare;

        for (float along = 0.0f; along <= length + step; along += step)
        {
            const float share = length > 0.0f ? std::min(along / length, 1.0f) : 1.0f;
            int32_t tileR = 0;
            int32_t tileC = 0;
            int32_t cellR = 0;
            int32_t cellC = 0;
            MCScenarioMap::WorldToMapPos(
                MCVector3D(from.X + (to.X - from.X) * share, from.Y + (to.Y - from.Y) * share, 0.0f), tileR, tileC,
                cellR, cellC);

            if (GameMap().OnMap(tileR, tileC) && !opened.contains({tileR, tileC}))
            {
                MCMapTile& tile = GameMap().TileAt(tileR, tileC);
                opened[{tileR, tileC}] = tile.Cells;
                tile.Cells |= clear;
            }
        }

        const int32_t answer = shooter.FireWeapon(target, 0.0f, weaponIndex, 0, -1, std::nullopt);

        for (const auto& [at, cells] : opened)
        {
            MCMapTile& tile = GameMap().TileAt(at.first, at.second);
            tile.Cells = (tile.Cells & ~clear) | (cells & clear);
        }

        if (MCPort::RandState() == loaded)
        {
            MCPort::SeedRand(unloaded);
        }

        return answer;
    }

    /// <summary>
    /// Every live unit not on <paramref name="victim"/>'s side firing each weapon that is ready and reaches it. First the
    /// <paramref name="readyNearest"/> units nearest it that have not been readied for <paramref name="rest"/> seconds
    /// (<paramref name="readiedAt"/>) have the weapons that reach it and have nearly recycled readied (those of
    /// <paramref name="form"/> only, if given): out of turn, so only a few, never all at once. Those in
    /// <paramref name="skip"/> (part ids) don't fire. Returns how many fired (their names added to
    /// <paramref name="names"/>, if given).
    /// </summary>
    int32_t FireAt(const std::shared_ptr<MCMover>& victim, int32_t readyNearest, MCReadiedAt& readiedAt, float rest,
                   const std::set<int32_t>& skip = {}, std::optional<MCComponentForm> form = std::nullopt,
                   MCOptionalRef<std::vector<std::string>> names = std::nullopt)
    {
        const std::shared_ptr<MCTeam> team = victim->Team.lock();
        const MCVector3D at = victim->GetPosition();
        std::vector<std::pair<float, std::shared_ptr<MCMover>>> shooters;

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> shooter = GetMoverFromPartId(partId);
                shooter != nullptr && shooter->Team.lock() != team && shooter->IsDisabled() == 0 &&
                !skip.contains(partId))
            {
                const MCVector3D from = shooter->GetPosition();
                shooters.emplace_back(std::hypot(at.X - from.X, at.Y - from.Y) / WorldUnitsPerMeter, shooter);
            }
        }

        std::ranges::sort(shooters, {}, &std::pair<float, std::shared_ptr<MCMover>>::first);
        int32_t readied = 0;
        int32_t fired = 0;

        for (const auto& [meters, shooter] : shooters)
        {
            const auto last = readiedAt.find(shooter->PartId);
            const bool rested = last == readiedAt.end() || ScenarioTime - last->second >= rest;
            bool ready = false;

            for (int32_t i = 0; i < shooter->NumWeapons && readied < readyNearest && rested; ++i)
            {
                MCInventoryItem& item = shooter->Inventory[shooter->NumOther + i];
                const MCMasterComponent& weapon = MasterComponentList[item.MasterID];

                if (meters >= weapon.WeaponRange[0] && meters <= weapon.WeaponRange[3] && Recycled(item) &&
                    (!form.has_value() || weapon.Form == *form))
                {
                    item.ReadyTime = 0.0f;
                    ready = true;
                }
            }

            if (ready)
            {
                readiedAt[shooter->PartId] = ScenarioTime;
                ++readied;
            }

            for (int32_t i = 0; i < shooter->NumWeapons; ++i)
            {
                if (StagedShot(*shooter, victim, shooter->NumOther + i) == 0)
                {
                    ++fired;

                    if (names)
                    {
                        names->get().push_back(
                            MasterComponentList[shooter->Inventory[shooter->NumOther + i].MasterID].Name);
                    }
                }
            }
        }

        return fired;
    }

    /// <summary>
    /// Between which distances every one of <paramref name="shooter"/>'s weapons reaches (meters): their longest
    /// minimum range and their shortest maximum (these can cross).
    /// </summary>
    std::pair<float, float> WeaponsBand(MCMover& shooter)
    {
        float shortest = 0.0f;
        float longest = std::numeric_limits<float>::max();

        for (int32_t i = 0; i < shooter.NumWeapons; ++i)
        {
            const MCMasterComponent& weapon = MasterComponentList[shooter.Inventory[shooter.NumOther + i].MasterID];
            shortest = std::max(shortest, weapon.WeaponRange[0]);
            longest = std::min(longest, weapon.WeaponRange[3]);
        }

        return {shortest, longest};
    }

    /// <summary>
    /// Where every one of <paramref name="shooter"/>'s weapons reaches (meters): halfway between their longest minimum
    /// range and their shortest maximum.
    /// </summary>
    float WeaponsReach(MCMover& shooter)
    {
        const auto [shortest, longest] = WeaponsBand(shooter);
        return shortest < longest ? (shortest + longest) * 0.5f : longest;
    }

    /// <summary>
    /// <paramref name="target"/> set down on its line from <paramref name="shooter"/> (or, with
    /// <paramref name="moveShooter"/>, the shooter on its line from the target) where every one of the shooter's weapons
    /// reaches it (<see cref="WeaponsReach"/>). Returns that distance (meters).
    /// </summary>
    float BringWithinReach(MCMover& shooter, MCMover& target, bool moveShooter = false)
    {
        const float meters = WeaponsReach(shooter);
        const float reach = meters * WorldUnitsPerMeter;
        MCMover& moved = moveShooter ? shooter : target;
        MCMover& stays = moveShooter ? target : shooter;
        const MCVector3D from = stays.GetPosition();
        const MCVector3D to = moved.GetPosition();
        const float dx = to.X - from.X;
        const float dy = to.Y - from.Y;
        const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        MCVector3D place(from.X + dx / length * reach, from.Y + dy / length * reach, from.Z);
        moved.SetPosition(place);
        return meters;
    }

    /// <summary>
    /// <paramref name="enemy"/> run in along its line to where every weapon of <paramref name="mech"/> reaches it (if
    /// further), on a player's order: a pilot under fire keeps to its alarm orders over its commander's. Returns how far
    /// it is (meters).
    /// </summary>
    float RunInto(MCMover& enemy, MCMover& mech)
    {
        const MCVector3D from = mech.GetPosition();
        const MCVector3D to = enemy.GetPosition();
        const float length = std::max(std::hypot(to.X - from.X, to.Y - from.Y), 1.0f);
        const float reach = std::min(WeaponsReach(mech) * WorldUnitsPerMeter, length);
        WalkTo(enemy,
               MCVector3D(from.X + (to.X - from.X) / length * reach, from.Y + (to.Y - from.Y) / length * reach, to.Z),
               MCOrderOrigin::Player, true);
        return length / WorldUnitsPerMeter;
    }

}

auto MCBattleSpec::Read(const MCLookup& value) -> std::expected<MCBattleSpec, std::string>
{
    MCBattleSpec spec;
    std::string wrong;

    // The first value that isn't what it should be.
    const auto Wrong = [&](std::string_view name, std::string_view given, std::string_view should)
    {
        if (wrong.empty())
        {
            wrong = std::format("{} {}: not {}", name, given, should);
        }
    };

    const auto Number = [&](std::string_view name, int32_t& into)
    {
        if (const std::optional<std::string> given = value(name); given.has_value())
        {
            if (const std::optional<int32_t> number = WholeNumber(*given); number.has_value())
            {
                into = *number;
            }
            else
            {
                Wrong(name, *given, "a whole number");
            }
        }
    };

    // Comma-separated.
    const auto Numbers = [&](std::string_view name, std::vector<int32_t>& into)
    {
        const std::string given = value(name).value_or(std::string());

        for (const auto part : std::views::split(std::string_view(given), ','))
        {
            const std::string_view text(part.begin(), part.end());
            const std::optional<int32_t> number = WholeNumber(text);
            into.push_back(number.value_or(0));

            if (!number.has_value())
            {
                Wrong(name, text, "a whole number");
            }
        }
    };

    // frame:mech[:what], comma-separated; a bare frame names no mech.
    const auto Events = [&](std::string_view name, std::vector<MCBattleEvent>& into)
    {
        const std::string given = value(name).value_or(std::string());

        for (const auto item : std::views::split(std::string_view(given), ','))
        {
            const std::string_view written(item.begin(), item.end());
            MCBattleEvent event;
            size_t field = 0;

            for (const auto part : std::views::split(written, ':'))
            {
                const std::string_view text(part.begin(), part.end());

                if (field >= 2)
                {
                    event.What = text;
                }
                else if (const std::optional<int32_t> number = WholeNumber(text); number.has_value())
                {
                    (field == 0 ? event.Frame : event.Mech) = *number;
                }
                else
                {
                    Wrong(name, written, "frame:mech[:what]");
                }

                ++field;
            }

            into.push_back(std::move(event));
        }
    };

    Number("mission", spec.Mission);
    spec.Force = value("force").value_or(std::string());
    Number("frames", spec.Frames);
    Number("zoom", spec.Zoom);
    Number("jump", spec.JumpEvery);
    Numbers("strike", spec.Strikes);
    Numbers("snow-least", spec.SnowLeast);
    Events("incoming", spec.Incoming);
    Number("walk", spec.Walk);
    Number("contact", spec.Contact);
    Number("contact-step", spec.ContactStep);
    Number("answer", spec.Answer);
    Numbers("kill", spec.Kills);
    Numbers("watch", spec.Watch);
    Number("watch-zoom", spec.WatchZoom);
    Number("zoom-ease", spec.ZoomEase);
    spec.ZoomEase = std::max(spec.ZoomEase, 1);
    Numbers("jump-at", spec.JumpAt);
    Events("camera", spec.Cameras);
    Events("dfa", spec.Dfas);
    Events("mines", spec.Mines);
    Events("alpha", spec.Alphas);
    Events("destroy", spec.Destroys);
    Events("run", spec.Runs);
    Events("close", spec.Closes);
    Events("volley", spec.Volleys);
    Events("loss", spec.Losses);
    Events("smoke", spec.Smokes);
    Events("wear", spec.Wears);
    spec.Converge = value("converge");
    spec.CloseNames = value("close-names");

    if (!wrong.empty())
    {
        return std::unexpected(std::format("the battle's spec: {}", wrong));
    }

    return spec;
}

auto MCBattleSpec::Values(std::string_view text)
    -> std::expected<std::map<std::string, std::string, std::less<>>, std::string>
{
    std::map<std::string, std::string, std::less<>> values;
    int32_t number = 0;

    for (const auto part : std::views::split(text, '\n'))
    {
        ++number;
        std::string_view line(part.begin(), part.end());
        line = line.substr(0, line.find('#'));

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0)
        {
            line.remove_suffix(1);
        }

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0)
        {
            line.remove_prefix(1);
        }

        if (line.empty())
        {
            continue;
        }

        const size_t space = line.find_first_of(" \t");
        const std::string_view name = line.substr(0, space);
        const std::string_view value =
            space == std::string_view::npos ? std::string_view() : line.substr(line.find_first_not_of(" \t", space));

        if (!std::ranges::contains(SpecNames, name))
        {
            return std::unexpected(std::format("line {}: no spec has {}", number, name));
        }

        if (value.empty())
        {
            return std::unexpected(std::format("line {}: {} has no value", number, name));
        }

        if (!values.emplace(name, value).second)
        {
            return std::unexpected(std::format("line {}: {} twice", number, name));
        }
    }

    return values;
}

auto MCBattleSpec::Parse(std::string_view text) -> std::expected<MCBattleSpec, std::string>
{
    const auto values = Values(text);

    if (!values.has_value())
    {
        return std::unexpected(std::format("the battle's spec: {}", values.error()));
    }

    return Read(
        [&](std::string_view name) -> std::optional<std::string>
        {
            const auto found = values->find(name);
            return found != values->end() ? std::optional(found->second) : std::nullopt;
        });
}

auto MCBattleSpec::BuiltIn(std::string_view name) -> std::optional<std::string_view>
{
    return name == "canyon" ? std::optional(CanyonSpec) : std::nullopt;
}

/// <summary>A battle's spec and log, and everything its staging keeps from frame to frame.</summary>
struct MCStagedBattle::MCState
{
    MCState(MCBattleSpec given, MCBattleLog log) : spec(std::move(given)), Log(std::move(log))
    {
        if (!Log)
        {
            Log = [](MCBattleLine, std::string_view) {};
        }

        for (size_t index = 0; index < mines.size(); ++index)
        {
            const auto field = std::ranges::find_if(minefields,
                                                    [&](const std::pair<const size_t, int32_t>& known)
                                                    {
                                                        return mines[known.first].Mech == mines[index].Mech &&
                                                               mines[index].Frame - known.second <= MineWalkFrames;
                                                    });

            if (field != minefields.end())
            {
                field->second = mines[index].Frame;
            }
            else
            {
                minefields[index] = mines[index].Frame;
            }
        }
    }

    void Say(std::string_view line) const { Log(MCBattleLine::Event, line); }

    void SaySecond(std::string_view line) const { Log(MCBattleLine::Second, line); }

    MCBattleSpec spec;
    MCBattleLog Log;
    const int32_t zoom = spec.Zoom;
    const int32_t jumpEvery = spec.JumpEvery;
    const std::vector<int32_t>& strikes = spec.Strikes;
    const std::vector<int32_t>& snowLeast = spec.SnowLeast;
    const std::vector<MCBattleEvent>& incoming = spec.Incoming;
    const int32_t walkFrame = spec.Walk;
    const int32_t contactFrame = spec.Contact;
    const int32_t contactStep = spec.ContactStep;
    const int32_t answerFrame = spec.Answer;
    const std::vector<int32_t>& kills = spec.Kills;
    const std::vector<int32_t>& watch = spec.Watch;
    const int32_t watchZoom = spec.WatchZoom;
    const int32_t zoomEase = spec.ZoomEase;
    const std::vector<int32_t>& jumpFrames = spec.JumpAt;
    const std::vector<MCBattleEvent>& cameras = spec.Cameras;
    const std::vector<MCBattleEvent>& dfas = spec.Dfas;
    const std::vector<MCBattleEvent>& mines = spec.Mines;
    const std::vector<MCBattleEvent>& alphas = spec.Alphas;
    const std::vector<MCBattleEvent>& destroys = spec.Destroys;
    const std::vector<MCBattleEvent>& runs = spec.Runs;
    const std::vector<MCBattleEvent>& closes = spec.Closes;
    const std::vector<MCBattleEvent>& volleys = spec.Volleys;
    const std::vector<MCBattleEvent>& losses = spec.Losses;
    const std::vector<MCBattleEvent>& smokes = spec.Smokes;
    const std::vector<MCBattleEvent>& wears = spec.Wears;
    const std::optional<std::string>& converge = spec.Converge;
    const std::optional<std::string>& closeNames = spec.CloseNames;
    /// <summary>The force as the frame staged last started (Staged answers with it), and whether its target was down.</summary>
    std::vector<std::shared_ptr<MCMover>> stagedLance;
    bool lastDown = false;
    std::shared_ptr<MCMover> target;
    // When each of the player's mechs lost its leg (it fights on where it stands), and how many arms.
    std::map<int32_t, int32_t> crippledAt;
    std::map<int32_t, int32_t> armsLost;
    // The alpha strike's mech and target, the contact coming in and the enemy mech being finished.
    std::shared_ptr<MCMover> alphaShooter;
    std::shared_ptr<MCMover> alphaTarget;
    int32_t alphaFrame = -1;
    // The enemy mech sent in for each alpha strike (by its frame), held standing until then.
    std::map<int32_t, std::shared_ptr<MCMover>> alphaRunners;
    std::shared_ptr<MCMover> contactMech;
    MCVector3D contactInSight;
    float contactEdge = 0.0f;
    std::optional<MCVector3D> watchAt;
    int32_t contactType = 0;
    std::shared_ptr<MCMover> killing;
    // When each of the force last had its weapons readied to answer (--showcase-answer), by part id.
    std::map<int32_t, float> answeredAt;
    // The mech the enemy being finished is near, and when the fire on it started.
    std::shared_ptr<MCMover> killNear;
    int32_t killFrom = -1;
    // The second line the last close brought in behind its mech (CloseIn), and when.
    std::vector<std::shared_ptr<MCMover>> brought;
    int32_t broughtFrame = -1;
    // Every unit's toughness as the frame starts running, those it hit, and the staged damage landed (due, mech, kind).
    std::map<int32_t, float> toughness;
    std::set<int32_t> hit;
    std::set<std::tuple<int32_t, int32_t, char>> landed;
    // When each mech (by part id) last took a staged loss.
    std::map<int32_t, int32_t> lastLanded;
    std::map<int32_t, bool> wasJumping;
    std::set<int32_t> fell;
    MCReadiedAt readiedAt;
    // The health the force's vital parts started with (by part id and inventory index), held with its armor.
    std::map<std::pair<int32_t, size_t>, uint8_t> vitals;
    // The armor the force's mechs really have (by part id, each location's points left and full), worn by the game's
    // hits.
    std::map<int32_t, std::vector<std::pair<float, float>>> worn;
    // The enemy units last sent to wear a mech down (part ids; by wear, its index): staged losses leave their fire be.
    std::set<int32_t> wearing;
    std::map<size_t, std::vector<std::shared_ptr<MCMover>>> wearSent;
    // The units holding their fire for each staged loss (due, mech, kind).
    std::map<std::tuple<int32_t, int32_t, char>, std::vector<std::shared_ptr<MCMover>>> holding;
    // The enemy mechs sent in at each staged loss's mech (FireFor), by loss: the stand-off's orders leave them be.
    std::map<std::tuple<int32_t, int32_t, char>, std::vector<std::shared_ptr<MCMover>>> approaching;
    int32_t fallen = 0;
    int32_t jumps = 0;
    // The fight's balance, each second: shots fired by each side, armor and structure the enemy mechs lost.
    std::map<int32_t, std::vector<float>> readyTimes;
    std::map<int32_t, float> enemyToughness;
    int32_t forceShots = 0;
    int32_t answerShots = 0;
    int32_t enemyShots = 0;
    float enemyLost = 0.0f;

    // Where the force has been, and each mech walking onto its minefield (part id): where from, where to, the way's
    // heading, when it backs out and when it is back.
    std::vector<MCVector3D> trail;
    struct MCMineWalk
    {
        MCVector3D From;
        MCVector3D To;
        float Heading = 0.0f;
        int32_t Back = 0;
        int32_t Until = 0;
    };

    std::map<int32_t, MCMineWalk> mineWalks;
    // Each minefield's first mine (its place in mines) and its last mine's frame: a mech's mines each within
    // MineWalkFrames of the one before.
    std::map<size_t, int32_t> minefields;

    // The mechs running now.
    std::set<int32_t> Running(int32_t frame)
    {
        std::set<int32_t> running;

        for (const MCBattleEvent& run : runs)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(run.Mech);

            if (mech != nullptr && mech->IsDisabled() == 0 && frame >= run.Frame && frame < run.Frame + RunFrames &&
                !crippledAt.contains(mech->PartId))
            {
                running.insert(mech->PartId);
            }
        }

        return running;
    }

    // The enemy mechs in the fight: within FightMeters of one of the force.
    static std::vector<std::shared_ptr<MCMover>> InFight(const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        std::vector<std::shared_ptr<MCMover>> fighting;

        if (lance.empty())
        {
            return fighting;
        }

        for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*lance.front()))
        {
            const MCVector3D to = enemy->GetPosition();

            if (std::ranges::any_of(lance,
                                    [&](const std::shared_ptr<MCMover>& mech)
                                    {
                                        const MCVector3D at = mech->GetPosition();
                                        return std::hypot(to.X - at.X, to.Y - at.Y) < FightMeters * WorldUnitsPerMeter;
                                    }))
            {
                fighting.push_back(enemy);
            }
        }

        return fighting;
    }

    // The mechs walking onto their minefields now.
    std::set<int32_t> MineWalking(int32_t frame)
    {
        std::set<int32_t> walking;

        for (const auto& [partId, walk] : mineWalks)
        {
            if (frame <= walk.Until)
            {
                walking.insert(partId);
            }
        }

        return walking;
    }

    // Where <mech> walks for its minefield, <lead> frames before the first mine (and how far it turns from its enemy,
    // degrees): the way open, level and fresh from MineSlackMeters short of the first mine on, at the first walk with
    // any such way, turned least. None if there is no such way.
    std::optional<std::pair<MCVector3D, float>> FreshGround(MCMover& mech, int32_t lead)
    {
        const float first = MineWalkPace * static_cast<float>(lead) / FramesPerSecond;
        const MCVector3D at = mech.GetPosition();
        const std::shared_ptr<MCMover> enemy = NearestEnemyMech(mech);
        const float toward =
            enemy != nullptr ? std::atan2(enemy->GetPosition().Y - at.Y, enemy->GetPosition().X - at.X) : 0.0f;
        const float level = GameMap().GetTerrainElevation(at);
        const float fresh = FreshMeters * WorldUnitsPerMeter;
        const auto Fresh = [&](const MCVector3D& point)
        {
            return std::ranges::none_of(trail, [&](const MCVector3D& been)
                                        { return std::hypot(been.X - point.X, been.Y - point.Y) < fresh; });
        };

        std::optional<std::pair<MCVector3D, float>> best;

        for (const float share : MineWalkShares)
        {
            const float meters = (first + MineBeyondMeters) * share;
            const float freshFrom = std::max(first - MineSlackMeters, first * MineSlackShare) / meters;

            for (float degrees = -MineTurnDegrees; degrees <= MineTurnDegrees; degrees += MineTurnStepDegrees)
            {
                const float turn = toward + degrees * std::numbers::pi_v<float> / 180.0f;
                const MCVector3D to(at.X + std::cos(turn) * meters * WorldUnitsPerMeter,
                                    at.Y + std::sin(turn) * meters * WorldUnitsPerMeter, at.Z);
                bool clear = true;

                for (float along = freshFrom; along <= 1.0f + WayStep * 0.5f && clear; along += WayStep)
                {
                    const MCVector3D point(at.X + (to.X - at.X) * along, at.Y + (to.Y - at.Y) * along, at.Z);
                    clear = OpenAndLevel(point, level) && Fresh(point);
                }

                if (clear && (!best.has_value() || std::abs(degrees) < std::abs(best->second)))
                {
                    best = std::pair(to, degrees);
                }
            }

            if (best.has_value())
            {
                break;
            }
        }

        return best;
    }

    // The lance (the first three) as SendLanceAt sends it (selected, as a player would), then each of the player's mechs
    // at its own enemy mech of those in the fight, to LineMeters from it (Engage; SpreadTargets: the force's fire spread
    // along their line); at the fight's start (spread false), or with none in the fight, all at the one given. Those
    // running run on; those walking onto a minefield walk on.
    void SendAllAt(const std::shared_ptr<MCMover>& at, int32_t frame, bool spread)
    {
        SendLanceAt(at);
        const std::set<int32_t> running = Running(frame);
        const std::set<int32_t> mining = MineWalking(frame);
        std::vector<std::shared_ptr<MCMover>> attackers;

        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            if (running.contains(mover->PartId))
            {
                RetreatFrom(*mover, RetreatDistance);
            }
            else if (!mining.contains(mover->PartId))
            {
                attackers.push_back(mover);
            }
        }

        const std::vector<std::shared_ptr<MCMover>> targets =
            SpreadTargets(attackers, spread ? InFight(attackers) : std::vector<std::shared_ptr<MCMover>>());

        for (size_t i = 0; i < attackers.size(); ++i)
        {
            Engage(*attackers[i], targets[i] != nullptr ? targets[i] : at, LineMeters, MCOrderOrigin::Player);
        }
    }

    // Before --showcase-walk's frame the force walks at the target instead: nothing is in sight yet.
    void WalkAllAt(const std::shared_ptr<MCMover>& at)
    {
        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            WalkTo(*mover, at->GetPosition());
        }
    }

    // The force's mech nearest a point, and how far it is (world units).
    static std::pair<std::shared_ptr<MCMover>, float> NearestOfForce(const std::vector<std::shared_ptr<MCMover>>& lance,
                                                                     const MCVector3D& point)
    {
        std::pair<std::shared_ptr<MCMover>, float> nearest{nullptr, std::numeric_limits<float>::max()};

        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            const MCVector3D mech = mover->GetPosition();

            if (const float away = std::hypot(mech.X - point.X, mech.Y - point.Y); away < nearest.second)
            {
                nearest = {mover, away};
            }
        }

        return nearest;
    }

    // From --showcase-contact's frame until the walk's end the force stands.
    void HaltAll()
    {
        for (const std::shared_ptr<MCMover>& mover : LiveLance())
        {
            MCTacticalOrder order;
            order.Reset(MCOrderOrigin::Player, MCTacticalOrderCode::Stop, 0);
            mover->HandleTacticalOrder(order, 1, 0);
        }
    }

    // The farthest the force's sensors reach standing (meters; moving cuts it).
    static float Sensors(const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        float sensors = 0.0f;

        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            if (const std::shared_ptr<MCSensorSystem> sensor = mover->SensorSystem.lock(); sensor != nullptr)
            {
                sensors = std::max(sensors, sensor->Range * sensor->Multiplier);
            }
        }

        return sensors;
    }

    // A mech held standing this frame: its armor (at <paramref name="share"/> of its full, raised to
    // <paramref name="full"/> first if given), the structure of what it still has, its vital parts (engine, gyro,
    // cockpit, life support: in its torso and head) and its pilot.
    void Hold(MCMover& mover, std::optional<uint8_t> full = std::nullopt, float share = 1.0f)
    {
        for (MCArmorLocation& armor : mover.Armor)
        {
            armor.MaxArmor = full.value_or(armor.MaxArmor);
            armor.CurArmor = armor.MaxArmor * share;
        }

        for (MCBodyLocation& body : mover.Body)
        {
            body.CurInternalStructure =
                body.CurInternalStructure > 0.0f ? body.MaxInternalStructure : body.CurInternalStructure;
        }

        for (size_t i = 0; i < mover.Inventory.size(); ++i)
        {
            MCInventoryItem& item = mover.Inventory[i];

            if (const MCComponentForm form = MasterComponentList[item.MasterID].Form;
                form == MCComponentForm::Engine || form == MCComponentForm::Gyroscope ||
                form == MCComponentForm::Cockpit || form == MCComponentForm::LifeSupport)
            {
                item.Health = vitals.try_emplace({mover.PartId, i}, item.Health).first->second;
            }
        }

        if (const std::shared_ptr<MCMechWarrior> pilot = mover.GetPilot(); pilot != nullptr)
        {
            pilot->Wounds = 0.0f;
        }
    }

    // A unit's fire held this frame: a weapon ready is put off a little (more than the frame the mission's clock moves
    // on before the unit fires), so it fires with the rest when let go.
    static void HoldFire(MCMover& mover)
    {
        constexpr float HeldSeconds = 0.3f;

        for (int32_t i = 0; i < mover.NumWeapons; ++i)
        {
            float& ready = mover.Inventory[mover.NumOther + i].ReadyTime;
            ready = std::max(ready, ScenarioTime + HeldSeconds);
        }
    }

    // The enemy mechs running in for an alpha strike still to come (part ids): others' orders leave them be.
    std::set<int32_t> Runners(int32_t frame)
    {
        std::set<int32_t> runners;

        for (const auto& [strike, runner] : alphaRunners)
        {
            if (strike > frame && runner != nullptr)
            {
                runners.insert(runner->PartId);
            }
        }

        return runners;
    }

    // When a strike's mech starts holding its fire: AlphaHoldFrames before, from the end of its last strike at the
    // earliest.
    int32_t AlphaHoldFrom(const MCBattleEvent& alpha)
    {
        int32_t holdFrom = alpha.Frame - AlphaHoldFrames;

        for (const MCBattleEvent& before : alphas)
        {
            holdFrom = before.Mech == alpha.Mech && before.Frame < alpha.Frame
                           ? std::max(holdFrom, before.Frame + AlphaFrames)
                           : holdFrom;
        }

        return holdFrom;
    }

    // The mechs holding their fire for a strike this frame (part ids): other staged fire leaves their weapons be, so
    // they have them all for it.
    std::set<int32_t> Holding(int32_t frame)
    {
        std::set<int32_t> holding;

        for (const MCBattleEvent& alpha : alphas)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(alpha.Mech);
                mech != nullptr && frame >= AlphaHoldFrom(alpha) && frame < alpha.Frame)
            {
                holding.insert(mech->PartId);
            }
        }

        return holding;
    }

    // When a wear (--showcase-wear) stops: its frames on, if given.
    static int32_t WearUntil(const MCBattleEvent& wear)
    {
        int32_t frames = std::numeric_limits<int32_t>::max() - wear.Frame;
        std::from_chars(wear.What.data(), wear.What.data() + wear.What.size(), frames);
        return wear.Frame + frames;
    }

    // The bar a worn mech is to be down to by a frame (see WearShooters): full at the wear's start, WearEndBar at its
    // end (an endless wear's stays full).
    float WearBar(const MCBattleEvent& wear, int32_t frame)
    {
        const int64_t span = std::max<int64_t>(static_cast<int64_t>(WearUntil(wear)) - wear.Frame, 1);
        const float progress =
            std::clamp(static_cast<float>(frame - wear.Frame) / static_cast<float>(span), 0.0f, 1.0f);
        return 1.0f - (1.0f - WearEndBar) * progress;
    }

    // A mech of the force held this frame (see ArmorHeld): what the game's hits took off its raised armor since the
    // last frame comes off the armor it really has, down to WornFloor of a location's (all of it once the location is
    // gone), and its raised armor is held at the share left.
    void HoldWorn(MCMover& mover)
    {
        const auto [entry, first] = worn.try_emplace(mover.PartId);
        std::vector<std::pair<float, float>>& armor = entry->second;

        for (size_t i = 0; first && i < mover.Armor.size(); ++i)
        {
            armor.emplace_back(mover.Armor[i].CurArmor, static_cast<float>(mover.Armor[i].MaxArmor));
        }

        const auto Share = [](const std::pair<float, float>& location)
        { return location.second > 0.0f ? location.first / location.second : 1.0f; };

        for (size_t i = 0; !first && i < armor.size(); ++i)
        {
            const float held = static_cast<float>(ArmorHeld) * Share(armor[i]);
            const float now = mover.Armor[i].CurArmor;
            armor[i].first = now <= 0.0f ? 0.0f
                                         : std::max(armor[i].first - std::max(held - now, 0.0f),
                                                    std::min(armor[i].first, armor[i].second * WornFloor));
        }

        Hold(mover, ArmorHeld);

        for (size_t i = 0; i < armor.size(); ++i)
        {
            mover.Armor[i].CurArmor = static_cast<float>(ArmorHeld) * Share(armor[i]);
        }
    }

    // A mech HoldWorn held let go: its armor back to what it really has and its full, so the hits after take that.
    void Release(MCMover& mover)
    {
        const auto held = worn.find(mover.PartId);

        if (held == worn.end())
        {
            return;
        }

        for (size_t i = 0; i < held->second.size() && i < mover.Armor.size(); ++i)
        {
            mover.Armor[i].MaxArmor = static_cast<uint8_t>(held->second[i].second);
            mover.Armor[i].CurArmor = held->second[i].first;
        }

        worn.erase(held);
    }

    // Whether staged damage of a kind due at a frame lands on its mech this frame: on a hit from HitWindowFrames
    // before (but not within LossGapFrames of the mech's last loss: two in one blast read as one), else HitGraceFrames
    // after regardless; once.
    bool Lands(MCMover& victim, int32_t frame, int32_t due, int32_t mech, char kind, std::string_view what)
    {
        if (due < 0 || frame < due - HitWindowFrames || frame > due + HitGraceFrames ||
            landed.contains({due, mech, kind}))
        {
            return false;
        }

        const auto last = lastLanded.find(victim.PartId);
        const bool apart = last == lastLanded.end() || frame >= last->second + LossGapFrames;
        const bool struck = hit.contains(victim.PartId);

        if ((!struck || !apart) && frame < due + HitGraceFrames)
        {
            return false;
        }

        landed.insert({due, mech, kind});
        lastLanded[victim.PartId] = kind == 'h' ? lastLanded[victim.PartId] : frame;
        Say(std::format("frame {}: {} (part {:#x}) {} at bar {:.2f} ({})", frame, victim.GetIfaceName(), victim.PartId,
                        what, victim.GetTotalEffectiveness(), struck ? "on a hit" : "no hit came"));
        return true;
    }

    // How long before staged damage of a kind is due its fire starts, and its shooters are sent at its mech (FireFor).
    static int32_t LeadFor(char kind) { return kind == 'd' ? DieLeadFrames : FireLeadFrames; }
    int32_t ApproachFor(char kind) { return LeadFor(kind) + ApproachFrames - FireLeadFrames; }

    // The units held for staged damage still to land (part ids; see FireFor): wearing fire leaves them be, so they have
    // their weapons for it.
    std::set<int32_t> HeldForLosses(int32_t frame)
    {
        std::set<int32_t> held;

        for (const auto& [loss, shooters] : holding)
        {
            if (const int32_t due = std::get<0>(loss);
                frame > due - ApproachFor(std::get<2>(loss)) && frame <= due + HitGraceFrames && !landed.contains(loss))
            {
                for (const std::shared_ptr<MCMover>& shooter : shooters)
                {
                    held.insert(shooter->PartId);
                }
            }
        }

        return held;
    }

    // The enemy mechs sent in to finish a staged loss's mech still to land (part ids): the stand-off's re-orders
    // (Converge, CloseIn) leave them closing in.
    std::set<int32_t> Finishing(int32_t frame)
    {
        std::set<int32_t> finishing;

        for (const auto& [loss, enemies] : approaching)
        {
            if (const int32_t due = std::get<0>(loss);
                frame > due - ApproachFor(std::get<2>(loss)) && frame <= due + HitGraceFrames && !landed.contains(loss))
            {
                for (const std::shared_ptr<MCMover>& enemy : enemies)
                {
                    finishing.insert(enemy->PartId);
                }
            }
        }

        return finishing;
    }

    // Its mech fired at for staged damage, from its lead (LeadFor) before it is due until it lands, the nearly recycled
    // weapons that reach it of the units nearest it readied every LossVolleyFrames: the ApproachingMechs enemy mechs
    // nearest it are sent at it before that (ApproachFor; the nearest set down within its reach if it is further and
    // neither is on screen), and the ReadiedShooters units nearest it hold their fire from then until the fire starts,
    // so they have weapons recycled for it (but those wearing a mech down: their fire lands hits anyway).
    void FireFor(const std::shared_ptr<MCMover>& victim, int32_t frame, int32_t due, int32_t mech, char kind)
    {
        if (due < 0 || landed.contains({due, mech, kind}))
        {
            return;
        }

        const int32_t lead = LeadFor(kind);
        const int32_t approach = ApproachFor(kind);

        if (frame == due - approach)
        {
            const std::vector<std::shared_ptr<MCMover>> nearest = EnemyMechsByDistance(*victim);

            // The nearest not where all its weapons reach: set down there, unless either is on screen.
            if (!nearest.empty())
            {
                const MCVector3D at = victim->GetPosition();
                const MCVector3D from = nearest.front()->GetPosition();

                if (std::hypot(at.X - from.X, at.Y - from.Y) > WeaponsReach(*nearest.front()) * WorldUnitsPerMeter &&
                    !EitherOnScreen(*nearest.front(), *victim))
                {
                    Say(std::format("frame {}: {} brought {:.0f} m from {}", frame, nearest.front()->GetIfaceName(),
                                    BringWithinReach(*nearest.front(), *victim, true), victim->GetIfaceName()));
                }
            }

            for (const std::shared_ptr<MCMover>& enemy : nearest | std::views::take(ApproachingMechs))
            {
                OrderAttack(*enemy, victim, MCOrderOrigin::Commander);
                approaching[{due, mech, kind}].push_back(enemy);
            }

            std::ranges::copy(EnemyMechsByDistance(*victim, true) | std::views::take(ReadiedShooters),
                              std::back_inserter(holding[{due, mech, kind}]));
        }

        // Those held hold their fire until the last FireLeadFrames (a death's earlier fire is the others').
        for (const std::shared_ptr<MCMover>& shooter : holding[{due, mech, kind}])
        {
            if (frame > due - approach && frame < due - FireLeadFrames && shooter->IsDisabled() == 0 &&
                !wearing.contains(shooter->PartId))
            {
                HoldFire(*shooter);
            }
        }

        if (frame < due - lead || frame > due + HitGraceFrames)
        {
            return;
        }

        const bool volley = (frame - (due - lead)) % LossVolleyFrames == 0;
        std::set<int32_t> skip = Holding(frame);

        if (frame < due - FireLeadFrames)
        {
            for (const std::shared_ptr<MCMover>& shooter : holding[{due, mech, kind}])
            {
                skip.insert(shooter->PartId);
            }
        }

        std::vector<std::string> names;
        int32_t fired =
            FireAt(victim, volley ? ReadiedShooters : 0, readiedAt, ShooterRestSeconds, skip, std::nullopt, names);

        // None that has rested reaches it: those that do, readied again sooner (their weapons still nearly recycled).
        if (volley && fired == 0)
        {
            fired = FireAt(victim, ReadiedShooters, readiedAt, 0.0f, skip, std::nullopt, names);
        }

        if (volley)
        {
            const std::vector<std::shared_ptr<MCMover>> nearest = EnemyMechsByDistance(*victim);
            const MCVector3D at = victim->GetPosition();
            const MCVector3D from = nearest.empty() ? at : nearest.front()->GetPosition();
            std::string fire;

            for (const std::string& name : names)
            {
                fire += std::format("{}{}", fire.empty() ? "" : ", ", name);
            }

            Say(std::format("frame {}: {} weapons fire at {} (part {:#x}), the nearest enemy mech {:.0f} m off ({})",
                            frame, fired, victim->GetIfaceName(), victim->PartId,
                            std::hypot(at.X - from.X, at.Y - from.Y) / WorldUnitsPerMeter, fire));
        }
    }

    // When the camera came to the mech it follows: the last --showcase-camera's frame (0 before any).
    int32_t CameraSince(int32_t frame)
    {
        int32_t since = 0;

        for (const MCBattleEvent& camera : cameras)
        {
            since = camera.Frame <= frame && ForceMech(camera.Mech) != nullptr ? camera.Frame : since;
        }

        return since;
    }

    // The mech the camera follows: the last --showcase-camera's from before this frame (fallen or not), else the
    // first still fighting.
    std::shared_ptr<MCMover> CameraMech(int32_t frame, const std::vector<std::shared_ptr<MCMover>>& lance)
    {
        std::shared_ptr<MCMover> followed = lance.empty() ? nullptr : lance.front();

        for (const MCBattleEvent& camera : cameras)
        {
            if (camera.Frame <= frame && ForceMech(camera.Mech) != nullptr)
            {
                followed = ForceMech(camera.Mech);
            }
        }

        return followed;
    }

    /// <summary>The last close (spec close) begun by <paramref name="frame"/>, if any.</summary>
    MCOptionalRef<const MCBattleEvent> LastClose(int32_t frame) const
    {
        MCOptionalRef<const MCBattleEvent> close;

        for (const MCBattleEvent& event : closes)
        {
            if (event.Frame <= frame)
            {
                close = event;
            }
        }

        return close;
    }

    /// <summary>The mech a close closes in on: its own while it stands, else the one the camera follows.</summary>
    static std::shared_ptr<MCMover> CloseOn(MCOptionalRef<const MCBattleEvent> close,
                                            const std::shared_ptr<MCMover>& followed)
    {
        const std::shared_ptr<MCMover> closeOn = close.has_value() ? ForceMech(close->get().Mech) : nullptr;
        return closeOn != nullptr && closeOn->IsDisabled() == 0 ? closeOn : followed;
    }

    std::expected<void, std::string> Begin()
    {
        // A force spec'd is the trailer's company of ten (mission/MCTrailerMission.h): without it, the mission's own.
        constexpr int32_t TrailerCompany = 10;

        if (!spec.Force.empty() && ForceMech(TrailerCompany - 1) == nullptr)
        {
            return std::unexpected(std::format("the mission's force isn't the trailer's ({} wasn't made)", spec.Force));
        }

        // Closer than a player's closest zoom (480 lines, 2.25 window pixels a game pixel at 1080) stands for a taller
        // screen's closest: 360 lines is a 1440-line screen's.
        if (zoom > 0)
        {
            constexpr int32_t ClosestPlayerZoom = 480;
            MCFixedZoomHeight = zoom < ClosestPlayerZoom ? static_cast<float>(zoom) : 0.0f;

            if (MCMainView() == nullptr)
            {
                return std::unexpected("the battle's view isn't up");
            }

            MCMainView()->ZoomTo(static_cast<float>(zoom));
            Say(std::format("zoom: {:.0f} lines", MCMainView()->ZoomTarget));
        }

        // Every mech of the player's, with its pilot and the pilot's voice.
        for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + PlayerMechs; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->IsDisabled() != 0)
            {
                continue;
            }

            const std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
            Say(std::format("player: {} (part {:#x}), pilot {} voice {} video {}, jump range {:.0f}, fire ranges "
                            "{:.0f}/{:.0f}/{:.0f}/{:.0f} m",
                            mover->GetIfaceName(), partId, pilot != nullptr ? pilot->Callsign : "-",
                            pilot != nullptr ? pilot->AudioStr : "-", pilot != nullptr ? pilot->VideoStr : "-",
                            mover->CanJump() != 0 ? mover->GetJumpRange() : 0.0f, mover->GetFireRange(0),
                            mover->GetFireRange(1), mover->GetFireRange(2), mover->GetFireRange(-2)));
        }

        // The other sides' units by name, to choose a mission.
        if (const std::vector<std::shared_ptr<MCMover>> lance = LiveLance(); !lance.empty())
        {
            const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
            std::map<std::string, int32_t> enemies;
            std::map<std::string, std::string> ranges;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                    mover != nullptr && mover->Team.lock() != team && mover->IsDisabled() == 0)
                {
                    ++enemies[std::string(mover->GetIfaceName())];
                    ranges[std::string(mover->GetIfaceName())] =
                        std::format("{:.0f}/{:.0f}/{:.0f}/{:.0f} m", mover->GetFireRange(0), mover->GetFireRange(1),
                                    mover->GetFireRange(2), mover->GetFireRange(-2));
                }
            }

            for (const auto& [name, count] : enemies)
            {
                Say(std::format("enemy: {} x{}, fire ranges {}", name, count, ranges[name]));
            }
        }

        return {};
    }

    void Stage(int32_t frame)
    {
        if (!snowLeast.empty() && frame == snowLeast.front())
        {
            MCWeatherPacing::Hold(MCWeatherPace::LeastShare);
        }
        else if (snowLeast.size() > 1 && frame == snowLeast[1])
        {
            MCWeatherPacing::Hold(1.0f);
        }

        const bool down = target != nullptr && target->IsDisabled() != 0;
        fallen += down ? 1 : 0;
        const std::vector<std::shared_ptr<MCMover>> lance = LiveLance();
        const bool walking = frame < walkFrame;
        const std::shared_ptr<MCMover> followed = CameraMech(frame, lance);
        const std::set<int32_t> running = Running(frame);

        // Those the last frame hit.
        hit.clear();

        for (const auto& [partId, before] : toughness)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && Toughness(*mover) < before)
            {
                hit.insert(partId);
            }
        }

        // The staged losses, each on a hit (each hit on a mech with one to come printed, once a frame).
        std::set<int32_t> hitShown;

        for (const MCBattleEvent& loss : losses)
        {
            const std::shared_ptr<MCMover> mover = ForceMech(loss.Mech);

            if (mover == nullptr || mover->IsDisabled() != 0 || mover->ObjectClass != MCObjectClass::BattleMech)
            {
                continue;
            }

            auto& mech = static_cast<MCBattleMech&>(*mover);
            const char kind = loss.What.empty() ? 'd' : loss.What.front();

            if (hit.contains(mech.PartId) && frame >= loss.Frame - LeadFor(kind) &&
                frame <= loss.Frame + HitGraceFrames && !landed.contains({loss.Frame, loss.Mech, kind}) &&
                hitShown.insert(mech.PartId).second)
            {
                Say(std::format("frame {}: {} (part {:#x}) hit", frame, mech.GetIfaceName(), mech.PartId));
            }

            FireFor(mover, frame, loss.Frame, loss.Mech, kind);

            if (loss.What == "leg" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "loses a leg"))
            {
                mech.DestroyBodyLocation(MechLeftLeg);
                crippledAt[mech.PartId] = frame;
            }
            else if (loss.What == "arm" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "loses an arm"))
            {
                mech.DestroyBodyLocation(armsLost[mech.PartId]++ == 0 ? MechLeftArm : MechRightArm);
            }
            else if (loss.What == "eject" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "ejects"))
            {
                mech.HandleEjection();
            }
            else if (loss.What == "die" && Lands(mech, frame, loss.Frame, loss.Mech, kind, "blown apart"))
            {
                mech.DestroyBodyLocation(MechCenterTorso);
            }
            else if (loss.What == "hit")
            {
                Lands(mech, frame, loss.Frame, loss.Mech, kind, "under fire");
            }
        }

        // Those being worn down: each one's enemy mechs kept on it, the nearest not on another filling its empty places
        // (the one to stop first first), every WearFrames.
        if (frame % WearFrames == 0)
        {
            std::set<int32_t> sent = Runners(frame);
            std::vector<std::pair<int32_t, size_t>> worns;

            for (size_t i = 0; i < wears.size(); ++i)
            {
                const std::shared_ptr<MCMover> mech = ForceMech(wears[i].Mech);
                std::vector<std::shared_ptr<MCMover>>& shooters = wearSent[i];

                if (mech == nullptr || mech->IsDisabled() != 0 || frame < wears[i].Frame ||
                    frame >= WearUntil(wears[i]))
                {
                    shooters.clear();
                    continue;
                }

                std::erase_if(shooters, [](const std::shared_ptr<MCMover>& enemy) { return enemy->IsDisabled() != 0; });
                worns.emplace_back(WearUntil(wears[i]), i);

                for (const std::shared_ptr<MCMover>& enemy : shooters)
                {
                    sent.insert(enemy->PartId);
                }
            }

            std::ranges::sort(worns);
            wearing.clear();

            for (const auto& [until, i] : worns)
            {
                const std::shared_ptr<MCMover> mech = ForceMech(wears[i].Mech);
                std::vector<std::shared_ptr<MCMover>>& shooters = wearSent[i];

                for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*mech))
                {
                    if (shooters.size() < WearShooters && sent.insert(enemy->PartId).second)
                    {
                        shooters.push_back(enemy);
                    }
                }

                for (const std::shared_ptr<MCMover>& enemy : shooters)
                {
                    OrderAttack(*enemy, mech, MCOrderOrigin::Commander);
                    wearing.insert(enemy->PartId);
                }

                if (frame < wears[i].Frame + WearFrames)
                {
                    Say(std::format("frame {}: {} worn down by {} enemy mechs", frame, mech->GetIfaceName(),
                                    shooters.size()));
                }
            }
        }

        // While a worn mech's bar is above its wear's (WearBar): its enemy mechs set down within reach once neither is
        // on screen, and what reaches it firing at it; its own shooters hold their fire between (their own shots,
        // blocked or missing, would leave them nothing recycled for it).
        for (size_t i = 0; i < wears.size(); ++i)
        {
            const MCBattleEvent& wear = wears[i];
            const std::shared_ptr<MCMover> mech = ForceMech(wear.Mech);

            if (mech == nullptr || mech->IsDisabled() != 0 || frame < wear.Frame || frame >= WearUntil(wear) ||
                mech->GetTotalEffectiveness() <= WearBar(wear, frame))
            {
                continue;
            }

            const MCVector3D at = mech->GetPosition();

            for (const std::shared_ptr<MCMover>& enemy : wearSent[i])
            {
                HoldFire(*enemy);
                const MCVector3D from = enemy->GetPosition();

                if (std::hypot(at.X - from.X, at.Y - from.Y) > WeaponsReach(*enemy) * WorldUnitsPerMeter * WearSlack &&
                    !EitherOnScreen(*enemy, *mech))
                {
                    Say(std::format("frame {}: {} brought {:.0f} m from {}", frame, enemy->GetIfaceName(),
                                    BringWithinReach(*enemy, *mech, true), mech->GetIfaceName()));
                }
            }

            if (frame >= wear.Frame + WearLeadFrames && (frame - wear.Frame) % LossVolleyFrames == 0)
            {
                if (const int32_t fired =
                        FireAt(mech, WearReadied, readiedAt, ShooterRestSeconds, HeldForLosses(frame));
                    fired > 0)
                {
                    Say(std::format("frame {}: {} weapons wear {} down", frame, fired, mech->GetIfaceName()));
                }
            }
        }

        // A mech without its leg turns on the nearest enemy mech.
        for (const auto& [partId, at] : crippledAt)
        {
            const std::shared_ptr<MCMover> mech = GetMoverFromPartId(partId);

            if (frame == at + 1 && mech != nullptr && mech->IsDisabled() == 0)
            {
                if (const std::shared_ptr<MCMover> enemy = NearestEnemyMech(*mech); enemy != nullptr)
                {
                    OrderAttack(*mech, enemy);
                }
            }
        }

        // Those running, ordered on every second.
        for (const MCBattleEvent& run : runs)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(run.Mech);
                mech != nullptr && running.contains(mech->PartId) && (frame - run.Frame) % RetreatFrames == 0)
            {
                RetreatFrom(*mech, RetreatDistance);

                if (frame == run.Frame)
                {
                    Say(std::format("frame {}: {} runs", frame, mech->GetIfaceName()));
                }
            }
        }

        // The last close's mech, those named closing in on it (on the camera's once it falls).
        const MCOptionalRef<const MCBattleEvent> close = LastClose(frame);
        const std::shared_ptr<MCMover> closeOn = CloseOn(close, followed);
        const bool closing =
            close.has_value() && closeNames.has_value() && closeOn != nullptr && closeOn->IsDisabled() == 0;

        if (closing && (frame - close->get().Frame) % CloseFrames == 0)
        {
            // Those wearing a mech down stay on it.
            std::set<int32_t> busy = Runners(frame);
            busy.merge(Finishing(frame));
            busy.insert(wearing.begin(), wearing.end());
            // Its what: how many to bring and, after an @, how far behind (meters): n or n@meters.
            const size_t split = close->get().What.find('@');
            const int32_t bring = close->get().What.empty() ? Brought : std::stoi(close->get().What.substr(0, split));
            const float behind =
                split == std::string::npos ? BehindMeters : std::strtof(close->get().What.c_str() + split + 1, nullptr);

            if (frame == close->get().Frame && bring > 0)
            {
                brought.clear();
                broughtFrame = frame;
            }

            const int32_t closed = CloseIn(Log, *closeNames, closeOn, frame == close->get().Frame ? bring : 0, busy,
                                           behind, brought, CameraSince(frame) == frame);

            if (frame == close->get().Frame)
            {
                Say(std::format("frame {}: {} enemies close in on {}, {} of the player's mechs left", frame, closed,
                                closeOn->GetIfaceName(), lance.size()));
            }
        }

        for (const MCBattleEvent& volley : volleys)
        {
            // The weapons of those near it readied at once when it starts, so they fire together (missiles can't
            // fire inside their minimum range, so not only those).
            if (const std::shared_ptr<MCMover> mech = ForceMech(volley.Mech);
                mech != nullptr && mech->IsDisabled() == 0 && frame >= volley.Frame &&
                frame < volley.Frame + VolleyFrames)
            {
                if (const int32_t fired = FireAt(mech, frame == volley.Frame ? VolleyShooters : 0, readiedAt, 0.0f);
                    fired > 0)
                {
                    Say(std::format("frame {}: {} enemy weapons fire at {}", frame, fired, mech->GetIfaceName()));
                }
            }
        }

        // With losses staged, the force holds until its staged end: the hits show, none falls before.
        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            if (losses.empty())
            {
                break;
            }

            HoldWorn(*mover);
        }

        // With the force answering, the other side's mechs hold the same way: the force's hits show on their bars, but
        // none falls before a strike or a kill stages its end.
        if (answerFrame >= 0 && frame >= answerFrame && !lance.empty())
        {
            for (const std::shared_ptr<MCMover>& enemy : EnemyMechsByDistance(*lance.front()))
            {
                HoldWorn(*enemy);
            }
        }

        const bool retarget = frame % RetargetFrames == 0 || (walkFrame > 0 && frame == walkFrame);

        if (target == nullptr || down || retarget)
        {
            if (std::shared_ptr<MCMover> next = NearestEnemy(); next != nullptr)
            {
                // The fight starts on the contact.
                const bool contactFirst =
                    frame == walkFrame && contactMech != nullptr && contactMech->IsDisabled() == 0;
                target = contactFirst ? contactMech : std::move(next);

                // Halted from just before the contact until the fight.
                const bool halted = contactFrame >= 0 && frame >= contactFrame - HaltLeadFrames;

                if (!walking)
                {
                    SendAllAt(target, frame, !contactFirst);

                    // A mech holding its fire for a strike stands, then keeps its weapons' reach of the strike's target
                    // once that one runs in (every weapon fires there: none inside its minimum range).
                    for (const MCBattleEvent& alpha : alphas)
                    {
                        const std::shared_ptr<MCMover> striker = ForceMech(alpha.Mech);

                        if (striker == nullptr || striker->IsDisabled() != 0 || frame < AlphaHoldFrom(alpha) ||
                            frame >= alpha.Frame)
                        {
                            continue;
                        }

                        if (const auto runner = alphaRunners.find(alpha.Frame); runner != alphaRunners.end() &&
                                                                                runner->second != nullptr &&
                                                                                runner->second->IsDisabled() == 0)
                        {
                            Engage(*striker, runner->second, WeaponsReach(*striker), MCOrderOrigin::Player);
                        }
                        else
                        {
                            WalkTo(*striker, striker->GetPosition());
                        }
                    }
                }
                else if (!halted)
                {
                    WalkAllAt(target);
                }
            }
        }

        if (contactFrame >= 0 && frame == contactFrame - HaltLeadFrames)
        {
            HaltAll();
        }

        // The contact: the nearest enemy mech of those converging set down ahead of the first mech, on the way to the
        // target, on its sensors and out of the force's sight, walking in to stand off; the force halts.
        if (frame == contactFrame && target != nullptr && !lance.empty())
        {
            MCMover& lead = *lance.front();
            contactMech = NearestEnemyMech(lead, false, converge.value_or(std::string()));

            if (contactMech != nullptr)
            {
                const MCVector3D at = lead.GetPosition();
                const MCVector3D to = target->GetPosition();
                const float dx = to.X - at.X;
                const float dy = to.Y - at.Y;
                const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
                // Sensor ranges are meters, positions world units.
                const float sensors = Sensors(lance) * WorldUnitsPerMeter;
                const auto Ahead = [&](float reach)
                { return MCVector3D(at.X + dx / length * reach, at.Y + dy / length * reach, at.Z); };
                // The fog's edge, and the nearest spot along the line that far from the force.
                const std::shared_ptr<MCTeam> team = lead.Team.lock();
                float edge = 0.0f;

                while (team->LineOfSight(Ahead(edge)) != 0 && edge < sensors)
                {
                    edge += ContactTry;
                }

                contactEdge = NearestOfForce(lance, Ahead(edge)).second;
                const auto Where = [&](float away)
                {
                    float reach = 0.0f;

                    while (NearestOfForce(lance, Ahead(reach)).second < away && reach < sensors * 2.0f)
                    {
                        reach += ContactTry;
                    }

                    return Ahead(reach);
                };

                const MCVector3D stand = Where(contactEdge * ContactOutside);
                MCVector3D place = Where(std::min(contactEdge * ContactFrom, sensors));
                contactMech->SetPosition(place);
                WalkTo(*contactMech, stand, MCOrderOrigin::Commander);
                contactInSight = Where(contactEdge * ContactStop);
                const MCVector3D front = NearestOfForce(lance, stand).first->GetPosition();
                watchAt = MCVector3D(front.X + (stand.X - front.X) * WatchShare,
                                     front.Y + (stand.Y - front.Y) * WatchShare, front.Z);
                Say(std::format("frame {}: contact {} (part {:#x}) {:.0f} m from the force, to stand {:.0f} m "
                                "from it (it sees {:.0f} m, its sensors reach {:.0f} m)",
                                frame, contactMech->GetIfaceName(), contactMech->PartId,
                                NearestOfForce(lance, place).second / WorldUnitsPerMeter,
                                NearestOfForce(lance, stand).second / WorldUnitsPerMeter,
                                contactEdge / WorldUnitsPerMeter, sensors / WorldUnitsPerMeter));
            }
        }

        // It steps into sight before the fight.
        if (contactMech != nullptr && contactMech->IsDisabled() == 0 && frame == walkFrame - contactStep)
        {
            WalkTo(*contactMech, contactInSight, MCOrderOrigin::Commander);
        }

        // How the force has the contact, when that changes: on its sensors (a blip), in sight.
        if (contactMech != nullptr && !lance.empty())
        {
            if (const int32_t type = lance.front()->Team.lock()->GetContactType(*contactMech); type != contactType)
            {
                contactType = type;
                Say(std::format("frame {}: the contact is {}, {:.0f} m from the force", frame,
                                type == 1   ? "in sight"
                                : type == 2 ? "on the sensors"
                                            : "unseen",
                                NearestOfForce(lance, contactMech->GetPosition()).second / WorldUnitsPerMeter));
            }
        }

        // Until then the rest of the other side's units are set back off the force's sensors.
        if (walking && contactFrame >= 0 && !lance.empty())
        {
            const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
            const float clear = Sensors(lance) * WorldUnitsPerMeter * SensorClear;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                if (enemy == nullptr || enemy == contactMech || enemy->Team.lock() == team || enemy->IsDisabled() != 0)
                {
                    continue;
                }

                const MCVector3D to = enemy->GetPosition();

                if (const auto [mech, away] = NearestOfForce(lance, to); away < clear)
                {
                    const MCVector3D at = mech->GetPosition();
                    const float scale = clear / std::max(away, 1.0f);
                    MCVector3D place(at.X + (to.X - at.X) * scale, at.Y + (to.Y - at.Y) * scale, to.Z);
                    enemy->SetPosition(place);
                }
            }
        }

        // While the force walks, and once it is gone, no unit fires (its weapons come ready a moment after).
        const bool ceasefire = walking || (frame > walkFrame && lance.empty());

        for (int32_t partId = MCMover::FirstPartId; ceasefire && partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId); mover != nullptr)
            {
                HoldFire(*mover);
            }
        }

        // (Those closing in get their own orders.)
        if (converge.has_value() && retarget && !walking && !closing)
        {
            // At the fight's start those further off are set down nearer, unseen, to come in.
            if (frame == walkFrame && contactEdge > 0.0f && !lance.empty())
            {
                const std::shared_ptr<MCTeam> team = lance.front()->Team.lock();
                const float from = contactEdge * ConvergeFrom;

                for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
                {
                    const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                    if (enemy == nullptr || enemy == contactMech || enemy->Team.lock() == team ||
                        enemy->IsDisabled() != 0 || !Named(*enemy, *converge))
                    {
                        continue;
                    }

                    const MCVector3D to = enemy->GetPosition();

                    if (const auto [mech, away] = NearestOfForce(lance, to); away > from)
                    {
                        const MCVector3D at = mech->GetPosition();
                        const float scale = from / away;
                        MCVector3D place(at.X + (to.X - at.X) * scale, at.Y + (to.Y - at.Y) * scale, to.Z);
                        enemy->SetPosition(place);
                    }
                }
            }

            std::set<int32_t> busy = Runners(frame);
            busy.merge(Finishing(frame));
            busy.insert(wearing.begin(), wearing.end());
            const int32_t ordered = Converge(*converge, lance, busy);

            if (frame == walkFrame)
            {
                Say(std::format("{} enemies sent at the force", ordered));
            }
        }

        // A mech that has landed goes back to the attack.
        for (const std::shared_ptr<MCMover>& mover : lance)
        {
            const bool jumping = Jumping(*mover);

            if (wasJumping[mover->PartId] && !jumping && target != nullptr && !running.contains(mover->PartId) &&
                !MineWalking(frame).contains(mover->PartId))
            {
                Engage(*mover, target, LineMeters, MCOrderOrigin::Player);
            }

            wasJumping[mover->PartId] = jumping;
        }

        // A death from above: the mech jumps at the enemy mech nearest it that is far enough to jump at.
        for (const MCBattleEvent& dfa : dfas)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(dfa.Mech);

            if (frame != dfa.Frame || mech == nullptr || mech->IsDisabled() != 0)
            {
                continue;
            }

            const std::vector<std::shared_ptr<MCMover>> enemies = EnemyMechsByDistance(*mech);
            const auto enemy =
                std::ranges::find_if(enemies, [&](const std::shared_ptr<MCMover>& at) { return JumpAt(*mech, *at); });

            if (enemy != enemies.end())
            {
                ++jumps;
                Say(std::format("frame {}: {} jumps at {}", frame, mech->GetIfaceName(), (*enemy)->GetIfaceName()));
            }
            else
            {
                Say(std::format("frame {}: {} has nothing to jump at", frame, mech->GetIfaceName()));
            }
        }

        const bool lanceJump = (jumpEvery > 0 && frame % jumpEvery == 0) || std::ranges::contains(jumpFrames, frame);

        if (target != nullptr && lanceJump)
        {
            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                if (JumpAt(*mover, *target))
                {
                    ++jumps;
                    Say(std::format("frame {}: {} jumps", frame, mover->GetIfaceName()));
                }
            }
        }

        if (target != nullptr && std::ranges::contains(strikes, frame))
        {
            const std::shared_ptr<MCCommander> commander = HomeCommander();
            commander->SetNumLargeStrikes(commander->NumLargeStrikes + 1);
            CallArtillery(commander->Id, LargeStrike, target->GetPosition(), StrikeDelaySeconds, 0);
            Say(std::format("frame {}: strike on {}", frame, target->GetIfaceName()));
        }

        // The other side's strike, called by the commander of the nearest enemy of the mech named (else the first),
        // landing on it and the mech nearest it.
        std::shared_ptr<MCMover> under;

        if (const auto called = std::ranges::find(incoming, frame, &MCBattleEvent::Frame);
            called != incoming.end() && !lance.empty())
        {
            const std::shared_ptr<MCMover> named = called->Mech >= 0 ? ForceMech(called->Mech) : nullptr;
            under = named != nullptr && named->IsDisabled() == 0 ? named : lance.front();
        }

        if (under != nullptr)
        {
            MCMover& beside = *under;

            if (const std::shared_ptr<MCMover> enemy = NearestEnemyMech(beside, true); enemy != nullptr)
            {
                const MCVector3D at = beside.GetPosition();
                const MCVector3D to = enemy->GetPosition();
                const float dx = to.X - at.X;
                const float dy = to.Y - at.Y;
                const float length = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
                const std::shared_ptr<MCCommander> commander = CommanderById(enemy->GetCommanderId());
                commander->SetNumLargeStrikes(commander->NumLargeStrikes + 1);
                CallArtillery(
                    commander->Id, LargeStrike,
                    MCVector3D(at.X + dx / length * IncomingOffset, at.Y + dy / length * IncomingOffset, at.Z),
                    IncomingSeconds, 0);

                // A strike's one frame of collisions as it lands is its damage: the strike just called (the newest
                // in the world) is moved onto the force, between the mech and its nearest mate (aimed at both), on
                // open ground level with them (else the nearest that is). The force is held, so it wears their bars.
                std::shared_ptr<MCGameObject> strike;

                for (const std::shared_ptr<MCBaseObject>& object : ObjectList()->DefaultList())
                {
                    strike = object->ObjectClass == MCObjectClass::Artillery
                                 ? std::static_pointer_cast<MCGameObject>(object)
                                 : strike;
                }

                std::shared_ptr<MCMover> mate;

                for (const std::shared_ptr<MCMover>& mover : lance)
                {
                    const MCVector3D other = mover->GetPosition();
                    mate = mover != under && (mate == nullptr || std::hypot(other.X - at.X, other.Y - at.Y) <
                                                                     std::hypot(mate->GetPosition().X - at.X,
                                                                                mate->GetPosition().Y - at.Y))
                               ? mover
                               : mate;
                }

                const MCVector3D with = mate != nullptr ? mate->GetPosition() : at;
                MCVector3D place = NearestOpenAndLevel(MCVector3D((at.X + with.X) * 0.5f, (at.Y + with.Y) * 0.5f, at.Z),
                                                       GameMap().GetTerrainElevation(at), IncomingSearchMeters);

                if (strike != nullptr)
                {
                    strike->SetPosition(place);
                }

                Say(std::format("frame {}: {}'s side strikes {:.0f} m from {}, by {}", frame, enemy->GetIfaceName(),
                                std::hypot(place.X - at.X, place.Y - at.Y) / WorldUnitsPerMeter, beside.GetIfaceName(),
                                mate != nullptr ? mate->GetIfaceName() : "itself"));
            }
        }

        // The forests nearest the mech set alight: the game's own fire, and its smoke all around the mech.
        for (const MCBattleEvent& smoke : smokes)
        {
            const std::shared_ptr<MCMover> mover = ForceMech(smoke.Mech);

            if (frame != smoke.Frame || mover == nullptr || mover->IsDisabled() != 0)
            {
                continue;
            }

            const MCVector3D at = mover->GetPosition();
            std::vector<std::pair<float, std::shared_ptr<MCMiscTerrainObject>>> forests;
            // The nearest forest at any distance, for the log (a mission's ground may have none in reach).
            float nearest = std::numeric_limits<float>::max();

            for (const std::shared_ptr<MCObjectList>& list : ObjectList()->Lists())
            {
                for (const std::shared_ptr<MCBaseObject> object : *list)
                {
                    if (object->ObjectClass != MCObjectClass::MiscTerrainObject)
                    {
                        continue;
                    }

                    const auto forest = std::static_pointer_cast<MCMiscTerrainObject>(object);
                    const MCVector3D where = forest->GetPosition();

                    const float meters = std::hypot(where.X - at.X, where.Y - at.Y) / WorldUnitsPerMeter;

                    if (forest->Kind != MCMiscTerrainKind::Forest)
                    {
                        continue;
                    }

                    nearest = std::min(nearest, meters);

                    if (meters <= SmokeTreeMeters)
                    {
                        forests.emplace_back(meters, forest);
                    }
                }
            }

            std::ranges::sort(forests, {}, &std::pair<float, std::shared_ptr<MCMiscTerrainObject>>::first);
            size_t alight = 0;

            for (const auto& [meters, forest] : forests | std::views::take(SmokeTrees))
            {
                forest->LightOnFire(SmokeSeconds);
                alight += forest->FireObject != nullptr ? 1 : 0;
            }

            Say(std::format("frame {}: {} smokes ({} forests alight, the nearest forest {:.0f} m off)", frame,
                            mover->GetIfaceName(), alight, nearest));
        }

        // Where the force is, kept.
        if (frame % TrailFrames == 0)
        {
            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                trail.push_back(mover->GetPosition());
            }
        }

        // A minefield's mech walks onto fresh ground, and later backs out of it.
        for (const auto& [first, last] : minefields)
        {
            const int32_t lead = mines[first].What.empty() ? MineWalkFrames : std::stoi(mines[first].What);

            if (const std::shared_ptr<MCMover> mech = ForceMech(mines[first].Mech);
                frame == mines[first].Frame - lead && mech != nullptr && mech->IsDisabled() == 0)
            {
                MCVector3D at = mech->GetPosition();
                const std::optional<std::pair<MCVector3D, float>> ground = FreshGround(*mech, lead);
                MCVector3D to = ground.has_value() ? ground->first : at;
                WalkTo(*mech, to);
                const float meters = std::hypot(to.X - at.X, to.Y - at.Y) / WorldUnitsPerMeter;
                const int32_t back = last + MineSettleFrames;
                mineWalks[mech->PartId] = {at, to, std::atan2(to.Y - at.Y, to.X - at.X), back,
                                           back + static_cast<int32_t>(meters / MineWalkPace * FramesPerSecond)};
                Say(std::format("frame {}: {} walks {:.0f} m onto {}, turned {:.0f} degrees from its enemy", frame,
                                mech->GetIfaceName(), std::hypot(to.X - at.X, to.Y - at.Y) / WorldUnitsPerMeter,
                                ground.has_value() ? "fresh ground" : "no fresh ground (none near)",
                                ground.has_value() ? ground->second : 0.0f));
            }
        }

        for (auto& [partId, walk] : mineWalks)
        {
            if (const std::shared_ptr<MCMover> mech = GetMoverFromPartId(partId);
                frame == walk.Back && mech != nullptr && mech->IsDisabled() == 0)
            {
                WalkTo(*mech, walk.From);
                Say(std::format("frame {}: {} backs out of the minefield", frame, mech->GetIfaceName()));
            }
        }

        // A mine under the mech as it steps onto that ground: a step ahead of it, left and right in turn.
        for (size_t index = 0; index < mines.size(); ++index)
        {
            if (const std::shared_ptr<MCMover> mech = ForceMech(mines[index].Mech);
                frame == mines[index].Frame && mech != nullptr && mech->IsDisabled() == 0)
            {
                const auto walk = mineWalks.find(mech->PartId);
                const float heading = walk != mineWalks.end() ? walk->second.Heading : 0.0f;
                const float side = (index % 2 == 0 ? 1.0f : -1.0f) * MineSideMeters * WorldUnitsPerMeter;
                const float ahead = MineAheadMeters * WorldUnitsPerMeter;
                MCVector3D at = mech->GetPosition();
                at.X += std::cos(heading) * ahead - std::sin(heading) * side;
                at.Y += std::sin(heading) * ahead + std::cos(heading) * side;
                CreateExplosion(MineExplosion, at, MineSplashDamage, WorldUnitsPerMeter * MineSplashRange);
                Say(std::format("frame {}: a mine under {}", frame, mech->GetIfaceName()));
            }
        }

        // An alpha strike: the enemy mech nearest the mech runs in to its reach AlphaApproachFrames before (held
        // standing until the strike), the mech holds its fire for AlphaHoldFrames, the enemy is brought in along its
        // line to within reach AlphaBringFrames before if still further (the camera elsewhere yet), and the mech turns
        // on it with every weapon that has (nearly) recycled, each fired as it comes to bear, the rest as they come
        // ready (the pilot fires each one as it comes ready, so they never go together otherwise).
        for (const MCBattleEvent& alpha : alphas)
        {
            const std::shared_ptr<MCMover> mech = ForceMech(alpha.Mech);

            if (mech == nullptr || mech->IsDisabled() != 0)
            {
                continue;
            }

            // The nearest enemy mech not sent in for another strike still to come, nor being finished.
            const auto Taken = [&](const std::shared_ptr<MCMover>& enemy)
            {
                return (enemy == killing && killing->IsDisabled() == 0) ||
                       std::ranges::any_of(
                           alphaRunners, [&](const auto& runner)
                           { return runner.first > frame && runner.first != alpha.Frame && runner.second == enemy; });
            };

            // Of the nearest few free no nearer than its weapons' minimum ranges (inside them they can't fire), the most
            // worn (an earned kill); else the nearest free.
            const std::vector<std::shared_ptr<MCMover>> enemies = EnemyMechsByDistance(*mech);
            const float tooNear = WeaponsBand(*mech).first * WorldUnitsPerMeter;
            const MCVector3D strikerAt = mech->GetPosition();
            std::vector<std::shared_ptr<MCMover>> free;

            for (const std::shared_ptr<MCMover>& enemy : enemies)
            {
                const MCVector3D to = enemy->GetPosition();

                if (free.size() < WornChoices && !Taken(enemy) &&
                    std::hypot(to.X - strikerAt.X, to.Y - strikerAt.Y) >= tooNear)
                {
                    free.push_back(enemy);
                }
            }

            if (free.empty())
            {
                if (const auto first = std::ranges::find_if(enemies, std::not_fn(Taken)); first != enemies.end())
                {
                    free.push_back(*first);
                }
            }

            std::ranges::stable_sort(free, {}, [](const std::shared_ptr<MCMover>& enemy)
                                     { return enemy->GetTotalEffectiveness(); });
            const std::shared_ptr<MCMover> nearest = free.empty() ? nullptr : free.front();

            if (frame == alpha.Frame - AlphaApproachFrames)
            {
                const std::shared_ptr<MCMover> enemy = nearest;

                if (enemy == nullptr)
                {
                    continue;
                }

                alphaRunners[alpha.Frame] = enemy;
                Say(std::format("frame {}: {} (part {:#x}) runs in from {:.0f} m to {:.0f} m of {}", frame,
                                enemy->GetIfaceName(), enemy->PartId, RunInto(*enemy, *mech), WeaponsReach(*mech),
                                mech->GetIfaceName()));
            }

            // Ordered on again every AlphaRunFrames (its own orders would turn it away), to the mech's reach as it is.
            if (const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                sent != nullptr && sent->IsDisabled() == 0 && frame > alpha.Frame - AlphaApproachFrames &&
                frame < alpha.Frame && (alpha.Frame - frame) % AlphaRunFrames == 0)
            {
                RunInto(*sent, *mech);
            }

            // The strike's target: the runner where every weapon of the mech reaches it on a clear line of fire (one
            // of its own in the way holds direct fire), else the nearest such enemy mech not sent in for another
            // strike; else those two where the weapons reach, clear or not; else the runner while it stands; else the
            // nearest.
            const auto Target = [&]
            {
                const auto Within = [&](const std::shared_ptr<MCMover>& enemy, bool clear)
                {
                    const auto [shortest, longest] = WeaponsBand(*mech);
                    const MCVector3D from = mech->GetPosition();
                    const MCVector3D to = enemy->GetPosition();
                    const float meters = std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter;
                    return meters <= longest && (meters >= shortest || shortest >= longest) &&
                           (!clear || mech->LineOfFire(*enemy) != 0);
                };

                const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                const bool standing = sent != nullptr && sent->IsDisabled() == 0;

                for (const bool clear : {true, false})
                {
                    if (standing && Within(sent, clear))
                    {
                        return sent;
                    }

                    if (const auto reached = std::ranges::find_if(enemies, [&](const std::shared_ptr<MCMover>& enemy)
                                                                  { return !Taken(enemy) && Within(enemy, clear); });
                        reached != enemies.end())
                    {
                        return *reached;
                    }
                }

                return standing ? sent : nearest;
            };

            // Held standing as the force's mechs are (HoldWorn: the hits it takes show), let go at the strike.
            if (const std::shared_ptr<MCMover> sent = alphaRunners[alpha.Frame];
                sent != nullptr && sent->IsDisabled() == 0 && frame < alpha.Frame)
            {
                HoldWorn(*sent);
            }
            else if (sent != nullptr && frame == alpha.Frame)
            {
                Release(*sent);
            }

            // Its fire held (AlphaHoldFrom).
            if (frame >= AlphaHoldFrom(alpha) && frame < alpha.Frame)
            {
                HoldFire(*mech);
            }

            // The target set down within reach when it is out of it (further than every weapon reaches, or, where the
            // weapons share a band, inside some's minimum range): the frame before the strike if neither shows, else on
            // the strike's own cut to the mech, which hides it.
            const auto BringIn = [&](MCMover& runner)
            {
                const MCVector3D from = mech->GetPosition();
                const MCVector3D to = runner.GetPosition();
                const float meters = std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter;
                const auto [shortest, longest] = WeaponsBand(*mech);

                if (meters > WeaponsReach(*mech) || (shortest < longest && meters < shortest))
                {
                    Say(std::format("frame {}: {} brought {:.0f} m from {}", frame, runner.GetIfaceName(),
                                    BringWithinReach(*mech, runner), mech->GetIfaceName()));
                }
            };

            const bool cutToMech = std::ranges::any_of(cameras, [&](const MCBattleEvent& camera)
                                                       { return camera.Frame == frame && camera.Mech == alpha.Mech; });

            if (const std::shared_ptr<MCMover> runner = frame == alpha.Frame - AlphaBringFrames ? Target() : nullptr;
                runner != nullptr && !EitherOnScreen(*runner, *mech))
            {
                BringIn(*runner);
            }
            else if (const std::shared_ptr<MCMover> late = frame == alpha.Frame && cutToMech ? Target() : nullptr;
                     late != nullptr)
            {
                BringIn(*late);
            }

            if (frame != alpha.Frame)
            {
                continue;
            }

            alphaShooter = mech;
            alphaTarget = Target();
            alphaFrame = frame;

            if (alphaTarget != nullptr)
            {
                const MCVector3D from = mech->GetPosition();
                const MCVector3D to = alphaTarget->GetPosition();
                Say(std::format("frame {}: {} fires on {} {:.0f} m away", frame, mech->GetIfaceName(),
                                alphaTarget->GetIfaceName(),
                                std::hypot(to.X - from.X, to.Y - from.Y) / WorldUnitsPerMeter));

                for (int32_t i = 0; i < mech->NumWeapons; ++i)
                {
                    if (MCInventoryItem& item = mech->Inventory[mech->NumOther + i]; Recycled(item))
                    {
                        item.ReadyTime = 0.0f;
                    }
                }

                OrderAttack(*mech, alphaTarget);
                OrderAttack(*alphaTarget, mech, MCOrderOrigin::Commander);
            }
        }

        if (alphaTarget != nullptr && alphaShooter != nullptr && alphaShooter->IsDisabled() == 0 &&
            frame >= alphaFrame && frame < alphaFrame + AlphaFrames)
        {
            int32_t fired = 0;
            // Each weapon's answer (FireWeapon's: 0 fired; 3 not ready: disabled, or the seconds left of its recycle; 4
            // out of range, sight or ammunition: its shots left and range).
            std::string answers;

            for (int32_t i = 0; i < alphaShooter->NumWeapons; ++i)
            {
                const MCInventoryItem& item = alphaShooter->Inventory[alphaShooter->NumOther + i];
                const int32_t answer = StagedShot(*alphaShooter, alphaTarget, alphaShooter->NumOther + i);
                fired += answer == 0 ? 1 : 0;
                const MCMasterComponent& weapon = MasterComponentList[item.MasterID];
                answers += answer == 4   ? std::format(" 4({} shots, {:.0f}-{:.0f} m)",
                                                       alphaShooter->GetWeaponShots(alphaShooter->NumOther + i),
                                                       weapon.WeaponRange[0], weapon.WeaponRange[3])
                           : answer != 3 ? std::format(" {}", answer)
                           : item.Disabled != 0
                               ? std::string(" 3(disabled)")
                               : std::format(" 3({:.1f}/{:.1f}s)", item.ReadyTime - ScenarioTime, weapon.RecycleTime);
            }

            if (fired > 0 || frame == alphaFrame)
            {
                Say(std::format("frame {}: {} fires {} weapons at {} (answers{})", frame, alphaShooter->GetIfaceName(),
                                fired, alphaTarget->GetIfaceName(), answers));
            }
        }

        // An enemy mech blown apart on a hit: the alpha strike's target while it stands, else the one nearest the mech
        // named (the camera's) that the player's mechs reach, fired at first.
        for (const MCBattleEvent& destroy : destroys)
        {
            const std::shared_ptr<MCMover> named = destroy.Mech >= 0 ? ForceMech(destroy.Mech) : nullptr;
            const std::shared_ptr<MCMover> near = named != nullptr && named->IsDisabled() == 0 ? named : followed;

            if (frame == destroy.Frame - FireLeadFrames && near != nullptr)
            {
                killNear = near;
                killFrom = frame;
                killing = alphaTarget != nullptr && alphaTarget->IsDisabled() == 0 &&
                                  (named == nullptr || named == alphaShooter)
                              ? alphaTarget
                              : nullptr;

                // Of the nearest few, the most worn first.
                std::vector<std::shared_ptr<MCMover>> candidates = EnemyMechsByDistance(*near);
                candidates.resize(std::min(candidates.size(), WornChoices));
                std::ranges::stable_sort(candidates, {}, [](const std::shared_ptr<MCMover>& enemy)
                                         { return enemy->GetTotalEffectiveness(); });

                for (const std::shared_ptr<MCMover>& enemy : candidates)
                {
                    if (killing != nullptr)
                    {
                        break;
                    }

                    killing = FireAt(enemy, ReadiedShooters, readiedAt, ShooterRestSeconds, Holding(frame)) > 0
                                  ? enemy
                                  : nullptr;
                }

                // None in reach: the nearest brought within the mech's, unless either is on screen.
                if (killing == nullptr)
                {
                    killing = NearestEnemyMech(*near);

                    if (killing != nullptr && !EitherOnScreen(*killing, *near))
                    {
                        Say(std::format("frame {}: {} brought {:.0f} m from {}", frame, killing->GetIfaceName(),
                                        BringWithinReach(*near, *killing), near->GetIfaceName()));
                    }
                }
            }

            if (killing != nullptr && killing->IsDisabled() == 0 && killing->ObjectClass == MCObjectClass::BattleMech)
            {
                FireFor(killing, frame, destroy.Frame, -1, 'k');

                if (Lands(*killing, frame, destroy.Frame, -1, 'k', "blown apart"))
                {
                    static_cast<MCBattleMech&>(*killing).DestroyBodyLocation(MechCenterTorso);
                }
            }
        }

        // A kill: the enemy mech nearest the force weakened and made its target.
        if (!lance.empty() && std::ranges::contains(kills, frame))
        {
            if (std::shared_ptr<MCMover> mech = NearestEnemyMech(*lance.front()); mech != nullptr)
            {
                Weaken(*mech, KillStructure);
                target = std::move(mech);
                SendAllAt(target, frame, false);
                Say(std::format("frame {}: {} (part {:#x}) weakened", frame, target->GetIfaceName(), target->PartId));
            }
        }

        // Who falls, and when.
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover != nullptr && mover->IsDisabled() != 0 && !fell.contains(partId))
            {
                fell.insert(partId);

                if (frame > 0)
                {
                    Say(std::format("frame {}: {} (part {:#x}) down", frame, mover->GetIfaceName(), partId));
                }

                // One of the force not by a staged loss: why.
                if (const std::shared_ptr<MCMechWarrior> pilot = mover->GetPilot();
                    partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs && pilot != nullptr &&
                    !std::ranges::any_of(
                        landed,
                        [&](const auto& staged)
                        {
                            const char kind = std::get<2>(staged);
                            return (kind == 'd' || kind == 'e') && ForceMech(std::get<1>(staged)) == mover;
                        }))
                {
                    std::string broken;

                    for (const MCInventoryItem& item : mover->Inventory)
                    {
                        broken += item.Health == 0 ? std::format(" {}", MasterComponentList[item.MasterID].Name) : "";
                    }

                    Say(std::format("  unstaged: status {}, pilot status {} wounds {:.1f}, broken:{}", mover->Status,
                                    pilot->Status, pilot->Wounds, broken));
                }
            }
        }

        AimAt(frame, lance, followed, closeOn);

        // Every unit's toughness as the frame starts, to tell what it hits.
        toughness.clear();

        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            if (const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);
                mover != nullptr && mover->IsDisabled() == 0)
            {
                toughness[partId] = Toughness(*mover);
            }
        }

        stagedLance = lance;
        lastDown = down;
    }

    /// <summary>The camera as frame <paramref name="frame"/> stages it, on the force and the mech followed given.</summary>
    void AimAt(int32_t frame, const std::vector<std::shared_ptr<MCMover>>& lance,
               const std::shared_ptr<MCMover>& followed, const std::shared_ptr<MCMover>& closeOn)
    {
        const bool walking = frame < walkFrame;

        // The camera on the contact being watched, else on the mech followed (or its wreck).
        const bool watching = watch.size() == 2 && frame >= watch[0] && frame < watch[1] && !lance.empty();
        // The second line brought in, those still standing, while they are framed coming.
        std::vector<MCVector3D> coming;

        if (broughtFrame >= 0 && frame < broughtFrame + BroughtFrames)
        {
            for (const std::shared_ptr<MCMover>& enemy : brought)
            {
                if (enemy->IsDisabled() == 0)
                {
                    coming.push_back(enemy->GetPosition());
                }
            }
        }

        // The watch's zoom, then the recording's again, each eased in over zoomEase frames from the last (one: a cut).
        if (watchZoom > 0 && watch.size() == 2)
        {
            const bool out = frame >= watch[0] && frame < watch[0] + zoomEase;
            const bool back = frame >= watch[1] && frame < watch[1] + zoomEase;

            if (out || back)
            {
                const float eased =
                    static_cast<float>(frame - (out ? watch[0] : watch[1]) + 1) / static_cast<float>(zoomEase);
                const float from = static_cast<float>(out ? zoom : watchZoom);
                const float to = static_cast<float>(out ? watchZoom : zoom);
                MCFixedZoomHeight = std::lerp(from, to, eased * eased * (3.0f - 2.0f * eased));
            }
        }

        const std::shared_ptr<MCMover> contact = !watching ? nullptr
                                                 : contactMech != nullptr && contactMech->IsDisabled() == 0
                                                     ? contactMech
                                                     : NearestEnemyMech(*lance.front());

        // The fog's edge where the watch holds while the force waits, then between the contact and the force's mech
        // nearest it, the fight's first shots.
        if (contact != nullptr)
        {
            const MCVector3D to = contact->GetPosition();
            const MCVector3D at = NearestOfForce(lance, to).first->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(walking && watchAt.has_value() ? *watchAt
                                                              : MCVector3D(at.X + (to.X - at.X) * FightShare,
                                                                           at.Y + (to.Y - at.Y) * FightShare, at.Z));
        }

        // A mech firing an alpha strike since the camera came to it: between it and its target (or the wreck).
        else if (followed != nullptr && followed == alphaShooter && alphaTarget != nullptr &&
                 alphaFrame >= CameraSince(frame))
        {
            const MCVector3D from = followed->GetPosition();
            const MCVector3D to = alphaTarget->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }

        // An enemy being finished near the mech followed: between them (or the wreck).
        else if (followed != nullptr && followed == killNear && killing != nullptr && frame >= killFrom &&
                 frame < killFrom + KillFrames)
        {
            const MCVector3D from = followed->GetPosition();
            const MCVector3D to = killing->GetPosition();
            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }

        // A second line brought in on the mech followed (or its wreck), walking in: between it and their middle.
        else if (followed != nullptr && followed == closeOn && !coming.empty())
        {
            const MCVector3D from = followed->GetPosition();
            MCVector3D to(0.0f, 0.0f, from.Z);

            for (const MCVector3D& at : coming)
            {
                to.X += at.X / static_cast<float>(coming.size());
                to.Y += at.Y / static_cast<float>(coming.size());
            }

            Eye()->ChangeTarget(nullptr, true);
            Eye()->SetPosition(
                MCVector3D(from.X + (to.X - from.X) * AlphaShare, from.Y + (to.Y - from.Y) * AlphaShare, from.Z));
        }
        else if (followed != nullptr)
        {
            Eye()->ChangeTarget(followed, true);
        }
    }

    void Aim(int32_t frame)
    {
        const std::vector<std::shared_ptr<MCMover>> lance = LiveLance();
        const std::shared_ptr<MCMover> followed = CameraMech(frame, lance);
        AimAt(frame, lance, followed, CloseOn(LastClose(frame), followed));
    }

    void Staged(int32_t frame)
    {
        const bool walking = frame < walkFrame;
        const std::vector<std::shared_ptr<MCMover>>& lance = stagedLance;

        // The force answers: from --showcase-answer's frame each of its mechs fires each weapon that has recycled (its
        // recycled weapons readied out of turn every AnswerRestSeconds, as the other side's staged fire has) at the
        // most worn enemy mech in the fight, focusing fire as a lance does, staged to land as the other side's staged
        // fire is (the game deals the damage); an enemy mech is left once its bar is down to AnswerFloor, so none falls
        // but on its cue (a strike's or a kill's), and a mech keeps its weapons for a strike of its own to come.
        if (answerFrame >= 0 && frame >= answerFrame && !walking && !lance.empty())
        {
            constexpr float AnswerFloor = 0.3f;
            constexpr float AnswerRestSeconds = 1.0f;
            // A strike's mech keeps its weapons for it this long before (they would not recycle in time).
            constexpr int32_t StrikeKeepFrames = 450;
            std::set<int32_t> holding = Holding(frame);

            for (const MCBattleEvent& alpha : alphas)
            {
                if (const std::shared_ptr<MCMover> striker = ForceMech(alpha.Mech);
                    striker != nullptr && frame >= alpha.Frame - StrikeKeepFrames && frame < alpha.Frame)
                {
                    holding.insert(striker->PartId);
                }
            }

            std::vector<std::shared_ptr<MCMover>> open = InFight(lance);
            std::erase_if(open, [](const std::shared_ptr<MCMover>& enemy)
                          { return enemy->GetTotalEffectiveness() <= AnswerFloor; });
            std::ranges::stable_sort(open, {}, [](const std::shared_ptr<MCMover>& enemy)
                                     { return enemy->GetTotalEffectiveness(); });

            for (const std::shared_ptr<MCMover>& mover : lance)
            {
                const MCVector3D at = mover->GetPosition();
                // In reach of its longest weapon (each shot is fired only where its own weapon reaches).
                const float reach = mover->GetFireRange(-2) * WorldUnitsPerMeter;
                const auto victim = std::ranges::find_if(open,
                                                         [&](const std::shared_ptr<MCMover>& enemy)
                                                         {
                                                             const MCVector3D to = enemy->GetPosition();
                                                             return std::hypot(to.X - at.X, to.Y - at.Y) <= reach;
                                                         });

                if (mover->IsDisabled() != 0 || victim == open.end() || holding.contains(mover->PartId))
                {
                    continue;
                }

                const auto last = answeredAt.find(mover->PartId);
                const bool rested = last == answeredAt.end() || ScenarioTime - last->second >= AnswerRestSeconds;

                for (int32_t i = 0; i < mover->NumWeapons; ++i)
                {
                    MCInventoryItem& item = mover->Inventory[mover->NumOther + i];

                    if (rested && item.Disabled == 0 && Recycled(item) && item.ReadyTime > ScenarioTime)
                    {
                        item.ReadyTime = ScenarioTime;
                        answeredAt[mover->PartId] = ScenarioTime;
                    }

                    if (item.Disabled == 0 && item.ReadyTime <= ScenarioTime &&
                        StagedShot(*mover, *victim, mover->NumOther + i) == 0)
                    {
                        ++answerShots;
                    }
                }
            }
        }

        // Each side's shots (a weapon whose recycle starts over: held fire only pushes it a moment on) and the armor and
        // structure the other side's mechs lose, counted each second for the fight's balance.
        for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
        {
            constexpr float FreshRecycle = 1.0f;
            const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

            if (mover == nullptr || mover->ObjectClass != MCObjectClass::BattleMech || mover->IsDisabled() != 0)
            {
                continue;
            }

            const bool ours = partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs;
            std::vector<float>& ready = readyTimes[partId];
            ready.resize(static_cast<size_t>(mover->NumWeapons), 0.0f);

            for (int32_t i = 0; i < mover->NumWeapons; ++i)
            {
                const float now = mover->Inventory[mover->NumOther + i].ReadyTime;
                const bool fired = now > ready[static_cast<size_t>(i)] && now - ScenarioTime > FreshRecycle;
                (ours ? forceShots : enemyShots) += fired ? 1 : 0;
                ready[static_cast<size_t>(i)] = now;
            }

            if (!ours)
            {
                const float left = Toughness(*mover);

                if (const auto last = enemyToughness.find(partId); last != enemyToughness.end() && left < last->second)
                {
                    enemyLost += last->second - left;
                }

                enemyToughness[partId] = left;
            }
        }

        if ((frame + 1) % FramesPerSecond == 0)
        {
            SaySecond(
                std::format("second {} fire: force {} shots ({} answering), enemy {} shots; enemy mechs lost {:.0f}",
                            (frame + 1) / FramesPerSecond, forceShots, answerShots, enemyShots, enemyLost));
            forceShots = 0;
            answerShots = 0;
            enemyShots = 0;
            enemyLost = 0.0f;
        }

        if ((frame + 1) % FramesPerSecond == 0)
        {
            // HD weather's showers: how hard the rain falls and how far its puddles have spread (MCWeather::ShowersAt).
            if (MCWeather::Shown() != MCWeatherKind::Clear)
            {
                const MCWeather::MCShowers showers = MCWeather::ShowersAt(MCWeather::Shown(), ScenarioTime);
                SaySecond(std::format("second {} weather: rain {:.3f} as made, puddles {:.3f} as made",
                                      (frame + 1) / FramesPerSecond, showers.Rain, showers.Puddles));
            }

            // The force's health bars, as the game measures them, and where each stands (world units).
            std::string bars;
            std::string places;

            for (const std::shared_ptr<MCMover>& mover : LiveLance())
            {
                bars += std::format("{}{} {:.2f}", bars.empty() ? "" : ", ", mover->GetIfaceName(),
                                    mover->GetTotalEffectiveness());
                const MCVector3D at = mover->GetPosition();
                places +=
                    std::format("{}{} {:.0f} {:.0f}", places.empty() ? "" : ", ", mover->GetIfaceName(), at.X, at.Y);
            }

            SaySecond(std::format("second {} bars: {}", (frame + 1) / FramesPerSecond, bars));
            SaySecond(std::format("second {} at: {}", (frame + 1) / FramesPerSecond, places));
            // The other side's mechs the same way, by part id: bar and place.
            std::string enemies;

            for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
            {
                const std::shared_ptr<MCMover> enemy = GetMoverFromPartId(partId);

                if (enemy == nullptr || enemy->ObjectClass != MCObjectClass::BattleMech || enemy->IsDisabled() != 0 ||
                    (partId >= PlayerMechPartId && partId < PlayerMechPartId + PlayerMechs))
                {
                    continue;
                }

                const MCVector3D at = enemy->GetPosition();
                enemies += std::format("{}{:#x} {} {:.2f} {:.0f} {:.0f}", enemies.empty() ? "" : ", ", partId,
                                       enemy->GetIfaceName(), enemy->GetTotalEffectiveness(), at.X, at.Y);
            }

            SaySecond(std::format("second {} enemies: {}", (frame + 1) / FramesPerSecond, enemies));
        }
    }
};

MCStagedBattle::MCStagedBattle(MCBattleSpec spec, MCBattleLog log)
    : _State(std::make_unique<MCState>(std::move(spec), std::move(log)))
{
}

MCStagedBattle::~MCStagedBattle() = default;

auto MCStagedBattle::Spec() const -> const MCBattleSpec&
{
    return _State->spec;
}

auto MCStagedBattle::Begin() -> std::expected<void, std::string>
{
    return _State->Begin();
}

auto MCStagedBattle::Stage(int32_t frame) -> void
{
    _State->Stage(frame);
}

auto MCStagedBattle::Aim(int32_t frame) -> void
{
    _State->Aim(frame);
}

auto MCStagedBattle::Staged(int32_t frame) -> void
{
    _State->Staged(frame);
}

auto MCStagedBattle::TargetDown() const -> bool
{
    return _State->lastDown;
}

auto MCStagedBattle::FramesTargetDown() const -> int32_t
{
    return _State->fallen;
}

auto MCStagedBattle::Jumps() const -> int32_t
{
    return _State->jumps;
}

std::shared_ptr<MCMover> MCStaging::NearestEnemy()
{
    constexpr int32_t LanceSize = 3;
    std::shared_ptr<MCMover> player;

    for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + LanceSize && player == nullptr; ++partId)
    {
        if (std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId); mover != nullptr && mover->IsDisabled() == 0)
        {
            player = std::move(mover);
        }
    }

    if (player == nullptr)
    {
        return nullptr;
    }

    const std::shared_ptr<MCTeam> team = player->Team.lock();
    const MCVector3D at = player->GetPosition();
    std::shared_ptr<MCMover> nearest;
    float best = std::numeric_limits<float>::max();

    for (int32_t partId = MCMover::FirstPartId; partId < MCMover::EndPartId; ++partId)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover == nullptr || mover->Team.lock() == team || mover->IsDisabled() != 0)
        {
            continue;
        }

        const MCVector3D there = mover->GetPosition();
        const float distance = (there.X - at.X) * (there.X - at.X) + (there.Y - at.Y) * (there.Y - at.Y);

        if (distance < best)
        {
            best = distance;
            nearest = mover;
        }
    }

    return nearest;
}

void MCStaging::SendLanceAt(const std::shared_ptr<MCMover>& target)
{
    constexpr int32_t LanceSize = 3;
    const std::shared_ptr<MCTacticalInterface> iface = TacticalInterface();

    if (iface == nullptr)
    {
        return;
    }

    iface->ClearMechSelection();

    for (const std::shared_ptr<MCFriendlyMechIcon>& button : iface->MechBar->Buttons())
    {
        if (button->Active)
        {
            iface->SelectMech(button->PartId);
        }
    }

    for (int32_t partId = PlayerMechPartId; partId < PlayerMechPartId + LanceSize; ++partId)
    {
        const std::shared_ptr<MCMover> mover = GetMoverFromPartId(partId);

        if (mover == nullptr || mover->IsDisabled() != 0)
        {
            continue;
        }

        OrderAttack(*mover, target);
    }

    Eye()->ChangeTarget(GetMoverFromPartId(PlayerMechPartId), true);
}

void MCStaging::FaceMech(MCBattleMech& mech, float degrees)
{
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    MCFrameOfRef frame(MCVector3D(c, s, 0.0f), MCVector3D(-s, c, 0.0f), UnitZ);
    mech.SetFrame(frame);
}

void MCStaging::FaceMechTo(MCBattleMech& mech, const MCVector3D& point)
{
    MCStaging::FaceMech(mech, 0.0f);
    const float turn = mech.RelFacingTo(point, -1);
    float best = std::numeric_limits<float>::max();
    float facing = 0.0f;

    for (const float candidate : {turn, -turn})
    {
        MCStaging::FaceMech(mech, candidate);

        if (const float left = std::abs(mech.RelFacingTo(point, -1)); left < best)
        {
            best = left;
            facing = candidate;
        }
    }

    MCStaging::FaceMech(mech, facing);
}
