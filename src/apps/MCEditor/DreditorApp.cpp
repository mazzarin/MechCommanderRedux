#include "stdafx.h"
#include "DreditorApp.h"
#include "AboutDialog.h"
#include "ChooseEditorDialog.h"
#include "DreditorDoc.h"
#include "DreditorView.h"
#include "EditorFiles.h"
#include "EditorSetupDialog.h"
#include "MainFrame.h"
#include "MultiView.h"
#include "SingleView.h"
#include "SplashDialog.h"
#include "TacticalMapDialog.h"
#include "resource.h"
#include "../MCRedux/MCCrashTrace.h"
#include "update/MCContentSwap.h"
#include "update/MCUpdateLock.h"

#include <filesystem>
#include <system_error>

namespace
{
    /// <summary>The window class MainFrame registers; finding it means an editor is already running.</summary>
    constexpr char MAIN_WINDOW_CLASS[] = "MCEDITORWND";

    /// <summary>The registry key the settings live under: HKCU\Software\FIT\Dreditor.</summary>
    constexpr char REGISTRY_COMPANY[] = "FIT";
    constexpr char REGISTRY_PROFILE[] = "Dreditor";

    /// <summary>How many files the File menu's recent list keeps.</summary>
    constexpr UINT RECENT_FILE_COUNT = 4;

    /// <summary>The Mission menu's position in the menu bar.</summary>
    constexpr UINT MISSION_MENU_POSITION = 3;

    /// <summary>The port's: MFC's command line, plus -d/--data &lt;folder&gt;, the game folder to work in.</summary>
    class EditorCommandLineInfo : public CCommandLineInfo
    {
    public:
        /// <summary>Takes the folder after -d/--data; passes everything else to MFC.</summary>
        void ParseParam(const char* pszParam, BOOL bFlag, BOOL bLast) override
        {
            if (_folderNext)
            {
                _folderNext = false;
                DataFolder = pszParam;
                ParseLast(bLast);
                return;
            }

            // MFC drops the first '-' or '/', so --data arrives as "-data".
            if (bFlag &&
                (_stricmp(pszParam, "d") == 0 || _stricmp(pszParam, "data") == 0 || _stricmp(pszParam, "-data") == 0))
            {
                _folderNext = true;
                ParseLast(bLast);
                return;
            }

            CCommandLineInfo::ParseParam(pszParam, bFlag, bLast);
        }

        /// <summary>The folder -d/--data named, or empty.</summary>
        CString DataFolder;

    private:
        /// <summary>Whether the next argument is the folder.</summary>
        bool _folderNext = false;
    };
}

HCURSOR g_hMechCursor = nullptr;

DreditorApp theApp;

DreditorApp::DreditorApp() : CWinApp("MCEditor")
{
}

DreditorApp::~DreditorApp()
{
    DestroyMenu(_missionMenus[MULTIPLAYER_VIEW]);
    DestroyMenu(_missionMenus[SOLO_VIEW]);
}

BEGIN_MESSAGE_MAP(DreditorApp, CWinApp)
ON_COMMAND(ID_APP_ABOUT, &DreditorApp::OnAppAbout)
ON_COMMAND(ID_FILE_NEW, &CWinApp::OnFileNew)
ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinApp::OnFilePrintSetup)
ON_COMMAND_RANGE(ID_FILE_MRU_FIRST, ID_FILE_MRU_FILE16, &DreditorApp::OnFileMruFirstRange)
END_MESSAGE_MAP()

BOOL DreditorApp::InitInstance()
{
    // The port's: a crash writes a stack and MCEditor.dmp beside the exe instead of a dialog.
    MCCrashTrace::Install();

    CWnd* running = CWnd::FromHandle(FindWindow(MAIN_WINDOW_CLASS, nullptr));

    if (running != nullptr)
    {
        SetForegroundWindow(running->m_hWnd);
        return FALSE;
    }

    return StartEditor();
}

