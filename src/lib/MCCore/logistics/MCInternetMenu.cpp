#include "stdafx.h"
#include "logistics/MCInternetMenu.h"
#include "logistics/MCIdleCheck.h"
#include "logistics/MCUpdatePage.h"
#include "main/MCUpdater.h"
#include "network/MCLobbyUpdate.h"
#include "gui/MCGuiEvent.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCGuiSystem.h"
#include "linkup/MCFidpSession.h"
#include "linkup/MCSessionManager.h"
#include "logistics/MCArtWords.h"
#include "logistics/MCConnectMenu.h"
#include "logistics/MCLogButton.h"
#include "logistics/MCLogGroupBox.h"
#include "logistics/MCLogLabel.h"
#include "logistics/MCLogMenus.h"
#include "logistics/MCLogTextButton.h"
#include "logistics/MCLogTextObject.h"
#include "logistics/MCRegistrySettings.h"
#include "logistics/MCReusableDialog.h"
#include "logistics/MCSplashScreen.h"
#include "main/MCGameStrings.h"
#include "main/MCLogistics.h"
#include "network/MCInternetGame.h"
#include "network/MCMultiPlayer.h"

#include <charconv>

namespace
{
    using Clock = std::chrono::steady_clock;

    // ---- MULTIPLAYER ACCESS (mpscreen.fit) ----

    /// <summary>Its buttons: MODEM, SERIAL, LAN (NETWORK's place), INTERNET.</summary>
    constexpr int32_t ModemButton = 2;
    constexpr int32_t SerialButton = 3;
    constexpr int32_t LanButton = 4;
    constexpr int32_t InternetButton = 5;
    /// <summary>
    /// Where its fit puts the buttons. INTERNET takes the first place, its count the second's room, and LAN stays in
    /// NETWORK's place; the original's INTERNET is the fourth.
    /// </summary>
    constexpr int32_t ButtonsLeft = 390;
    constexpr int32_t FirstPlaceTop = 152;
    constexpr int32_t NetworkPlaceTop = 214;
    constexpr int32_t InternetPlaceTop = 245;
    constexpr int32_t BigButtonWidth = 175;
    constexpr int32_t BigButtonHeight = 28;
    /// <summary>The count's line, under INTERNET.</summary>
    constexpr int32_t CountGap = 4;
    /// <summary>The access screen's timer (the count's answer looked for).</summary>
    constexpr int16_t AccessTimer = 1;
    constexpr int32_t AccessTimerMs = 250;
    constexpr std::string_view OnlineFormat = "{} ONLINE";
    /// <summary>The original's NETWORK pictures, put back for the original's connection screen.</summary>
    constexpr std::string_view NetworkUp = "bh_lan.tga";
    constexpr std::string_view NetworkDown = "bg_lan.tga";
    constexpr std::string_view NetworkGray = "bn_lan.tga";

    // ---- INTERNET CONNECTION (lanscreen.fit) ----

    /// <summary>Its elements, as the LAN screen's.</summary>
    constexpr int32_t CancelButton = 1;
    constexpr int32_t GameListElement = 2;
    constexpr int32_t PlayerListElement = 3;
    constexpr int32_t PlayerNameEntry = 4;
    constexpr int32_t HostButton = 5;
    constexpr int32_t JoinButton = 6;
    constexpr int32_t PanelImage = 7;
    constexpr int32_t CreateButton = 9;
    constexpr int32_t GameNameEntry = 10;
    constexpr int32_t PlayersEntry = 11;
    constexpr int32_t BrowseBlock = 0;
    constexpr int32_t HostBlock = 1;
    /// <summary>The LAN screen's: its lists' font and the column for the players.</summary>
    constexpr int32_t ListFont = 1;
    constexpr int32_t SlotsColumn = 0x73;
    constexpr uint8_t PickedHighlight = 0x14;
    /// <summary>The lists' colours: a game's line, the picked one's, one that can't be joined; the players.</summary>
    constexpr uint8_t GameColor = 0x0c;
    constexpr uint8_t PickedColor = 0x1f;
    constexpr uint8_t GreyedColor = 0x19;
    constexpr uint8_t PlayerColor = 0x1f;
    /// <summary>Where a click on the list picks (as the LAN list's): its rows less the scroll bar.</summary>
    constexpr int32_t RowGap = 4;
    constexpr int32_t ScrollBarWidth = 0xd;
    /// <summary>The counts' line, over the list; the CODE box, right of the player's name.</summary>
    constexpr int32_t CountsLeft = 361;
    constexpr int32_t CountsTop = 131;
    constexpr int32_t CodeLeft = 478;
    constexpr int32_t CodeLabelTop = 300;
    constexpr int32_t CodeTop = 309;
    constexpr int32_t CodeWidth = 32;
    constexpr int32_t EntryHeight = 13;
    constexpr uint8_t EntryBackground = 0x10;
    /// <summary>The art's boxes round its entries: a line in this colour, a pixel left and two rows above the text.</summary>
    constexpr uint8_t BoxOutline = 0x13;
    constexpr int32_t BoxLeftGap = 1;
    constexpr int32_t BoxTopGap = 2;
    /// <summary>PUBLIC or PRIVATE, under the game's name in the host block.</summary>
    constexpr int32_t PrivacyLeft = 359;
    constexpr int32_t PrivacyTop = 328;
    /// <summary>The game's code (the service's four characters), the player's and game's names' lengths (the LAN screen's).</summary>
    constexpr int32_t CodeLength = 4;
    constexpr int32_t PlayerNameLength = 0x10;
    constexpr int32_t GameNameLength = 0x18;
    constexpr int32_t FewestPlayers = 2;
    constexpr int32_t MostPlayers = 6;
    /// <summary>The screen's timer: the list and a game being joined or hosted moved along.</summary>
    constexpr int16_t InternetTimer = 1;
    constexpr int32_t InternetTimerMs = 100;
    /// <summary>A guest's search for its host's room over the link: every half second, for ten seconds.</summary>
    constexpr auto JoinEvery = std::chrono::milliseconds(500);
    constexpr int32_t JoinTries = 20;
    /// <summary>The game's string for "<player>'s game" (the LAN screen's).</summary>
    constexpr uint32_t GameNameString = 0x377;
    constexpr int32_t SecondsPerMinute = 60;

