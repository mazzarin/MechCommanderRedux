#include "stdafx.h"
#include "logistics/MCLogChat.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"

auto MCLogChat::SetHistoryArea(int32_t width, int32_t height) -> void
{
    HistoryWidth = width;
    HistoryHeight = height;
}

auto MCLogChat::HandleNetworkMessage(uint32_t fromPlayerId, std::span<const uint8_t> message) -> void
{
    // Team messages are in colour 6, messages to all in 4; the text follows the 9-byte header.
    const std::span<const uint8_t> text = message.subspan(9);
    ProcessChatString(fromPlayerId, std::string_view(reinterpret_cast<const char*>(text.data()), text.size()),
                      message[8] != 0 ? 6 : 4);
}

auto MCLogChat::ProcessChatString(uint32_t fromPlayerId, std::string_view text, int32_t textColor) -> void
{
    text = text.substr(0, text.find('\0'));
    auto player = MultiPlayer()->SessionManager->GetPlayer(fromPlayerId);
    // OB-162 (fixed): a sender no longer in the session shows as "?" in player 0's colour (the original read its name
    // and number through null).
    const std::string_view name = fromPlayerId != 0 && player != nullptr ? std::string_view(player->Name) : "?";

    if (textColor == -1)
    {
        textColor = 6;
    }

    const int32_t playerNumber = player != nullptr ? player->PlayerNumber : 0;
    AddLine(std::format("%fc{}{}: %fc{}{}", GlobalLog()->PlayerColors[playerNumber], name, textColor, text));
}

auto MCLogChat::AddLine(std::string_view line) -> void
{
    // The original moved the history picture up by the text's height, wiped the strip along the bottom and wrote the
    // text there; the history keeps the line and draws it so each frame.
    std::string text(line);
    const int32_t used = GuiSystem().TextFormatter.Process(text, std::nullopt, HistoryWidth, 0);
    Lines.push_back(HistoryLine{std::move(text), used});
    LinesAdded++;

    // A line whose strip moved off the top shows nothing any more.
    int32_t above = 0;
    size_t first = Lines.size();

    while (first > 0 && above < HistoryHeight)
    {
        first--;
        above += Lines[first].Used;
    }

    Lines.erase(Lines.begin(), Lines.begin() + static_cast<std::ptrdiff_t>(first));
}

auto MCLogChat::Clear() -> void
{
    Lines.clear();
    HideText();
}
