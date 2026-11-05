#pragma once

class MCUpdateLock;

/// <summary>The animated 'Mech cursor (teditor\bMech.ani), loaded at startup.</summary>
extern HCURSOR g_hMechCursor;

/// <summary>The editor application.</summary>
class DreditorApp : public CWinApp
{
public:
    /// <summary>The two mission views the frame switches between.</summary>
    enum ViewIndex
    {
        MULTIPLAYER_VIEW = 0,
        SOLO_VIEW = 1,
        VIEW_COUNT = 2,
    };

    /// <summary>Registers the application under its name, "MCEditor".</summary>
    DreditorApp();

    /// <summary>Frees the Mission menus the frame isn't using.</summary>
    ~DreditorApp() override;

    /// <summary>Starts the editor, or brings the running one to the front.</summary>
    BOOL InitInstance() override;

    /// <summary>Gives the splash screen the first look at every message.</summary>
    BOOL PreTranslateMessage(MSG* pMsg) override;

    /// <summary>Shows the solo or the multiplayer view and swaps in its Mission menu; returns the view it replaced.</summary>
    CView* SwitchView(int viewType);

protected:
    /// <summary>Handles ON_COMMAND(ID_APP_ABOUT).</summary>
    afx_msg void OnAppAbout();

    /// <summary>
    /// File > recent file (ON_COMMAND_RANGE(ID_FILE_MRU_FIRST, ID_FILE_MRU_FILE16)): opens the mission named after
    /// the entry's file, or drops the entry when that fails.
    /// </summary>
    afx_msg void OnFileMruFirstRange(UINT nID);

    DECLARE_MESSAGE_MAP()

private:
    /// <summary>The rest of InitInstance, once no other editor is running.</summary>
    BOOL StartEditor();

    /// <summary>
    /// The port's: in a copy that updates its content, holds the game's lock as a running game does, so no game puts
    /// an update in place under the editor (update/MCUpdateLock.h). False when an update stopped midway, whose files
    /// only the game can finish putting in place, or a game is still putting one in place once the editor has waited
    /// as long as a game's start would (the player is told either way: the editor never hangs on the lock).
    /// </summary>
    BOOL HoldGameLock(const CString& dataPath);

    /// <summary>The multiplayer view (the document template's) and the solo view.</summary>
    CView* _views[VIEW_COUNT] = {};

    /// <summary>The view to show once the main window exists, when SwitchView ran before it.</summary>
    int _startView = MULTIPLAYER_VIEW;

    /// <summary>Each view's Mission menu while the other view's is in the menu bar.</summary>
    HMENU _missionMenus[VIEW_COUNT] = {};

    /// <summary>The game's lock, held shared while the editor runs (none in a copy that doesn't update).</summary>
    std::unique_ptr<MCUpdateLock> _gameLock;
};

/// <summary>The one application object.</summary>
extern DreditorApp theApp;
