#include "stdafx.h"
#include "logistics/MCGenericScreen.h"
#include "color/MCPalette.h"
#include "gui/MCGuiEvent.h"
#include "lib/MCFatal.h"
#include "lib/MCFile.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCSplashScreen.h"
#include "logistics/MCInventoryBlock.h"
#include "logistics/MCMechInventoryBlock.h"
#include "logistics/MCPilotInventoryBlock.h"
#include "logistics/MCVehicleInventoryBlock.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCDragIcon.h"
#include "logistics/MCLogRows.h"
#include "main/MCGamePaths.h"
#include "logistics/MCPreferencesMenu.h"
#include "logistics/MCLoadSaveMenu.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCMainMenu.h"
#include "main/MCLogistics.h"
#include "mission/MCMission.h"
#include "platform/MCFileSystem.h"

namespace
{
    /// <summary>
    /// Entry <paramref name="name"/> of the current block, as the original's ReadId* read it: a failure is reported
    /// (the game goes on) and gives zero for a missing entry, <paramref name="fallback"/> for a bad one.
    /// </summary>
    template <MCFitValue T>
    T ReadEntry(MCFitIniFile& file, std::string_view name, std::string_view message, T fallback = T{})
    {
        const MCFitResult<T> value = file.Read<T>(name);

        if (value.has_value())
        {
            return *value;
        }

        Assert(false, static_cast<uint32_t>(std::to_underlying(value.error())), message);
        return value.error() == MCFitError::VariableNotFound ? T{} : fallback;
    }
}

// MCGenericScreen

auto MCGenericScreen::PaletteFromArt(std::string_view fileName) -> std::vector<uint8_t>
{
    MCFile file;
    std::string path = std::format("{}{}", ArtPath, fileName);

    if (file.Open(path) != 0)
    {
        path = fileName;

        if (file.Open(path) != 0)
        {
            GeneralMsg(std::format("Error reading '{}'", path));
        }
    }

    const uint32_t size = file.FileSize();

    if (size == 0)
    {
        GeneralMsg(std::format("Error reading '{}'", path));
    }

    std::vector<uint8_t> data(size);
    file.Read(data);
    file.Close();

    // The TGA's palette (BGR after the 18-byte header), as 6-bit RGB.
    std::vector<uint8_t> palette(0x300);
    const std::span<const uint8_t> source = std::span<const uint8_t>(data).subspan(0x12);

    for (size_t i = 0; i < 0x100; i++)
    {
        palette[i * 3] = source[i * 3 + 2] >> 2;
        palette[i * 3 + 1] = source[i * 3 + 1] >> 2;
        palette[i * 3 + 2] = source[i * 3] >> 2;
    }

    return palette;
}

auto MCGenericScreen::ReadElementCount(MCFitIniFile& file) -> void
{
    const int32_t result = file.SeekBlock("Elements");
    Assert(result == 0, static_cast<uint32_t>(result), " Could not Find Elements block in Generic Screen ");
    const int32_t count = ReadEntry<int32_t>(file, "NumElements", " Could not Find Elements number in Generic Screen ");
    // Element types the screen doesn't make leave their place empty.
    Elements.assign(static_cast<size_t>(std::max(count, 0)), {});
}

auto MCGenericScreen::ReadElement(MCFitIniFile& file, int32_t index) -> ElementHeader
{
    const int32_t result = file.SeekBlock(std::format("Element{}", index));
    Assert(result == 0, static_cast<uint32_t>(result), " Could not Find Element block in Generic Screen ");
    ElementHeader header;
    header.Type = ReadEntry<int32_t>(file, "ElementType", " Could not Find Element Type in Generic Screen ", -1);
    header.Left = ReadEntry<int32_t>(file, "Left", " Could not Find Element Coord in Generic Screen ");
    header.Top = ReadEntry<int32_t>(file, "Top", " Could not Find Element Coord in Generic Screen ");
    header.Width = ReadEntry<int32_t>(file, "Width", " Could not Find Element Coord in Generic Screen ");
    header.Height = ReadEntry<int32_t>(file, "Height", " Could not Find Element Coord in Generic Screen ");
    header.Art = ReadEntry<std::string>(file, "NormalArt", " Could not Find Element Art in Generic Screen ");
    return header;
}

