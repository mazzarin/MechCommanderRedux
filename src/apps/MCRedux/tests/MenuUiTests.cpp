#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "platform/MCUiSoftware.h"
#include "platform/MCUtf8Path.h"
#include "sprite/MCHdArt.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlProbe.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

// Port-only (SPEC-menus.md, stage 2): the remade menus' building blocks (MCCore's ui/) proved on the probe screen
// (src/res/ui/probe.rml), which no player sees: RmlUi's layout with the app's face, drawn on the software path the same
// on every machine; the layout and legibility checks at every window the players' reports name; a resize; the keys; a
// model's values and list; the mission's layer; and over the game, its whole window on both renderers.

namespace
{
    constexpr float FrameSeconds = 1.0f / 15.0f;
    /// <summary>Frames a change takes to show (the menus' layout, then the presenter's frame).</summary>
    constexpr int32_t SettleFrames = 3;
    /// <summary>The look's class of the tab chosen, and of a list's row picked.</summary>
    constexpr const char* NowClass = "now";
    constexpr const char* PickedClass = "picked";

    struct MCWindowSize
    {
        int32_t Width;
        int32_t Height;
    };

    /// <summary>
    /// The windows the players' reports name (90 days to 19 Nov, HISTORY.md: 16:9, 16:10, 21:9 and 4:3, from 640x480
    /// to 3840x2410).
    /// </summary>
    constexpr std::array<MCWindowSize, 16> PlayersWindows = {{
        {640, 480},
        {1280, 960},
        {1366, 768},
        {1440, 900},
        {1600, 900},
        {1920, 1080},
        {1920, 1200},
        {2560, 1440},
        {2560, 1920},
        {3440, 1440},
        {3456, 2168},
        {3600, 2024},
        {3600, 2260},
        {3840, 1600},
        {3840, 2160},
        {3840, 2410},
    }};

    /// <summary>A remade screen's recorded frames' windows (SPEC-menus.md, "Testing"): 4:3, 16:9 and 5:4.</summary>
    constexpr std::array<MCWindowSize, 3> FrameWindows = {{{640, 480}, {1280, 720}, {1280, 1024}}};

    constexpr uint32_t FnvBasis = 0x811c9dc5;
    constexpr uint32_t FnvPrime = 0x01000193;
    constexpr size_t PixelBytes = 4;

    /// <summary>The FNV-1a hash of a picture's size and its RGBA pixels, row by row.</summary>
    uint32_t PictureHash(const SDL_Surface& picture)
    {
        uint32_t hash = FnvBasis;
        hash = (hash ^ static_cast<uint32_t>(picture.w)) * FnvPrime;
        hash = (hash ^ static_cast<uint32_t>(picture.h)) * FnvPrime;

        for (int y = 0; y < picture.h; ++y)
        {
            const auto* row = static_cast<const uint8_t*>(picture.pixels) + static_cast<ptrdiff_t>(y) * picture.pitch;

            for (size_t i = 0; i < static_cast<size_t>(picture.w) * PixelBytes; ++i)
            {
                hash = (hash ^ row[i]) * FnvPrime;
            }
        }

        return hash;
    }

    /// <summary>The FNV-1a hash of a window's picture as the display reads it.</summary>
    uint32_t PictureHash(const MCDisplay::MCShownScreen& picture)
    {
        uint32_t hash = FnvBasis;
        hash = (hash ^ static_cast<uint32_t>(picture.Width)) * FnvPrime;
        hash = (hash ^ static_cast<uint32_t>(picture.Height)) * FnvPrime;

        for (const SDL_Color& pixel : picture.Pixels)
        {
            for (const uint8_t channel : {pixel.r, pixel.g, pixel.b, pixel.a})
            {
                hash = (hash ^ channel) * FnvPrime;
            }
        }

        return hash;
    }

    SDL_Event KeyEvent(uint32_t type, SDL_Keycode key, SDL_Keymod held)
    {
        SDL_Event event{};
        event.type = type;
        event.key.key = key;
        event.key.mod = held;
        event.key.down = type == SDL_EVENT_KEY_DOWN;
        return event;
    }

