#include "stdafx.h"
#include "logistics/MCFileScrollPane.h"
#include "ai/MCMoveGeometry.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "lib/MCFitIniFile.h"
#include "lib/MCPacketFile.h"
#include "logistics/MCGenericScreen.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPurMechList.h"
#include "logistics/MCPurPilotList.h"
#include "logistics/MCPurVehicleList.h"
#include "logistics/MCMechPurchaseBlock.h"
#include "logistics/MCPilotPurchaseBlock.h"
#include "logistics/MCVehiclePurchaseBlock.h"
#include "logistics/MCCompPurchaseBlock.h"
#include "logistics/MCUnitLimits.h"
#include "logistics/MCPurProfile.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "platform/MCFileSystem.h"
#include "vfx/MCVfxFunctions.h"

std::string EmptyFile;

namespace
{
    /// <summary>Where each column header goes: x, y, width, height.</summary>
    constexpr std::array<std::array<int32_t, 4>, 3> HeaderRects = {
        {{0, 0xa5, 0x37, 0xe}, {0x49, 0xa5, 0x37, 0xe}, {0x90, 0xa5, 0x60, 0xe}}};

    /// <summary>
    /// The operation and mission of scenario <paramref name="scenario"/> in a planet's master mission file (its
    /// OpInfo block current).
    /// </summary>
    void ReadOpInfo(MCFitIniFile& master, int32_t scenario, MCFileScrollPane::File& file)
    {
        const MCFitResult<int32_t> operation = master.Read<int32_t>(std::format("Scenario{}Operation", scenario));
        Assert(operation.has_value(), 0, " could not find operation number in master mission file ");
        const MCFitResult<int32_t> mission = master.Read<int32_t>(std::format("Scenario{}Mission", scenario));
        Assert(mission.has_value(), 0, " could not find mission number in master mission file ");
        file.Operation = operation.value_or(0);
        file.Mission = mission.value_or(0);
    }
}

// MCFileColumnHeader

auto MCFileColumnHeader::Draw() -> void
{
    VfxPaneWipe(Lport()->Frame(), 0x10);

    // The selected save's operation, mission and resource points (none for the multiplayer list, nor for a save
    // without an operation).
    const std::shared_ptr<MCFileScrollPane> pane = Pane.lock();

    if (pane == nullptr)
    {
        return;
    }

    const int32_t file = pane->SelectedFile;

    if (pane->Parent.expired() || file < 0 || file >= pane->NumFiles() || pane->Multiplayer)
    {
        return;
    }

    const MCFileScrollPane::File& save = pane->Files[static_cast<size_t>(file)];

    if (save.Operation <= 0)
    {
        return;
    }

    int32_t value = save.Operation;

    if (Column == 1)
    {
        value = save.Mission;
    }
    else if (Column == 2)
    {
        value = static_cast<int32_t>(save.ResourcePoints);
    }

    LgWhiteFont->WriteString(Lport()->Frame(), 2, 2, std::format("{}", value));
}

// MCFileScrollPane

MCFileScrollPane::~MCFileScrollPane()
{
    MCFileScrollPane::Destroy();
}

auto MCFileScrollPane::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height) -> void
{
    if (LgWhiteFont != nullptr)
    {
        LineHeight = LgWhiteFont->Height() + 1;
    }

    MCScrollPane::Init(width, height, xPos, yPos, std::nullopt);
    // The files are drawn into the content each frame (DrawContent).
    ContentPort->InitView(width - SliderWidth, height);

    // The splash screens' own slider art over the scroll pane's.
    ColumnArts.push_back(ColumnArt{"splashscroll.tga", "splashsupbup.tga", "splashsdnbup.tga"});

    // The column headers (operation, mission, resource points). Each is added and removed again straight away:
    // they are displayed by the pane itself, not as children.
    for (int32_t i = 0; i < 3; i++)
    {
        const std::array<int32_t, 4>& rect = HeaderRects[static_cast<size_t>(i)];
        ColumnHeaders[static_cast<size_t>(i)] = MCMakeGui<MCFileColumnHeader>();
        MCFileColumnHeader& header = *ColumnHeaders[static_cast<size_t>(i)];
        header.Init(rect[0], rect[1], rect[2], rect[3]);
        header.Pane = WeakAs<MCFileScrollPane>();
        header.Column = i;
        AddChild(header.weak_from_this());
        RemoveChild(header);
    }
}

