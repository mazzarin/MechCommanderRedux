#pragma once

/// <summary>
/// The command the player's next click gives: the tactical interface's mode, which the keys and the command palette's
/// buttons choose. The original's <c>IntMode</c>; its enumerator names were lost, so these come from what each mode
/// does (and the palette's help strings 0x8a..0x90).
/// </summary>
enum class MCInterfaceMode : int32_t
{
    /// <summary>Before the first scenario starts.</summary>
    Unset = -1,
    /// <summary>A click selects, moves (walking) or attacks by what it is on.</summary>
    None = 0,
    Eject = 1,
    Stop = 2,
    /// <summary>"Move Full Speed [SPACE]": moves run.</summary>
    Run = 3,
    /// <summary>A refit vehicle refits the mover clicked (chosen by the mouse state).</summary>
    Refit = 9,
    /// <summary>The selected mover goes to the repair bay clicked (chosen by the mouse state).</summary>
    Repair = 10,
    /// <summary>[O]: attack from the movers' optimal range.</summary>
    AttackOptimalRange = 0xb,
    /// <summary>"Attack-Long Range [L]".</summary>
    AttackLongRange = 0xc,
    /// <summary>"Attack-Medium Range [M]".</summary>
    AttackMediumRange = 0xd,
    /// <summary>"Attack-Short Range [S]".</summary>
    AttackShortRange = 0xe,
    /// <summary>"Attack-From Position [C]": attack without closing in.</summary>
    AttackFromPosition = 0xf,
    /// <summary>[A]: attack conserving ammunition (attack type 3).</summary>
    AttackConservingAmmo = 0x10,
    /// <summary>"Use Jumpjets [J]".</summary>
    Jump = 0x11,
    /// <summary>"Guard/Escort [G]".</summary>
    Guard = 0x13,
    PowerUp = 0x15,
    PowerDown = 0x16,
    /// <summary>The aimed shots (numeric keypad), one per body location; a mech only.</summary>
    AimHead = 0x17,
    AimLeftTorso = 0x18,
    AimRightTorso = 0x19,
    AimCenterTorso = 0x1a,
    AimLeftArm = 0x1b,
    AimRightArm = 0x1c,
    AimLeftLeg = 0x1d,
    AimRightLeg = 0x1e,
    /// <summary>[F]: move laying mines (move mode 1).</summary>
    LayMines = 0x1f,
    /// <summary>Ctrl+F1..F4: the next click on a mover makes the selection lance 1..4, the mover its point.</summary>
    LinkLance1 = 0x29,
    LinkLance2 = 0x2a,
    LinkLance3 = 0x2b,
    LinkLance4 = 0x2c,
    /// <summary>[B] with the bunny-strikes switch on: a large strike where the player clicks.</summary>
    DebugStrike = 0x32,
    /// <summary>[I]: the tactical map's info page on the unit clicked.</summary>
    Info = 0x33,
    /// <summary>The command palette's zoom toggle (a button action, never the interface's mode).</summary>
    ToggleZoom = 0x35,
    /// <summary>[T]: the camera follows the unit clicked, or moves to the point.</summary>
    CameraFollow = 0x4a,
};

/// <summary>Whether <paramref name="mode"/> is one of the six plain attack modes (optimal .. conserving ammo).</summary>
constexpr bool IsAttackMode(MCInterfaceMode mode)
{
    return mode >= MCInterfaceMode::AttackOptimalRange && mode <= MCInterfaceMode::AttackConservingAmmo;
}

/// <summary>Whether <paramref name="mode"/> is an aimed shot.</summary>
constexpr bool IsAimedShot(MCInterfaceMode mode)
{
    return mode >= MCInterfaceMode::AimHead && mode <= MCInterfaceMode::AimRightLeg;
}

/// <summary>Whether <paramref name="mode"/> links a lance.</summary>
constexpr bool IsLanceLink(MCInterfaceMode mode)
{
    return mode >= MCInterfaceMode::LinkLance1 && mode <= MCInterfaceMode::LinkLance4;
}

/// <summary>The lance (0-3) a lance-link mode makes.</summary>
constexpr int32_t LinkedLance(MCInterfaceMode mode)
{
    return static_cast<int32_t>(mode) - static_cast<int32_t>(MCInterfaceMode::LinkLance1);
}

/// <summary>The body location (0 head .. 7 right leg) an aimed shot aims at.</summary>
constexpr int32_t AimedLocation(MCInterfaceMode mode)
{
    constexpr int32_t locations[8] = {0, 2, 3, 1, 4, 5, 6, 7};
    return locations[static_cast<int32_t>(mode) - static_cast<int32_t>(MCInterfaceMode::AimHead)];
}

