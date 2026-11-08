#include "stdafx.h"
#include "MCTest.h"
#include "fakes/MCServiceScript.h"
#include "network/MCInternetLobby.h"

#include <nlohmann/json.hpp>

// The INTERNET screens' view of the games service (network/MCInternetLobby): the list's socket fed by a script of the
// service's messages (its protocol as tools/cloudflare/games/README.md in the workspace has it), the count's request
// by a stand-in transfer, and the busiest hours as the empty list says them. The real service is checked by its own
// checks (check.mjs).

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view Service = "wss://games.test";
    constexpr std::string_view ListUrl = "wss://games.test/list?proto=1";
    constexpr std::string_view CountUrl = "https://games.test/count";
    constexpr std::string_view Version = "0.6.0 (812)";
    /// <summary>A dropped list comes back in a second (the list's first try), well within this.</summary>
    constexpr auto ComebackPatience = std::chrono::seconds(3);
    constexpr auto StepWait = std::chrono::milliseconds(10);
    constexpr int32_t DayHours = 24;
    constexpr std::chrono::minutes UtcAlone{0};
    constexpr std::chrono::minutes NewYork = -std::chrono::hours(5);
    constexpr std::chrono::minutes Kolkata = std::chrono::hours(5) + std::chrono::minutes(30);

    using MCServiceScript::ListScript;
    using MCServiceScript::StandInGet;

    /// <summary>The list's sockets scripted by <paramref name="script"/> and the count's transfer, for the test's time.</summary>
    class LobbySetupScope : public MCServiceScript::SetupScope
    {
    public:
        LobbySetupScope(ListScript& script, std::function<std::unique_ptr<MCHttpSource::MCGet>()> get)
            : SetupScope(MCServiceScript::ListOpener(script), std::move(get), Service, Version)
        {
        }
    };

    Json ListedGame(std::string_view code, std::string_view name, Json battleSeconds)
    {
        return MCServiceScript::ListedGame(code, name, {"BOB", "ANN"}, 6, std::move(battleSeconds), Version);
    }

    /// <summary>A week whose busiest hours are Friday to Sunday, 19:00 to 23:00 UTC (others quieter, some unseen).</summary>
    Json WeekendEvenings()
    {
        Json hours = Json::array();

        for (size_t hour = 0; hour < MCWeekHours; hour++)
        {
            const auto day = static_cast<int32_t>(hour) / DayHours;
            const auto ofDay = static_cast<int32_t>(hour) % DayHours;
            const bool weekend = day == 0 || day >= 5;
            const bool evening = ofDay >= 19 && ofDay < 23;

            if (ofDay == 4)
            {
                hours.push_back(nullptr);
            }
            else
            {
                hours.push_back(weekend ? (evening ? 80.0 : 20.0) : (evening ? 34.0 : 4.0));
            }
        }

        return hours;
    }

    MCBusiestHours HoursOf(const Json& hours)
    {
        MCBusiestHours out;

        for (const Json& hour : hours)
        {
            out.push_back(hour.is_null() ? std::nullopt : std::optional(hour.get<double>()));
        }

        return out;
    }
}

TEST_CASE("internet lobby: the busiest hours are said in the player's own time, days then hours around their peaks")
{
    const MCBusiestHours week = HoursOf(WeekendEvenings());
    CHECK(MCBusiestText(week, UtcAlone) == "FRI TO SUN, 7 TO 11 PM");
    // In New York the same hours are Friday to Sunday, 2 to 6 PM; in Kolkata they're the next mornings, half past
    // midnight to half past four, said to the hour: Saturday to Monday, 1 to 5 AM.
    CHECK(MCBusiestText(week, NewYork) == "FRI TO SUN, 2 TO 6 PM");
    CHECK(MCBusiestText(week, Kolkata) == "SAT TO MON, 1 TO 5 AM");

    // Saturday night alone is busy, 10 PM to 2 AM: the night is said as its evening's day's.
    MCBusiestHours saturday(MCWeekHours, 1.0);

    for (int32_t hour = 22; hour < DayHours + 2; hour++)
    {
        saturday[static_cast<size_t>(6 * DayHours + hour) % MCWeekHours] = 50.0;
    }

    CHECK(MCBusiestText(saturday, UtcAlone) == "SAT, 10 PM TO 2 AM");

    // Every hour the same: nothing to single out; no peaks: nothing to say; not the week's hours: nothing.
    CHECK(MCBusiestText(MCBusiestHours(MCWeekHours, 3.0), UtcAlone).empty());
    CHECK(MCBusiestText(MCBusiestHours(MCWeekHours), UtcAlone).empty());
    CHECK(MCBusiestText(MCBusiestHours(MCWeekHours, 0.0), UtcAlone).empty());
    CHECK(MCBusiestText(MCBusiestHours(DayHours, 3.0), UtcAlone).empty());
}