    constexpr std::string_view PublicWord = "PUBLIC";
    constexpr std::string_view PrivateWord = "PRIVATE";
    constexpr std::string_view CodeWord = "CODE";
    constexpr std::string_view CountsFormat = "{} ONLINE, {} IN GAMES";
    constexpr std::string_view LookingText = "LOOKING FOR GAMES...";
    constexpr std::string_view NoGamesText = "NO GAMES ARE OPEN RIGHT NOW. HOST ONE AND IT WILL APPEAR HERE.";
    constexpr std::string_view BusiestFormat = "\n\nMOST PLAYERS ARE ON:\n{}";
    constexpr std::string_view RegionFormat = "HOSTED IN {}";
    /// <summary>A game in battle under a minute: no time said.</summary>
    constexpr std::string_view InBattleLine = "IN BATTLE";
    constexpr std::string_view InBattleForFormat = "IN BATTLE FOR {} MIN";
    constexpr std::string_view OnMissionFormat = "{}, ON {}";
    constexpr std::string_view NeedsFormat = "NEEDS VERSION {}";
    constexpr std::string_view JoinUpdatesLine = "PRESS JOIN GAME TO UPDATE AND JOIN.";
    constexpr std::string_view ConnectingFormat = "CONNECTING TO {}...";
    constexpr std::string_view HostingText = "SETTING UP YOUR GAME...";
    constexpr std::string_view CodeGameFormat = "GAME {}";
    constexpr std::string_view NoGameText = "THERE'S NO GAME WITH THAT CODE.";
    constexpr std::string_view FullText = "THAT GAME IS FULL.";
    constexpr std::string_view InBattleText = "THAT GAME HAS ALREADY STARTED.";
    constexpr std::string_view UnreachableFormat = "COULDN'T CONNECT TO {}. TRY AGAIN.";
    constexpr std::string_view NotHostedText = "COULDN'T SET UP YOUR GAME. TRY AGAIN.";
    constexpr std::string_view UpdateText = "UPDATE YOUR GAME TO PLAY OVER THE INTERNET.";
    constexpr std::string_view TooManyText = "TOO MANY TRIES. WAIT A MINUTE AND TRY AGAIN.";
    constexpr std::string_view BannedText = "THIS COPY OF THE GAME IS BLOCKED FROM INTERNET GAMES.";
    constexpr std::string_view CrowdedText = "TOO MANY PLAYERS ON YOUR NETWORK ARE IN GAMES RIGHT NOW.";
    constexpr std::string_view NoInternetText = "THIS VERSION CAN'T PLAY INTERNET GAMES.";

    /// <summary>The continents Cloudflare names a host's location by.</summary>
    constexpr std::array<std::pair<std::string_view, std::string_view>, 7> Regions{{
        {"AF", "AFRICA"},
        {"AN", "ANTARCTICA"},
        {"AS", "ASIA"},
        {"EU", "EUROPE"},
        {"NA", "N. AMERICA"},
        {"OC", "OCEANIA"},
        {"SA", "S. AMERICA"},
    }};