auto MCGenericScreen::ReadButton(MCFitIniFile& file, MCLogButton& button, std::string_view art, bool loadUp)
    -> std::optional<int32_t>
{
    auto isNone = [](std::string_view name) { return MCIEquals(name, "NONE"); };

    if (loadUp && !isNone(art))
    {
        button.SetUpPicture(art);
    }

    const std::string gray =
        ReadEntry<std::string>(file, "GreyArt", " Could not Find gray button art in Generic Screen ");

    if (!isNone(gray))
    {
        button.SetGrayPicture(gray);
    }

    const std::string press =
        ReadEntry<std::string>(file, "PressArt", " Could not Find down button art in Generic Screen ");

    if (!isNone(press))
    {
        button.SetDownPicture(press);
    }

    const std::string over =
        ReadEntry<std::string>(file, "OverArt", " Could not Find button rollover art in Generic Screen ");

    if (!isNone(over))
    {
        button.SetOverPicture(over);
    }

    button.OverSound =
        static_cast<uint32_t>(ReadEntry<int32_t>(file, "OverSFX", " Could not Find Element Sound in Generic Screen "));
    button.PressSound =
        static_cast<uint32_t>(ReadEntry<int32_t>(file, "PressSFX", " Could not Find Element Sound in Generic Screen "));
    const MCFitResult<int32_t> callback = file.Read<int32_t>("Callback");
    return callback.has_value() ? std::optional<int32_t>(*callback) : std::nullopt;
}

auto MCGenericScreen::SetScreenCallback(MCLogButton& button, int32_t callback) -> bool
{
    std::function<void()> exec;

    switch (callback)
    {
        case 0:
            exec = NewMcxCampaign;
            break;
        case 1:
            exec = SaveScreen;
            break;
        case 2:
            exec = LoadScreen;
            break;
        case 3:
            exec = ShowPreferences;
            break;
        case 4:
            exec = ConnectScreen;
            break;
        case 5:
        {
            exec = ReplayCinema;
            CinemaButton = button.WeakAs<MCLogButton>();
            break;
        }
        case 6:
            exec = ReturnToGame;
            break;
        case 7:
            exec = GameOverMan;
            break;
        case 8:
        case 9:
        {
            button.Callback().SetExec(callback == 8 ? LoadGame : SaveGame);
            LoadSaveButton = button.WeakAs<MCLogButton>();
            button.Disabled = true;
            return true;
        }
        case 10:
        {
            button.Callback().SetExec(DeleteGame);
            DeleteButton = button.WeakAs<MCLogButton>();
            button.Disabled = true;
            return true;
        }
        case 11:
        {
            button.Callback().SetExec(Cancel);
            CancelButton = button.WeakAs<MCLogButton>();
            return true;
        }
        default:
            return false;
    }

    button.Callback().SetExec(exec);
    return true;
}

auto MCGenericScreen::MakeTextEntry(MCGuiObject& parent, int32_t index, const ElementHeader& header, bool show)
    -> MCGuiHandle<MCLogTextObject>
{
    return MakeElement<MCLogTextObject>(parent, index,
                                        [&](MCLogTextObject& entry)
                                        {
                                            entry.MCLogObject::Init(header.Left, header.Top, header.Width,
                                                                    header.Height);
                                            entry.CursorPos = 0;
                                            entry.CursorPixel = 0;
                                            entry.BackgroundColor = 0x1f;
                                            entry.Font = LgBlackFont;

                                            if (show)
                                            {
                                                entry.ShowGuiWindow(true);
                                            }
                                        });
}

auto MCGenericScreen::MakeFilePane(MCGuiObject& parent, MCFitIniFile& file, int32_t index, const ElementHeader& header)
    -> MCGuiHandle<MCFileScrollPane>
{
    FilePane = MakeElement<MCFileScrollPane>(parent, index,
                                             [&](MCFileScrollPane& pane)
                                             {
                                                 pane.Init(header.Left, header.Top, header.Width, header.Height);
                                                 pane.SavePane = file.Read<bool>("SavePane").value_or(false);
                                                 pane.SetStartDirectory(SavePath);
                                                 pane.ShowGuiWindow(true);
                                             });
    return FilePane;
}

auto MCGenericScreen::InitBackground(MCFitIniFile& file, const ElementHeader& header) -> void
{
    const bool useBackPalette =
        ReadEntry<bool>(file, "UseBackPalette", " Could not find UseBackPalette for background Generic Screen");

    if (useBackPalette)
    {
        Palette = PaletteFromArt(header.Art);
    }

    const int32_t result = MCLogObject::Init(header.Left, header.Top, header.Width, header.Height);
    Assert(result == 0, static_cast<uint32_t>(result), " Could not start background Generic Screen ");
    Elements[0] = weak_from_this();
}

auto MCGenericScreen::MakeElementOn(MCGuiObject& parent, MCFitIniFile& file, int32_t index, const ElementHeader& header)
    -> bool
{
    switch (header.Type)
    {
        case 1:
        {
            MakeElement<MCLogButton>(
                parent, index,
                [&](MCLogButton& button)
                {
                    const int32_t result = button.Init(header.Left, header.Top, header.Width, header.Height, {});
                    Assert(result == 0, static_cast<uint32_t>(result), " Couldn't init new button ");

                    if (const std::optional<int32_t> callback = ReadButton(file, button, header.Art, true);
                        callback.has_value() && !SetScreenCallback(button, *callback))
                    {
                        Fatal(*callback, " Illegal callback value");
                    }
                });
            return true;
        }

        case 4:
        {
            MakeTextEntry(parent, index, header, false);
            return true;
        }

        case 5:
        {
            MakeFilePane(parent, file, index, header);
            return true;
        }

        default:
            return false;
    }
}

