#pragma once

// Port (Redux HD): words the art doesn't have, made of its own letters (SPEC-in-game-lobby.md's "In the game"):
// MULTIPLAYER ACCESS's LAN button (L and A from SERIAL, N from INTERNET, on NETWORK's plate) and the INTERNET screen's
// title (INTERNET CONNECTION from NETWORK CONNECTION's letters, on the background every multiplayer panel shares). A
// word's letters are the pieces of its picture: the pixels that differ from the background behind them, each joined
// run of them (side by side or above each other) a letter, or two letters the art joined. A made picture's HD copy is
// the HD art pack's own, found by its pixels as any picture's is: the pack draws its word in its font, from the pictures
// mc_tests' --hd-extract-menus writes, in its lanbutton and internetpanel folders.

#include <memory>
#include <optional>

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