    /// <summary>An element's id, or its first class when it has none, or its tag.</summary>
    std::string NameOf(const Rml::Element* element)
    {
        if (element == nullptr)
        {
            return "nothing";
        }

        if (!element->GetId().empty())
        {
            return element->GetId();
        }

        const std::string classes = element->GetClassNames();
        return classes.empty() ? element->GetTagName() : classes.substr(0, classes.find(' '));
    }

    /// <summary>
    /// The menus' RmlUi without a window, for a test: the probe shown on the menus' layer at a window's pixels, its
    /// frames timeless (a clock that stays at 0) and silent, drawn on the software path. The probe's 1998 picture
    /// isn't drawn (no game here): what every machine draws alike is the face and the look.
    /// </summary>
    class MCHeadlessProbe
    {
    public:
        MCHeadlessProbe(int32_t width, int32_t height, MCRmlLayer::MCUnit missionUnit = MCRmlMenus::MenusUnit)
        {
            auto made = MCRmlMenus::Create(MCRmlMenus::MCOptions{
                .Width = width, .Height = height, .MissionUnit = std::move(missionUnit), .Sound = [](uint32_t) {}});

            if (!made)
            {
                Error = made.error();
                return;
            }

            Menus = std::move(*made);
            Menus->SetClock([] { return 0.0; });
            Probe = Menus->Menus().CreateChild<MCRmlProbe>([](MCRmlProbe&) {});

            if (const auto opened = Probe->Open(); !opened)
            {
                Error = opened.error();
            }
        }

        std::unique_ptr<MCRmlMenus> Menus;
        MCGuiHandle<MCRmlProbe> Probe;
        std::string Error;

        Rml::ElementDocument& Document() const { return Probe->Document()->get(); }
        Rml::Context& Context() const { return Menus->Menus().Context(); }

        /// <summary>Runs <paramref name="frames"/> of the menus' frames; the last's draws.</summary>
        std::shared_ptr<const MCUiDraws> Frames(int32_t frames = SettleFrames)
        {
            std::shared_ptr<const MCUiDraws> draws;

            for (int32_t frame = 0; frame < frames; ++frame)
            {
                draws = Menus->Frame();
            }

            return draws;
        }

        /// <summary>The settled frame drawn on the software path; its picture's hash (0: none drawn).</summary>
        uint32_t Drawn(const std::optional<std::filesystem::path>& save = std::nullopt)
        {
            const std::shared_ptr<const MCUiDraws> draws = Frames();

            if (draws == nullptr)
            {
                return 0;
            }

            if (const auto drawn = _Software.Draw(*draws); !drawn)
            {
                FAIL_CHECK(drawn.error());
                return 0;
            }

            const SDL_Surface& picture = *_Software.Picture();

            // SDL's save takes the surface as changeable; it only reads it.
            if (save.has_value() &&
                !SDL_SavePNG(const_cast<SDL_Surface*>(&picture), MCFileSystem::ToUtf8(*save).c_str()))
            {
                FAIL_CHECK(std::format("the probe's picture can't be saved to {}: {}", save->string(), SDL_GetError()));
            }

            return PictureHash(picture);
        }