auto MCGenericScreen::RemakeElementOn(MCGuiObject& parent, MCFitIniFile& file, int32_t index) -> void
{
    auto& element = Elements[static_cast<size_t>(index)];
    CloseGuiObject(MCGuiHandle<MCGuiObject>(element));
    element.reset();
    const ElementHeader header = ReadElement(file, index);
    MakeElementOn(parent, file, index, header);
}

auto MCGenericScreen::Init(MCFitIniFile& screenFile) -> int32_t
{
    ReadElementCount(screenFile);

    for (int32_t i = 0; i < NumElements(); i++)
    {
        const ElementHeader header = ReadElement(screenFile, i);

        if (header.Type == 0)
        {
            // The background: the screen itself.
            Assert(i == 0, static_cast<uint32_t>(i), " Background MUST be first element ");
            InitBackground(screenFile, header);
            ArtPort = std::make_shared<MCLogPort>();
            ArtPort->Load(header.Art);
            continue;
        }

        MakeElementOn(*this, screenFile, i, header);
    }

    ShowGuiWindow(false);
    return 0;
}

auto MCGenericScreen::OnDestroy() -> void
{
    // The elements are the screen's children (element 0 is the screen itself).
    Elements.clear();
    Palette.clear();
    ArtPort.reset();
    MCLogObject::OnDestroy();
}

auto MCGenericScreen::Draw() -> void
{
    if (ArtPort != nullptr && Lport()->ViewOpen())
    {
        ArtPort->CopyTo(Lport()->Frame(), 0, 0, false);
    }

    MCLogObject::Draw();
}

auto MCGenericScreen::HandleEvent(MCGuiEvent& event) -> void
{
    if (event.Type == MCGuiEventType::KeyDown && event.Key == 0x1b)
    {
        Cancel();
    }

    if (EventRoutine != nullptr)
    {
        EventRoutine(*this, event);
    }
}

auto MCGenericScreen::ShowGuiWindow(bool show) -> void
{
    // A screen shows its buttons up (the original painted it afresh), the one clicked to leave it included.
    MCLogButton::LetGoPress();

    if (!show)
    {
        // Hiding drops a half-typed save name.
        if (FilePane != nullptr)
        {
            CloseGuiObject(FilePane->NameEntry);
        }

        ShowWindow = false;
        return;
    }

    auto filesExist = [](std::string_view pattern) { return !MCFileSystem::FindFiles(pattern).empty(); };

    if (GlobalLog()->MainScreen.Is(*this))
    {
        // The main menu enables what the install and the campaign allow.
        const bool noMission = GlobalLog()->CurrentMission < 0;
        Element<MCLogButton>(2)->Disabled = Solo == 0 ? noMission : true;
        Element<MCLogButton>(7)->Disabled = noMission;
        Element<MCLogButton>(3)->Disabled = !filesExist(std::format("{}*.sav", SavePath));
        Element<MCLogButton>(10)->Disabled = !filesExist(std::format("{}*.sol", SavePath));

        // Multiplayer needs 30 MB.
        if (MCPort::TotalPhysicalMemory() < 30000000)
        {
            Element<MCLogButton>(5)->Disabled = true;
        }

        if (InDemo != 0)
        {
            Element<MCLogButton>(4)->Disabled = true;
            Element<MCLogButton>(5)->Disabled = true;
            Element<MCLogButton>(6)->Disabled = !filesExist(std::format("{}opening.smk", CDmoviePath));
        }
    }
    else if (GlobalLog()->SaveScreen.Is(*this) || GlobalLog()->LoadScreen.Is(*this))
    {
        if (FilePane->Multiplayer)
        {
            FilePane->GetAllFiles(".mpk", true);
        }
        else
        {
            FilePane->GetAllFiles(LoadingSolo == 0 ? ".sav" : ".sol", true);
        }
    }

    // The load and save screens (single player) and the preferences keep the current palette.
    const std::shared_ptr<MCLogObject> current = GlobalLog()->CurrentScreen();
    const bool fileScreen = current == GlobalLog()->SaveScreen || current == GlobalLog()->LoadScreen;

    if ((!fileScreen || GlobalLog()->LoadScreen->FilePane->Multiplayer) && current != GlobalLog()->PrefScreen)
    {
        if (!Palette.empty())
        {
            GuiSystem().ActivatePalette(Palette, 0, 0x100);
            ShowWindow = true;
            return;
        }

        GamePalette().Activate();
    }

    ShowWindow = true;
}

// MCLogImage

auto MCLogImage::OnDestroy() -> void
{
    Art.reset();
    MCLogObject::OnDestroy();
}

auto MCLogImage::Draw() -> void
{
    if (Art != nullptr)
    {
        Art->CopyTo(Lport()->Frame(), 0, 0, false);
    }

    MCLogObject::Draw();
}