TEST_CASE("internet lobby: the list as the service sends it, a game that isn't one left out, and back after a drop")
{
    ListScript script;
    LobbySetupScope scope(script, nullptr);
    std::unique_ptr<MCInternetList> list = MCInternetList::Open();
    REQUIRE(list != nullptr);
    REQUIRE(script.Opened.size() == 1);
    CHECK(script.Opened[0] == ListUrl);
    CHECK(!list->Update());
    CHECK(!list->Listing().has_value());

    // As it opens: the counts, an open game, one in battle, one that isn't the service's (a bad code), and the hours.
    Json first{{"t", "list"},
               {"online", 37},
               {"inGames", 12},
               {"games",
                {ListedGame("K7QF", "BOB'S GAME", nullptr), ListedGame("PXYL", "ANN'S GAME", 720),
                 ListedGame("K0QF", "NOT A CODE", nullptr)}},
               {"busiest", WeekendEvenings()}};
    script.Send(first);
    CHECK(list->Update());
    REQUIRE(list->Listing().has_value());
    const MCInternetListing& listing = *list->Listing();
    CHECK(listing.Online == 37);
    CHECK(listing.InGames == 12);
    REQUIRE(listing.Games.size() == 2);
    CHECK(listing.Games[0].Code == "K7QF");
    CHECK(listing.Games[0].Name == "BOB'S GAME");
    CHECK((listing.Games[0].Players == std::vector<std::string>{"BOB", "ANN"}));
    CHECK(listing.Games[0].Max == 6);
    CHECK(listing.Games[0].Region == "EU");
    CHECK(listing.Games[0].Version == Version);
    CHECK(!listing.Games[0].BattleSeconds.has_value());
    CHECK(listing.Games[1].BattleSeconds == 720);
    CHECK(MCBusiestText(list->Busiest(), UtcAlone) == "FRI TO SUN, 7 TO 11 PM");

    // A change: the counts and games without the hours, which stay; the same list again changes nothing.
    Json second{{"t", "list"}, {"online", 38}, {"inGames", 12}, {"games", {ListedGame("K7QF", "BOB'S GAME", nullptr)}}};
    script.Send(second);
    CHECK(list->Update());
    CHECK(list->Listing()->Online == 38);
    CHECK(list->Listing()->Games.size() == 1);
    CHECK(list->Busiest().size() == MCWeekHours);
    script.Send(second);
    CHECK(!list->Update());

    // What isn't a list is ignored, the list kept: not JSON, a field of the wrong kind, a name past its length.
    script.Inbox.push_back({false, "not json"});
    script.Send(Json{{"t", "list"}, {"online", "many"}, {"inGames", 0}, {"games", Json::array()}});
    Json longName = second;
    longName["games"][0]["name"] = std::string(25, 'A');
    script.Send(longName);
    CHECK(list->Update());
    CHECK(list->Listing()->Online == 38);
    CHECK(list->Listing()->Games.empty());

    // A drop (the service's deploy): the list is gone until the socket's back, a second later.
    script.State = MCWebSocket::State::Closed;
    CHECK(list->Update());
    CHECK(!list->Listing().has_value());
    const auto deadline = Clock::now() + ComebackPatience;

    while (script.Opened.size() < 2 && Clock::now() < deadline)
    {
        list->Update();
        std::this_thread::sleep_for(StepWait);
    }

    REQUIRE(script.Opened.size() == 2);
    script.Send(first);
    CHECK(list->Update());
    CHECK(list->Listing()->Online == 37);

    // Another protocol: turned away for good, the socket closed and never opened again.
    script.Send(Json{{"t", "update"}});
    CHECK(list->Update());
    CHECK(list->Refusal() == MCServiceRefusal::Update);
    CHECK(!list->Listing().has_value());
    CHECK(script.ClosedByGame);
    CHECK(!list->Update());
    CHECK(script.Opened.size() == 2);

    // Closing the screen closes its socket: the service counts it no longer.
    std::unique_ptr<MCInternetList> again = MCInternetList::Open();
    CHECK(!script.ClosedByGame);
    again.reset();
    CHECK(script.ClosedByGame);
}

TEST_CASE("internet lobby: the count asked once by the service's plain request, a failure saying nothing")
{
    ListScript script;
    std::string asked;
    const auto waitDone = [](MCInternetCount& count)
    {
        const auto deadline = Clock::now() + ComebackPatience;

        while (!count.Done() && Clock::now() < deadline)
        {
            std::this_thread::sleep_for(StepWait);
        }

        return count.Done();
    };

    {
        LobbySetupScope scope(
            script, [&] { return std::make_unique<StandInGet>(StandInGet::Kind::Answer, R"({"online":37})", asked); });
        std::unique_ptr<MCInternetCount> count = MCInternetCount::Ask();
        REQUIRE(count != nullptr);
        REQUIRE(waitDone(*count));
        CHECK(asked == CountUrl);
        CHECK(count->Online() == 37);
    }

    for (const auto& [kind, body] : {std::pair{StandInGet::Kind::Fail, std::string()},
                                     std::pair{StandInGet::Kind::Answer, std::string(R"({"online":-1})")},
                                     std::pair{StandInGet::Kind::Answer, std::string("37")}})
    {
        LobbySetupScope scope(script, [&] { return std::make_unique<StandInGet>(kind, body, asked); });
        std::unique_ptr<MCInternetCount> count = MCInternetCount::Ask();
        REQUIRE(count != nullptr);
        REQUIRE(waitDone(*count));
        CHECK(!count->Online().has_value());
    }

    // Leaving the screen while the service says nothing: the request stops, and the screen isn't held.
    {
        LobbySetupScope scope(script, [&] { return std::make_unique<StandInGet>(StandInGet::Kind::Hold, "", asked); });
        std::unique_ptr<MCInternetCount> count = MCInternetCount::Ask();
        REQUIRE(count != nullptr);
        CHECK(!count->Done());
        const auto before = Clock::now();
        count.reset();
        CHECK(Clock::now() - before < ComebackPatience);
    }

    // No transfer set: nothing is asked.
    LobbySetupScope scope(script, nullptr);
    CHECK(MCInternetCount::Ask() == nullptr);
}
