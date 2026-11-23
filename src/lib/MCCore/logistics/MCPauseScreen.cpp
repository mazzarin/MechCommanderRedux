#include "stdafx.h"
#include "logistics/MCPauseScreen.h"
#include "gui/MCGuiGlobals.h"
#include "iface/MCTacticalInterface.h"
#include "ui/MCRmlFlow.h"
#include "mission/MCMission.h"
#include "network/MCMultiPlayer.h"
#include "platform/MCInput.h"
#include "ui/MCRmlLayer.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using MCSettingsModel::MCQuick;
    using MCSettingsModel::MCSetting;
    using Rml::StringUtilities::EncodeRml;

    constexpr const char* PauseDocument = "pause.rml";

    /// <summary>The document's places (ui/pause.rml).</summary>
    constexpr const char* NameId = "name";
    constexpr const char* QuickId = "quick";
    constexpr const char* ResumeId = "resume";
    constexpr const char* AllId = "all";
    constexpr const char* LeaveId = "leave";

    /// <summary>The window's name and its way out: single player's, then multiplayer's (the battle goes on).</summary>
    constexpr std::string_view PausedName = "Paused";
    constexpr std::string_view MenuName = "Menu";
    constexpr std::string_view AbortWord = "Abort mission";
    constexpr std::string_view LeaveWord = "Leave match";

    /// <summary>LEAVE MATCH's question (1998's "EXIT MULTIPLAYER?" box) and its act.</summary>
    constexpr std::string_view ExitQuestion = "Exit multiplayer?";
    constexpr std::string_view ExitWord = "Exit";

    /// <summary>The result a player who leaves a match gets (1998's: gui/MCGuiInput.cpp's Enter while asked).</summary>
    constexpr uint32_t LeftMatchResult = 3;

    /// <summary>The pause shown, if any (the mission's layer owns it).</summary>
    MCGuiHandle<MCPauseScreen> ThePause;

    std::string_view QuickGroup(MCQuick quick)
    {
        switch (quick)
        {
            case MCQuick::View:
                return "View";
            case MCQuick::Sound:
                return "Sound";
            case MCQuick::None:
                break;
        }

        return {};
    }

    void SetWords(Rml::ElementDocument& document, const char* id, std::string_view words)
    {
        if (Rml::Element* element = document.GetElementById(id); element != nullptr)
        {
            element->SetInnerRML(EncodeRml(std::string(words)));
        }
    }
}

MCPauseScreen::MCPauseScreen(bool multiplayer) : MCSettingsScreen(true), _Multiplayer(multiplayer)
{
    MCSettingsModel::Opened(true);
    SetDocument(PauseDocument);
    Keys = {{"Esc", "Resume"}};
}

bool MCPauseScreen::OpenOverMission()
{
    if (Shows())
    {
        return true;
    }

    const MCOptionalRef<MCRmlMenus> menus = MCRmlFlow::Menus();

    if (!menus.has_value())
    {
        return false;
    }

    const bool multiplayer = MultiPlayer() != nullptr;
    const MCGuiHandle<MCPauseScreen> pause = menus->get().Mission().CreateChild<MCPauseScreen>(
        [](MCPauseScreen& made) { made.Back = [&made] { made.Resume(); }; }, multiplayer);

    if (const std::expected<void, std::string> opened = pause->Open(); !opened)
    {
        SDL_Log("MCPauseScreen: cannot open %s: %s; the mission pauses as the original's did", PauseDocument,
                opened.error().c_str());
        pause->Close();
        return false;
    }

    ThePause = pause;

    // The pause takes the keys and the mouse: what the battle had held down is let go, or it would act on after.
    MCInput::LetGoOfKeys();

    if (const std::shared_ptr<MCTacticalInterface> tactical = TacticalInterface(); tactical != nullptr)
    {
        tactical->ReleaseScrollKeys();
    }

    // Single player: the mission waits behind it (1998's pause); a match plays on.
    if (!multiplayer)
    {
        GamePaused = true;
    }

    return true;
}

bool MCPauseScreen::Shows()
{
    return ThePause != nullptr;
}

void MCPauseScreen::MissionEnding()
{
    if (const std::shared_ptr<MCPauseScreen> pause = ThePause.Lock(); pause != nullptr)
    {
        pause->Resume();
    }
}