/// <summary>What the mouse is over (<see cref="MCTacticalInterface::UpdateMouseState"/>).</summary>
enum class MCMouseTarget : int32_t
{
    /// <summary>Before the first update.</summary>
    NotYetUpdated = -1,
    /// <summary>A mover of the player's, or its mech bar icon.</summary>
    OwnMover = 0,
    /// <summary>A live enemy mover in sight (or an enemy train car).</summary>
    Enemy = 1,
    /// <summary>A disabled enemy mover.</summary>
    DisabledEnemy = 2,
    /// <summary>A mover the player captured, an ally's, a teammate's, or the player's train car.</summary>
    Ally = 3,
    /// <summary>A contact seen on sensors only.</summary>
    SensorContact = 4,
    /// <summary>Another object of the player's, a wreck, a bridge, or a camera drone.</summary>
    Object = 5,
    /// <summary>Another object of someone else's.</summary>
    EnemyObject = 6,
    /// <summary>The terrain: no object, or one out of sight.</summary>
    Nothing = 7,
};

/// <summary>The order a click gives while a forced-order key (ctrl, F9-F12) is held: it is queued behind the others.</summary>
enum class MCForcedOrder : int32_t
{
    /// <summary>None possible here (the click only beeps).</summary>
    None = -1,
    Move = 0,
    Run = 1,
    Jump = 2,
};

/// <summary>
/// The cursor shapes the tactical interface picks (<see cref="MCGuiSystem::SetCurrentCursor"/>'s numbers; the move,
/// run and jump shapes are turned by <see cref="MCTacticalInterface::CursorOffset"/>).
/// </summary>
enum class MCInterfaceCursor : int32_t
{
    Normal = 0,
    Attack = 1,
    LongRange = 2,
    MediumRange = 3,
    ShortRange = 4,
    /// <summary>Attack from position, and an aimed shot at a mech.</summary>
    FromPosition = 5,
    /// <summary>Optimal range, and attack conserving ammunition.</summary>
    OptimalRange = 6,
    Guard = 7,
    /// <summary>The order can't be given here.</summary>
    Forbidden = 8,
    Refit = 10,
    Capture = 0xb,
    CaptureBlocked = 0xc,
    LinkLance = 0xd,
    Info = 0xe,
    Move = 0xf,
    Run = 0x10,
    Jump = 0x11,
};