        /// <summary>A key pressed and let go (with <paramref name="held"/>), then a frame.</summary>
        void Key(SDL_Keycode key, SDL_Keymod held = SDL_KMOD_NONE)
        {
            Menus->HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, key, held));
            Menus->HandleEvent(KeyEvent(SDL_EVENT_KEY_UP, key, held));
            Frames(1);
        }

        /// <summary>Letters typed (a text box takes them), then a frame.</summary>
        void Type(const char* text)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_TEXT_INPUT;
            event.text.text = text;
            Menus->HandleEvent(event);
            Frames(1);
        }

        /// <summary>The mouse moved to window pixel (<paramref name="x"/>, <paramref name="y"/>), then a frame.</summary>
        void MouseTo(float x, float y)
        {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.x = x;
            event.motion.y = y;
            Menus->HandleEvent(event);
            Frames(1);
        }

        /// <summary>What has the keys' focus (its id; nothing: the document).</summary>
        std::string Focused() const
        {
            const Rml::Element* focus = Context().GetFocusElement();
            return focus == nullptr || focus == &Document() ? "nothing" : NameOf(focus);
        }

        /// <summary>Whether the focus ring shows.</summary>
        bool Ringed() const
        {
            const Rml::Element* ring = Document().QuerySelector(".focus-ring");
            return ring != nullptr && ring->IsVisible();
        }

        /// <summary>The tab chosen's words.</summary>
        std::string Tab() const
        {
            const Rml::Element* now = Document().QuerySelector(std::format(".tab.{}", NowClass));
            return now != nullptr ? now->GetInnerRML() : "none";
        }

        /// <summary>The list's rows shown, in order (not the model's row it makes them from, hidden).</summary>
        std::vector<Rml::Element*> Rows() const
        {
            Rml::ElementList rows;
            Document().GetElementsByClassName(rows, "list-row");
            std::erase_if(rows, [](Rml::Element* row) { return !row->IsVisible(); });
            return rows;
        }

        /// <summary>The sounds the kit played since the last call.</summary>
        std::vector<uint32_t> Sounds()
        {
            const std::vector<uint32_t>& played = Menus->Kit().Played();
            std::vector<uint32_t> since(played.begin() + static_cast<ptrdiff_t>(std::min(_Heard, played.size())),
                                        played.end());
            _Heard = played.size();
            return since;
        }

    private:
        MCUiSoftware _Software;
        size_t _Heard = 0;
    };

    std::optional<std::filesystem::path> ShotPath(std::string_view name)
    {
        const std::optional<std::string_view> shots = MCTest::Option("shots");

        if (!shots.has_value())
        {
            return std::nullopt;
        }

        std::filesystem::create_directories(std::filesystem::path(*shots));
        return std::filesystem::path(*shots) / std::format("{}.png", name);
    }
}

/// <summary>
/// The probe drawn on the software path at 640x480, 1280x720 and 1280x1024: its pictures' hashes the same on every
/// machine (the vendored FreeType's letters on whole pixels, SDL's software renderer), so these are the Mac's and
/// Windows' alike. With <c>--shots &lt;folder&gt;</c> each is saved there.
/// </summary>
TEST_CASE_ISOLATED("menus: the probe screen draws the same on every machine at 4:3, 16:9 and 5:4")
{
    struct MCFrame
    {
        MCWindowSize Window;
        uint32_t Hash;
    };

    constexpr std::array<MCFrame, FrameWindows.size()> Frames = {{
        {FrameWindows[0], 0xa19fd4db},
        {FrameWindows[1], 0xbe76a914},
        {FrameWindows[2], 0xaa7edade},
    }};

    MCHeadlessProbe probe(Frames[0].Window.Width, Frames[0].Window.Height);
    REQUIRE_EQ(probe.Error, "");

    for (const MCFrame& frame : Frames)
    {
        probe.Menus->SetWindow(frame.Window.Width, frame.Window.Height);
        const uint32_t hash =
            probe.Drawn(ShotPath(std::format("menus_probe_{}x{}", frame.Window.Width, frame.Window.Height)));
        CHECK_EQ(std::format("{}x{}: {:#010x}", frame.Window.Width, frame.Window.Height, hash),
                 std::format("{}x{}: {:#010x}", frame.Window.Width, frame.Window.Height, frame.Hash));
    }
}

/// <summary>
/// The probe at every window the players' reports name, each of its tabs chosen: every element shown inside the
/// window, no two siblings in the flow overlapping, no text spilling from its box (MCRmlChecks::BoxProblems); every
/// text at least 8 units high, at its contrast, off the pictures, whole (MCRmlChecks::Illegible).
/// </summary>
TEST_CASE_ISOLATED("menus: the probe screen lays out and reads at every window the players' reports name")
{
    MCHeadlessProbe probe(PlayersWindows[0].Width, PlayersWindows[0].Height);
    REQUIRE_EQ(probe.Error, "");
    constexpr int TabCount = 3;

    for (const MCWindowSize& window : PlayersWindows)
    {
        probe.Menus->SetWindow(window.Width, window.Height);
        const float unit = MCRmlMenus::MenusUnit(window.Width, window.Height);

        for (int tab = 0; tab < TabCount; ++tab)
        {
            probe.Frames();
            const std::string where = std::format("{}x{}, {}: ", window.Width, window.Height, probe.Tab());
            CHECK_EQ(where + MCRmlChecks::BoxProblems(probe.Context()), where);
            CHECK_EQ(where + MCRmlChecks::Illegible(probe.Context(), unit), where);
            CHECK(!MCRmlChecks::Texts(probe.Context(), unit).empty());
            probe.Key(SDLK_E);
        }
    }
}

