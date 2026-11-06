#include "stdafx.h"
#include "logistics/MCLoadSaveMenu.h"
#include "lib/MCFile.h"
#include "logistics/MCBriefingScreen.h"
#include "logistics/MCFileScrollPane.h"
#include "logistics/MCGenericScreen.h"
#include "logistics/MCLogDialogButton.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSessionScreen.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCLogistics.h"
#include "main/MCGameStrings.h"
#include "platform/MCFileSystem.h"
#include "sound/MCSoundSystem.h"

bool LoadingSolo = false;

namespace
{
    /// <summary>The longest default save name (the original's 0x68-byte buffer).</summary>
    constexpr size_t MaxDefaultNameLength = 0x67;

    /// <summary>The file pane of the screen being shown (the load or save screen), or null.</summary>
    std::shared_ptr<MCFileScrollPane> ShownFilePane()
    {
        std::shared_ptr<MCFileScrollPane> pane;

        if (GlobalLog()->CurrentScreen() == GlobalLog()->LoadScreen)
        {
            pane = GlobalLog()->LoadScreen->FilePane.Lock();
        }

        if (GlobalLog()->CurrentScreen() == GlobalLog()->SaveScreen)
        {
            pane = GlobalLog()->SaveScreen->FilePane.Lock();
        }

        return pane;
    }

    /// <summary>Whether <paramref name="pane"/> has a file selected.</summary>
    bool HasSelection(MCOptionalRef<const MCFileScrollPane> pane)
    {
        return pane.has_value() && pane->get().HasSelection();
    }

    /// <summary>Takes down the file pane's name entry, if it has one (its <c>destroy</c>).</summary>
    void DestroyNameEntry(MCFileScrollPane& pane)
    {
        CloseGuiObject(pane.NameEntry);
    }

    /// <summary>Shows <paramref name="screen"/>'s file list (from the main menu), nothing selected.</summary>
    void OpenFileScreen(MCSplashScreen& screen, int32_t state)
    {
        GlobalLog()->MainScreen->ShowGuiWindow(false);
        GlobalLog()->SetCurrentScreen(screen);
        screen.ShowGuiWindow(true);
        GlobalLog()->LogisticsState = state;
        screen.FilePane->SetSelectedFile(-1);
    }

    /// <summary>
    /// The first default save name (string 0x37c with a number, 0 to 999) with no save in <paramref name="directory"/>,
    /// or nothing.
    /// </summary>
    std::optional<std::string> FreeDefaultName(std::string_view directory)
    {
        for (int32_t i = 0; i < 1000; i++)
        {
            std::string candidate = MCFormatPrintf(LoadGameString(0x37c, 0x95).c_str(), i);
            candidate.resize(std::min(candidate.size(), MaxDefaultNameLength));

            if (!FileExists(GamePath(directory, candidate, ".sav")))
            {
                return candidate;
            }
        }

        return std::nullopt;
    }
}

void SaveScreen()
{
    EnsureRegistryVersion();

    if (GlobalLog()->CurrentMission == -1)
    {
        return;
    }

    OpenFileScreen(*GlobalLog()->SaveScreen, 6);
}

void LoadScreen()
{
    EnsureRegistryVersion();
    OpenFileScreen(*GlobalLog()->LoadScreen, 5);
}

void SoloLoadScreen()
{
    EnsureRegistryVersion();
    LoadingSolo = true;
    OpenFileScreen(*GlobalLog()->LoadScreen, 5);
}

void LoadGame()
{
    const std::shared_ptr<MCFileScrollPane> pane = GlobalLog()->LoadScreen->FilePane.Lock();

    if (!HasSelection(MCOptionalOf(pane)))
    {
        return;
    }

    std::string fileName = pane->Files[pane->SelectedFile].Name;
    SoundSystem()->StopDigitalMusic();
    GlobalLog()->BriefingScreen->HideBriefing();
    const bool campaign = !LoadingSolo;
    LoadingSolo = false;
    Solo = !campaign;
    LastLogisticsMissionState = 0;

    if (GlobalLog()->LoadCampaign(fileName, campaign ? ".sav" : ".sol", false, true) == 0)
    {
        // Original behaviour: the save screen is the one hidden, not the load screen shown.
        GlobalLog()->SaveScreen->ShowGuiWindow(false);
        GlobalLog()->SetUpBriefingScreen(false);
        SoundSystem()->PlayDigitalMusic(0x16, true);
    }
}

void LoadMPGame()
{
    const std::shared_ptr<MCFileScrollPane> pane = GlobalLog()->LoadScreen->FilePane.Lock();

    if (HasSelection(MCOptionalOf(pane)))
    {
        GlobalLog()->SessionScreen->LoadMission(pane->Files[pane->SelectedFile].Name);
    }
}

void SaveWorkedCallback(int32_t)
{
    GlobalLog()->SaveScreen->ShowGuiWindow(false);

    if (GlobalLog()->PreviousState == 2)
    {
        GlobalLog()->SetUpPurchaseScreen(false);
    }
    else if (GlobalLog()->PreviousState != 4)
    {
        GlobalLog()->SetUpBriefingScreen(false);
    }
    else
    {
        GlobalLog()->SetUpRepairScreen(false);
    }

    SoundSystem()->PlayDigitalMusic(0x16, true);
}

