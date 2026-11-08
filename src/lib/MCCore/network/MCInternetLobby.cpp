#include "stdafx.h"
#include "network/MCInternetLobby.h"

#include "lib/MCThreadKeeper.h"
#include "platform/MCServices.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <span>

namespace
{
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    constexpr std::string_view ListPath = "/list";
    constexpr std::string_view CountPath = "/count";
    /// <summary>The service's sockets' scheme and the plain requests' that go with it.</summary>
    constexpr std::array<std::pair<std::string_view, std::string_view>, 2> Schemes{{
        {"wss://", "https://"},
        {"ws://", "http://"},
    }};
    constexpr int HttpOk = 200;

    /// <summary>The service's limits (its protocol.js and list.js): what's past them isn't the service's.</summary>
    constexpr size_t MostListed = 100;
    constexpr size_t MostPlayers = 6;
    constexpr int32_t FewestPlayers = 2;
    constexpr size_t MostGameName = MCServiceGameNameMost;
    constexpr size_t MostPlayerName = MCServicePlayerNameMost;
    constexpr size_t MostMission = MCServiceMissionMost;
    constexpr size_t MostVersion = 32;
    constexpr size_t MostRegion = 8;
    constexpr size_t MostType = 16;
    /// <summary>A count past any the service will hold: one past it isn't the service's.</summary>
    constexpr uint64_t MostCount = 10'000'000;
    /// <summary>The count's answer, {"online":n}: anything longer isn't it.</summary>
    constexpr size_t MostCountBody = 256;

    /// <summary>
    /// Coming back after a drop, as every socket to the service does: each try waits up to twice the last, from a
    /// second up to half a minute, the first anywhere in its first few seconds (the service's deploy drops every list
    /// at once).
    /// </summary>
    constexpr MCRetryDelay Comeback = MCServiceComebackTries;

    /// <summary>A busy hour or day: at least this share of the busiest one's mean.</summary>
    constexpr double BusyShare = 0.75;
    constexpr int32_t DayHours = 24;
    constexpr int32_t WeekDays = 7;
    constexpr int32_t HalfDay = 12;
    constexpr std::chrono::minutes HourMinutes{60};
    constexpr std::array<std::string_view, WeekDays> DayNames = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

    /// <summary>A plain request's address on the service whose sockets are at <paramref name="service"/>.</summary>
    std::optional<std::string> HttpAddress(std::string_view service, std::string_view path)
    {
        for (const auto& [socket, plain] : Schemes)
        {
            if (service.starts_with(socket))
            {
                return std::format("{}{}{}", plain, service.substr(socket.size()), path);
            }
        }

        return std::nullopt;
    }

    /// <summary>A text field the screens show: printable ASCII, at most <paramref name="most"/>; none otherwise.</summary>
    std::optional<std::string> ShownText(const Json& value, size_t most)
    {
        if (!value.is_string())
        {
            return std::nullopt;
        }

        const std::string& text = value.get_ref<const std::string&>();

        if (text.size() > most || !std::ranges::all_of(text, [](char c) { return c >= ' ' && c <= '~'; }))
        {
            return std::nullopt;
        }

        return text;
    }

    std::optional<std::string> ShownField(const Json& message, const char* name, size_t most)
    {
        const auto found = message.find(name);
        return found != message.end() ? ShownText(*found, most) : std::nullopt;
    }

    std::optional<int64_t> CountIn(const Json& value)
    {
        if (!value.is_number_unsigned() || value.get<uint64_t>() > MostCount)
        {
            return std::nullopt;
        }

        return static_cast<int64_t>(value.get<uint64_t>());
    }

    std::optional<int64_t> CountField(const Json& message, const char* name)
    {
        const auto found = message.find(name);
        return found != message.end() ? CountIn(*found) : std::nullopt;
    }