/// <summary>A probe resized while shown draws as one shown fresh at the new size: 1280x720 to 1920x1080, and 640x480 to 3440x1440.</summary>
TEST_CASE_ISOLATED("menus: a screen resized while shown draws as one shown fresh at its new size")
{
    struct MCResize
    {
        MCWindowSize From;
        MCWindowSize To;
    };

    constexpr std::array<MCResize, 2> Resizes = {{{{1280, 720}, {1920, 1080}}, {{640, 480}, {3440, 1440}}}};

    for (const MCResize& resize : Resizes)
    {
        uint32_t resized = 0;
        {
            MCHeadlessProbe probe(resize.From.Width, resize.From.Height);
            REQUIRE_EQ(probe.Error, "");
            CHECK(probe.Drawn() != 0);
            probe.Menus->SetWindow(resize.To.Width, resize.To.Height);
            resized = probe.Drawn();
        }

        MCHeadlessProbe fresh(resize.To.Width, resize.To.Height);
        REQUIRE_EQ(fresh.Error, "");
        CHECK_EQ(std::format("{:#010x}", resized), std::format("{:#010x}", fresh.Drawn()));
    }
}

/// <summary>
/// The probe from the keys alone: the focus first on what it names (no ring until a key), Tab and Shift+Tab through
/// its controls in order, the ring on each, gone at the mouse's first move; a stepper's and a switch's Left and Right,
/// a slider's steps into its model; Q and E through the tabs (not while typing in a text box, whose letters are typed);
/// a list's Down to the row below, Enter picking it and pressing the screen's main button once; the window opened by
/// Enter, its Escape closing it; Escape to the screen's way back; and the 1998 sounds of each.
/// </summary>
TEST_CASE_ISOLATED("menus: the probe screen plays from the keys with the 1998 sounds")
{
    MCHeadlessProbe probe(1280, 720);
    REQUIRE_EQ(probe.Error, "");
    probe.Frames();
    MCRmlProbe& screen = *probe.Probe;
    int backs = 0;
    screen.Back = [&backs] { ++backs; };

    // The first focus, without its ring until a key is pressed.
    CHECK_EQ(probe.Focused(), "volume");
    CHECK(!probe.Ringed());

    // The slider's steps, into the model.
    const int volume = screen.Volume();
    probe.Key(SDLK_RIGHT);
    CHECK_EQ(screen.Volume(), volume + 5);
    CHECK(probe.Ringed());
    probe.Key(SDLK_LEFT);
    CHECK_EQ(screen.Volume(), volume);
    probe.Sounds();

    // Tab to the stepper; its Left and Right step through its choices, the 1998 on and off sounds.
    probe.Key(SDLK_TAB);
    CHECK_EQ(probe.Focused(), "detail");
    CHECK(probe.Ringed());
    Rml::Element& detail = *probe.Document().GetElementById("detail");
    const int choice = MCRmlKit::Choice(detail);
    probe.Key(SDLK_RIGHT);
    CHECK_EQ(MCRmlKit::Choice(detail), choice + 1);
    CHECK_EQ(probe.Sounds(), std::vector<uint32_t>{MCRmlKit::OnSound});
    probe.Key(SDLK_LEFT);
    CHECK_EQ(MCRmlKit::Choice(detail), choice);
    CHECK_EQ(probe.Sounds(), std::vector<uint32_t>{MCRmlKit::OffSound});

    // The switch, and back with Shift+Tab.
    probe.Key(SDLK_TAB);
    CHECK_EQ(probe.Focused(), "music");
    Rml::Element& music = *probe.Document().GetElementById("music");
    probe.Key(SDLK_RIGHT);
    CHECK_EQ(MCRmlKit::Choice(music), 1);
    probe.Key(SDLK_TAB, SDL_KMOD_SHIFT);
    CHECK_EQ(probe.Focused(), "detail");
    probe.Key(SDLK_TAB);
    probe.Key(SDLK_TAB);
    CHECK_EQ(probe.Focused(), "name");

    // Q and E are letters in a text box; Enter leaves it.
    const std::string tab = probe.Tab();
    probe.Key(SDLK_E);
    probe.Type("e");
    CHECK_EQ(probe.Tab(), tab);
    probe.Key(SDLK_RETURN);
    CHECK(screen.Name().find('e') != std::string::npos);
    CHECK(probe.Focused() != "name");

    // Q and E through the tabs, with the tab's sound.
    probe.Sounds();
    probe.Key(SDLK_E);
    CHECK_EQ(probe.Tab(), "Games");
    CHECK_EQ(probe.Sounds(), std::vector<uint32_t>{MCRmlKit::TabSound});
    probe.Key(SDLK_Q);
    CHECK_EQ(probe.Tab(), "Controls");

    // A list's row, Down to the next, Enter picks it and presses JOIN GAME once.
    const std::vector<Rml::Element*> rows = probe.Rows();
    REQUIRE(rows.size() > 3);
    rows[1]->Focus(true);
    probe.Frames(1);
    probe.Key(SDLK_DOWN);
    REQUIRE_EQ(probe.Context().GetFocusElement(), rows[2]);
    probe.Sounds();
    probe.Key(SDLK_RETURN);
    CHECK_EQ(screen.Picked(), 2);
    CHECK(rows[2]->IsClassSet(PickedClass));
    CHECK_EQ(screen.Joins(), 1);
    CHECK_EQ(screen.Status(), "Joining Game 3");
    CHECK_EQ(probe.Sounds(), std::vector<uint32_t>{MCRmlKit::PressSound});

    // HOST GAME is off: its press refused with the 1998 sound, nothing done.
    probe.Document().GetElementById("host")->Focus(true);
    probe.Key(SDLK_RETURN);
    CHECK_EQ(probe.Sounds(), std::vector<uint32_t>{MCRmlKit::OffClickSound});

    // The window: opened by Enter, the focus on its first button, Escape closes it and the focus comes back.
    probe.Document().GetElementById("window")->Focus(true);
    probe.Key(SDLK_RETURN);
    REQUIRE(static_cast<bool>(screen.Window()));
    CHECK_EQ(probe.Focused(), "cancel");
    CHECK_EQ(&probe.Menus->Menus().Top()->get(), screen.Window().Lock().get());
    probe.Key(SDLK_ESCAPE);
    CHECK_EQ(backs, 0);
    CHECK_EQ(&probe.Menus->Menus().Top()->get(), static_cast<MCRmlScreen*>(&screen));

    // Escape: the screen's way back.
    probe.Key(SDLK_ESCAPE);
    CHECK_EQ(backs, 1);

    // The mouse's first move takes the ring away.
    probe.Key(SDLK_TAB);
    CHECK(probe.Ringed());
    probe.MouseTo(1.0f, 1.0f);
    CHECK(!probe.Ringed());
}

