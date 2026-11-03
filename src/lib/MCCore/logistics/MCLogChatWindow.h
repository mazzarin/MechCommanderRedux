#pragma once

#include "logistics/MCLogObject.h"

class MCLogChatInput;
class MCScrollPane;

/// <summary>
/// The multiplayer chat panel of the logistics screens: a scrolling history and an input line with a team/all
/// toggle.
/// </summary>
/// <remarks>Original source: <c>logistics\logscrn.cpp</c> (<c>LogChatWindow</c>).</remarks>
class MCLogChatWindow : public MCLogObject
{
public:
    /// <summary>A line of the history, and the height the text formatter gave it.</summary>
    struct HistoryLine
    {
        std::string Text;
        int32_t Used = 0;
    };

    /// <summary>Calls <see cref="Destroy"/>.</summary>
    ~MCLogChatWindow() override;

    /// <summary>
    /// Places the window; <paramref name="historySize"/> is the history's size in pixels of the pane's width (so its
    /// height is <c>historySize / width</c>).
    /// </summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, int32_t historySize);

    /// <summary>Shows or hides the window (its children show as they are).</summary>
    void ShowGuiWindow(bool show) override;

    void Destroy() override;

    /// <summary>A chat message from the network: the team flag at +8 of <paramref name="message"/>, the text at +9.</summary>
    void HandleNetworkMessage(uint32_t fromPlayerId, std::span<const uint8_t> message);

    /// <summary>
    /// Adds "<c>name: text</c>" to the history in the sender's colour and <paramref name="textColor"/> (-1 = 6),
    /// scrolling the old lines up.
    /// </summary>
    void ProcessChatString(uint32_t fromPlayerId, std::string_view text, int32_t textColor);

    /// <summary>Passes key presses on to the parent screen.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Changes the height, keeping the history.</summary>
    void Resize(int32_t height);

    /// <summary>Clears the history and the input line.</summary>
    void Reset();

    /// <summary>Port: draws the frame along the bottom (the history pane and the input line draw themselves).</summary>
    void Draw() override;

    /// <summary>Port: the window draws itself each frame (its port is a view).</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Port: adds <paramref name="line"/> (text with <c>SMUTI</c> codes) to the bottom of the history, the older lines
    /// moving up by its height (the second half of <see cref="ProcessChatString"/>).
    /// </summary>
    void AddLine(std::string_view line);

    /// <summary>
    /// Port: draws <paramref name="lines"/> (oldest first) into <paramref name="port"/> (the history's view, open) as
    /// the original's history picture held them: each line was written along the bottom over a wiped strip one row
    /// taller than its text, after the picture had moved up by its height, so a line's bottom row is wiped by the next.
    /// </summary>
    static void DrawHistory(MCGuiPort& port, const std::vector<HistoryLine>& lines);

    /// <summary>Port: makes the history pane's content, a view that draws <see cref="Lines"/>.</summary>
    std::shared_ptr<MCLogPort> NewHistoryView(int32_t width, int32_t height);

    /// <summary>The history pane.</summary>
    MCGuiOwned<MCScrollPane> HistoryPane;
    /// <summary>The frame picture along the bottom (<c>lsbdw04</c>).</summary>
    std::shared_ptr<MCLogPort> FramePort;
    /// <summary>The input line.</summary>
    MCGuiOwned<MCLogChatInput> ChatInput;
    /// <summary>The history size given to <see cref="Init"/>.</summary>
    int32_t HistorySize = 0;

    /// <summary>
    /// Port: the history, oldest first (the original kept it as the pixels of the pane's picture). Lines that moved
    /// off the top are dropped.
    /// </summary>
    std::vector<HistoryLine> Lines;
    /// <summary>Port: the lines added since the window was made (the session screen's MATCH OPTIONS tab watches it).</summary>
    size_t LinesAdded = 0;

private:
    /// <summary>Makes the history pane above the frame, its content <paramref name="history"/>, scrolled to the end.</summary>
    void MakeHistoryPane(std::shared_ptr<MCLogPort> history);
};