    /// <summary>A listed game as the service sends it; none when it isn't one.</summary>
    std::optional<MCInternetListing::Game> GameIn(const Json& entry)
    {
        if (!entry.is_object())
        {
            return std::nullopt;
        }

        MCInternetListing::Game game;
        const std::optional<std::string> code = ShownField(entry, "code", MostType);
        const std::optional<std::string> name = ShownField(entry, "name", MostGameName);
        const std::optional<int64_t> max = CountField(entry, "max");
        const std::optional<std::string> region = ShownField(entry, "region", MostRegion);
        const std::optional<std::string> version = ShownField(entry, "version", MostVersion);
        const auto players = entry.find("players");
        const auto mission = entry.find("mission");
        const auto battle = entry.find("battleSeconds");

        if (!code || !MCIsGameCode(*code) || !name || !max || *max < FewestPlayers ||
            *max > static_cast<int64_t>(MostPlayers) || !region || !version || players == entry.end() ||
            !players->is_array() || players->size() > MostPlayers || mission == entry.end() || battle == entry.end())
        {
            return std::nullopt;
        }

        for (const Json& player : *players)
        {
            std::optional<std::string> playerName = ShownText(player, MostPlayerName);

            if (!playerName)
            {
                return std::nullopt;
            }

            game.Players.push_back(std::move(*playerName));
        }

        // The mission, and the battle's time, are null until the host picks one and its battle starts.
        if (!mission->is_null())
        {
            std::optional<std::string> missionName = ShownText(*mission, MostMission);

            if (!missionName)
            {
                return std::nullopt;
            }

            game.Mission = std::move(*missionName);
        }

        if (!battle->is_null())
        {
            game.BattleSeconds = CountIn(*battle);

            if (!game.BattleSeconds)
            {
                return std::nullopt;
            }
        }

        game.Code = *code;
        game.Name = *name;
        game.Max = static_cast<int32_t>(*max);
        game.Region = *region;
        game.Version = *version;
        return game;
    }

    /// <summary>The list in a "list" message; none when it isn't one (a game that isn't one is left out, logged).</summary>
    std::optional<MCInternetListing> ListingIn(const Json& message)
    {
        const std::optional<int64_t> online = CountField(message, "online");
        const std::optional<int64_t> inGames = CountField(message, "inGames");
        const auto games = message.find("games");

        if (!online || !inGames || games == message.end() || !games->is_array() || games->size() > MostListed)
        {
            return std::nullopt;
        }

        MCInternetListing listing{.Online = static_cast<int32_t>(*online), .InGames = static_cast<int32_t>(*inGames)};

        for (const Json& entry : *games)
        {
            if (std::optional<MCInternetListing::Game> game = GameIn(entry))
            {
                listing.Games.push_back(std::move(*game));
            }
            else
            {
                SDL_Log("MCInternetList: the service listed a game that isn't one; it's left out");
            }
        }

        return listing;
    }

    /// <summary>The week's busiest hours in a list message; none when it has none, or they aren't the week's.</summary>
    std::optional<MCBusiestHours> BusiestIn(const Json& message)
    {
        const auto found = message.find("busiest");

        if (found == message.end() || !found->is_array() || found->size() != MCWeekHours)
        {
            return std::nullopt;
        }

        MCBusiestHours hours;

        for (const Json& hour : *found)
        {
            if (hour.is_null())
            {
                hours.emplace_back();
                continue;
            }

            if (!hour.is_number() || !std::isfinite(hour.get<double>()) || hour.get<double>() < 0)
            {
                return std::nullopt;
            }

            hours.emplace_back(hour.get<double>());
        }

        return hours;
    }

    /// <summary>The mean of the values present; none when none is.</summary>
    std::optional<double> MeanOf(std::span<const std::optional<double>> values)
    {
        double sum = 0;
        int32_t count = 0;

        for (const std::optional<double>& value : values)
        {
            if (value.has_value())
            {
                sum += *value;
                count++;
            }
        }

        return count > 0 ? std::optional(sum / count) : std::nullopt;
    }

    /// <summary>A circular run of busy entries around the busiest: its first and last, inclusive.</summary>
    struct BusyRun
    {
        /// <summary>No value at all: nothing to say.</summary>
        bool None = true;
        /// <summary>Every entry is busy: nothing to single out.</summary>
        bool All = false;
        int32_t First = 0;
        int32_t Last = 0;
    };