BOOL DreditorApp::HoldGameLock(const CString& dataPath)
{
    // The editor is multibyte: the path is in the system's code page, as std::filesystem reads a char string.
    const std::filesystem::path folder(static_cast<LPCTSTR>(dataPath));
    std::error_code error;

    if (!std::filesystem::is_directory(folder / MCContentSwap::UpdateFolder, error))
    {
        return TRUE;
    }

    // Held shared, it waits out a game putting an update in place right now; a journal left after that is an update
    // stopped midway, which the game's next start finishes.
    _gameLock = MCUpdateLock::Open(folder, MCUpdateLock::GameLock);

    if (_gameLock == nullptr || !_gameLock->Shared())
    {
        _gameLock.reset();
        return TRUE;
    }

    if (std::filesystem::exists(folder / MCContentSwap::JournalFile, error))
    {
        _gameLock.reset();
        AfxMessageBox(
            "The game's last update stopped partway. Start the game to finish it, then open the editor again.",
            MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    return TRUE;
}

BOOL DreditorApp::StartEditor()
{
    {
        EditorCommandLineInfo cmdInfo;
        ParseCommandLine(cmdInfo);
        SplashDialog::EnableSplashScreen(cmdInfo.m_bShowSplash);

        if (!cmdInfo.DataFolder.IsEmpty())
        {
            SetEditorDataPath(cmdInfo.DataFolder);
        }
    }

    if (!AfxOleInit())
    {
        AfxMessageBox(IDS_OLE_INITIALIZATION_FAILED_MAKE);
        return FALSE;
    }

    AfxEnableControlContainer();

    // The port's: the editor works in the game folder it's in (or -d/--data names), where the original read the folder
    // from the registry, and installs its Teditor files there when they're missing (the original's setup did).
    const CString dataPath = EditorDataPath();

    if (!IsGameFolder(dataPath))
    {
        CString message;
        message.Format("%s isn't a MechCommander Gold folder: it has no SYSTEM.CFG.\n\n"
                       "Put MCEditor.exe in your MechCommander Gold folder, or start it with -d <folder>.",
                       static_cast<LPCTSTR>(dataPath));
        AfxMessageBox(message, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }

    if (!HoldGameLock(dataPath))
    {
        return FALSE;
    }

    if (!MissingEditorFiles(dataPath).empty())
    {
        EditorSetupDialog setup(dataPath);

        if (setup.DoModal() != IDOK)
        {
            return FALSE;
        }
    }

    m_nCmdShow = SW_SHOWMAXIMIZED;

    // The original called Enable3dControls() here; Windows has drawn 3D controls itself since 95, and MFC dropped it.

    // The settings key is HKCU\Software\FIT\Dreditor: SetRegistryKey names the profile after m_pszAppName.
    LPCTSTR appName = m_pszAppName;
    m_pszAppName = REGISTRY_PROFILE;
    SetRegistryKey(REGISTRY_COMPANY);
    m_pszAppName = appName;

    LoadStdProfileSettings(RECENT_FILE_COUNT);

    CString cursorPath = EditorDataPath();
    cursorPath += "\\Teditor\\bMech.ani";
    g_hMechCursor = LoadCursorFromFile(cursorPath);

    auto pDocTemplate = new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(DreditorDoc), RUNTIME_CLASS(MainFrame),
                                               RUNTIME_CLASS(MultiView));
    AddDocTemplate(pDocTemplate);
    _missionMenus[MULTIPLAYER_VIEW] = LoadMenu(m_hInstance, MAKEINTRESOURCE(IDR_MULTIPLAYER_MISSION_MENU));
    _missionMenus[SOLO_VIEW] = LoadMenu(m_hInstance, MAKEINTRESOURCE(IDR_SOLO_MISSION_MENU));

    // Dropped (EB-002): the original connected the template to a COleTemplateServer as the automation server
    // Dreditor.Document and rewrote its COM registration on every start; its dispatch map was empty.

    EditorCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);

    if (!ProcessShellCommand(cmdInfo))
    {
        return FALSE;
    }

    auto frame = static_cast<CFrameWnd*>(m_pMainWnd);
    _views[MULTIPLAYER_VIEW] = frame->GetActiveView();

    // The solo view is created hidden beside the template's view; SwitchView swaps them.
    _views[SOLO_VIEW] = new SingleView;
    CCreateContext context;
    context.m_pCurrentDoc = frame->GetActiveDocument();
    _views[SOLO_VIEW]->Create(nullptr, nullptr, WS_CHILD | WS_BORDER, CFrameWnd::rectDefault, frame,
                              AFX_IDW_PANE_FIRST + 1, &context);
    _views[SOLO_VIEW]->OnInitialUpdate();

    if (_startView != MULTIPLAYER_VIEW)
    {
        SwitchView(_startView);
    }

    m_pMainWnd->ShowWindow(SW_SHOWMAXIMIZED);
    m_pMainWnd->UpdateWindow();
    return TRUE;
}

BOOL DreditorApp::PreTranslateMessage(MSG* pMsg)
{
    if (SplashDialog::PreTranslateAppMessage(pMsg))
    {
        return TRUE;
    }

    return CWinApp::PreTranslateMessage(pMsg);
}

CView* DreditorApp::SwitchView(int viewType)
{
    const int index = viewType == ChooseEditorDialog::EDITOR_MULTIPLAYER ? MULTIPLAYER_VIEW : SOLO_VIEW;
    CView* view = _views[index];

    if (m_pMainWnd == nullptr)
    {
        _startView = index;
        return nullptr;
    }

    auto frame = static_cast<MainFrame*>(m_pMainWnd);
    CView* active = frame->GetActiveView();

    if (active == nullptr)
    {
        return nullptr;
    }

    if (view == active)
    {
        frame->ActivateFrame(-1);
        view->OnInitialUpdate();
        tacticalMap->setParentView(active);
        return active;
    }

    _startView = index;
    CMenu* menu = CMenu::FromHandle(::GetMenu(frame->m_hWnd));
    CString title;
    title.LoadString(IDS_MISSION);

    // The Mission menu is the menu bar's fourth.
    if (!ModifyMenu(menu->m_hMenu, MISSION_MENU_POSITION, MF_BYPOSITION | MF_POPUP,
                    reinterpret_cast<UINT_PTR>(_missionMenus[index]), title))
    {
        CString message;
        message.Format("Couldn't modify menu item because %ld", GetLastError());
        AfxMessageBox(message, 0, 0);
    }

    // The menu bar owns the Mission menu now: load a copy for the next switch.
    _missionMenus[index] = LoadMenu(m_hInstance, MAKEINTRESOURCE(viewType == ChooseEditorDialog::EDITOR_MULTIPLAYER
                                                                     ? IDR_MULTIPLAYER_MISSION_MENU
                                                                     : IDR_SOLO_MISSION_MENU));

    // The views trade their child window ids, so the shown one is the frame's pane.
    const LONG activeId = GetWindowLong(active->m_hWnd, GWL_ID);
    const LONG viewId = GetWindowLong(view->m_hWnd, GWL_ID);
    SetWindowLong(active->m_hWnd, GWL_ID, viewId);
    SetWindowLong(view->m_hWnd, GWL_ID, activeId);
    active->ShowWindow(SW_HIDE);
    view->ShowWindow(SW_SHOW);
    frame->attachView(static_cast<DreditorView*>(view), viewType == ChooseEditorDialog::EDITOR_MULTIPLAYER);
    frame->SetActiveView(view, TRUE);
    frame->RecalcLayout(TRUE);
    ::InvalidateRect(view->m_hWnd, nullptr, TRUE);
    frame->ActivateFrame(-1);
    tacticalMap->setParentView(view);
    return active;
}

void DreditorApp::OnAppAbout()
{
    AboutDialog aboutDialog;
    aboutDialog.DoModal();
}

void DreditorApp::OnFileMruFirstRange(UINT nID)
{
    // The list holds the documents' paths (Data\Terrain\<name>.fit); the file's title is the mission's name.
    const int index = static_cast<int>(nID - ID_FILE_MRU_FIRST);
    CString name(m_pRecentFileList->m_arrNames[index]);
    name = name.Right(name.GetLength() - name.ReverseFind('\\') - 1);
    name = name.Left(name.ReverseFind('.'));
    DreditorDoc& document = *static_cast<DreditorDoc*>(static_cast<CFrameWnd*>(m_pMainWnd)->GetActiveDocument());

    // Original behaviour (EB-133): the entry is always opened as a mission, map-only ones too.
    if (!document.loadMission(name))
    {
        m_pRecentFileList->Remove(index);
    }
}