auto MCFileScrollPane::Destroy() -> void
{
    StartDirectory.clear();
    Files.clear();
    NameEntry.reset();

    for (MCGuiOwned<MCFileColumnHeader>& header : ColumnHeaders)
    {
        header.reset();
    }

    MCScrollPane::Destroy();
}

auto MCFileScrollPane::PressedArrowArt([[maybe_unused]] bool down) -> std::shared_ptr<MCLogPort>
{
    return nullptr;
}

auto MCFileScrollPane::DrawContent() -> void
{
    DrawFiles();
}

auto MCFileScrollPane::Display() -> void
{
    if (IsShowing() != 0)
    {
        DrawInFramePass(*PanePort, 0, false, false);
    }

    for (MCGuiOwned<MCFileColumnHeader>& header : ColumnHeaders)
    {
        header->Display();
    }

    for (size_t i = 0; i < ChildList.size(); i++)
    {
        if (const std::shared_ptr<MCGuiObject> child = ChildList[i].lock(); child != nullptr)
        {
            child->Display();
        }
    }
}

auto MCFileScrollPane::HandleEvent(MCGuiEvent& event) -> void
{
    MCScrollPane::HandleEvent(event);

    switch (event.Type)
    {
        case MCGuiEventType::LeftButtonDown:
        {
            if (NameEntry != nullptr && NameEntry->ParentObject().get() == this)
            {
                NameEntry->Destroy();
            }

            const int32_t file = GetFileAtPosition(event.X - GlobalX(), event.Y - GlobalY());

            if (file < 0)
            {
                return;
            }

            if (file != SelectedFile)
            {
                SetSelectedFile(file);
            }

            if (!SavePane)
            {
                return;
            }

            // Saving: an entry field over the clicked name.
            if (NameEntry == nullptr)
            {
                NameEntry = MCMakeGui<MCLogTextObject>();
            }
            else
            {
                if (!NameEntry->Parent.expired())
                {
                    return;
                }

                NameEntry->Destroy();
            }

            int32_t entryY = LineHeight * file - GetScrollOffset() - 1;
            int32_t entryHeight = LineHeight;

            if (entryY < 0)
            {
                entryY = 0;
                entryHeight--;
            }

            MCLogTextObject& entry = *NameEntry;
            entry.MCLogObject::Init(1, entryY, Width() - 0x12, entryHeight);
            entry.CursorPos = 0;
            entry.CursorPixel = 0;
            entry.BackgroundColor = 0x1f;
            entry.Font = LgBlackFont;
            entry.ClearEmptyOnFocus = true;
            entry.InitBuffer(NameBufferSize, MCLogInputType::Any);
            entry.SetStringBuffer(Files[static_cast<size_t>(SelectedFile)].Name);
            GuiSystem().SetText(entry.weak_from_this());
            AddChild(entry.weak_from_this());
            break;
        }

        case MCGuiEventType::LeftDoubleClick:
        {
            // A double click on the selected file presses the screen's load/save button.
            const int32_t file = GetFileAtPosition(event.X - GlobalX(), event.Y - GlobalY());

            if (file > -1 && file == SelectedFile)
            {
                std::shared_ptr<MCLogButton> button = static_cast<MCGenericScreen&>(*ParentObject()).LoadSaveButton;
                PlayLogSound(button->PressSound);
                button->RunCallback();
                return;
            }
            break;
        }

        case MCGuiEventType::Timer:
            return;
        case MCGuiEventType::Focus:
        {
            ParentObject()->HandleEvent(event);
            return;
        }
        default:
            break;
    }
}

auto MCFileScrollPane::GetFileAtPosition(int32_t xPos, int32_t yPos) -> int32_t
{
    const int32_t contentY = GetScrollOffset() + yPos;

    for (int32_t i = 0; i < NumFiles(); i++)
    {
        const tagRECT row = {1, LineHeight * i, Width() - SliderWidth, (i + 1) * LineHeight};

        if (PtInRect(row, tagPOINT{xPos, contentY}))
        {
            return i;
        }
    }

    return -1;
}

