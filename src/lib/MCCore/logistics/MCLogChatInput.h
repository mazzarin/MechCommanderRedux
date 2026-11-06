#pragma once

#include "gui/MCGuiRef.h"
#include "logistics/MCLogChat.h"
#include "logistics/MCLogObject.h"
#include "logistics/MCLogToolButton.h"

class MCGuiEvent;
class MCGuiFont;

/// <summary>
/// The chat input line of a chat window: the chat's typed text (<see cref="MCLogChat"/>) with a blinking caret; Enter
/// sends it to everyone, or to the team when the team button is toggled, and echoes it in the chat's history.
/// </summary>
/// <remarks>Original source: <c>logistics\logsession.cpp</c> (<c>lChatInput</c>).</remarks>
class MCLogChatInput : public MCLogObject
{
public:
    /// <summary>The line edits <paramref name="chat"/> (set before <see cref="Init"/>).</summary>
    void SetChat(std::weak_ptr<MCLogChat> chat) { _Chat = std::move(chat); }

    /// <summary>Places the line, makes the team button and takes <paramref name="text"/> as the starting text.</summary>
    int32_t Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text) override;

    /// <summary>
    /// Clears the box and writes the text, wrapped; it also turned the caret off. Port: draws the box, the text and
    /// the caret (which the original's display drew into the picture each frame).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the line draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Port: an edit turns the caret off, starting its blink over (the original's paint after an edit did).
    /// </summary>
    void RestartBlink();

    /// <summary>Draws the caret (lit or not) and displays the line. Port: the caret is drawn by <see cref="Draw"/>.</summary>
    void Display() override;

    /// <summary>Takes the text focus on a click; typing, Backspace, Enter; the blink timer.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Works out the caret's pixel position for character <paramref name="position"/>.</summary>
    void SetCursorPos(int32_t position);

    /// <summary>The chat edited, or null once it is gone; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogChat> Chat() const { return _Chat.lock(); }

    /// <summary>Toggled: send to the team only (a child).</summary>
    MCGuiHandle<MCLogToolButton> TeamButton;
    std::shared_ptr<MCGuiFont> Font;

private:
    /// <summary>The chat edited (the logistics screen's).</summary>
    std::weak_ptr<MCLogChat> _Chat;
};
