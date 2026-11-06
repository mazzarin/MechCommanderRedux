#pragma once

/// <summary>
/// The multiplayer chat of the logistics screens: the history, the line being typed and the team/all choice. Each
/// screen that shows the chat has its own window of it (<see cref="MCLogChatWindow"/>); they all show this, so the
/// history and a half-typed line stay the same from one screen to the next.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\logscrn.cpp</c> (<c>LogChatWindow</c>) and <c>logistics\logsession.cpp</c>
/// (<c>lChatInput</c>). The original's chat was one window that the logistics screens moved from screen to screen; its
/// state is this model, owned by the logistics screen (<c>MCLogistics::Chat</c>).
/// </remarks>
class MCLogChat
{
public:
    /// <summary>A line of the history, and the height the text formatter gave it.</summary>
    struct HistoryLine
    {
        std::string Text;
        int32_t Used = 0;
    };

    /// <summary>The most characters a chat line takes (a game rule: the chat message's size).</summary>
    static constexpr int32_t MaxLength = 0xff;

    /// <summary>
    /// A chat whose history is <paramref name="historySize"/> pixels of the history's width (so its height is
    /// <c>historySize / width</c>).
    /// </summary>
    explicit MCLogChat(int32_t historySize) : HistorySize(historySize) {}

    /// <summary>The history's size in pixels (the windows' history panes are all this size).</summary>
    void SetHistoryArea(int32_t width, int32_t height);

    /// <summary>A chat message from the network: the team flag at +8 of <paramref name="message"/>, the text at +9.</summary>
    void HandleNetworkMessage(uint32_t fromPlayerId, std::span<const uint8_t> message);

    /// <summary>
    /// Adds "<c>name: text</c>" to the history in the sender's colour and <paramref name="textColor"/> (-1 = 6),
    /// scrolling the old lines up.
    /// </summary>
    void ProcessChatString(uint32_t fromPlayerId, std::string_view text, int32_t textColor);

    /// <summary>
    /// Port: adds <paramref name="line"/> (text with <c>SMUTI</c> codes) to the bottom of the history, the older lines
    /// moving up by its height (the second half of <see cref="ProcessChatString"/>).
    /// </summary>
    void AddLine(std::string_view line);

    /// <summary>Clears the history, and empties the line being typed as shown (<see cref="HideText"/>).</summary>
    void Clear();

    /// <summary>What the input line shows: <see cref="Text"/> up to its first NUL.</summary>
    std::string_view ShownText() const { return std::string_view(Text.c_str()); }

    /// <summary>
    /// Empties the input line as the chat window's reset does: the text shown ends at once, but what was typed stays
    /// behind it (OB-164).
    /// </summary>
    void HideText() { Text[0] = 0; }

    /// <summary>The history size given to the constructor.</summary>
    int32_t HistorySize = 0;
    /// <summary>The history's width and height in pixels (<see cref="SetHistoryArea"/>).</summary>
    int32_t HistoryWidth = 0;
    int32_t HistoryHeight = 0;
    /// <summary>
    /// Port: the history, oldest first (the original kept it as the pixels of the pane's picture). Lines that moved
    /// off the top are dropped.
    /// </summary>
    std::vector<HistoryLine> Lines;
    /// <summary>Port: the lines added since the chat was made (the session screen's MATCH OPTIONS tab watches it).</summary>
    size_t LinesAdded = 0;
    /// <summary>
    /// The characters typed (<see cref="TextLength"/> of them), NUL-filled to <see cref="MaxLength"/> + 1. What is
    /// shown and sent ends at the first NUL (see <see cref="HideText"/>).
    /// </summary>
    std::string Text = std::string(MaxLength + 1, '\0');
    /// <summary>The number of characters typed.</summary>
    int32_t TextLength = 0;
    /// <summary>The caret's x in the input line.</summary>
    int32_t CursorX = 0;
    /// <summary>The caret's y in the input line.</summary>
    int32_t CursorY = 0;
    /// <summary>The caret's blink phase.</summary>
    bool CursorOn = false;
    /// <summary>The team button is down: a line goes to the team only.</summary>
    bool TeamOnly = false;
};