auto MCFileScrollPane::SetStartDirectory(std::string_view directory) -> void
{
    StartDirectory = directory;
    GetAllFiles(Multiplayer ? ".mpk" : ".sav", true);
}

auto MCFileScrollPane::LayoutFiles() -> void
{
    const int32_t contentHeight = std::max(NumFiles() * LineHeight, Height());

    if (ContentPort->Height() != contentHeight)
    {
        std::shared_ptr<MCLogPort> port = ContentPort;
        port->Resize(Width() - 0x12, contentHeight);
        SetDisplayPort(port, true);
    }
}

auto MCFileScrollPane::DrawFiles() -> void
{
    MCPane& content = ContentPort->Frame();
    VfxPaneWipe(content, 0x10);

    for (int32_t i = 0; i < NumFiles(); i++)
    {
        const std::string& name = Files[static_cast<size_t>(i)].Name;

        if (i == SelectedFile)
        {
            MCPane box = _Port->Frame();
            box.X0 = 1;
            box.X1 = Width() - 0x12;
            const int32_t rowY = LineHeight * i;
            box.Y0 = rowY - 1;
            box.Y1 = (i + 1) * LineHeight - 2;
            VfxPaneWipe(box, 0x14);
            LgWhiteFont->WriteString(content, 1, rowY, name);
        }
        else
        {
            LgGreyFont->WriteString(content, 1, i * LineHeight, name);
        }
    }
}

auto MCFileScrollPane::GetAllFiles(std::string_view extension, bool sort) -> void
{
    const std::vector<MCFoundFile> saves = MCFileSystem::FindFileSources(GamePath(StartDirectory, "*", extension));
    Files.clear();

    // Saving mid-campaign offers a new (empty) slot first (single player only: the original left a multiplayer list's
    // slot unnamed).
    const bool newSlot = SavePane && GlobalLog()->CurrentMission >= 0 && !Multiplayer;

    if (saves.empty() && !newSlot)
    {
        SetSelectedFile(-1);
    }

    // The operation and mission numbers come from the planets' master mission files (Port Arthur, Cermak).
    std::array<MCFitIniFile, 2> masterFiles;

    if (!Multiplayer)
    {
        int32_t result = masterFiles[0].Open(GamePath(MissionPath, "mechcmdr1", ".fit"));
        Assert(result == 0, 0, " could not open Port Arthur master mission file ");
        result = masterFiles[0].SeekBlock("OpInfo");
        Assert(result == 0, 0, " could not find operation information in master mission file");
        result = masterFiles[1].Open(GamePath(MissionPath, "xmechcmdr1", ".fit"));
        Assert(result == 0, 0, " could not open Cermak master mission file ");
        result = masterFiles[1].SeekBlock("OpInfo");
        Assert(result == 0, 0, " could not find operation information in master mission file");

        if (newSlot)
        {
            File& slot = Files.emplace_back();
            slot.Name = EmptyFile;
            ReadOpInfo(masterFiles[CurPlanet == 0 ? 0 : 1], GlobalLog()->CurrentMission, slot);
            slot.ResourcePoints = static_cast<uint32_t>(ResourcePoints);
        }
    }

    for (const MCFoundFile& found : saves)
    {
        File& file = Files.emplace_back();
        // The name without its extension.
        file.Name = std::filesystem::path(found.Name).stem().string();
        file.InUserFolder = found.InUserFolder;
        file.InGameFolder = found.InGameFolder;

        if (Multiplayer)
        {
            continue;
        }

        // Every save in the folder is read as the screen opens. One that can't be read, or names a planet the game
        // doesn't have, is listed without its operation, mission and points (issue #25: the original went on reading
        // a file it couldn't open, and indexed the two master files with the save's Setting).
        MCPacketFile saveFile;
        MCFitIniFile saveFit;
        int32_t result = saveFile.Open(GamePath(StartDirectory, file.Name, extension));
        Assert(result == 0, result, " Could not find save game file ");

        if (result == 0)
        {
            result = saveFile.SeekPacket(0);
            Assert(result == 0, 0, " could not find packet 0 in save game file ");
        }

        if (result == 0)
        {
            result = saveFit.Open(saveFile, static_cast<uint32_t>(saveFile.GetPacketSize()));
            Assert(result == 0, 0, " could not open save game file ");
        }

        if (result != 0)
        {
            continue;
        }

        int32_t planet = 0;

        if (saveFit.SeekBlock("Planet") == 0)
        {
            planet = saveFit.Read<int32_t>("Setting").value_or(0);
        }

        result = saveFit.SeekBlock("General");
        Assert(result == 0, 0, " could not find General Block in campaign file ");
        const MCFitResult<int32_t> missionNumber = saveFit.Read<int32_t>("MissionNumber");
        Assert(missionNumber.has_value(), 0, " Could not find MissionNumber in save game file ");
        result = saveFit.SeekBlock("ResourcePoints");
        Assert(result == 0, 0, " could not find ResourcePoints Block in save game file ");
        const MCFitResult<uint32_t> points = saveFit.Read<uint32_t>("numPoints");
        Assert(points.has_value(), 0, " Could not find numPoints in save game file ");
        file.ResourcePoints = points.value_or(0);
        saveFit.Close();
        saveFile.Close();

        if (planet < 0 || static_cast<size_t>(planet) >= masterFiles.size())
        {
            SDL_Log("MCFileScrollPane: %s names planet %d", file.Name.c_str(), planet);
            continue;
        }

        ReadOpInfo(masterFiles[static_cast<size_t>(planet)], missionNumber.value_or(0), file);
    }

    masterFiles[0].Close();
    masterFiles[1].Close();

    if (SelectedFile >= NumFiles())
    {
        SelectedFile = -1;
    }

    if (sort)
    {
        SortByName(Files, SelectedFile);
    }

    LayoutFiles();
}

