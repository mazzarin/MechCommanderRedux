#pragma once

#include "logistics/MCLogChat.h"
#include "logistics/MCLogObject.h"

class MCLogChatInput;
class MCScrollPane;

/// <summary>
/// A logistics screen's multiplayer chat panel: a scrolling history and an input line with a team/all toggle, showing
/// the chat (<see cref="MCLogChat"/>) the screens share.
/// </summary>
/// <remarks>Original source: <c>logistics\logscrn.cpp</c> (<c>LogChatWindow</c>).</remarks>
class MCLogChatWindow : public MCLogObject
{
public:
    /// <summary>
    /// Places the window and makes its history pane and input line for <paramref name="chat"/> (whose history size
    /// sets the pane's height: <c>historySize / width</c>).
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::weak_ptr<MCLogChat> chat);

    /// <summary>Shows or hides the window (its children show as they are).</summary>
    void ShowGuiWindow(bool show) override;

    /// <summary>Frees the frame.</summary>
    void OnDestroy() override;

    /// <summary>Passes key presses on to the parent screen.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Changes the height, keeping the history.</summary>
    void Resize(int32_t height);

    /// <summary>Port: the history shows from its top again (the chat was cleared: <see cref="MCLogChat::Clear"/>).</summary>
    void ResetHistoryPane();

    /// <summary>Port: draws the frame along the bottom (the history pane and the input line draw themselves).</summary>
    void Draw() override;

    /// <summary>Port: the window draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Port: draws <paramref name="lines"/> (oldest first) into <paramref name="port"/> (the history's view, open) as
    /// the original's history picture held them: each line was written along the bottom over a wiped strip one row
    /// taller than its text, after the picture had moved up by its height, so a line's bottom row is wiped by the next.
    /// </summary>
    static void DrawHistory(MCGuiPort& port, const std::vector<MCLogChat::HistoryLine>& lines);

    /// <summary>Port: makes the history pane's content, a view that draws the chat's lines.</summary>
    std::shared_ptr<MCLogPort> NewHistoryView(int32_t width, int32_t height);

    /// <summary>The chat shown (the logistics screen's), or null once it is gone; the caller keeps it as its pin.</summary>
    std::shared_ptr<MCLogChat> Chat() const { return _Chat.lock(); }

    /// <summary>The history pane (a child).</summary>
    MCGuiHandle<MCScrollPane> HistoryPane;
    /// <summary>The frame picture along the bottom (<c>lsbdw04</c>).</summary>
    std::shared_ptr<MCLogPort> FramePort;
    /// <summary>The input line (a child).</summary>
    MCGuiHandle<MCLogChatInput> ChatInput;

private:
    /// <summary>Makes the history pane above the frame, its content <paramref name="history"/>, scrolled to the end.</summary>
    void MakeHistoryPane(std::shared_ptr<MCLogPort> history);

    /// <summary>The chat shown.</summary>
    std::weak_ptr<MCLogChat> _Chat;
};