    std::string Upper(std::string_view text)
    {
        std::string upper(text);
        std::ranges::transform(upper, upper.begin(),
                               [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
        return upper;
    }

    std::string_view RegionName(std::string_view region)
    {
        const auto found = std::ranges::find(Regions, region, &std::pair<std::string_view, std::string_view>::first);
        return found != Regions.end() ? found->second : std::string_view();
    }

    /// <summary>A version's release ("0.6.0" of "0.6.0 (812)") as numbers; none when it isn't one.</summary>
    std::optional<std::vector<int32_t>> ReleaseOf(std::string_view version)
    {
        const std::string_view release = version.substr(0, version.find(' '));
        std::vector<int32_t> parts;
        size_t at = 0;

        while (at <= release.size())
        {
            const size_t dot = std::min(release.find('.', at), release.size());
            int32_t part = 0;
            const auto [end, error] = std::from_chars(release.data() + at, release.data() + dot, part);

            if (error != std::errc() || end != release.data() + dot)
            {
                return std::nullopt;
            }

            parts.push_back(part);
            at = dot + 1;
        }

        return parts;
    }

    /// <summary>Whether a game on <paramref name="version"/> needs a newer release than this game's.</summary>
    bool NeedsNewer(std::string_view version)
    {
        const std::optional<std::vector<int32_t>> theirs = ReleaseOf(version);
        const std::optional<std::vector<int32_t>> ours = ReleaseOf(MCInternetGame::SetupNow().Version);
        return theirs.has_value() && ours.has_value() && *theirs > *ours;
    }

    /// <summary>
    /// Whether the update this game has found brings <paramref name="game"/>'s release or a later one, so JOIN GAME
    /// updates first (the UPDATE page over the screen, the new version joining the game).
    /// </summary>
    bool UpdateBrings(const MCInternetListing::Game& game)
    {
        if (!NeedsNewer(game.Version) || !MCUpdater::Available() || !MCUpdater::Waiting() ||
            MCUpdater::Status().Stage == MCUpdater::MCStage::Failed)
        {
            return false;
        }

        const std::optional<std::vector<int32_t>> theirs = ReleaseOf(game.Version);
        const std::optional<std::vector<int32_t>> update = ReleaseOf(MCUpdater::Status().Version);
        return theirs.has_value() && update.has_value() && *update >= *theirs;
    }

    // ---- the screens' state ----

    /// <summary>MULTIPLAYER ACCESS's count's line, and the count being asked.</summary>
    MCGuiHandle<MCLogLabel> CountLine;
    std::unique_ptr<MCInternetCount> Count;

    /// <summary>INTERNET CONNECTION's added parts, and the service's list while it shows.</summary>
    MCGuiHandle<MCInternetGameList> Games;
    MCGuiHandle<MCLogLabel> CountsLine;
    MCGuiHandle<MCLogLabel> CodeLabel;
    MCGuiHandle<MCLogGroupBox> CodeBox;
    MCGuiHandle<MCLogTextObject> CodeEntry;
    MCGuiHandle<MCLogTextButton> PublicButton;
    MCGuiHandle<MCLogTextButton> PrivateButton;
    std::unique_ptr<MCInternetList> List;
    /// <summary>The list let go while the player is away (logistics/MCIdleCheck.h), opened again at the next input.</summary>
    bool ListAway = false;
    bool HostsPublic = true;
    int32_t ShownBlock = BrowseBlock;
    /// <summary>Whether the player list says JOIN GAME UPDATES (drawn again when the update is found after a pick).</summary>
    bool ShownUpdateBrings = false;

    /// <summary>A game being joined or hosted, moving along on the screen's timer.</summary>
    struct PendingGame
    {
        bool Hosting = false;
        /// <summary>The game as the messages name it: its name, or "GAME K7QF" for a code typed.</summary>
        std::string Shown;
        std::string PlayerName;
        std::string GameName;
        int32_t MaxPlayers = MostPlayers;
        Clock::time_point NextSearch;
        int32_t Searches = 0;
    };

    std::optional<PendingGame> Pending;

    MCSplashScreen& InternetScreen()
    {
        return *GlobalLog()->InternetScreen;
    }

    MCSessionManager& Manager()
    {
        return *MultiPlayer()->SessionManager;
    }

    /// <summary>INTERNET CONNECTION's own parts shown as its block <paramref name="block"/> is.</summary>
    void ShowInternetBlock(int32_t block)
    {
        MCSplashScreen& screen = InternetScreen();
        screen.ShowBlock(block);
        ShownBlock = block;
        CountsLine->ShowGuiWindow(true);
        CodeLabel->ShowGuiWindow(block == BrowseBlock);
        CodeBox->ShowGuiWindow(block == BrowseBlock);
        CodeEntry->ShowGuiWindow(block == BrowseBlock);
        PublicButton->ShowGuiWindow(block == HostBlock && HostsPublic);
        PrivateButton->ShowGuiWindow(block == HostBlock && !HostsPublic);
    }

    /// <summary>The picked game's players, then where it is and what it's doing, in the player list.</summary>
    void ShowPickedPlayers()
    {
        auto players = InternetScreen().Element<MCLogScrollTextObject>(PlayerListElement);
        players->Clear();
        ShownUpdateBrings = false;
        const std::optional<MCInternetListing::Game> game = Games->Picked();

        if (!game.has_value())
        {
            return;
        }

        for (const std::string& player : game->Players)
        {
            players->Print(Upper(player), PlayerColor);
        }

        players->PrintBlank(PlayerColor);
        std::vector<std::string> details;

        // What the player can do first (the list is narrow and short): the version it needs, or once the update that
        // brings it is found, how to join it; then its battle; then where it's hosted.
        if (NeedsNewer(game->Version))
        {
            ShownUpdateBrings = UpdateBrings(*game);
            details.push_back(ShownUpdateBrings
                                  ? std::string(JoinUpdatesLine)
                                  : std::format(NeedsFormat, game->Version.substr(0, game->Version.find(' '))));
        }

        if (game->BattleSeconds.has_value())
        {
            const int64_t minutes = *game->BattleSeconds / SecondsPerMinute;
            std::string battle = minutes > 0 ? std::format(InBattleForFormat, minutes) : std::string(InBattleLine);
            details.push_back(game->Mission.empty() ? battle
                                                    : std::format(OnMissionFormat, battle, Upper(game->Mission)));
        }

        if (const std::string_view region = RegionName(game->Region); !region.empty())
        {
            details.push_back(std::format(RegionFormat, region));
        }

        for (const std::string& detail : details)
        {
            players->PrintWrapped(detail, GreyedColor, -1);
        }
    }

    /// <summary>The code typed in the CODE box, upper-cased, when it's a code's length.</summary>
    std::optional<std::string> TypedCode()
    {
        std::string code = Upper(CodeEntry->Text().substr(0, CodeEntry->Text().find('\0')));
        std::erase(code, ' ');
        return static_cast<int32_t>(code.size()) == CodeLength ? std::optional(code) : std::nullopt;
    }

    /// <summary>
    /// JOIN GAME takes a code typed, or a game picked that can be joined (or that the update this game found brings).
    /// </summary>
    void UpdateJoinButton()
    {
        const std::optional<MCInternetListing::Game> picked = Games->Picked();
        const bool updateBrings = picked.has_value() && UpdateBrings(*picked);
        InternetScreen().Element<MCLogButton>(JoinButton)->Disabled =
            !TypedCode().has_value() &&
            !(picked.has_value() && (MCInternetGameList::Joinable(*picked) || updateBrings));

        // The updater found (or lost) the update that brings the picked game since the list was drawn.
        if (updateBrings != ShownUpdateBrings)
        {
            ShowPickedPlayers();
        }
    }

    /// <summary>The list and the counts as the service last said them.</summary>
    void ShowListing()
    {
        const std::optional<MCInternetListing>& listing = List != nullptr ? List->Listing() : std::nullopt;
        std::string empty(NoGamesText);

        if (List != nullptr)
        {
            if (const std::string busiest = MCBusiestText(List->Busiest(), MCLocalUtcOffset()); !busiest.empty())
            {
                empty += std::format(BusiestFormat, busiest);
            }
        }

        const std::string_view waiting =
            List != nullptr && List->Refusal() == MCServiceRefusal::Update ? UpdateText : LookingText;
        Games->Show(listing, waiting, empty);
        CountsLine->SetText(listing.has_value() ? std::format(CountsFormat, listing->Online, listing->InGames) : "");
        ShowPickedPlayers();
        UpdateJoinButton();
    }

    // ---- joining and hosting ----

    /// <summary>The message dialog saying <paramref name="text"/> while a game is joined or hosted, its button a way out.</summary>
    void ShowConnecting(std::string_view text, std::function<void()> cancel)
    {
        MCReusableDialog& dialog = *GlobalLog()->MessageDialog;
        dialog.SetText(text);
        dialog.SetTwoButton(false);
        dialog.Callback = nullptr;
        dialog.OkButton->SetUpPicture("bh_cancl.tga");
        dialog.OkButton->SetDownPicture("bg_cancl.tga");
        dialog.OkButton->Disabled = false;
        dialog.OkButton->Callback().SetExec(std::move(cancel));
        dialog.Activate();
    }

    /// <summary>Gives up the game being joined or hosted: its internet game ended, and <paramref name="why"/> said (if anything).</summary>
    void EndPending(std::string_view why)
    {
        if (!Pending.has_value())
        {
            return;
        }

        SDL_Log("INTERNET CONNECTION: %s %s ended: %.*s", Pending->Hosting ? "hosting" : "joining",
                Pending->Shown.c_str(), static_cast<int>(why.size()), why.data());
        Pending.reset();

        if (MultiPlayer() != nullptr && MultiPlayer()->SessionManager != nullptr)
        {
            Manager().EndInternet();
        }

        GlobalLog()->MessageDialog->Deactivate(0);

        if (!why.empty())
        {
            ShowMenuMessage(why);
        }
    }

    void CancelPending()
    {
        EndPending({});
    }

    /// <summary>What the player is told when the service turned a join or a host away, or nothing answered.</summary>
    std::string FailureText(const PendingGame& pending, std::optional<MCServiceRefusal> refusal)
    {
        switch (refusal.value_or(MCServiceRefusal::Unreachable))
        {
            case MCServiceRefusal::NoGame:
                return std::string(NoGameText);
            case MCServiceRefusal::Full:
                return std::string(FullText);
            case MCServiceRefusal::InBattle:
                return std::string(InBattleText);
            case MCServiceRefusal::Update:
                return std::string(UpdateText);
            case MCServiceRefusal::TooManyTries:
                return std::string(TooManyText);
            case MCServiceRefusal::Banned:
                return std::string(BannedText);
            case MCServiceRefusal::Crowded:
                return std::string(CrowdedText);
            // Anything else the service refused reads as the game not reached or not set up.
            default:
                return pending.Hosting ? std::string(NotHostedText) : std::format(UnreachableFormat, pending.Shown);
        }
    }

    /// <summary>The game joined or hosted: its room (the session screen) opens.</summary>
    void EnterRoom()
    {
        Pending.reset();
        GlobalLog()->MessageDialog->Deactivate(0);
        GlobalLog()->SetUpSessionScreen();
    }

    /// <summary>Moves the game being joined or hosted along: the service's answer, the link, then the room.</summary>
    void TendPending()
    {
        if (!Pending.has_value())
        {
            return;
        }

        MCSessionManager& manager = Manager();

        if (manager.Internet == nullptr)
        {
            EndPending(FailureText(*Pending, std::nullopt));
            return;
        }

        const MCInternetGame::Stage stage = manager.Internet->Update();

        if (stage == MCInternetGame::Stage::Failed)
        {
            EndPending(FailureText(*Pending, manager.Internet->Refusal()));
            return;
        }

        if (stage != MCInternetGame::Stage::Ready)
        {
            return;
        }

        if (Pending->Hosting)
        {
            if (MultiPlayer()->CreateSession(Pending->GameName, Pending->PlayerName, Pending->MaxPlayers) != 0)
            {
                EndPending(NotHostedText);
                return;
            }

            EnterRoom();
            return;
        }

        // A guest's link is open: its host's room is looked for over it, then joined.
        const Clock::time_point now = Clock::now();

        if (now < Pending->NextSearch)
        {
            return;
        }

        Pending->NextSearch = now + JoinEvery;

        if (++Pending->Searches > JoinTries)
        {
            EndPending(FailureText(*Pending, std::nullopt));
            return;
        }

        const std::optional<std::vector<std::shared_ptr<MCFidpSession>>> rooms = manager.GetSessions();

        if (!rooms.has_value() || rooms->empty())
        {
            return;
        }

        const MCFidpSession& room = *rooms->front();

        if (room.SessionDesc.dwCurrentPlayers >= room.SessionDesc.dwMaxPlayers)
        {
            EndPending(FullText);
            return;
        }

        if (manager.JoinSession(room.SessionDesc.guidInstance, Pending->PlayerName) == 0)
        {
            NumLanPlayers = static_cast<int32_t>(manager.GetPlayers(nullptr).size());
            EnterRoom();
            return;
        }

        // The host refused this game at the door, saying why, or the room can't be joined.
        const std::string refusal = manager.RefusalReason();
        EndPending(!refusal.empty() ? MCRefusedText(refusal) : LoadGameString(0xa7, 0xfe));
    }

    /// <summary>
    /// Begins joining (<paramref name="code"/>) or hosting a game, "CONNECTING TO ..." shown meanwhile: the screen's
    /// timer moves it along. A public game (<paramref name="isPublic"/>: a host's choice, or a guest's game from the
    /// service's list) takes relays only.
    /// </summary>
    void BeginPending(PendingGame pending, std::string_view code, bool isPublic)
    {
        if (MultiPlayer() == nullptr || MultiPlayer()->SessionManager == nullptr)
        {
            return;
        }

        MCSessionManager& manager = Manager();
        // The connection is chosen first (choosing it again would leave the session, and the game with it); a guest's
        // address is its game's code, as the links name it.
        manager.ConnectTcp(pending.Hosting ? std::string_view() : code);

        if (!manager.BeginInternet(pending.Hosting, code, isPublic))
        {
            ShowMenuMessage(NoInternetText);
            return;
        }

        SDL_Log("INTERNET CONNECTION: %s %s", pending.Hosting ? "hosting" : "joining", pending.Shown.c_str());
        ShowConnecting(pending.Hosting ? std::string(HostingText) : std::format(ConnectingFormat, pending.Shown),
                       CancelPending);
        Pending = std::move(pending);
    }

    // ---- the buttons ----

    /// <summary>The listed game with <paramref name="code"/>, if any.</summary>
    std::optional<MCInternetListing::Game> ListedGame(std::string_view code)
    {
        if (List == nullptr || !List->Listing().has_value())
        {
            return std::nullopt;
        }

        const std::vector<MCInternetListing::Game>& games = List->Listing()->Games;
        const auto found = std::ranges::find(games, code, &MCInternetListing::Game::Code);
        return found != games.end() ? std::optional(*found) : std::nullopt;
    }

    std::string PlayerName()
    {
        std::string name = ElementText(InternetScreen(), PlayerNameEntry);
        SaveUserName(name);
        return name;
    }

    void JoinInternetGame()
    {
        if (Pending.has_value())
        {
            return;
        }

        PendingGame pending{.PlayerName = PlayerName()};
        std::string code;
        // A game on the service's list is public: its links take relays only, whatever the service says after (its
        // host can't make it private for those who found it there).
        bool listed = false;

        if (const std::optional<std::string> typed = TypedCode())
        {
            code = *typed;
            const std::optional<MCInternetListing::Game> game = ListedGame(code);
            listed = game.has_value();
            pending.Shown = listed ? Upper(game->Name) : std::format(CodeGameFormat, code);
        }
        else if (const std::optional<MCInternetListing::Game> picked = Games->Picked();
                 picked.has_value() && MCInternetGameList::Joinable(*picked))
        {
            code = picked->Code;
            listed = true;
            pending.Shown = Upper(picked->Name);
        }
        else if (picked.has_value() && UpdateBrings(*picked))
        {
            // A newer game: the update first, its new version joining the game (network/MCLobbyUpdate.h).
            MCLobbyUpdate::JoinAfterUpdate(MCLobbyUpdate::MCJoinAfter{
                .Code = picked->Code, .Name = picked->Name, .PlayerName = pending.PlayerName, .RelayOnly = true});
            ShowUpdatePageOver(InternetScreen());
            return;
        }
        else
        {
            return;
        }

        BeginPending(std::move(pending), code, listed);
    }

    void HostInternetGame()
    {
        MCSplashScreen& screen = InternetScreen();
        const std::string playerName = PlayerName();
        ShowInternetBlock(HostBlock);
        GuiSystem().SetText(screen.Elements[GameNameEntry]);
        screen.Element<MCLogTextObject>(GameNameEntry)
            ->SetStringBuffer(MCFormatPrintf(LoadGameString(GameNameString, 0xfe).c_str(), playerName.c_str()));
    }

    void CreateInternetSession()
    {
        if (Pending.has_value())
        {
            return;
        }

        MCSplashScreen& screen = InternetScreen();
        PendingGame pending{.Hosting = true,
                            .PlayerName = PlayerName(),
                            .GameName = ElementText(screen, GameNameEntry),
                            .MaxPlayers = std::clamp(ElementNumber(screen, PlayersEntry), FewestPlayers, MostPlayers)};
        pending.Shown = Upper(pending.GameName);
        BeginPending(std::move(pending), {}, HostsPublic);
    }

    void SetHostsPublic(bool isPublic)
    {
        HostsPublic = isPublic;
        PublicButton->ShowGuiWindow(isPublic);
        PrivateButton->ShowGuiWindow(!isPublic);
    }

    /// <summary>CANCEL: from the host block back to the games, from the games back to MULTIPLAYER ACCESS.</summary>
    void CancelInternet()
    {
        MCSplashScreen& screen = InternetScreen();

        if (ShownBlock == HostBlock)
        {
            ShowInternetBlock(BrowseBlock);
            GuiSystem().SetText(screen.Elements[PlayerNameEntry]);
            return;
        }

        screen.ShowGuiWindow(false);
        ShowAccessScreen();
    }

    /// <summary>INTERNET CONNECTION's events: its timer, and a game picked or gone.</summary>
    void InternetScreenHandleEvent(MCGuiObject&, MCGuiEvent& event)
    {
        if (event.Type == MCGuiEventType::Timer && event.Data == InternetTimer)
        {
            // A player away isn't counted online: the list's socket goes until the next input.
            if (MCIdleCheck::Away() && List != nullptr && !Pending.has_value())
            {
                SDL_Log("INTERNET CONNECTION: the player is away: the list closes until the next input");
                List.reset();
                ListAway = true;
                ShowListing();
            }
            else if (ListAway && !MCIdleCheck::Away())
            {
                List = MCInternetList::Open();
                ListAway = false;
                ShowListing();
            }

            if (List != nullptr && List->Update())
            {
                ShowListing();
            }

            UpdateJoinButton();
            TendPending();
            return;
        }

        if (event.Type == MCGuiEventType::Focus &&
            (event.Data == MCLogNotice::GameSelected || event.Data == MCLogNotice::GameLost))
        {
            ShowPickedPlayers();
            UpdateJoinButton();
        }
    }

    /// <summary>MULTIPLAYER ACCESS's events: its timer, the count's answer looked for.</summary>
    void AccessScreenHandleEvent(MCGuiObject&, MCGuiEvent& event)
    {
        if (event.Type != MCGuiEventType::Timer || event.Data != AccessTimer || Count == nullptr || !Count->Done())
        {
            return;
        }

        // Nothing when the service can't be reached.
        if (const std::optional<int32_t> online = Count->Online())
        {
            CountLine->SetText(std::format(OnlineFormat, *online));
            CountLine->MoveTo(ButtonsLeft + (BigButtonWidth - CountLine->Width()) / 2, CountLine->Y());
            CountLine->ShowGuiWindow(true);
        }

        Count.reset();
        GuiSystem().RemoveTimer(*GlobalLog()->MultiplayerScreen, AccessTimer);
    }

    /// <summary>Lays the access screen's buttons: INTERNET first, its count, then LAN; or the original's four.</summary>
    void LayAccessButtons(bool lanAndInternet)
    {
        MCSplashScreen& screen = *GlobalLog()->MultiplayerScreen;
        screen.Element<MCGuiObject>(ModemButton)->ShowGuiWindow(!lanAndInternet);
        screen.Element<MCGuiObject>(SerialButton)->ShowGuiWindow(!lanAndInternet);
        auto lan = screen.Element<MCLogButton>(LanButton);
        auto internet = screen.Element<MCLogButton>(InternetButton);
        const int32_t internetTop = lanAndInternet ? FirstPlaceTop : InternetPlaceTop;
        lan->MoveTo(ButtonsLeft, NetworkPlaceTop);
        internet->MoveTo(ButtonsLeft, internetTop);
        CountLine->MoveTo(CountLine->X(), internetTop + BigButtonHeight + CountGap);

        // LAN's pictures are made of the art's letters (and their HD copies, made again as HD art may have come on).
        std::optional<MCButtonPictures> pictures = lanAndInternet ? MCLanButtonPictures() : std::nullopt;

        if (pictures.has_value())
        {
            lan->UpPicture = pictures->Up;
            lan->OverPicture = pictures->Up;
            lan->DownPicture = pictures->Down;
            lan->GrayPicture = pictures->Gray;
        }
        else
        {
            lan->SetUpPicture(NetworkUp);
            lan->SetOverPicture(NetworkUp);
            lan->SetDownPicture(NetworkDown);
            lan->SetGrayPicture(NetworkGray);
        }
    }
}

int32_t MCInternetGameList::Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height, std::string_view text)
{
    const int32_t result = MCLogScrollTextObject::Init(xPos, yPos, width, height, text);
    TabColumn = SlotsColumn;
    HighlightColor[0] = PickedHighlight;
    FontIndex = ListFont;
    return result;
}

bool MCInternetGameList::Joinable(const MCInternetListing::Game& game)
{
    return !game.BattleSeconds.has_value() && static_cast<int32_t>(game.Players.size()) < game.Max &&
           !NeedsNewer(game.Version);
}

void MCInternetGameList::Show(const std::optional<MCInternetListing>& listing, std::string_view waiting,
                              std::string_view empty)
{
    _Games = listing.has_value() ? listing->Games : std::vector<MCInternetListing::Game>();
    const bool kept =
        _PickedCode.has_value() && std::ranges::contains(_Games, *_PickedCode, &MCInternetListing::Game::Code);

    if (_PickedCode.has_value() && !kept)
    {
        _PickedCode.reset();
        MCGuiEvent lost;
        lost.Type = MCGuiEventType::Focus;
        lost.Data = MCLogNotice::GameLost;
        lost.LParam = -1;
        ParentObject()->HandleEvent(lost);
    }

    _Lines.clear();

    if (!listing.has_value())
    {
        _Lines.emplace_back(waiting);
    }
    else if (_Games.empty())
    {
        _Lines.emplace_back(empty);
    }

    Rebuild();
}

void MCInternetGameList::Rebuild()
{
    Clear();

    // Why there are none, wrapped to the list's width.
    for (const std::string& line : _Lines)
    {
        size_t at = 0;

        while (at <= line.size())
        {
            const size_t end = std::min(line.find('\n', at), line.size());

            if (end == at)
            {
                PrintBlank(GameColor);
            }
            else
            {
                PrintWrapped(std::string_view(line).substr(at, end - at), GameColor, Width() - ScrollBarWidth);
            }

            at = end + 1;
        }
    }

    // "name <tab> players/most", or FULL; one that can't be joined greyed; the pick highlighted.
    for (size_t i = 0; i < _Games.size(); i++)
    {
        const MCInternetListing::Game& game = _Games[i];
        const auto players = static_cast<int32_t>(game.Players.size());
        const std::string slots = players >= game.Max ? "FULL" : std::format("{}/{}", players, game.Max);
        const std::string line = std::format("{}\t{}", Upper(game.Name), slots);
        const bool picked = _PickedCode == game.Code;

        if (picked)
        {
            HighlightLine[0] = static_cast<int32_t>(i);
        }

        Print(line, picked ? PickedColor : Joinable(game) ? GameColor : GreyedColor);
    }
}

void MCInternetGameList::HandleEvent(MCGuiEvent& event)
{
    if (event.Type != MCGuiEventType::LeftButtonDown || _Games.empty())
    {
        return;
    }

    const int32_t rowHeight = Fonts[0][FontIndex]->Height() + RowGap;
    const int32_t clickX = event.X - GlobalX();
    const int32_t clickY = FirstPixel + event.Y - GlobalY();

    if (clickX < 1 || clickX >= Width() - ScrollBarWidth || clickY < 0)
    {
        return;
    }

    const auto row = static_cast<size_t>(clickY / rowHeight);

    if (row < _Games.size())
    {
        Pick(_Games[row].Code);
    }
}

std::optional<MCInternetListing::Game> MCInternetGameList::Picked() const
{
    if (!_PickedCode.has_value())
    {
        return std::nullopt;
    }

    const auto found = std::ranges::find(_Games, *_PickedCode, &MCInternetListing::Game::Code);
    return found != _Games.end() ? std::optional(*found) : std::nullopt;
}

void MCInternetGameList::Pick(std::string_view code)
{
    if (!std::ranges::contains(_Games, code, &MCInternetListing::Game::Code))
    {
        return;
    }

    _PickedCode = std::string(code);
    Rebuild();
    MCGuiEvent picked;
    picked.Type = MCGuiEventType::Focus;
    picked.Data = MCLogNotice::GameSelected;
    ParentObject()->HandleEvent(picked);
}

void MCInternetGameList::ClearPick()
{
    _PickedCode.reset();
    Rebuild();
}

bool HasInternetGames()
{
    return MCInternetGame::Available();
}

void SetUpAccessScreen(MCSplashScreen& screen)
{
    screen.SetEventRoutine(AccessScreenHandleEvent);
    CountLine = screen.CreateChild<MCLogLabel>(
        [](MCLogLabel& label)
        {
            label.InitAt(ButtonsLeft, InternetPlaceTop + BigButtonHeight + CountGap);
            label.ShowGuiWindow(false);
        },
        std::string_view());
}

void ShowAccessScreen()
{
    MCSplashScreen& screen = *GlobalLog()->MultiplayerScreen;
    LayAccessButtons(true);
    screen.Element<MCLogButton>(LanButton)->Disabled = false;
    screen.Element<MCLogButton>(InternetButton)->Disabled = false;
    GlobalLog()->MainScreen->ShowGuiWindow(false);
    screen.ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(screen);
    GlobalLog()->LogisticsState = 10;

    // The count, asked once as it opens: shown when it comes, nothing when the service can't be reached.
    CountLine->ShowGuiWindow(false);
    Count = MCInternetCount::Ask();

    if (Count != nullptr)
    {
        GuiSystem().AddTimer(screen.weak_from_this(), AccessTimer, AccessTimerMs, 0, 0, false);
    }
}

void ShowOriginalAccessButtons()
{
    LayAccessButtons(false);
    CountLine->ShowGuiWindow(false);
}

void AccessScreenHidden()
{
    if (GlobalLog() == nullptr || GlobalLog()->MultiplayerScreen == nullptr)
    {
        return;
    }

    Count.reset();
    GuiSystem().RemoveTimer(*GlobalLog()->MultiplayerScreen, AccessTimer);
}

namespace
{
    /// <summary>
    /// The panel retitled INTERNET CONNECTION (as it was, if the art isn't the original's), with its HD copy: made again
    /// as the screen shows, as HD art may have come on since.
    /// </summary>
    void RetitlePanel(MCSplashScreen& screen)
    {
        if (std::shared_ptr<MCLogPort> panel = MCInternetPanel(); panel != nullptr)
        {
            screen.Element<MCLogImage>(PanelImage)->Art = std::move(panel);
        }
    }
}

void SetUpInternetScreen(MCSplashScreen& screen)
{
    screen.SetEventRoutine(InternetScreenHandleEvent);

    RetitlePanel(screen);

    // The service's games in the LAN list's place.
    const std::shared_ptr<MCGuiObject> lanList = screen.Element<MCGuiObject>(GameListElement);
    const int32_t left = lanList->X();
    const int32_t top = lanList->Y();
    const int32_t width = lanList->Width();
    const int32_t height = lanList->Height();
    lanList->Close();
    Games = screen.CreateChild<MCInternetGameList>([&](MCInternetGameList& list)
                                                   { list.Init(left, top, width, height, {}); });
    screen.Elements[GameListElement] = Games.Weak();

    auto players = screen.Element<MCLogScrollTextObject>(PlayerListElement);
    players->FontIndex = ListFont;

    CountsLine = screen.CreateChild<MCLogLabel>([](MCLogLabel& label) { label.InitAt(CountsLeft, CountsTop); },
                                                std::string_view());
    CodeLabel =
        screen.CreateChild<MCLogLabel>([](MCLogLabel& label) { label.InitAt(CodeLeft, CodeLabelTop); }, CodeWord);
    screen.CreateChild<MCLogGroupBox>(
        [](MCLogGroupBox& box)
        {
            box.Init(CodeLeft - BoxLeftGap, CodeTop - BoxTopGap, CodeWidth + 2 * BoxLeftGap,
                     EntryHeight + BoxTopGap + 1);
            box.SetTransparent(true);
            CodeBox = box.WeakAs<MCLogGroupBox>();
        },
        BoxOutline, EntryBackground);
    CodeEntry = screen.CreateChild<MCLogTextObject>(
        [](MCLogTextObject& entry)
        {
            entry.MCLogObject::Init(CodeLeft, CodeTop, CodeWidth, EntryHeight);
            entry.CursorPos = 0;
            entry.CursorPixel = 0;
            entry.Font = MedWhiteFont;
            entry.SetBackColor(EntryBackground);
            // Its buffer holds the code and the end of its text.
            entry.InitBuffer(CodeLength + 1, MCLogInputType::Text);
        });
    PublicButton = screen.CreateChild<MCLogTextButton>(
        [](MCLogTextButton& button)
        { button.InitWithLabel(PrivacyLeft, PrivacyTop, PublicWord, [] { SetHostsPublic(false); }); });
    PrivateButton = screen.CreateChild<MCLogTextButton>(
        [](MCLogTextButton& button)
        { button.InitWithLabel(PrivacyLeft, PrivacyTop, PrivateWord, [] { SetHostsPublic(true); }); });

    screen.Element<MCLogButton>(CancelButton)->Callback().SetExec(CancelInternet);
    screen.Element<MCLogButton>(HostButton)->Callback().SetExec(HostInternetGame);
    screen.Element<MCLogButton>(JoinButton)->Callback().SetExec(JoinInternetGame);
    screen.Element<MCLogButton>(CreateButton)->Callback().SetExec(CreateInternetSession);

    auto nameEntry = screen.Element<MCLogTextObject>(PlayerNameEntry);
    nameEntry->Font = MedWhiteFont;
    nameEntry->SetBackColor(EntryBackground);
    nameEntry->InitBuffer(PlayerNameLength, MCLogInputType::Text);
    auto gameEntry = screen.Element<MCLogTextObject>(GameNameEntry);
    gameEntry->Font = MedWhiteFont;
    gameEntry->SetBackColor(EntryBackground);
    gameEntry->InitBuffer(GameNameLength, MCLogInputType::Text);
    auto playersEntry = screen.Element<MCLogTextObject>(PlayersEntry);
    playersEntry->Font = MedWhiteFont;
    playersEntry->SetBackColor(EntryBackground);
    playersEntry->InitBuffer(2, MCLogInputType::Port);
    playersEntry->SetStringBuffer(std::to_string(MostPlayers));
    ShowInternetBlock(BrowseBlock);
}

void ShowInternetScreen()
{
    MCSplashScreen& screen = InternetScreen();
    RetitlePanel(screen);
    GlobalLog()->MultiplayerScreen->ShowGuiWindow(false);
    screen.ShowGuiWindow(true);
    GlobalLog()->SetCurrentScreen(screen);
    GlobalLog()->LogisticsState = 0xb;
    screen.Element<MCLogTextObject>(PlayerNameEntry)->SetStringBuffer(MyGetUserName().value_or("Player"));
    CodeEntry->SetStringBuffer("");
    Games->ClearPick();
    ShowInternetBlock(BrowseBlock);
    GuiSystem().SetText(screen.Elements[PlayerNameEntry]);

    // The list, kept open while the screen shows.
    List = MCInternetList::Open();
    ListAway = false;
    ShowListing();
    GuiSystem().AddTimer(screen.weak_from_this(), InternetTimer, InternetTimerMs, 0, 0, false);
}

void InternetScreenHidden()
{
    if (GlobalLog() == nullptr || GlobalLog()->InternetScreen == nullptr)
    {
        return;
    }

    // Its socket's close takes it off the service's count; a game joined is counted as it plays. A newer game picked to
    // join after the update is no longer.
    List.reset();
    ListAway = false;
    MCLobbyUpdate::JoinAfterUpdate(std::nullopt);
    GuiSystem().RemoveTimer(*GlobalLog()->InternetScreen, InternetTimer);

    if (Pending.has_value())
    {
        EndPending({});
    }
}

std::string AccessCountShown()
{
    return CountLine != nullptr && CountLine->ShowWindow ? CountLine->Text() : std::string();
}

std::string InternetCountsShown()
{
    return CountsLine != nullptr && CountsLine->ShowWindow ? CountsLine->Text() : std::string();
}

bool InternetHostsPublic()
{
    return HostsPublic;
}