auto MCFileScrollPane::SortByName(std::vector<File>& files, int32_t& selected) -> void
{
    const std::string selectedName = selected != -1 ? files[static_cast<size_t>(selected)].Name : "";

    // The original's exchange sort (R5: its order for equal names is kept).
    for (size_t i = 0; i < files.size(); i++)
    {
        for (size_t j = i; j < files.size(); j++)
        {
            if (std::strcmp(files[i].Name.c_str(), files[j].Name.c_str()) > 0)
            {
                std::swap(files[i], files[j]);
            }
        }
    }

    if (selected != -1)
    {
        const auto found = std::ranges::find_if(files, [&](const File& file) { return file.Name == selectedName; });

        if (found != files.end())
        {
            selected = static_cast<int32_t>(found - files.begin());
        }
    }
}

auto MCFileScrollPane::SetSelectedFile(int32_t file) -> void
{
    if (NameEntry != nullptr)
    {
        NameEntry->Destroy();
    }

    MCGuiEvent event;
    event.Clear();

    if (file < 0 || file >= NumFiles())
    {
        SelectedFile = -1;
        LayoutFiles();

        if (!Parent.expired())
        {
            event.Type = MCGuiEventType::Focus;
            event.Data = MCLogNotice::FileCleared;
            event.LParam = file;
            ParentObject()->HandleEvent(event);
        }

        return;
    }

    // Scroll half a unit at a time until the row is in view.
    while (LineHeight * file - GetScrollOffset() < 0 && ScrollPos > 0.0f)
    {
        SetScrollPos(static_cast<float>(ScrollPos - 0.5));
    }

    while (Height() < (LineHeight + 1) * file - GetScrollOffset() && ScrollPos < MaxScroll)
    {
        SetScrollPos(static_cast<float>(ScrollPos + 0.5));
    }

    SelectedFile = file;
    LayoutFiles();
    event.Type = MCGuiEventType::Focus;
    event.Data = MCLogNotice::FileSelected;
    event.LParam = file;
    ParentObject()->HandleEvent(event);
}

auto MCFileScrollPane::SetMultiplayer(bool multiplayer) -> void
{
    Multiplayer = multiplayer;
    GetAllFiles(multiplayer ? ".mpk" : ".sav", true);
}