    BusyRun BusyRunIn(std::span<const std::optional<double>> values)
    {
        const auto size = static_cast<int32_t>(values.size());
        std::optional<int32_t> peak;

        for (int32_t i = 0; i < size; i++)
        {
            if (values[i].has_value() && (!peak.has_value() || *values[i] > *values[*peak]))
            {
                peak = i;
            }
        }

        if (!peak.has_value() || *values[*peak] <= 0)
        {
            return {};
        }

        const double busy = *values[*peak] * BusyShare;
        const auto isBusy = [&](int32_t i) { return values[i].has_value() && *values[i] >= busy; };
        BusyRun run{.None = false, .First = *peak, .Last = *peak};
        int32_t length = 1;

        while (length < size && isBusy((run.First + size - 1) % size))
        {
            run.First = (run.First + size - 1) % size;
            length++;
        }

        while (length < size && isBusy((run.Last + 1) % size))
        {
            run.Last = (run.Last + 1) % size;
            length++;
        }

        run.All = length == size;
        return run;
    }

    /// <summary>An hour of the day on a 12-hour clock: its number and whether it's after noon.</summary>
    std::pair<int32_t, bool> TwelveHour(int32_t hour)
    {
        const int32_t shown = hour % HalfDay;
        return {shown == 0 ? HalfDay : shown, hour % DayHours >= HalfDay};
    }

    std::string HoursText(int32_t first, int32_t end)
    {
        const auto [from, fromPm] = TwelveHour(first);
        const auto [to, toPm] = TwelveHour(end);
        const std::string_view toHalf = toPm ? "PM" : "AM";

        if (fromPm == toPm)
        {
            return std::format("{} TO {} {}", from, to, toHalf);
        }

        return std::format("{} {} TO {} {}", from, fromPm ? "PM" : "AM", to, toHalf);
    }

    std::string DaysText(int32_t first, int32_t last)
    {
        if (first == last)
        {
            return std::string(DayNames[first]);
        }

        return std::format("{} TO {}", DayNames[first], DayNames[last]);
    }
}

std::string MCBusiestText(const MCBusiestHours& hours, std::chrono::minutes utcOffset)
{
    if (hours.size() != MCWeekHours)
    {
        return {};
    }

    // The week in the player's own time: local hour L is UTC hour L - offset.
    const auto week = static_cast<int32_t>(MCWeekHours);
    const auto shift = static_cast<int32_t>(std::lround(static_cast<double>(utcOffset.count()) / HourMinutes.count()));
    std::vector<std::optional<double>> local(MCWeekHours);

    for (int32_t hour = 0; hour < week; hour++)
    {
        local[hour] = hours[static_cast<size_t>(((hour - shift) % week + week) % week)];
    }

    std::array<std::optional<double>, DayHours> dayHours;

    for (int32_t hour = 0; hour < DayHours; hour++)
    {
        std::array<std::optional<double>, WeekDays> sameHour;

        for (int32_t day = 0; day < WeekDays; day++)
        {
            sameHour[day] = local[static_cast<size_t>(day * DayHours + hour)];
        }

        dayHours[hour] = MeanOf(sameHour);
    }

    const BusyRun busyHours = BusyRunIn(dayHours);

    if (busyHours.None)
    {
        return {};
    }

    // Each day by the hours said (an evening past midnight is its starting day's), or by all its hours.
    const int32_t windowStart = busyHours.All ? 0 : busyHours.First;
    const int32_t windowLength =
        busyHours.All ? DayHours : (busyHours.Last - busyHours.First + DayHours) % DayHours + 1;
    std::array<std::optional<double>, WeekDays> days;

    for (int32_t day = 0; day < WeekDays; day++)
    {
        std::vector<std::optional<double>> window;

        for (int32_t hour = 0; hour < windowLength; hour++)
        {
            window.push_back(local[static_cast<size_t>((day * DayHours + windowStart + hour) % week)]);
        }

        days[day] = MeanOf(window);
    }

    const BusyRun busyDays = BusyRunIn(days);

    if (busyDays.None)
    {
        return {};
    }

    std::vector<std::string> parts;

    if (!busyDays.All)
    {
        parts.push_back(DaysText(busyDays.First, busyDays.Last));
    }

    if (!busyHours.All)
    {
        parts.push_back(HoursText(busyHours.First, (busyHours.Last + 1) % DayHours));
    }

    std::string text;

    for (const std::string& part : parts)
    {
        text += text.empty() ? part : std::format(", {}", part);
    }

    return text;
}

