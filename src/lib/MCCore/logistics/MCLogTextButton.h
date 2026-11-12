#pragma once

#include "logistics/MCArtWords.h"
#include "logistics/MCLogButton.h"

#include <array>
#include <string_view>

/// <summary>
/// Port-only: a logistics push button for a word the art has no button for (the preferences' CONTROLS, DEFAULTS and
/// DONE). Its pictures are the art's ACCEPT button's (bh_accept.tga up and lit, bg_accept.tga pressed, bn_accept.tga
/// grayed) with ACCEPT wiped from the face and the label written in its place in the art's own lettering: letters cut
/// from the words of the art's other buttons of that size, F made from E. A word with a letter none of them writes (H,
/// Q, W, Z) is written in the white font instead, dark on the face, light pressed and faint grayed, as the art's words
/// are. It sounds as the screens' buttons do.
/// </summary>
class MCLogTextButton : public MCLogButton
{
public:
    /// <summary>
    /// Places the button with its top left corner at (<paramref name="xPos"/>, <paramref name="yPos"/>), showing
    /// <paramref name="label"/>; <paramref name="clicked"/> runs on a click.
    /// </summary>
    void InitWithLabel(int32_t xPos, int32_t yPos, std::string_view label, std::function<void()> clicked);

    /// <summary>The word the button shows.</summary>
    const std::string& Label() const { return _Label; }

private:
    /// <summary>The word the button shows.</summary>
    std::string _Label;
};

/// <summary>
/// Port-only: every word the port's text buttons show (each screen names its own). The HD art pack draws each one's
/// pictures in its font, from the pictures mc_tests' --hd-extract-menus writes of these words; a button with a word not
/// here has no HD copy, which it logs as it's made.
/// </summary>
inline constexpr std::array<std::string_view, 14> MCTextButtonLabels = {
    "CONTROLS", "DEFAULTS", "LAPTOP",  "DONE",    "OPTIONS", "ORIGINAL", "GET",
    "STOP",     "REMOVE",   "RESTART", "INSTALL", "SKIP",    "PUBLIC",   "PRIVATE",
};

/// <summary>Port-only: the pictures a text button showing <paramref name="label"/> has (MCLogTextButton::InitWithLabel's).</summary>
MCButtonPictures MCTextButtonPictures(std::string_view label);
