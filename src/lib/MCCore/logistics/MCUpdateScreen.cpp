#include "stdafx.h"
#include "logistics/MCUpdateScreen.h"
#include "gui/MCScreenFlow.h"
#include "logistics/MCMenuScreens.h"
#include "logistics/MCRoomScreen.h"
#include "ui/MCRmlDialog.h"
#include "ui/MCRmlFlow.h"
#include "logistics/MCUpdateModel.h"
#include "logistics/MCUpdatePage.h"
#include "network/MCLobbyUpdate.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

namespace
{
    using MCUpdater::MCStage;
    using MCUpdater::MCStatus;
    using MCUpdateWork::MCFailureKind;
    using Rml::StringUtilities::EncodeRml;

    constexpr const char* UpdateDocument = "update.rml";

    /// <summary>The document's places (ui/update.rml).</summary>
    constexpr const char* RunningId = "running";
    constexpr const char* BringsId = "brings";
    constexpr const char* ChangesId = "changes";
    constexpr const char* NoteListId = "note-list";
    constexpr const char* VersionId = "version";
    constexpr const char* SizeFactId = "size-fact";
    constexpr const char* SizeId = "size";
    constexpr const char* StatusId = "status";
    constexpr const char* MeterId = "meter";
    constexpr const char* MeterFillId = "meter-fill";
    constexpr const char* BackId = "back";
    constexpr const char* SecondId = "second";
    constexpr const char* ActId = "act";

    constexpr const char* HiddenClass = "hidden";

    /// <summary>The title bar's words, and the notes' head.</summary>
    constexpr std::string_view RunningFormat = "Running {}";
    constexpr std::string_view BringsFormat = "What {} brings";
    constexpr std::string_view OneChange = "1 change";
    constexpr std::string_view ChangesFormat = "{} changes";
    constexpr std::string_view VersionFormat = "Version {}";

    /// <summary>A note's leading mark in the release's notes (CHANGELOG.md's list items).</summary>
    constexpr std::string_view NoteMark = "- ";

    /// <summary>The status line's words (SPEC-menus.md's UPDATE, and its "When something goes wrong").</summary>
    constexpr std::string_view FoundText = "A newer version is out.";
    constexpr std::string_view FoundSizeFormat = "A newer version is out: {} to download.";
    constexpr std::string_view DownloadingFormat = "Downloading: {}.";
    constexpr std::string_view StoppedFormat = "The download stopped at {}.";
    constexpr std::string_view InRestartFormat = "Version {} is in. Restart to play it.";
    constexpr std::string_view InNextStartFormat = "Version {} is in. It plays when the game starts again.";
    constexpr std::string_view PlayersStartFormat = "Version {} installs when you start Redux HD yourself.";
    constexpr std::string_view AsItQuitsFormat = "Version {} installs as you quit.";
    constexpr std::string_view InstallingText = "Installing. The game will restart by itself.";
    constexpr std::string_view UnreachableText = "Couldn't reach the update server.";
    constexpr std::string_view NoSpaceFormat = "Not enough space: {} needed, {} free.";
    constexpr std::string_view DamagedText = "The download was damaged.";
    constexpr std::string_view FailedFormat = "The update failed: {}";

    /// <summary>What a meter's fill is as wide as, when the whole is known.</summary>
    constexpr double WholePercent = 100.0;

    /// <summary>The remade UPDATE made last (the flow makes it afresh at each press).</summary>
    MCGuiHandle<MCUpdateScreen> TheScreen;

    /// <summary>What's in of a download: of the whole once that's known ("3.1 GB of 7.4 GB"), else what's in.</summary>
    std::string InHand(const MCStatus& status)
    {
        return status.Expected == 0 ? MCUpdateWork::SizeText(status.Received)
                                    : std::format("{} of {}", MCUpdateWork::SizeText(status.Received),
                                                  MCUpdateWork::SizeText(status.Expected));
    }

    std::string ReadyText(const MCStatus& status)
    {
        const std::string release(ReleaseOf(status.Version));

        // A game picked on INTERNET to join once this is in (network/MCLobbyUpdate.h).
        if (std::string joining = MCLobbyUpdate::JoinAfterLine(); !joining.empty())
        {
            return joining;
        }

        // Port-only (stage 4): over a room, what RESTART does there (network/MCLobbyUpdate.h), as a sentence.
        if (MCLobbyUpdate::InRoom())
        {
            if (const std::string room = MCLobbyUpdate::ReadyText(); !room.empty())
            {
                return MCRmlDialogs::AsSentence(room);
            }
        }

        switch (status.GoesIn)
        {
            case MCUpdater::MCGoesIn::NextStart:
                return MCUpdater::CanStartAgain() ? std::format(InRestartFormat, release)
                                                  : std::format(InNextStartFormat, release);
            case MCUpdater::MCGoesIn::PlayersStart:
                return std::format(PlayersStartFormat, release);
            case MCUpdater::MCGoesIn::AsItQuits:
                break;
        }

        return std::format(AsItQuitsFormat, release);
    }