void SaveGameCallback()
{
    int32_t result = -1;
    const std::shared_ptr<MCFileScrollPane> pane = GlobalLog()->SaveScreen->FilePane.Lock();

    if (HasSelection(MCOptionalOf(pane)))
    {
        std::string fileName = pane->Files[pane->SelectedFile].Name;
        SoundSystem()->StopDigitalMusic();
        result = MCLogistics::SaveCampaign(fileName.data());
    }

    if (result != 0)
    {
        pane->SetSelectedFile(-1);
    }

    pane->GetAllFiles(".sav", true);

    if (result == 0)
    {
        // "Game saved", closing by itself after three seconds.
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(LoadGameString(0x76, 0xfe));
        dialog.SetTwoButton(false);
        dialog.Callback = SaveWorkedCallback;
        dialog.OkButton->SetUpPicture("bh_okay.tga");
        dialog.OkButton->SetDownPicture("bg_okay.tga");
        dialog.OkButton->Disabled = false;
        dialog.Timeout = 3000;
        dialog.Activate();
        DestroyNameEntry(*pane);
    }

    SoundSystem()->PlayDigitalMusic(0x16, true);
}

void ClearForSaveGameCallback()
{
    const std::shared_ptr<MCFileScrollPane> pane = GlobalLog()->SaveScreen->FilePane.Lock();

    // The original cleared the file's read-only attribute first.
    if (MCFileSystem::RemoveSave(GamePath(pane->StartDirectory, pane->Files[pane->SelectedFile].Name, ".sav")))
    {
        SaveGameCallback();
    }
}

void SaveGame()
{
    const std::shared_ptr<MCFileScrollPane> pane = GlobalLog()->SaveScreen->FilePane.Lock();

    if (pane == nullptr || !pane->HasSelection())
    {
        return;
    }

    std::string& slot = pane->Files[pane->SelectedFile].Name;
    // The name typed replaces the selected entry's.
    const std::shared_ptr<MCLogTextObject> entry = pane->NameEntry.Lock();

    if (entry != nullptr)
    {
        const std::string_view typed = entry->Text();
        slot = typed.empty() ? EmptyFile : std::string(typed);
    }

    // The empty entry gets the first free default name.
    if (slot == EmptyFile)
    {
        if (std::optional<std::string> name = FreeDefaultName(pane->StartDirectory))
        {
            slot = std::move(*name);
        }
    }

    // OB-084 (fixed): the original tested the old, freed name here (the empty entry's text); the name now in the slot
    // is tested instead.
    if (!FileExists(GamePath(pane->StartDirectory, slot, ".sav")))
    {
        SaveGameCallback();
        return;
    }

    // Overwrite? (The cancel button keeps whatever action it had.)
    MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
    dialog.SetText(LoadGameString(0x75, 0xfe));
    dialog.SetTwoButton(true);
    dialog.Callback = nullptr;
    dialog.OkButton->SetUpPicture("bh_okay.tga");
    dialog.OkButton->SetDownPicture("bg_okay.tga");
    dialog.OkButton->Callback().SetExec(ClearForSaveGameCallback);
    dialog.CancelButton->SetUpPicture("bh_cancl.tga");
    dialog.CancelButton->SetDownPicture("bg_cancl.tga");
    dialog.Activate();
}

void DeleteCallbackTrue()
{
    const std::shared_ptr<MCFileScrollPane> pane = ShownFilePane();

    if (!HasSelection(MCOptionalOf(pane)))
    {
        return;
    }

    // The original cleared the file's read-only attribute first.
    MCFileSystem::RemoveSave(
        GamePath(pane->StartDirectory, pane->Files[pane->SelectedFile].Name, LoadingSolo ? ".sol" : ".sav"));
    pane->SetSelectedFile(-1);

    if (!LoadingSolo)
    {
        GlobalLog()->LoadScreen->FilePane->GetAllFiles(".sav", true);
        GlobalLog()->SaveScreen->FilePane->GetAllFiles(".sav", true);
    }
    else
    {
        GlobalLog()->LoadScreen->FilePane->GetAllFiles(".sol", true);
    }
}

void DeleteCallbackFalse()
{
    const std::shared_ptr<MCFileScrollPane> pane = ShownFilePane();

    if (pane != nullptr)
    {
        DestroyNameEntry(*pane);
    }
}

void DeleteGame()
{
    const std::shared_ptr<MCFileScrollPane> pane = ShownFilePane();

    if (!HasSelection(MCOptionalOf(pane)))
    {
        return;
    }

    AskMenuQuestion(*GlobalLog()->MessageDialog, 0x74, DeleteCallbackTrue, DeleteCallbackFalse, "bg_cancl.tga", false);
    DestroyNameEntry(*pane);
}

void LoadSaveScreenHandleEvent(MCGuiObject& object, MCGuiEvent& event)
{
    if (event.Type != 0x1e)
    {
        return;
    }

    auto& screen = static_cast<MCGenericScreen&>(object);
    const int32_t message = event.Data;

    if (message == 1)
    {
        // A file was picked: it can be loaded (or saved over); deleted unless it is the empty entry.
        screen.LoadSaveButton->Disabled = false;
        const std::shared_ptr<MCFileScrollPane> pane = screen.FilePane.Lock();
        screen.DeleteButton->Disabled =
            !(HasSelection(MCOptionalOf(pane)) && pane->Files[pane->SelectedFile].Name != EmptyFile);
    }
    else if (message == 2)
    {
        screen.LoadSaveButton->Disabled = true;
        screen.DeleteButton->Disabled = true;
    }
    else if (message == 5 && GlobalLog()->SaveScreen.Is(object))
    {
        // Enter in the name entry: save.
        SoundSystem()->PlayInterfaceSample(screen.LoadSaveButton->PressSound);
        SaveGame();
    }
}
