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

auto MCLogChatWindow::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::weak_ptr<MCLogChat> chat)
    -> void
{
    // The original wiped its picture to the key and pasted the frame (lsbdw04) along the bottom; Draw shows the frame.
    _Chat = std::move(chat);
    MCLogObject::Init(xPos, yPos, width, height);
    SetTransparent(true);
    FramePort = std::make_shared<MCLogPort>();
    FramePort->Load(std::format("{}logart\\lsbdw04.tga", ArtPath));

    // The history (wiped to 0x10, then written along the bottom as lines come) is drawn from the chat's lines. The
    // pane is made first, the content for its width after.
    MakeHistoryPane(nullptr);
    MCScrollPane& pane = *HistoryPane;
    const int32_t historyWidth = pane.Lport()->Width();
    const int32_t historyHeight = Chat()->HistorySize / historyWidth;
    Chat()->SetHistoryArea(historyWidth, historyHeight);
    pane.SetDisplayPort(NewHistoryView(historyWidth, historyHeight), true);
    pane.SetScrollPos(100.0f);

    ChatInput = CreateChild<MCLogChatInput>(
        [&](MCLogChatInput& input)
        {
            input.SetChat(_Chat);
            input.Init(6, height - 0x21, 0xb8, 0x1a, {});
        });
    ChatInput->ShowGuiWindow(true);
}

auto MCLogChatWindow::MakeHistoryPane(std::shared_ptr<MCLogPort> history) -> void
{
    CloseGuiObject(HistoryPane);
    const bool given = history != nullptr;
    HistoryPane = CreateChild<MCScrollPane>(
        [&](MCScrollPane& pane)
        {
            pane.Init(0xb8, Height() - FramePort->Height() - 7, 6, 6, std::nullopt);

            if (given)
            {
                pane.SetDisplayPort(std::move(history), true);
            }
        });

    if (!given)
    {
        HistoryPane->ShowGuiWindow(true);
    }
}

auto MCLogChatWindow::ShowGuiWindow(bool show) -> void
{
    ShowWindow = show;
}

auto MCLogChatWindow::OnDestroy() -> void
{
    FramePort.reset();
    MCLogObject::OnDestroy();
}

auto MCLogChatWindow::DrawHistory(MCGuiPort& port, const std::vector<MCLogChat::HistoryLine>& lines) -> void
{
    MCPane& frame = port.Frame();
    const int32_t portWidth = port.Width();
    const int32_t portHeight = port.Height();
    VfxPaneWipe(frame, 0x10);

    // How far each line moved up: the heights of the lines after it.
    int32_t moved = 0;

    for (const MCLogChat::HistoryLine& line : lines)
    {
        moved += line.Used;
    }

    const MCRect scissor = port.View().Scissor;

    for (const MCLogChat::HistoryLine& line : lines)
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
    view->DrawContent = [chat = _Chat](MCGuiPort& port)
    {
        if (const std::shared_ptr<MCLogChat> shown = chat.lock(); shown != nullptr)
        {
            DrawHistory(port, shown->Lines);
        }
    };

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

auto MCLogChatWindow::ResetHistoryPane() -> void
{
    MCScrollPane& pane = *HistoryPane;
    std::shared_ptr<MCLogPort> oldHistory = pane.ContentPort;
    pane.SetDisplayPort(NewHistoryView(oldHistory->Width(), oldHistory->Height()), true);
}