    std::string FailedText(const MCStatus& status)
    {
        switch (status.Failure)
        {
            case MCFailureKind::Unreachable:
                return std::string(UnreachableText);
            case MCFailureKind::DiskFull:
            {
                if (status.NeededBytes != 0)
                {
                    return std::format(NoSpaceFormat, MCUpdateWork::SizeText(status.NeededBytes),
                                       MCUpdateWork::SizeText(status.FreeBytes));
                }

                break;
            }
            case MCFailureKind::Damaged:
                return std::string(DamagedText);
            case MCFailureKind::NewerProgram:
            case MCFailureKind::Other:
                break;
        }

        // The updater's own sentence (the log's), ended as a sentence.
        std::string text = std::format(FailedFormat, status.Error);

        if (!text.empty() && text.back() != '.')
        {
            text += '.';
        }

        return text;
    }

    void SetWords(Rml::ElementDocument& document, const char* id, const std::string& words)
    {
        if (Rml::Element* element = document.GetElementById(id); element != nullptr)
        {
            if (const std::string encoded = EncodeRml(words); element->GetInnerRML() != encoded)
            {
                element->SetInnerRML(encoded);
            }
        }
    }

    void SetShown(Rml::ElementDocument& document, const char* id, bool shown)
    {
        if (Rml::Element* element = document.GetElementById(id);
            element != nullptr && element->IsClassSet(HiddenClass) == shown)
        {
            element->SetClass(HiddenClass, !shown);
        }
    }

    /// <summary>The release notes' lines, each a change: blank lines between paragraphs left out, a list's mark taken off.</summary>
    std::vector<std::string> NoteLines(std::string_view notes)
    {
        std::vector<std::string> lines;

        for (const auto part : std::views::split(notes, '\n'))
        {
            std::string_view line(part.begin(), part.end());

            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front())) != 0)
            {
                line.remove_prefix(1);
            }

            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0)
            {
                line.remove_suffix(1);
            }

            if (line.starts_with(NoteMark))
            {
                line.remove_prefix(NoteMark.size());
            }

            if (!line.empty())
            {
                lines.emplace_back(line);
            }
        }

        return lines;
    }
}

MCUpdateScreen::MCUpdateScreen()
{
    SetDocument(UpdateDocument);
    Keys = {{"Esc", "Back"}};
}

void MCUpdateScreen::OpenFromMenus()
{
    // Logged as it failed: the old page, over the main menu.
    if (!Open())
    {
        ShowUpdatePage();
    }
}

bool MCUpdateScreen::Open()
{
    if (!MCRmlFlow::Menus().has_value())
    {
        return false;
    }

    // Refused while another change runs: the press goes no further.
    MCScreenFlow::Open(MCMenuScreens::Update());
    return true;
}

std::string_view MCUpdateScreen::Word(MCAct act)
{
    switch (act)
    {
        case MCAct::Install:
            return "Install";
        case MCAct::Skip:
            return "Skip";
        case MCAct::Stop:
            return "Stop";
        case MCAct::Resume:
            return "Resume";
        case MCAct::Restart:
            return "Restart";
        case MCAct::Retry:
            return "Retry";
        case MCAct::None:
            break;
    }

    return {};
}

bool MCUpdateScreen::Shows()
{
    const std::shared_ptr<MCUpdateScreen> screen = TheScreen.Lock();
    return screen != nullptr && screen->Shown();
}

std::string MCUpdateScreen::StatusText(const MCStatus& status)
{
    switch (status.Stage)
    {
        case MCStage::None:
            return {};
        case MCStage::Found:
            return status.Expected == 0 ? std::string(FoundText)
                                        : std::format(FoundSizeFormat, MCUpdateWork::SizeText(status.Expected));
        case MCStage::Downloading:
            return MCUpdater::DownloadStopped() ? std::format(StoppedFormat, InHand(status))
                                                : std::format(DownloadingFormat, InHand(status));
        case MCStage::Ready:
            return ReadyText(status);
        case MCStage::Installing:
            return std::string(InstallingText);
        case MCStage::Failed:
            return FailedText(status);
    }

    return {};
}

void MCUpdateScreen::OnLoaded(Rml::ElementDocument& document)
{
    TheScreen = std::static_pointer_cast<MCUpdateScreen>(shared_from_this());
    SetWords(document, RunningId, std::format(RunningFormat, ReleaseOf(MCUpdater::RunningVersion())));
    OnClick(BackId,
            [this]
            {
                if (Back)
                {
                    Back();
                }
            });
    OnClick(SecondId, [this] { Press(_Second); });
    OnClick(ActId, [this] { Press(_Main); });
    Refresh();
}

void MCUpdateScreen::OnShow()
{
    // The game to join once it's in, set by INTERNET or LAN as it opened UPDATE (after the screen was made): said now.
    Refresh();
}

void MCUpdateScreen::OnDestroy()
{
    TheScreen.reset();
}

MCOptionalRef<Rml::Element> MCUpdateScreen::FirstFocus(Rml::ElementDocument& document)
{
    // The one thing to do about the update, else BACK.
    Rml::Element* act = document.GetElementById(ActId);

    if (act != nullptr && !act->IsClassSet(HiddenClass))
    {
        return MCOptionalRef<Rml::Element>(*act);
    }

    Rml::Element* back = document.GetElementById(BackId);
    return back != nullptr ? MCOptionalRef<Rml::Element>(*back) : std::nullopt;
}

