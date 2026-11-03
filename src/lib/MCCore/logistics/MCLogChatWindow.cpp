#include "stdafx.h"
#include "logistics/MCLogChatWindow.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCScrollPane.h"
#include "linkup/MCFidpPlayer.h"
#include "linkup/MCSessionManager.h"
#include "main/MCGamePaths.h"
#include "logistics/MCLogChatInput.h"
#include "main/MCLogistics.h"
#include "network/MCMultiPlayer.h"
#include "vfx/MCVfxFunctions.h"

MCLogChatWindow::~MCLogChatWindow()
{
    MCLogChatWindow::Destroy();
}

auto MCLogChatWindow::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, int32_t historySize) -> void
{
    // The original wiped its picture to the key and pasted the frame (lsbdw04) along the bottom; Draw shows the frame.
    MCLogObject::Init(xPos, yPos, width, height);
    SetTransparent(true);
    HistorySize = historySize;
    FramePort = std::make_shared<MCLogPort>();
    FramePort->Load(std::format("{}logart\\lsbdw04.tga", ArtPath));

    // The history (wiped to 0x10, then written along the bottom as lines come) is drawn from Lines. The pane is made
    // first, the content for its width after.
    Lines.clear();
    MakeHistoryPane(nullptr);
    MCScrollPane& pane = *HistoryPane;
    pane.SetDisplayPort(NewHistoryView(pane.Lport()->Width(), historySize / pane.Lport()->Width()), true);
    pane.SetScrollPos(100.0f);

    ChatInput = MCMakeGui<MCLogChatInput>();
    ChatInput->Init(6, height - 0x21, 0xb8, 0x1a, {});
    AddChild(ChatInput);
    ChatInput->ShowGuiWindow(true);
}

auto MCLogChatWindow::MakeHistoryPane(std::shared_ptr<MCLogPort> history) -> void
{
    HistoryPane.reset();
    HistoryPane = MCMakeGui<MCScrollPane>();
    MCScrollPane& pane = *HistoryPane;
    pane.Init(0xb8, Height() - FramePort->Height() - 7, 6, 6, std::nullopt);

    if (history != nullptr)
    {
        pane.SetDisplayPort(std::move(history), true);
    }

    AddChild(pane.weak_from_this());

    if (history == nullptr)
    {
        pane.ShowGuiWindow(true);
    }
}

auto MCLogChatWindow::ShowGuiWindow(bool show) -> void
{
    ShowWindow = show;
}

auto MCLogChatWindow::Destroy() -> void
{
    HistoryPane.reset();
    ChatInput.reset();
    FramePort.reset();
    MCLogObject::Destroy();
}

auto MCLogChatWindow::HandleNetworkMessage(uint32_t fromPlayerId, std::span<const uint8_t> message) -> void
{
    // Team messages are in colour 6, messages to all in 4; the text follows the 9-byte header.
    const std::span<const uint8_t> text = message.subspan(9);
    ProcessChatString(fromPlayerId, std::string_view(reinterpret_cast<const char*>(text.data()), text.size()),
                      message[8] != 0 ? 6 : 4);
}

auto MCLogChatWindow::ProcessChatString(uint32_t fromPlayerId, std::string_view text, int32_t textColor) -> void
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

auto MCLogChatWindow::AddLine(std::string_view line) -> void
{
    // The original moved the history picture up by the text's height, wiped the strip along the bottom and wrote the
    // text there; the history keeps the line and draws it so each frame.
    MCScrollPane& pane = *HistoryPane;
    std::string text(line);
    const int32_t used = GuiSystem().TextFormatter.Process(text, std::nullopt, pane.Lport()->Width(), 0);
    Lines.push_back(HistoryLine{std::move(text), used});
    LinesAdded++;

    // A line whose strip moved off the top shows nothing any more.
    int32_t above = 0;
    size_t first = Lines.size();

    while (first > 0 && above < pane.Lport()->Height())
    {
        first--;
        above += Lines[first].Used;
    }

    Lines.erase(Lines.begin(), Lines.begin() + static_cast<std::ptrdiff_t>(first));
}

auto MCLogChatWindow::DrawHistory(MCGuiPort& port, const std::vector<HistoryLine>& lines) -> void
{
    MCPane& frame = port.Frame();
    const int32_t portWidth = port.Width();
    const int32_t portHeight = port.Height();
    VfxPaneWipe(frame, 0x10);

    // How far each line moved up: the heights of the lines after it.
    int32_t moved = 0;

    for (const HistoryLine& line : lines)
    {
        moved += line.Used;
    }

    const MCRect scissor = port.View().Scissor;

    for (const HistoryLine& line : lines)
    {
        moved -= line.Used;
        const int32_t bottom = portHeight - 1 - moved;
        const int32_t top = bottom - line.Used;

        // The picture ended at its last row when the line was written: nothing of it lies below that.
        port.View().Scissor.Y1 = std::min(scissor.Y1, port.View().OriginY + bottom);

        if (port.View().Open())
        {
            MCPane strip = frame;
            strip.X0 = 0;
            strip.Y0 = top;
            strip.X1 = portWidth - 1;
            strip.Y1 = bottom;
            VfxPaneWipe(strip, 0x10);
            GuiSystem().TextFormatter.Process(line.Text, port, 0, top);
        }

        port.View().Scissor = scissor;
    }
}

auto MCLogChatWindow::NewHistoryView(int32_t width, int32_t height) -> std::shared_ptr<MCLogPort>
{
    auto view = std::make_shared<MCLogPort>();
    view->InitView(width, height);
    view->DrawContent = [this](MCGuiPort& port) { DrawHistory(port, Lines); };
    return view;
}

auto MCLogChatWindow::Draw() -> void
{
    if (Lport()->ViewOpen())
    {
        FramePort->CopyTo(Lport()->Frame(), 0, Height() - FramePort->Height(), true);
    }

    MCLogObject::Draw();
}

auto MCLogChatWindow::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::KeyDown && !Parent.expired())
    {
        ParentObject()->HandleEvent(event);
    }
}

auto MCLogChatWindow::Resize(int32_t height) -> void
{
    // The original wiped its picture and pasted the frame at the new bottom (Draw shows it there).
    MCLogObject::Resize(Width(), height);
    ChatInput->MoveTo(6, height - 0x21, false);

    // Keep the history across the new pane (the original copied its picture into a new one).
    std::shared_ptr<MCLogPort> oldHistory = HistoryPane->ContentPort;
    MakeHistoryPane(NewHistoryView(oldHistory->Width(), oldHistory->Height()));
    HistoryPane->SetScrollPos(100.0f);
    HistoryPane->ShowGuiWindow(true);
}

auto MCLogChatWindow::Reset() -> void
{
    MCScrollPane& pane = *HistoryPane;
    std::shared_ptr<MCLogPort> oldHistory = pane.ContentPort;
    Lines.clear();
    pane.SetDisplayPort(NewHistoryView(oldHistory->Width(), oldHistory->Height()), true);
    ChatInput->HideText();
}