/// <summary>
/// A screen shows its model: a list's rows made from its rows, an empty list's line, a value's words; and keeps its
/// draws while nothing changes (the software path then keeps its picture), records new ones when something does.
/// </summary>
TEST_CASE_ISOLATED("menus: a screen shows its model and keeps its draws while nothing changes")
{
    MCHeadlessProbe probe(1280, 720);
    REQUIRE_EQ(probe.Error, "");
    MCRmlProbe& screen = *probe.Probe;

    screen.SetGames(MCRmlProbe::SampleGames(3));
    probe.Frames();
    CHECK_EQ(probe.Rows().size(), size_t{3});
    CHECK_EQ(probe.Rows()[1]->GetChild(0)->GetInnerRML(), "Game 2");
    CHECK(!probe.Document().QuerySelector(".empty")->IsVisible(true));

    screen.SetGames({});
    probe.Frames();
    CHECK(probe.Rows().empty());
    const Rml::Element* empty = probe.Document().QuerySelector(".empty");
    REQUIRE(empty != nullptr);
    CHECK(empty->IsVisible(true));

    // Unchanged: the same draws, the same object.
    const std::shared_ptr<const MCUiDraws> first = probe.Frames();
    CHECK_EQ(probe.Frames().get(), first.get());

    // A change: new draws.
    screen.SetGames(MCRmlProbe::SampleGames(2));
    CHECK(probe.Frames().get() != first.get());

    // Hidden: none at all.
    screen.Hide();
    CHECK(probe.Frames() == nullptr);
}