/// <summary>
/// A key binding's slot in <see cref="MCTacticalInterface::Keys"/>: the command the key gives. The slots are the
/// original's (its table at <c>InterfaceObject</c> +0x118); the comments give the default key.
/// </summary>
enum class MCKeyCommand : int32_t
{
    /// <summary>Unused.</summary>
    Unused = 0,
    /// <summary>Home.</summary>
    Eject = 1,
    /// <summary>Backspace: the selection stops.</summary>
    Stop = 2,
    /// <summary>Space.</summary>
    Run = 3,
    /// <summary>F9: held, the next click gives a forced order.</summary>
    ForcedOrder = 4,
    /// <summary>F10: a forced order, and the run mode.</summary>
    ForcedRun = 5,
    /// <summary>F12: a forced order.</summary>
    ForcedOrderAlternate = 6,
    /// <summary>F11: a forced order, and the jump mode.</summary>
    ForcedJump = 7,
    /// <summary>Ctrl+Space.</summary>
    RunAlternate = 8,
    /// <summary>O.</summary>
    AttackOptimalRange = 11,
    /// <summary>L.</summary>
    AttackLongRange = 12,
    /// <summary>M.</summary>
    AttackMediumRange = 13,
    /// <summary>S.</summary>
    AttackShortRange = 14,
    /// <summary>C.</summary>
    AttackFromPosition = 15,
    /// <summary>A.</summary>
    AttackConservingAmmo = 16,
    /// <summary>J.</summary>
    Jump = 17,
    /// <summary>Ctrl+J.</summary>
    JumpAlternate = 18,
    /// <summary>G.</summary>
    Guard = 19,
    /// <summary>No key (code -1).</summary>
    Unbound = 20,
    /// <summary>Page Up.</summary>
    PowerUp = 21,
    /// <summary>Page Down.</summary>
    PowerDown = 22,
    /// <summary>Keypad 8, 7, 9, 5, 4, 6, 1, 3: aimed shots at the head .. right leg.</summary>
    AimHead = 23,
    AimLeftTorso = 24,
    AimRightTorso = 25,
    AimCenterTorso = 26,
    AimLeftArm = 27,
    AimRightArm = 28,
    AimLeftLeg = 29,
    AimRightLeg = 30,
    /// <summary>F (only for a mech that can lay mines).</summary>
    LayMines = 31,
    /// <summary>Ctrl+F.</summary>
    LayMinesAlternate = 32,
    /// <summary>F1..F4: select lance 1..4.</summary>
    SelectLance1 = 33,
    SelectLance2 = 34,
    SelectLance3 = 35,
    SelectLance4 = 36,
    /// <summary>Shift+F1..F4: add lance 1..4 to the selection.</summary>
    AddLance1 = 37,
    AddLance2 = 38,
    AddLance3 = 39,
    AddLance4 = 40,
    /// <summary>Ctrl+F1..F4 (on release): link the selection into lance 1..4.</summary>
    LinkLance1 = 41,
    LinkLance2 = 42,
    LinkLance3 = 43,
    LinkLance4 = 44,
    /// <summary>F5: break up the selected movers' lances.</summary>
    BreakLances = 45,
    /// <summary>1: arm the first artillery button.</summary>
    Artillery1 = 46,
    /// <summary>2.</summary>
    Artillery2 = 47,
    /// <summary>4: the fourth button.</summary>
    Artillery4 = 48,
    /// <summary>3: the third button.</summary>
    Artillery3 = 49,
    /// <summary>B (with the bunny-strikes switch).</summary>
    DebugStrike = 50,
    /// <summary>I.</summary>
    Info = 51,
    /// <summary>Alt: held, the tactical map's large view; released, it toggles.</summary>
    HoldTacticalMap = 52,
    /// <summary>Keypad +.</summary>
    ZoomIn = 53,
    /// <summary>Keypad -.</summary>
    ZoomOut = 54,
    /// <summary>=.</summary>
    ZoomInAlternate = 55,
    /// <summary>-.</summary>
    ZoomOutAlternate = 56,
    /// <summary>Ctrl+keypad +: the tactical map zooms in.</summary>
    TacticalMapZoomIn = 57,
    /// <summary>Ctrl+keypad -.</summary>
    TacticalMapZoomOut = 58,
    /// <summary>Ctrl+=.</summary>
    TacticalMapZoomInAlternate = 59,
    /// <summary>Ctrl+-.</summary>
    TacticalMapZoomOutAlternate = 60,
    /// <summary>Alt+M: the tactical map's map page.</summary>
    MapPage = 61,
    /// <summary>Alt+S (alt+C in multiplayer): the salvage (chat) page.</summary>
    SalvagePage = 62,
    /// <summary>Alt+D: the info page.</summary>
    InfoPage = 63,
    /// <summary>Alt+B: the mission page.</summary>
    MissionPage = 64,
    /// <summary>The arrows scroll the map.</summary>
    ScrollUp = 65,
    ScrollDown = 66,
    ScrollLeft = 67,
    ScrollRight = 68,
    /// <summary>Ctrl+arrows scroll the tactical map.</summary>
    TacticalMapScrollUp = 69,
    TacticalMapScrollDown = 70,
    TacticalMapScrollLeft = 71,
    TacticalMapScrollRight = 72,
    /// <summary>Tab.</summary>
    TogglePalette = 73,
    /// <summary>T.</summary>
    CameraFollow = 74,
    /// <summary>E (on release): select every mover of the player's on screen.</summary>
    SelectVisible = 75,
    /// <summary>Enter (multiplayer): the chat line.</summary>
    Chat = 76,
    /// <summary>Port-only, alt+=: the next bigger interface size (PREFS "InterfaceSize").</summary>
    InterfaceSizeUp = 77,
    /// <summary>Port-only, alt+-: the next smaller interface size.</summary>
    InterfaceSizeDown = 78,
    /// <summary>Port-only, alt with the keypad's + and -: the same, on keys in the same place on every layout.</summary>
    InterfaceSizeUpAlternate = 79,
    InterfaceSizeDownAlternate = 80,
};

/// <summary>How many key binding slots there are: the original's 77, then the port's.</summary>
inline constexpr size_t NumKeyCommands = 81;

/// <summary>The modifier bits of a key binding: the key code is in bits 0-15.</summary>
inline constexpr uint32_t KeyShift = 0x10000;
inline constexpr uint32_t KeyCtrl = 0x100000;
inline constexpr uint32_t KeyAlt = 0x1000000;

/// <summary>A mech bar icon's lance when its mover is in none.</summary>
inline constexpr int32_t NoLance = 5;

/// <summary>Each lance's colour (0-3), then no lance's (index <see cref="NoLance"/>).</summary>
inline constexpr std::array<uint8_t, 8> LanceColors = {0xee, 0xe5, 0x0e, 0xb5, 0x12, 0x00, 0x00, 0x00};