std::chrono::minutes MCLocalUtcOffset()
{
    // Both clocks count 100 ns ticks; read a moment apart, so to the minute.
    constexpr int64_t TicksPerMinute = 600'000'000;
    MCSystemClock clock;
    const auto local = static_cast<int64_t>(clock.LocalFileTime());
    const auto utc = static_cast<int64_t>(clock.UtcFileTime());
    return std::chrono::minutes(static_cast<int64_t>(std::llround(static_cast<double>(local - utc) / TicksPerMinute)));
}

std::unique_ptr<MCInternetList> MCInternetList::Open()
{
    const MCInternetSetup setup = MCInternetGame::SetupNow();

    if (!setup.Open)
    {
        return nullptr;
    }

    return std::unique_ptr<MCInternetList>(
        new MCInternetList(setup.Open, std::format("{}{}?proto={}", setup.Service, ListPath, MCServiceProtocol)));
}

MCInternetList::MCInternetList(MCWebSocketOpener open, std::string url) : _Open(std::move(open)), _Url(std::move(url))
{
    Reopen(Clock::now());
}

MCInternetList::~MCInternetList()
{
    // Its close is the screen's leaving: the service counts it no longer.
    if (_Ws != nullptr)
    {
        _Ws->Close();
    }
}

void MCInternetList::Reopen(Clock::time_point now)
{
    _Ws = _Open(_Url);
    _LastPing = now;
    _NextTry.reset();

    if (_Ws == nullptr)
    {
        SDL_Log("MCInternetList: the system couldn't open a socket to the service's list");
        Dropped(now, Comeback.Most);
    }
}

void MCInternetList::Dropped(Clock::time_point now, std::chrono::milliseconds wait)
{
    if (_Ws != nullptr)
    {
        _Ws->Close();
        _Ws.reset();
    }

    _Listing.reset();
    _Tries++;
    _NextTry = now + wait;
}

bool MCInternetList::Update()
{
    if (_Refusal.has_value())
    {
        return false;
    }

    const Clock::time_point now = Clock::now();

    if (_Ws == nullptr)
    {
        if (_NextTry.has_value() && now >= *_NextTry)
        {
            Reopen(now);
        }

        return false;
    }

    bool changed = false;

    for (const MCWebSocket::Message& message : _Ws->Take())
    {
        changed |= Hear(message);

        if (_Refusal.has_value())
        {
            _Ws->Close();
            _Ws.reset();
            _Listing.reset();
            return true;
        }
    }

    switch (_Ws->GetState())
    {
        case MCWebSocket::State::Open:
        {
            MCServicePingIfDue(*_Ws, _LastPing, now);
            break;
        }

        case MCWebSocket::State::Closed:
        {
            const std::chrono::milliseconds wait = Comeback.Wait(_Tries);
            SDL_Log("MCInternetList: the list's socket closed (%s); trying again in %lld ms", _Ws->WhyClosed().c_str(),
                    static_cast<long long>(wait.count()));
            changed |= _Listing.has_value();
            Dropped(now, wait);
            break;
        }

        case MCWebSocket::State::Opening:
            break;
    }

    return changed;
}

bool MCInternetList::Hear(const MCWebSocket::Message& message)
{
    if (message.Binary || message.Data == MCServicePong)
    {
        return false;
    }

    const Json parsed = Json::parse(message.Data, nullptr, false);
    const std::optional<std::string> type = parsed.is_object() ? ShownField(parsed, "t", MostType) : std::nullopt;

    if (!type.has_value())
    {
        SDL_Log("MCInternetList: the service sent the list %zu bytes that aren't a message", message.Data.size());
        return false;
    }

    if (*type == "list")
    {
        std::optional<MCInternetListing> listing = ListingIn(parsed);

        if (!listing.has_value())
        {
            SDL_Log("MCInternetList: the service sent a list that isn't one; the last is kept");
            return false;
        }

        // It's in: a later drop starts its tries again.
        _Tries = 0;
        bool changed = _Listing != listing;
        _Listing = std::move(listing);

        if (std::optional<MCBusiestHours> busiest = BusiestIn(parsed); busiest.has_value() && *busiest != _Busiest)
        {
            _Busiest = std::move(*busiest);
            changed = true;
        }

        return changed;
    }

    const std::optional<MCServiceRefusal> refusal = MCServiceRefusalNamed(*type);

    if (refusal == MCServiceRefusal::TooManyTries)
    {
        // Its close follows: the next try waits the longest.
        _Tries = MCRetryDelay::MostDoublings;
        return false;
    }

    if (refusal.has_value())
    {
        SDL_Log("MCInternetList: the service turned the list away for good (\"%s\")", type->c_str());
        _Refusal = refusal;
        return false;
    }

    SDL_Log("MCInternetList: the service sent the list a \"%s\", which it doesn't take", type->c_str());
    return false;
}