/// <summary>
/// The two layers: the menus' unit the window over 640x480, the mission's its own (the mission's INTERFACE SIZE in the
/// game); a screen on each, the mission's drawn under the menus', the keys the menus' while one of theirs shows.
/// </summary>
TEST_CASE_ISOLATED("menus: the mission's layer has its own unit and draws under the menus'")
{
    constexpr float MissionUnit = 2.0f;
    MCHeadlessProbe probe(1280, 720, [](int32_t, int32_t) { return MissionUnit; });
    REQUIRE_EQ(probe.Error, "");
    CHECK_EQ(probe.Menus->Menus().Unit(), 1.5f);
    CHECK_EQ(probe.Menus->Mission().Unit(), MissionUnit);

    const size_t menusDraws = probe.Frames()->Draws.size();
    MCGuiHandle<MCRmlProbe> mission = probe.Menus->Mission().CreateChild<MCRmlProbe>([](MCRmlProbe&) {});
    REQUIRE(mission->Open().has_value());
    const std::shared_ptr<const MCUiDraws> both = probe.Frames();
    CHECK(both->Draws.size() > menusDraws);

    // The mission's letters drawn larger: its title's box taller than the menus' at the same font size.
    Rml::Element* menusTitle = probe.Document().QuerySelector(".title");
    Rml::Element* missionTitle = mission->Document()->get().QuerySelector(".title");
    REQUIRE(menusTitle != nullptr);
    REQUIRE(missionTitle != nullptr);
    CHECK(missionTitle->GetBox().GetSize().y > menusTitle->GetBox().GetSize().y);

    // The keys go to the menus' screen while it shows, then to the mission's.
    probe.Key(SDLK_E);
    CHECK_EQ(probe.Tab(), "Games");
    probe.Probe->Hide();
    probe.Key(SDLK_E);
    const Rml::Element* missionTab = mission->Document()->get().QuerySelector(std::format(".tab.{}", NowClass));
    REQUIRE(missionTab != nullptr);
    CHECK_EQ(missionTab->GetInnerRML(), "Games");
}

namespace
{
    /// <summary>The window made <paramref name="width"/> x <paramref name="height"/> pixels (in points on a dense screen).</summary>
    bool SetWindowPixels(MCDisplay& display, int width, int height)
    {
        SDL_Window& window = display.Window();
        const float density = std::max(SDL_GetWindowPixelDensity(&window), 1.0f);
        SDL_SetWindowSize(&window, static_cast<int>(std::lround(static_cast<float>(width) / density)),
                          static_cast<int>(std::lround(static_cast<float>(height) / density)));
        SDL_SyncWindow(&window);
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(&window, &pixelWidth, &pixelHeight);
        return pixelWidth == width && pixelHeight == height;
    }

    /// <summary>The game's own menus' RmlUi started over the display, the probe shown on its menus' layer; stopped at the end.</summary>
    class MCGameProbe
    {
    public:
        MCGameProbe(MCDisplay& display, bool hd)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(&display.Window(), &width, &height);
            auto started = MCRmlMenus::Start(MCRmlMenus::MCOptions{.Width = width, .Height = height, .Hd = hd});

            if (!started)
            {
                Error = started.error();
                return;
            }

            started->get().SetClock([] { return 0.0; });
            Probe = started->get().Menus().CreateChild<MCRmlProbe>([](MCRmlProbe&) {});

            if (const auto opened = Probe->Open(); !opened)
            {
                Error = opened.error();
            }
        }

        ~MCGameProbe() { MCRmlMenus::Stop(); }
        MCGameProbe(const MCGameProbe&) = delete;
        MCGameProbe& operator=(const MCGameProbe&) = delete;

        MCGuiHandle<MCRmlProbe> Probe;
        std::string Error;
    };

    void RunFrames(int32_t frames)
    {
        for (int32_t frame = 0; frame < frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }
}