void MCUpdateScreen::OnFrame()
{
    Refresh();
}

void MCUpdateScreen::Refresh()
{
    const MCOptionalRef<Rml::ElementDocument> shown = Document();

    if (!shown.has_value())
    {
        return;
    }

    Rml::ElementDocument& document = shown->get();
    const MCStatus& status = MCUpdater::Status();
    const std::string release(ReleaseOf(status.Version));
    SetWords(document, VersionId, release.empty() ? std::string() : std::format(VersionFormat, release));
    SetWords(document, BringsId, release.empty() ? std::string() : std::format(BringsFormat, release));
    ShowNotes(status);

    // The download's size while it's to come or coming.
    const bool sized =
        status.Expected != 0 &&
        (status.Stage == MCStage::Found || status.Stage == MCStage::Downloading || status.Stage == MCStage::Failed);
    SetShown(document, SizeFactId, sized);
    SetWords(document, SizeId, sized ? MCUpdateWork::SizeText(status.Expected) : std::string());

    if (std::string words = StatusText(status); words != _Status)
    {
        _Status = std::move(words);
        SetWords(document, StatusId, _Status);
    }

    // The meter while it downloads, in place of nothing once it's stopped (its line says where).
    const bool metered = status.Stage == MCStage::Downloading && !MCUpdater::DownloadStopped() && status.Expected != 0;
    SetShown(document, MeterId, metered);

    if (Rml::Element* fill = document.GetElementById(MeterFillId); fill != nullptr && metered)
    {
        const double percent = std::min(WholePercent, WholePercent * static_cast<double>(status.Received) /
                                                          static_cast<double>(status.Expected));
        fill->SetProperty(Rml::PropertyId::Width, Rml::Property(static_cast<float>(percent), Rml::Unit::PERCENT));
    }

    // The one thing to do about it, at the right; a second beside it.
    MCAct main = MCAct::None;
    MCAct second = MCAct::None;

    switch (status.Stage)
    {
        case MCStage::Found:
        {
            main = MCAct::Install;
            second = MCAct::Skip;
            break;
        }
        case MCStage::Downloading:
        {
            if (MCUpdater::DownloadStopped())
            {
                main = MCAct::Resume;
            }
            else if (MCUpdater::CanStopDownload())
            {
                second = MCAct::Stop;
            }

            break;
        }
        case MCStage::Ready:
            main = UpdateRestartOffered() ? MCAct::Restart : MCAct::None;
            break;
        case MCStage::Failed:
            main = status.Retry ? MCAct::Retry : MCAct::None;
            break;
        case MCStage::None:
        case MCStage::Installing:
            break;
    }

    if (main == _Main && second == _Second)
    {
        return;
    }

    _Main = main;
    _Second = second;
    SetWords(document, ActId, std::string(Word(main)));
    SetWords(document, SecondId, std::string(Word(second)));
    SetShown(document, ActId, main != MCAct::None);
    SetShown(document, SecondId, second != MCAct::None);
    Keys.clear();

    if (main != MCAct::None)
    {
        Keys.push_back({"Enter", std::string(Word(main))});
    }

    Keys.push_back({"Esc", "Back"});
    ShowKeys();
}

void MCUpdateScreen::ShowNotes(const MCStatus& status)
{
    if (status.Notes == _Notes)
    {
        return;
    }

    _Notes = status.Notes;
    const std::vector<std::string> lines = NoteLines(_Notes);
    std::string rml;

    for (const std::string& line : lines)
    {
        rml += std::format("<div class=\"note\"><div class=\"dot\"></div><div class=\"grow\">{}</div></div>",
                           EncodeRml(line));
    }

    const MCOptionalRef<Rml::ElementDocument> document = Document();

    if (!document.has_value())
    {
        return;
    }

    if (Rml::Element* list = document->get().GetElementById(NoteListId); list != nullptr)
    {
        list->SetInnerRML(rml);
    }

    SetWords(document->get(), ChangesId,
             lines.empty()       ? std::string()
             : lines.size() == 1 ? std::string(OneChange)
                                 : std::format(ChangesFormat, lines.size()));
}

void MCUpdateScreen::Press(MCAct act)
{
    switch (act)
    {
        case MCAct::Install:
        case MCAct::Retry:
            MCUpdater::Answer(MCUpdater::MCAnswer::Install);
            break;
        case MCAct::Skip:
        {
            // This version isn't offered again: nothing is left to show.
            MCUpdater::Answer(MCUpdater::MCAnswer::Skip);

            if (Back)
            {
                Back();
            }

            return;
        }
        case MCAct::Stop:
            MCUpdater::StopDownload();
            break;
        case MCAct::Resume:
            MCUpdater::ResumeDownload();
            break;
        case MCAct::Restart:
            RestartForUpdate();
            break;
        case MCAct::None:
            return;
    }

    Refresh();
}