namespace
{
    /// <summary>The count's answer, kept as it arrives (at most <see cref="MostCountBody"/>).</summary>
    class CountReply final : public MCHttpSource::MCReply
    {
    public:
        bool Status(int status, std::string_view) override
        {
            StatusSeen = status;
            return status == HttpOk;
        }

        bool Body(std::span<const char> bytes) override
        {
            if (Text.size() + bytes.size() > MostCountBody)
            {
                TooLong = true;
                return false;
            }

            Text.append(bytes.begin(), bytes.end());
            return true;
        }

        int StatusSeen = 0;
        bool TooLong = false;
        std::string Text;
    };
}

std::unique_ptr<MCInternetCount> MCInternetCount::Ask()
{
    const MCInternetSetup setup = MCInternetGame::SetupNow();

    if (!setup.Get)
    {
        return nullptr;
    }

    std::optional<std::string> url = HttpAddress(setup.Service, CountPath);
    std::unique_ptr<MCHttpSource::MCGet> get = setup.Get();

    if (!url.has_value() || get == nullptr)
    {
        SDL_Log("MCInternetCount: the service's count can't be asked (no transfer, or the service isn't at a ws:// or "
                "wss:// address)");
        return nullptr;
    }

    return std::unique_ptr<MCInternetCount>(new MCInternetCount(std::move(get), std::move(*url)));
}

struct MCInternetCount::MCAsking
{
    std::mutex Lock;
    /// <summary>Set once, before the thread starts.</summary>
    std::unique_ptr<MCHttpSource::MCGet> Get;
    bool Done = false;
    std::optional<int32_t> Online;
};

MCInternetCount::MCInternetCount(std::unique_ptr<MCHttpSource::MCGet> get, std::string url)
    : _Asking(std::make_shared<MCAsking>())
{
    _Asking->Get = std::move(get);
    _Thread = std::thread(
        [asking = _Asking, url = std::move(url)]
        {
            CountReply reply;
            const std::expected<void, MCHttpSource::MCGetError> got = asking->Get->Get(url, 0, reply);
            std::optional<int32_t> online;

            if (!got.has_value())
            {
                SDL_Log("MCInternetCount: asking the service's count failed: %s", got.error().Reason.c_str());
            }
            else if (reply.StatusSeen != HttpOk || reply.TooLong)
            {
                SDL_Log("MCInternetCount: the service's count answered %d (%zu bytes%s)", reply.StatusSeen,
                        reply.Text.size(), reply.TooLong ? ", too long" : "");
            }
            else
            {
                const Json parsed = Json::parse(reply.Text, nullptr, false);

                if (const std::optional<int64_t> count =
                        parsed.is_object() ? CountField(parsed, "online") : std::nullopt)
                {
                    online = static_cast<int32_t>(*count);
                }
                else
                {
                    SDL_Log("MCInternetCount: the service's count wasn't one (%zu bytes)", reply.Text.size());
                }
            }

            const std::scoped_lock lock(asking->Lock);
            asking->Online = online;
            asking->Done = true;
        });
}

MCInternetCount::~MCInternetCount()
{
    // The transfer stops; its thread ends on its own, joined off the game's thread.
    _Asking->Get->Cancel();
    MCThreadKeeper::Process().Keep(std::move(_Thread));
}

bool MCInternetCount::Done()
{
    const std::scoped_lock lock(_Asking->Lock);
    return _Asking->Done;
}

std::optional<int32_t> MCInternetCount::Online()
{
    const std::scoped_lock lock(_Asking->Lock);
    return _Asking->Online;
}
