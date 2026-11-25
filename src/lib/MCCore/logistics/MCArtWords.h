#pragma once

// Port (Redux HD): words the art doesn't have, made of its own letters (SPEC-in-game-lobby.md's "In the game"):
// MULTIPLAYER ACCESS's LAN button (L and A from SERIAL, N from INTERNET, on NETWORK's plate) and the INTERNET screen's
// title (INTERNET CONNECTION from NETWORK CONNECTION's letters, on the background every multiplayer panel shares). A
// word's letters are the pieces of its picture: the pixels that differ from the background behind them, each joined
// run of them (side by side or above each other) a letter, or two letters the art joined. A made picture's HD copy is
// the HD art pack's own, found by its pixels as any picture's is: the pack draws its word in its font, from the pictures
// mc_tests' --hd-extract-menus writes, in its lanbutton and internetpanel folders.
//
// Port-only (SPEC-menus.md's stage 9): with the 1998 menus gone, the other words they made are here for
// --hd-extract-menus alone, as they were made: the text buttons' (textbutton), UPDATE's (updatebutton), MATCH OPTIONS'
// tabs (matchtabs) and REMATCH's (rematchbutton). Whether new packs still make them is the census's (stage 9, step 2's
// commit 12): nothing in the game draws them now.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

class MCGuiPort;
class MCLogPort;

/// <summary>A button's pictures: up (also lit under the mouse), pressed and grayed.</summary>
struct MCButtonPictures
{
    std::shared_ptr<MCLogPort> Up;
    std::shared_ptr<MCLogPort> Down;
    std::shared_ptr<MCLogPort> Gray;
};

/// <summary>
/// MULTIPLAYER ACCESS's LAN button: NETWORK's pictures with their word wiped and LAN written in the art's letters, at
/// NETWORK's middle; none (logged) when the art isn't the original's as expected.
/// </summary>
std::optional<MCButtonPictures> MCLanButtonPictures();

/// <summary>
/// The INTERNET screen's panel: the LAN screen's (lan00) titled INTERNET CONNECTION, the old title wiped to the
/// panels' shared background; null (logged) when the art isn't the original's as expected.
/// </summary>
std::shared_ptr<MCLogPort> MCInternetPanel();

/// <summary>
/// Every word the 1998 screens' text buttons showed (1998's MCLogTextButton, the preferences' CONTROLS and the UPDATE
/// page's): the HD art pack draws each one's pictures in its font, from the pictures --hd-extract-menus writes.
/// </summary>
inline constexpr std::array<std::string_view, 12> MCTextButtonLabels = {
    "CONTROLS", "DEFAULTS", "LAPTOP", "DONE",    "OPTIONS", "ORIGINAL",
    "GET",      "STOP",     "REMOVE", "RESTART", "INSTALL", "SKIP",
};

/// <summary>
/// The pictures of a text button showing <paramref name="label"/>: the art's ACCEPT button's (bh_accept.tga up and lit,
/// bg_accept.tga pressed, bn_accept.tga grayed) with ACCEPT wiped from the face and the label written in its place in
/// the art's own lettering (letters cut from the words of the art's other buttons of that size, F made from E), or in
/// the white font for a word with a letter none of them writes, dark on the face, light pressed and faint grayed.
/// </summary>
MCButtonPictures MCTextButtonPictures(std::string_view label);

/// <summary>
/// The 1998 main menu's UPDATE button's pictures: ORIGINAL CINEMA's with its word wiped and UPDATE written in the menu's
/// own lettering, its letters cut from the other buttons' words (in the white font if the art's words can't be read);
/// up and pressed in amber, grayed as the menu's.
/// </summary>
/// <param name="palette">The main menu's palette, 6-bit RGB (mainScreen.fit's background's): it tells a letter from its edge.</param>
MCButtonPictures UpdateButtonPictures(std::span<const uint8_t> palette);

/// <summary>A tab of the 1998 session screen's communications panel.</summary>
enum class MCSessionTab : uint8_t
{
    Chat,
    Options,
};

/// <summary>
/// The 1998 session screen's tabs as the briefing's multiplayer pictures draw them (lsbdw02 with MULTIPLAYER CHAT in
/// front, lsbdw03 behind), MISSION BRIEFING lettered MATCH OPTIONS, with <paramref name="front"/>'s tab in front: for
/// MULTIPLAYER CHAT only the tab behind it. Null when the art isn't the tabs' as expected.
/// </summary>
std::shared_ptr<MCLogPort> MakeMatchTabs(MCSessionTab front);

/// <summary>REMATCH's pictures, up and held (none where a face can't be read).</summary>
struct MCRematchFaces
{
    std::shared_ptr<MCGuiPort> Up;
    std::shared_ptr<MCGuiPort> Down;
};

/// <summary>
/// The 1998 multiplayer results' REMATCH, made of NEXT MISSION's faces (up and held): the word wiped, each row the face's
/// colour, and REMATCH laid on in the art's lettering (mission/MCRematchLetters.h) and the art's ink, dark on the up face
/// and light on the held one, each pixel the palette's grey nearest the face blended with the ink by the word's
/// coverage, in the faces' files' own colours.
/// </summary>
MCRematchFaces RematchFaces();

/// <summary>
/// The palette of the TGA <paramref name="fileName"/> (under <c>ArtPath</c>, else as it is) as 6-bit RGB, 0x300 bytes
/// (1998's MCGenericScreen::PaletteFromArt). A file that can't be read is fatal.
/// </summary>
std::vector<uint8_t> PaletteFromArt(std::string_view fileName);
