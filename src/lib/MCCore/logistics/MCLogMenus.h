#pragma once

// What the menu screens' callbacks share (MCMainMenu, MCLoadSaveMenu, MCPreferencesMenu, MCConnectMenu): the message
// and question dialogs, and the screen elements' event routine. Original source: logistics\logmain.cpp.

class MCGenericScreen;
class MCGuiEvent;
class MCGuiObject;
class MCReusableDialog;

/// <summary>
/// The one-button message dialog with <paramref name="text"/>: an enabled OK button showing <paramref name="upArt"/> /
/// <paramref name="downArt"/>, and <paramref name="callback"/> for the answer.
/// </summary>
void ShowMenuMessage(std::string_view text, std::function<void(int32_t)> callback = nullptr,
                     std::string_view upArt = "bh_okay.tga", std::string_view downArt = "bg_okay.tga");

/// <summary>As the other overload, with string <paramref name="stringId"/>.</summary>
void ShowMenuMessage(uint32_t stringId, std::function<void(int32_t)> callback = nullptr,
                     std::string_view upArt = "bh_okay.tga", std::string_view downArt = "bg_okay.tga");

/// <summary>
/// The two-button question dialog <paramref name="dialog"/> with string <paramref name="stringId"/>: OK runs
/// <paramref name="okExec"/>, cancel <paramref name="cancelExec"/>; the cancel button shows
/// <paramref name="cancelDownArt"/> when pressed. With <paramref name="enableButtons"/> both buttons are enabled.
/// </summary>
/// <remarks>The functions are kept as pointers: the logistics screens ask the OK button whether it runs DoExit.</remarks>
void AskMenuQuestion(MCReusableDialog& dialog, uint32_t stringId, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons);

/// <summary>As the other overload, with <paramref name="text"/> (port: a question the game's strings don't hold).</summary>
void AskMenuQuestion(MCReusableDialog& dialog, std::string_view text, std::function<void()> okExec,
                     std::function<void()> cancelExec, std::string_view cancelDownArt, bool enableButtons);

/// <summary>The text typed in text element <paramref name="index"/> of <paramref name="screen"/> (up to its first NUL).</summary>
std::string ElementText(MCGenericScreen& screen, int32_t index);

/// <summary>The number typed in text element <paramref name="index"/> of <paramref name="screen"/>, as <c>atoi</c> reads it.</summary>
int32_t ElementNumber(MCGenericScreen& screen, int32_t index);

/// <summary>A screen's picture element: it passes its events on to its screen.</summary>
void ImageHandleEvent(MCGuiObject& object, MCGuiEvent& event);