void MCPauseScreen::OnLoaded(Rml::ElementDocument& document)
{
    SetWords(document, NameId, _Multiplayer ? MenuName : PausedName);
    SetWords(document, LeaveId, _Multiplayer ? LeaveWord : AbortWord);

    std::string rows;
    MCQuick group = MCQuick::None;
    bool first = true;

    for (const MCQuick quick : {MCQuick::View, MCQuick::Sound})
    {
        for (const MCSetting& setting : MCSettingsModel::All())
        {
            if (setting.Quick != quick)
            {
                continue;
            }

            if (quick != group)
            {
                rows +=
                    std::format("<div class=\"group eyebrow{}\">{}</div>", first ? " first" : "", QuickGroup(quick));
                group = quick;
                first = false;
            }

            rows += RowRml(setting, true);
        }
    }

    if (Rml::Element* quick = document.GetElementById(QuickId); quick != nullptr)
    {
        quick->SetInnerRML(rows);
    }

    for (const MCQuick quick : {MCQuick::View, MCQuick::Sound})
    {
        for (const MCSetting& setting : MCSettingsModel::All())
        {
            if (setting.Quick == quick)
            {
                ListenToRow(setting);
            }
        }
    }

    OnClick(ResumeId, [this] { Resume(); });
    OnClick(AllId, [this] { AllPreferences(); });
    OnClick(LeaveId,
            [this]
            {
                if (!_Multiplayer)
                {
                    LeaveMission();
                    return;
                }

                const std::weak_ptr<MCPauseScreen> self = std::static_pointer_cast<MCPauseScreen>(shared_from_this());
                _Question = MCQuestionWindow::Ask(*this, std::string(ExitQuestion), std::string(ExitWord),
                                                  [self]
                                                  {
                                                      if (const std::shared_ptr<MCPauseScreen> pause = self.lock();
                                                          pause != nullptr)
                                                      {
                                                          pause->LeaveMission();
                                                      }
                                                  },
                                                  {});
            });
    Refresh();
}

void MCPauseScreen::Resume()
{
    // The mission's end in a match can come while Preferences is open over it: its changes aren't kept.
    CloseOver();
    Keep();

    if (!_Multiplayer)
    {
        GamePaused = false;
    }

    Close();
}

void MCPauseScreen::Keep()
{
    if (std::exchange(_Changed, false))
    {
        MCSettingsModel::Keep();
    }
}

void MCPauseScreen::CloseOver()
{
    if (const std::shared_ptr<MCPrefsScreen> prefs = _Prefs.Lock(); prefs != nullptr)
    {
        prefs->Close();
    }

    if (const std::shared_ptr<MCQuestionWindow> question = _Question.Lock(); question != nullptr)
    {
        question->Close();
    }
}

void MCPauseScreen::AllPreferences()
{
    if (_Prefs)
    {
        return;
    }

    const std::weak_ptr<MCPauseScreen> self = std::static_pointer_cast<MCPauseScreen>(shared_from_this());
    const MCGuiHandle<MCPrefsScreen> prefs = Layer().CreateChild<MCPrefsScreen>(
        [self](MCPrefsScreen& made)
        {
            // CANCEL and Escape put back what it opened with; ACCEPT keeps it (PREFS.CFG written): the pause again.
            made.Back = [self, &made]
            {
                made.Leave();

                if (const std::shared_ptr<MCPauseScreen> pause = self.lock(); pause != nullptr)
                {
                    pause->PreferencesClosed();
                }
            };

            made.Accepted = [self]
            {
                if (const std::shared_ptr<MCPauseScreen> pause = self.lock(); pause != nullptr)
                {
                    pause->PreferencesClosed();
                }
            };
        },
        MCPrefsScreen::MCPlace::Mission);

    if (const std::expected<void, std::string> opened = prefs->Open(); !opened)
    {
        SDL_Log("MCPauseScreen: cannot open Preferences over the battlefield: %s", opened.error().c_str());
        prefs->Close();
        return;
    }

    _Prefs = prefs;
    Hide();
}

void MCPauseScreen::PreferencesClosed()
{
    if (const std::shared_ptr<MCPrefsScreen> prefs = _Prefs.Lock(); prefs != nullptr)
    {
        prefs->Close();
    }

    _Prefs.reset();
    Show();
    Refresh();
}

void MCPauseScreen::LeaveMission()
{
    Keep();
    const std::shared_ptr<MCMission> mission = Mission();

    if (mission != nullptr)
    {
        // 1998's Enter while paused (or asked): the mission ends as a loss with no points, its results shown.
        mission->EndScenarioRequested = -1;

        if (_Multiplayer)
        {
            ScenarioResult = LeftMatchResult;
        }
    }

    GamePaused = false;
    Close();
}

void MCPauseScreen::OnDestroy()
{
    // The mission's end takes it down too: the battle isn't left paused.
    CloseOver();
    Keep();

    if (!_Multiplayer)
    {
        GamePaused = false;
    }

    MCSettingsScreen::OnDestroy();
}