/// <summary>
/// Over the game on the software renderer, the game's own menus' RmlUi: the probe drawn over the logistics screen as
/// each frame is presented, the whole window's picture at 640x480, 1280x720 and 1280x1024 (the 1998 painting from the
/// game's art among it), its hash recorded: the Mac's and Windows' alike with the game's data (the A10, the laptop).
/// The software presenter draws the menus' picture again only when it changes. With <c>--shots</c> each is saved.
/// </summary>
TEST_CASE_ISOLATED("game: the probe screen draws over the game with the 1998 art the same on every machine")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Software);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    REQUIRE(display->RendererKind() == MCRendererKind::Software);
    REQUIRE(SetWindowPixels(*display, FrameWindows[0].Width, FrameWindows[0].Height));
    MCGameProbe probe(*display, false);
    REQUIRE_EQ(probe.Error, "");

    // The probe covers the window: at 4:3 and 5:4 (no painting) the same pictures as the headless probe's.
    constexpr std::array<uint32_t, FrameWindows.size()> Hashes = {0xa19fd4db, 0x4f810a48, 0xaa7edade};

    for (size_t i = 0; i < FrameWindows.size(); ++i)
    {
        const MCWindowSize& size = FrameWindows[i];
        REQUIRE(SetWindowPixels(*display, size.Width, size.Height));
        RunFrames(SettleFrames);
        const auto window = display->ReadWindow();
        REQUIRE(window.has_value());
        REQUIRE_EQ(window->Width, size.Width);
        REQUIRE_EQ(window->Height, size.Height);
        CHECK_EQ(std::format("{}x{}: {:#010x}", size.Width, size.Height, PictureHash(*window)),
                 std::format("{}x{}: {:#010x}", size.Width, size.Height, Hashes[i]));

        if (const std::optional<std::filesystem::path> path =
                ShotPath(std::format("game_probe_{}x{}", size.Width, size.Height));
            path.has_value())
        {
            REQUIRE(display->SaveWindowScreenshot(*path).has_value());
        }
    }

    // Nothing changes: the software presenter keeps its picture of the menus.
    const uint64_t redraws = display->OverlayRedraws();
    RunFrames(SettleFrames);
    CHECK_EQ(display->OverlayRedraws(), redraws);

    // Something does: drawn again.
    probe.Probe->SetGames(MCRmlProbe::SampleGames(2));
    RunFrames(SettleFrames);
    CHECK(display->OverlayRedraws() > redraws);
}

