#pragma once

#include "gui/MCGuiObject.h"
#include "gui/MCGuiRef.h"

class MCFidpMessage;
class MCGuiFont;
class MCGuiToolButton;

/// <summary>The multiplayer chat entry line: a team toggle button and the text being typed (at most three lines).</summary>
/// <remarks>Original source: <c>gui\atextbox.cpp</c> (<c>aChatInput</c>).</remarks>
class MCGuiChatInput : public MCGuiObject
{
public:
    /// <summary>
    /// The most characters a chat line holds: the original's buffer, which the other players' chat lines are read
    /// into as well.
    /// </summary>
    static constexpr size_t MaxLength = 0xff;

    ~MCGuiChatInput() override;

    /// <summary>Like the base's init; makes the team button; the white font; back colour 0x10.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;
    /// <summary>The text wrapped over its lines, and the caret.</summary>
    void Draw() override;
    /// <summary>Port: draws itself each frame from its text, caret and blink state.</summary>
    bool DrawsLive() override { return true; }
    /// <summary>Typing, editing keys, and Enter (sends the line to the chat window).</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// With <paramref name="maxLines"/> 0, draws the text wrapped over lines (the first leaves room for the team
    /// button); otherwise only counts its wrapped lines, up to <paramref name="maxLines"/> past the first.
    /// </summary>
    /// <returns>Whether the text needs fewer than <paramref name="maxLines"/> extra lines.</returns>
    bool DrawAndCheck(int32_t maxLines);
    /// <summary>Places the caret after the first <paramref name="pos"/> characters.</summary>
    void SetCursorPos(int32_t pos);

    /// <summary>The "to team only" toggle.</summary>
    MCGuiHandle<MCGuiToolButton> TeamButton;
    /// <summary>The text typed (the caret is at its end).</summary>
    std::string Text;
    /// <summary>The caret's x (0x15 by init), set by <see cref="SetCursorPos"/>.</summary>
    int32_t CursorX = 0;
    /// <summary>The caret's top y.</summary>
    int32_t CursorY = 0;
    /// <summary>The caret's blink phase: set draws it in colour 0x10, clear in 0x1f.</summary>
    bool CursorVisible = false;
    /// <summary>The text's font (the white one).</summary>
    std::shared_ptr<MCGuiFont> InputFont;
};

/// <summary>The in-game chat window: the incoming lines and the <see cref="MCGuiChatInput"/> under them.</summary>
/// <remarks>Original source: <c>gui\atextbox.cpp</c> (<c>aChatWindow</c>).</remarks>
class MCGuiChatWindow : public MCGuiObject
{
public:
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view name) override;

    /// <summary>
    /// A network chat message arrived from player <paramref name="fromID"/>: byte 8 is the "to all" flag, the text
    /// follows.
    /// </summary>
    void HandleNetworkMessage(uint32_t fromID, std::span<const uint8_t> data);
    /// <summary>Shows <paramref name="text"/> from player <paramref name="playerId"/> (0: the game) in <paramref name="color"/> (-1: 6).</summary>
    void ProcessChatString(uint32_t playerId, std::string_view text, int32_t color);

    /// <summary>
    /// Port: draws the lines from the bottom up, each above the next. (The original scrolled its picture up by a
    /// new line's height and drew the line at the bottom; the lines are kept instead.)
    /// </summary>
    void Draw() override;
    /// <summary>Port: draws itself each frame from its lines.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The entry line under the incoming lines.</summary>
    MCGuiHandle<MCGuiChatInput> ChatInput;

    /// <summary>Port: a line as the chat formatter takes it (with its colour codes), and its height.</summary>
    struct ChatLine
    {
        /// <summary>The formatted line.</summary>
        std::string Text;
        /// <summary>Its height as the formatter lays it out.</summary>
        int32_t Height = 0;
    };

    /// <summary>Port: the lines still (partly) on show, oldest first.</summary>
    std::vector<ChatLine> ChatLines;
};

/// <summary>The network callback for chat messages during a scenario.</summary>
void ScenarioChatCallback(MCFidpMessage& message);

/// <summary>The players' chat colours, by player number.</summary>
inline constexpr std::array<int32_t, 6> PlayerColor = {1, 3, 4, 2, 6, 5};
/// <summary>Set to swallow the next Enter in the chat line (by the interface, when Enter opens the chat).</summary>
extern bool FirstReturn;