namespace
{
    /// <summary>The milliseconds from <paramref name="start"/> to <paramref name="end"/> (SDL's performance counter).</summary>
    double Milliseconds(uint64_t start, uint64_t end)
    {
        constexpr double PerSecond = 1000.0;
        return static_cast<double>(end - start) * PerSecond / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    /// <summary>A mouse move to window pixel (<paramref name="x"/>, <paramref name="y"/>), as SDL gives it (points).</summary>
    SDL_Event MouseAt(MCDisplay& display, float x, float y)
    {
        const float density = std::max(SDL_GetWindowPixelDensity(&display.Window()), 1.0f);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(&display.Window());
        event.motion.x = x / density;
        event.motion.y = y / density;
        return event;
    }
}

/// <summary>
/// Tool: <c>--rml-probe &lt;folder&gt;</c> shows the probe screen at <c>--window WxH</c> (1280x720 by default) over the
/// logistics screen, HD art on with the pack (<c>--hd-pack</c>, else the user folder's) or off with <c>--hd-original
/// 1</c>, on the GPU renderer or with <c>--rml-software 1</c> the software one, and saves the whole window's picture
/// into the folder. Then it times <c>--rml-frames</c> frames (200 by default) without vsync, the mouse moving down the
/// list's rows (each frame restyles two): the menus' layout and recording a frame, the present with their draws and
/// without, and their draws drawn alone and read back.
/// </summary>
TEST_CASE_ISOLATED("tool: --rml-probe pictures and times the RmlUi probe screen")
{
    const std::optional<std::string_view> shots = MCTest::Option("rml-probe");

    if (!shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const bool software = MCTest::Option("rml-software").has_value();
    const bool hd = !MCTest::Option("hd-original").has_value();
    const std::optional<std::string_view> packOption = MCTest::Option("hd-pack");
    const std::filesystem::path pack = packOption.has_value()
                                           ? std::filesystem::absolute(std::filesystem::path(*packOption))
                                           : std::filesystem::path();
    GRenderer = static_cast<int>(software ? MCRendererKind::Software : MCRendererKind::Vulkan);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);
    std::cout << std::format("  renderer: {}\n", display->RendererName());

    constexpr MCWindowSize DefaultWindow{1280, 720};
    int width = DefaultWindow.Width;
    int height = DefaultWindow.Height;

    if (const std::optional<std::string_view> window = MCTest::Option("window"); window.has_value())
    {
        const size_t x = window->find('x');
        REQUIRE(x != std::string_view::npos);
        std::from_chars(window->data(), window->data() + x, width);
        std::from_chars(window->data() + x + 1, window->data() + window->size(), height);
    }

    REQUIRE(SetWindowPixels(*display, width, height));

    if (hd)
    {
        GHdArt = 1;
        MCHdArt::SetPackFolder(pack);
    }

    MCGameProbe probe(*display, hd);
    REQUIRE_EQ(probe.Error, "");
    MCRmlMenus& menus = MCRmlMenus::Running()->get();
    RunFrames(SettleFrames);
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / std::format("rml_probe_{}x{}_{}_{}.png", width, height,
                                                            software ? "software" : "gpu", hd ? "hd" : "original");
    REQUIRE(display->SaveWindowScreenshot(path).has_value());
    std::cout << std::format("  {}\n  boxes: {}\n  legibility: {}\n", path.string(),
                             MCRmlChecks::BoxProblems(menus.Menus().Context()),
                             MCRmlChecks::Illegible(menus.Menus().Context(), menus.Menus().Unit()));

    constexpr int32_t DefaultFrames = 200;
    int32_t frames = DefaultFrames;

    if (const std::optional<std::string_view> option = MCTest::Option("rml-frames"); option.has_value())
    {
        std::from_chars(option->data(), option->data() + option->size(), frames);
    }

    // The menus' frames recorded here, timed apart from the present (the display's own source off meanwhile).
    display->SetOverlaySource(nullptr);
    Rml::Element* list = probe.Probe->Document()->get().GetElementById("list");
    REQUIRE(list != nullptr);
    const float rowsX = list->GetAbsoluteLeft() + list->GetClientWidth() / 2.0f;
    const float rowsTop = list->GetAbsoluteTop();
    const float rowsHeight = list->GetClientHeight();
    // The mouse's step down the rows each frame, in window pixels.
    constexpr float RowStep = 7.0f;
    // Presents timed alone (no game frame, whose pacing hides the work), without waiting for the display.
    display->SetVSync(false);
    double menusMs = 0.0;
    double withMs = 0.0;
    double readMs = 0.0;

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        const float y = rowsTop + std::fmod(static_cast<float>(frame) * RowStep, rowsHeight);
        MCInput::HandleEvent(MouseAt(*display, rowsX, y));
        const uint64_t start = SDL_GetPerformanceCounter();
        const std::shared_ptr<const MCUiDraws> draws = menus.FrameFor(width, height);
        const uint64_t laid = SDL_GetPerformanceCounter();
        display->SetOverlay(draws);
        REQUIRE(display->Present(false).has_value());
        const uint64_t shown = SDL_GetPerformanceCounter();
        // Drawn alone, offscreen, waited for and read back (a hidden window's GPU frame has no image to draw on).
        REQUIRE(display->ReadOverlay(*draws).has_value());
        menusMs += Milliseconds(start, laid);
        withMs += Milliseconds(laid, shown);
        readMs += Milliseconds(shown, SDL_GetPerformanceCounter());
    }

    // The same without the menus: a present alone, and an empty window's read back.
    const auto empty = std::make_shared<MCUiDraws>(MCUiDraws{.Width = width, .Height = height, .Draws = {}});
    display->SetOverlay(nullptr);
    double withoutMs = 0.0;
    double readEmptyMs = 0.0;

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        const uint64_t start = SDL_GetPerformanceCounter();
        REQUIRE(display->Present(false).has_value());
        const uint64_t shown = SDL_GetPerformanceCounter();
        REQUIRE(display->ReadOverlay(*empty).has_value());
        withoutMs += Milliseconds(start, shown);
        readEmptyMs += Milliseconds(shown, SDL_GetPerformanceCounter());
    }

    std::cout << std::format(
        "  {} frames at {}x{}, no vsync, ms a frame: the menus' layout and recording {:.3f}; a present {:.3f} with "
        "their draws, {:.3f} without (+{:.3f}); drawn alone and read back {:.3f}, an empty window {:.3f} (+{:.3f})\n",
        frames, width, height, menusMs / frames, withMs / frames, withoutMs / frames, (withMs - withoutMs) / frames,
        readMs / frames, readEmptyMs / frames, (readMs - readEmptyMs) / frames);

    GHdArt = 0;
    MCHdArt::SetPackFolder({});
}
