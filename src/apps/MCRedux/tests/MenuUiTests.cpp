#include "stdafx.h"
#include "MCTest.h"
#include "ScreenInput.h"
#include "TestGame.h"
#include "gui/MCGuiFont.h"
#include "gui/MCGuiGlobals.h"
#include "gui/MCListView.h"
#include "gui/MCScrollPane.h"
#include "logistics/MCCompInventoryBlock.h"
#include "logistics/MCMainMenu.h"
#include "logistics/MCMechRepairBlock.h"
#include "logistics/MCRepairScreen.h"
#include "logistics/MCUnitLimits.h"
#include "main/MCLogistics.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCHdArt.h"
#include "ui/MCRmlMechLab.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>

// Port-only (SPEC-menus.md, "How it's built"): RmlUi drawing a screen written in RML and RCSS with the game's art and
// fonts (MCCore's ui/), over the game's picture, on both renderers: the probe screen (src/res/ui/probe.rml), and the
// 'MECH BAY's mech lab (src/res/ui/mechlab.rml, ui/MCRmlMechLab) on the logistics data.

namespace
{
    constexpr float FrameSeconds = 1.0f / 15.0f;
    constexpr std::string_view Probe = "probe.rml";
    /// <summary>Frames a change takes to show (the menus' layout, then the presenter's frame).</summary>
    constexpr int32_t SettleFrames = 3;
    /// <summary>How far a box may lie past another's edge and still be taken as within it (RmlUi's layout is floats).</summary>
    constexpr float Slack = 0.5f;

    /// <summary>The FNV-1a hash of a picture's size and pixels.</summary>
    uint32_t PictureHash(const MCDisplay::MCShownScreen& picture)
    {
        constexpr uint32_t Basis = 0x811c9dc5;
        constexpr uint32_t Prime = 0x01000193;
        uint32_t hash = Basis;
        hash = (hash ^ static_cast<uint32_t>(picture.Width)) * Prime;
        hash = (hash ^ static_cast<uint32_t>(picture.Height)) * Prime;

        for (const SDL_Color& pixel : picture.Pixels)
        {
            for (const uint8_t channel : {pixel.r, pixel.g, pixel.b, pixel.a})
            {
                hash = (hash ^ channel) * Prime;
            }
        }

        return hash;
    }

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

    /// <summary>The window's size in pixels.</summary>
    std::pair<int, int> WindowPixels(MCDisplay& display)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(&display.Window(), &width, &height);
        return {width, height};
    }

    /// <summary>Runs <paramref name="frames"/> game frames with the menus' draws over each.</summary>
    void RunFrames(MCRmlMenus& menus, MCDisplay& display, int32_t frames)
    {
        for (int32_t frame = 0; frame < frames; ++frame)
        {
            display.SetOverlay(menus.Frame());
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    struct MCArea
    {
        float Left = 0.0f;
        float Top = 0.0f;
        float Right = 0.0f;
        float Bottom = 0.0f;

        bool Empty() const { return Right - Left <= Slack || Bottom - Top <= Slack; }
        bool Within(const MCArea& outer) const
        {
            return Left >= outer.Left - Slack && Top >= outer.Top - Slack && Right <= outer.Right + Slack &&
                   Bottom <= outer.Bottom + Slack;
        }

        bool Overlaps(const MCArea& other) const
        {
            return Left < other.Right - Slack && other.Left < Right - Slack && Top < other.Bottom - Slack &&
                   other.Top < Bottom - Slack;
        }

        MCArea Meet(const MCArea& other) const
        {
            return MCArea{std::max(Left, other.Left), std::max(Top, other.Top), std::min(Right, other.Right),
                          std::min(Bottom, other.Bottom)};
        }
    };

    MCArea AreaOf(Rml::Element& element, Rml::BoxArea box)
    {
        const Rml::Vector2f at = element.GetAbsoluteOffset(box);
        const Rml::Vector2f size = element.GetBox().GetSize(box);
        return MCArea{at.x, at.y, at.x + size.x, at.y + size.y};
    }

    std::string NameOf(Rml::Element& element)
    {
        const std::string id = element.GetId();
        return std::format("{}{}{}", element.GetTagName(), id.empty() ? "" : "#", id);
    }

    /// <summary>Whether <paramref name="element"/> clips what it holds (it scrolls or hides what overflows).</summary>
    bool Clips(Rml::Element& element)
    {
        const Rml::Style::ComputedValues& values = element.GetComputedValues();
        return values.overflow_x() != Rml::Style::Overflow::Visible ||
               values.overflow_y() != Rml::Style::Overflow::Visible;
    }

    /// <summary>
    /// What's wrong with the boxes under <paramref name="element"/> (shown within <paramref name="clip"/>): an element
    /// shown outside the window or spilling from its parent (one that doesn't clip it), siblings overlapping, a text's
    /// line spilling from its element's content box.
    /// </summary>
    void CheckBoxes(Rml::Element& element, const MCArea& window, const MCArea& clip, std::string& wrong)
    {
        std::vector<std::pair<Rml::Element*, MCArea>> shown;
        const MCArea parent = AreaOf(element, Rml::BoxArea::Padding);
        const bool clipping = Clips(element);

        for (int i = 0; i < element.GetNumChildren(); ++i)
        {
            Rml::Element& child = *element.GetChild(i);

            if (!child.IsVisible())
            {
                continue;
            }

            if (auto* text = dynamic_cast<Rml::ElementText*>(&child); text != nullptr)
            {
                const MCArea content = AreaOf(element, Rml::BoxArea::Content);
                const Rml::Vector2f origin = text->GetAbsoluteOffset(Rml::BoxArea::Border);
                const float size = text->GetComputedValues().font_size();

                for (const Rml::ElementText::Line& line : text->GetLines())
                {
                    const MCArea letters{origin.x + line.position.x, origin.y + line.position.y - size,
                                         origin.x + line.position.x + static_cast<float>(line.width),
                                         origin.y + line.position.y};

                    if (!letters.Within(content))
                    {
                        wrong += std::format("'{}' spills from {}; ", line.text, NameOf(element));
                    }
                }

                continue;
            }

            const MCArea border = AreaOf(child, Rml::BoxArea::Border);

            // Scrolled out of its clip: not shown.
            if (border.Meet(clip).Empty())
            {
                continue;
            }

            if (!border.Within(window))
            {
                wrong += std::format("{} lies outside the window; ", NameOf(child));
            }

            if (!clipping && !border.Empty() && !border.Within(parent))
            {
                wrong += std::format("{} spills from {}; ", NameOf(child), NameOf(element));
            }

            for (const auto& [other, area] : shown)
            {
                if (!border.Empty() && !area.Empty() && border.Overlaps(area))
                {
                    wrong += std::format("{} overlaps {}; ", NameOf(child), NameOf(*other));
                }
            }

            shown.emplace_back(&child, border);
            CheckBoxes(child, window, Clips(child) ? clip.Meet(AreaOf(child, Rml::BoxArea::Padding)) : clip, wrong);
        }
    }

    std::string BoxProblems(MCRmlMenus& menus, int width, int height)
    {
        const MCArea window{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
        std::string wrong;
        const MCOptionalRef<Rml::ElementDocument> document = menus.Document();

        if (!document.has_value())
        {
            return "no document";
        }

        CheckBoxes(document->get(), window, window, wrong);
        return wrong;
    }

    /// <summary>The probe's panels' offsets: side by side (one row) or stacked (one column).</summary>
    bool PanelsSideBySide(MCRmlMenus& menus)
    {
        Rml::Element* buttons = menus.Document()->get().GetElementById("buttons");
        Rml::Element* notes = menus.Document()->get().GetElementById("notes");
        return buttons != nullptr && notes != nullptr &&
               std::fabs(buttons->GetAbsoluteTop() - notes->GetAbsoluteTop()) < Slack &&
               notes->GetAbsoluteLeft() > buttons->GetAbsoluteLeft() + Slack;
    }

    /// <summary>
    /// The menus' RmlUi over the display, for a test: started for the window's pixels, its frames timeless (a clock
    /// that stays at 0), <paramref name="document"/> shown (none: nothing yet) and the menus' events layer on; all
    /// undone at the end.
    /// </summary>
    class MCProbeScope
    {
    public:
        MCProbeScope(MCDisplay& display, bool hd, std::optional<std::string_view> document = Probe) : _Display(display)
        {
            const auto [width, height] = WindowPixels(display);
            auto made = MCRmlMenus::Create(MCRmlMenus::MCOptions{.Width = width, .Height = height, .Hd = hd});

            if (!made)
            {
                Error = made.error();
                return;
            }

            Menus = std::move(*made);
            Menus->SetClock([] { return 0.0; });

            if (document.has_value())
            {
                if (auto shown = Menus->Show(*document); !shown)
                {
                    Error = shown.error();
                    return;
                }
            }

            MCInput::SetEventLayer([&menus = *Menus](const SDL_Event& event) { return menus.HandleEvent(event); });
        }

        ~MCProbeScope()
        {
            MCInput::SetEventLayer(nullptr);
            _Display.SetOverlay(nullptr);
        }

        MCProbeScope(const MCProbeScope&) = delete;
        MCProbeScope& operator=(const MCProbeScope&) = delete;

        std::unique_ptr<MCRmlMenus> Menus;
        std::string Error;

    private:
        MCDisplay& _Display;
    };

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

    /// <summary>What <see cref="TimeFrames"/> measured, in milliseconds over all its frames.</summary>
    struct MCFrameTimes
    {
        int32_t Frames = 0;
        /// <summary>The menus' layout and recording (RmlUi's update and render through our recorder).</summary>
        double Menus = 0.0;
        /// <summary>A present with the menus' draws, and one without.</summary>
        double With = 0.0;
        double Without = 0.0;
        /// <summary>The draws drawn alone offscreen and read back, and an empty window's.</summary>
        double Read = 0.0;
        double ReadEmpty = 0.0;
    };

    /// <summary>
    /// Times <paramref name="frames"/> frames of the menus without vsync, <paramref name="input"/> sending each frame's
    /// input first: the menus' layout and recording, a present with their draws and without, and the draws drawn alone
    /// and read back (the software renderer's drawing of them, or the GPU's waited for).
    /// </summary>
    MCFrameTimes TimeFrames(MCRmlMenus& menus, MCDisplay& display, int32_t frames,
                            const std::function<void(int32_t)>& input)
    {
        // Presents timed alone (no game frame, whose pacing hides the work), without waiting for the display.
        display.SetVSync(false);
        MCFrameTimes times{.Frames = std::max(frames, 1)};
        const auto [width, height] = WindowPixels(display);

        for (int32_t frame = 0; frame < frames; ++frame)
        {
            input(frame);
            const uint64_t start = SDL_GetPerformanceCounter();
            const std::shared_ptr<const MCUiDraws> draws = menus.Frame();
            const uint64_t laid = SDL_GetPerformanceCounter();
            display.SetOverlay(draws);
            REQUIRE(display.Present(false).has_value());
            const uint64_t shown = SDL_GetPerformanceCounter();
            // Drawn alone, offscreen, waited for and read back (a hidden window's GPU frame has no image to draw on).
            REQUIRE(display.ReadOverlay(*draws).has_value());
            times.Menus += Milliseconds(start, laid);
            times.With += Milliseconds(laid, shown);
            times.Read += Milliseconds(shown, SDL_GetPerformanceCounter());
        }

        // The same without the menus: a present alone, and an empty window's read back.
        const auto empty = std::make_shared<MCUiDraws>(MCUiDraws{.Width = width, .Height = height, .Draws = {}});
        display.SetOverlay(nullptr);

        for (int32_t frame = 0; frame < frames; ++frame)
        {
            const uint64_t start = SDL_GetPerformanceCounter();
            REQUIRE(display.Present(false).has_value());
            const uint64_t shown = SDL_GetPerformanceCounter();
            REQUIRE(display.ReadOverlay(*empty).has_value());
            times.Without += Milliseconds(start, shown);
            times.ReadEmpty += Milliseconds(shown, SDL_GetPerformanceCounter());
        }

        return times;
    }

    void PrintTimes(const MCFrameTimes& times, int width, int height)
    {
        const auto frames = static_cast<double>(times.Frames);
        std::cout << std::format(
            "  {} frames at {}x{}, no vsync, ms a frame: the menus' layout and recording {:.3f}; a present {:.3f} with "
            "their draws, {:.3f} without (+{:.3f}); drawn alone and read back {:.3f}, an empty window {:.3f} "
            "(+{:.3f})\n",
            times.Frames, width, height, times.Menus / frames, times.With / frames, times.Without / frames,
            (times.With - times.Without) / frames, times.Read / frames, times.ReadEmpty / frames,
            (times.Read - times.ReadEmpty) / frames);
    }

    /// <summary>The tools' window when <c>--window</c> gives none.</summary>
    constexpr std::pair<int, int> DefaultToolWindow = {1280, 720};
    /// <summary>The frames the tools time when <c>--rml-frames</c> gives none.</summary>
    constexpr int32_t DefaultToolFrames = 200;

    /// <summary>
    /// The menus tools' options: <c>--rml-software 1</c> (the software renderer, else the GPU's), <c>--hd-original 1</c>
    /// (HD art off, else on with <c>--hd-pack</c> or the user folder's pack), <c>--window WxH</c>,
    /// <c>--rml-frames N</c>.
    /// </summary>
    struct MCToolOptions
    {
        bool Software = false;
        bool Hd = true;
        std::filesystem::path Pack;
        std::optional<std::pair<int, int>> Window;
        int32_t Frames = DefaultToolFrames;
    };

    MCToolOptions ToolOptions()
    {
        MCToolOptions options;
        options.Software = MCTest::Option("rml-software").has_value();
        options.Hd = !MCTest::Option("hd-original").has_value();

        if (const std::optional<std::string_view> pack = MCTest::Option("hd-pack"); pack.has_value())
        {
            options.Pack = std::filesystem::absolute(std::filesystem::path(*pack));
        }

        if (const std::optional<std::string_view> window = MCTest::Option("window"); window.has_value())
        {
            const size_t x = window->find('x');
            REQUIRE(x != std::string_view::npos);
            std::pair<int, int> size;
            std::from_chars(window->data(), window->data() + x, size.first);
            std::from_chars(window->data() + x + 1, window->data() + window->size(), size.second);
            options.Window = size;
        }

        if (const std::optional<std::string_view> frames = MCTest::Option("rml-frames"); frames.has_value())
        {
            std::from_chars(frames->data(), frames->data() + frames->size(), options.Frames);
        }

        return options;
    }

    /// <summary>Boots the game to logistics on the options' renderer, HD art on or off as they say: its display.</summary>
    std::shared_ptr<MCDisplay> StartTool(const MCToolOptions& options)
    {
        GRenderer = static_cast<int>(options.Software ? MCRendererKind::Software : MCRendererKind::Vulkan);
        REQUIRE(MCTestGame::StartLogistics());
        std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);
        std::cout << std::format("  renderer: {}\n", display->RendererName());

        if (options.Hd)
        {
            GHdArt = 1;
            MCHdArt::SetPackFolder(options.Pack);
        }

        return display;
    }

    /// <summary>A mouse button's press or release at window pixel (<paramref name="x"/>, <paramref name="y"/>), as SDL gives it.</summary>
    SDL_Event ButtonAt(MCDisplay& display, float x, float y, bool down)
    {
        const float density = std::max(SDL_GetWindowPixelDensity(&display.Window()), 1.0f);
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(&display.Window());
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = x / density;
        event.button.y = y / density;
        return event;
    }

    /// <summary>The middle of <paramref name="element"/>'s border box, in window pixels.</summary>
    Rml::Vector2f MiddleOf(Rml::Element& element)
    {
        return element.GetAbsoluteOffset(Rml::BoxArea::Border) + element.GetBox().GetSize(Rml::BoxArea::Border) / 2.0f;
    }

    /// <summary>
    /// A drag through RmlUi's own input, as SDL's events bring it: <paramref name="dragged"/> scrolled into its list's
    /// view, the left button pressed in its middle, the mouse moved to the middle of <paramref name="onto"/> in a few
    /// steps a frame apart, the button let go there.
    /// </summary>
    void DragIn(MCRmlMenus& menus, MCDisplay& display, Rml::Element& dragged, Rml::Element& onto)
    {
        constexpr int32_t Steps = 6;
        dragged.ScrollIntoView(false);
        RunFrames(menus, display, 1);
        const Rml::Vector2f from = MiddleOf(dragged);
        const Rml::Vector2f to = MiddleOf(onto);
        MCInput::HandleEvent(MouseAt(display, from.x, from.y));
        RunFrames(menus, display, 1);
        MCInput::HandleEvent(ButtonAt(display, from.x, from.y, true));
        RunFrames(menus, display, 1);

        for (int32_t step = 1; step <= Steps; ++step)
        {
            const Rml::Vector2f at = from + (to - from) * (static_cast<float>(step) / static_cast<float>(Steps));
            MCInput::HandleEvent(MouseAt(display, at.x, at.y));
            RunFrames(menus, display, 1);
        }

        MCInput::HandleEvent(ButtonAt(display, to.x, to.y, false));
        RunFrames(menus, display, SettleFrames);
    }

    /// <summary>The elements of class <paramref name="name"/> under the element <paramref name="id"/> of the shown document.</summary>
    Rml::ElementList ElementsOf(MCRmlMenus& menus, const char* id, const char* name)
    {
        Rml::ElementList elements;
        Rml::Element* parent = menus.Document()->get().GetElementById(id);
        REQUIRE(parent != nullptr);
        parent->GetElementsByClassName(elements, name);
        // Not a data-for's template, which RmlUi keeps hidden.
        std::erase_if(elements, [](Rml::Element* element) { return !element->IsVisible(true); });
        return elements;
    }

    /// <summary>
    /// What a drag in the 'MECH BAY changes, written out to compare: <paramref name="mech"/>'s used and weapon tonnage,
    /// battle rating, and payload (each component's count and its copies' damage, sorted); the spares (each component's
    /// count and inventory row); the resource points.
    /// </summary>
    std::string LabState(const MCLogMech& mech)
    {
        std::string state = std::format("used {} weapons {} rating {} points {}; payload", mech.UsedTonnage,
                                        mech.WeaponTonnage, mech.BattleRating, ResourcePoints);
        std::map<int32_t, std::string> payload;

        for (const std::shared_ptr<MCLogInventoryItem>& item : mech.Inventory->Items)
        {
            std::vector<int32_t> hits;

            for (const std::shared_ptr<MCLogInventoryStat>& stat : item->Stats)
            {
                hits.push_back(stat->Hits);
            }

            std::ranges::sort(hits);
            std::string& text = payload[item->MasterID];
            text += std::format(" {}x{} hits", item->MasterID, item->Count);

            for (const int32_t hit : hits)
            {
                text += std::format(" {}", hit);
            }
        }

        for (const auto& [master, text] : payload)
        {
            state += text;
        }

        state += "; spares";
        std::map<int32_t, std::string> spares;

        for (const std::shared_ptr<MCLogInventoryItem>& item : GlobalLog()->ComponentInventory->Items)
        {
            spares[item->MasterID] += std::format(" {}x{}@{}", item->MasterID, item->Count, item->ListIndex);
        }

        for (const auto& [master, text] : spares)
        {
            state += text;
        }

        return state;
    }

    /// <summary>Runs the logistics screens for two seconds: a screen change or a drop settles.</summary>
    void Settle()
    {
        constexpr int32_t Frames = 30;

        for (int32_t frame = 0; frame < Frames; ++frame)
        {
            MCTestGame::RunFrame(FrameSeconds);
        }
    }

    /// <summary>A new campaign's logistics (the force its starting mechs), on the software renderer at 640x480.</summary>
    std::shared_ptr<MCDisplay> StartCampaignLogistics()
    {
        GRenderer = static_cast<int>(MCRendererKind::Software);
        REQUIRE(MCTestGame::StartLogistics());
        Settle();
        NewCampaign();
        Settle();
        std::shared_ptr<MCDisplay> display = MCInput::Display();
        REQUIRE(display != nullptr);
        REQUIRE(display->RendererKind() == MCRendererKind::Software);
        REQUIRE(SetWindowPixels(*display, MCRmlMenus::ScreenWidth, MCRmlMenus::ScreenHeight));
        REQUIRE(GlobalLog()->ForceMechList->GetMechCount() > 0);
        return display;
    }
}

/// <summary>
/// The probe screen laid out at 640x480 and 1280x720 on the software renderer: its pixels (the whole window, its
/// hash recorded), and its boxes: every element shown inside the window, no two siblings overlapping, no text spilling
/// from its box; the panels stacked at 4:3 and side by side at 16:9 (its media query).
/// </summary>
TEST_CASE_ISOLATED("game: the RmlUi probe screen lays out and draws with the game's art and fonts at 4:3 and 16:9")
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
    REQUIRE(SetWindowPixels(*display, MCRmlMenus::ScreenWidth, MCRmlMenus::ScreenHeight));
    MCProbeScope probe(*display, false);
    REQUIRE_EQ(probe.Error, "");
    MCRmlMenus& menus = *probe.Menus;

    // The fonts the probe's style sheet sizes by their cells' heights.
    CHECK_EQ(menus.CellHeight("white10"), 10);
    CHECK_EQ(menus.CellHeight("yellow12"), 12);

    struct MCLayout
    {
        int Width;
        int Height;
        bool SideBySide;
        uint32_t Hash;
    };

    constexpr std::array<MCLayout, 2> Layouts = {{
        {640, 480, false, 0x23aebabd},
        {1280, 720, true, 0xb93a10f9},
    }};

    for (const MCLayout& layout : Layouts)
    {
        REQUIRE(SetWindowPixels(*display, layout.Width, layout.Height));
        menus.SetWindow(layout.Width, layout.Height);
        RunFrames(menus, *display, SettleFrames);
        CHECK_EQ(BoxProblems(menus, layout.Width, layout.Height), "");
        CHECK_EQ(PanelsSideBySide(menus), layout.SideBySide);
        const auto window = display->ReadWindow();
        REQUIRE(window.has_value());
        REQUIRE_EQ(window->Width, layout.Width);
        REQUIRE_EQ(window->Height, layout.Height);
        CHECK_EQ(PictureHash(*window), layout.Hash);

        if (const std::optional<std::string_view> shots = MCTest::Option("shots"); shots.has_value())
        {
            const std::filesystem::path path =
                std::filesystem::path(*shots) / std::format("rml_probe_{}x{}.png", layout.Width, layout.Height);
            REQUIRE(display->SaveWindowScreenshot(path).has_value());
        }
    }
}

/// <summary>
/// Tool: <c>--rml-probe &lt;folder&gt;</c> shows the probe screen at <c>--window WxH</c> (1280x720 by default) over the
/// main menu, HD art on with the pack (<c>--hd-pack</c>, else the user folder's) or off with <c>--hd-original 1</c>, on
/// the GPU renderer or with <c>--rml-software 1</c> the software one, and saves the whole window's picture into the
/// folder. Then it times <c>--rml-frames</c> frames (200 by default) without vsync, the mouse moving over the list's
/// rows: the menus' layout and recording per frame, the game's frame with the menus' draws over it and without, and the
/// software renderer's drawing of them alone.
/// </summary>
TEST_CASE_ISOLATED("tool: --rml-probe pictures and times the RmlUi probe screen")
{
    const std::optional<std::string_view> shots = MCTest::Option("rml-probe");

    if (!shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const MCToolOptions options = ToolOptions();
    const std::shared_ptr<MCDisplay> display = StartTool(options);
    const int width = options.Window.value_or(DefaultToolWindow).first;
    const int height = options.Window.value_or(DefaultToolWindow).second;
    const bool software = options.Software;
    const bool hd = options.Hd;
    REQUIRE(SetWindowPixels(*display, width, height));
    MCProbeScope probe(*display, hd);
    REQUIRE_EQ(probe.Error, "");
    MCRmlMenus& menus = *probe.Menus;

    for (const std::string_view family : {"white10", "yellow10", "yellow12", "dim10"})
    {
        std::cout << std::format("  {}: {} lines\n", family, menus.CellHeight(family));
    }

    RunFrames(menus, *display, SettleFrames);
    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);
    const std::filesystem::path path = folder / std::format("rml_probe_{}x{}_{}_{}.png", width, height,
                                                            software ? "software" : "gpu", hd ? "hd" : "original");
    REQUIRE(display->SaveWindowScreenshot(path).has_value());
    std::cout << std::format("  {}\n  boxes: {}\n", path.string(), BoxProblems(menus, width, height));

    // The timing: the mouse over each of the list's rows in turn, so each frame restyles two rows.
    const int32_t frames = options.Frames;
    Rml::Element* list = menus.Document()->get().GetElementById("list");
    REQUIRE(list != nullptr);
    const float rowsX = list->GetAbsoluteLeft() + list->GetClientWidth() / 2.0f;
    const float rowsTop = list->GetAbsoluteTop();
    const float rowsHeight = list->GetClientHeight();
    // The mouse's step down the rows each frame, in window pixels.
    constexpr float RowStep = 7.0f;
    const MCFrameTimes times = TimeFrames(menus, *display, frames,
                                          [&](int32_t frame)
                                          {
                                              const float y =
                                                  rowsTop + std::fmod(static_cast<float>(frame) * RowStep, rowsHeight);
                                              MCInput::HandleEvent(MouseAt(*display, rowsX, y));
                                          });
    PrintTimes(times, width, height);
}

/// <summary>
/// The mech lab's two drags through RmlUi's own input (SDL's mouse events: pressed on an element's box, moved, let go
/// over another's), each from the same state as the 1998 screen's same drag and changing the game's data exactly as it
/// does: a copy of the picked mech's payload dragged onto the spares (the mech's weight, battle rating and payload, the
/// spares' count and rows, the resource points), and the spare dragged back onto the lab.
/// </summary>
TEST_CASE_ISOLATED("game: the RmlUi mech lab's drags change the logistics data as the 1998 'MECH BAY's do")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCDisplay> display = StartCampaignLogistics();
    std::shared_ptr<MCLogMech> mech;
    GlobalLog()->ForceMechList->GetMechInfo(0, mech);
    REQUIRE(mech != nullptr);
    const std::string before = LabState(*mech);

    // The 1998 screen: the force's first mech picked, its first undamaged copy dragged out of its weapon list onto the
    // inventory, then (on the components tab) dragged back onto the mech.
    GlobalLog()->SetUpRepairScreen(true);
    Settle();
    REQUIRE(GlobalLog()->CurrentScreen() == GlobalLog()->RepairScreen);
    constexpr int32_t FirstMechX = 300;
    constexpr int32_t FirstMechY = 60;
    constexpr int32_t InventoryX = 100;
    constexpr int32_t InventoryY = 200;
    constexpr int32_t ComponentsTabX = 200;
    constexpr int32_t ComponentsTabY = 290;
    MCScreenInput::Click(FirstMechX, FirstMechY);
    Settle();
    REQUIRE(GlobalLog()->RepairScreen->SelectedMech() == mech);
    const std::shared_ptr<MCMechRepairBlock> row = GlobalLog()->RepairScreen->RowOf(*mech);
    REQUIRE(row != nullptr);
    MCScrollPane& weapons = *row->InventoryPane;
    const int32_t lineHeight = GreenFont->Height() + 2;
    int32_t master = -1;
    int32_t line = 0;

    for (; line * lineHeight < weapons.Height() - lineHeight; ++line)
    {
        uint8_t itemNum = 0;
        const std::shared_ptr<MCLogInventoryItem> item = row->GetItemFromScrollPane(line, itemNum);

        if (item != nullptr && std::ranges::any_of(item->Stats, [itemNum](const auto& stat)
                                                   { return stat->ItemNum == itemNum && stat->Hits == 0; }))
        {
            master = item->MasterID;
            break;
        }
    }

    REQUIRE(master >= 0);
    MCTest::Scope named(std::format("component {}", master));
    MCScreenInput::Drag(weapons.GlobalX() + 20, weapons.GlobalY() + line * lineHeight + lineHeight / 2, InventoryX,
                        InventoryY);
    Settle();
    const std::string strippedThen = LabState(*mech);
    CHECK(strippedThen != before);
    MCScreenInput::Click(ComponentsTabX, ComponentsTabY);
    Settle();
    MCInventoryList& spares = *GlobalLog()->ComponentInventory;
    const std::shared_ptr<MCLogInventoryItem> spare =
        spares.GetItemInfo(spares.GetIndexFromMasterID(static_cast<uint8_t>(master)));
    REQUIRE(spare != nullptr);
    const std::shared_ptr<MCCompInventoryBlock> spareRow =
        GlobalLog()->RepairScreen->InventoryPane->RowOf<MCCompInventoryBlock>(*spare);
    REQUIRE(spareRow != nullptr);
    MCScreenInput::Drag(spareRow->GlobalX() + 40, spareRow->GlobalY() + spareRow->Height() / 2, FirstMechX, FirstMechY);
    Settle();
    const std::string mountedThen = LabState(*mech);
    CHECK_EQ(mountedThen, before);

    // The lab, from the same state: the same copy dragged onto the spares, then back onto the lab.
    MCProbeScope rml(*display, false, std::nullopt);
    REQUIRE_EQ(rml.Error, "");
    MCRmlMenus& menus = *rml.Menus;
    std::expected<std::unique_ptr<MCRmlMechLab>, std::string> lab = MCRmlMechLab::Open(menus);
    REQUIRE_EQ((lab ? std::string() : lab.error()), "");
    RunFrames(menus, *display, SettleFrames);
    REQUIRE((*lab)->Picked() == mech);

    Rml::Element* copy = nullptr;

    for (Rml::Element* entry : ElementsOf(menus, "payload", "line"))
    {
        if (copy == nullptr && entry->GetAttribute<int>("master", -1) == master && !entry->IsClassSet("damaged"))
        {
            copy = entry;
        }
    }

    REQUIRE(copy != nullptr);
    Rml::Element* sparesList = menus.Document()->get().GetElementById("spares");
    REQUIRE(sparesList != nullptr);
    DragIn(menus, *display, *copy, *sparesList);
    CHECK_EQ(LabState(*mech), strippedThen);

    Rml::Element* back = nullptr;

    for (Rml::Element* entry : ElementsOf(menus, "spares", "spare"))
    {
        if (entry->GetAttribute<int>("master", -1) == master)
        {
            back = entry;
        }
    }

    REQUIRE(back != nullptr);
    CHECK(!back->IsClassSet("greyed"));
    Rml::Element* labRow = menus.Document()->get().GetElementById("lab");
    REQUIRE(labRow != nullptr);
    DragIn(menus, *display, *back, *labRow);
    CHECK_EQ(LabState(*mech), mountedThen);
}

/// <summary>
/// The mech lab laid out at 640x480, 1280x720, 1920x1200 and 2560x1080 on the software renderer: every element shown
/// inside the window, no two siblings overlapping, no text spilling from its box; the cards a row takes (one at 4:3
/// and 16:10, two at 16:9, four at 21:9); the spares under the lab at 4:3 and beside it from 3:2; and at 640x480 its
/// software frame's hash.
/// </summary>
TEST_CASE_ISOLATED("game: the RmlUi mech lab lays out at 4:3, 16:10, 16:9 and 21:9, its frame recorded at 640x480")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    const std::shared_ptr<MCDisplay> display = StartCampaignLogistics();
    MCProbeScope scope(*display, false, std::nullopt);
    REQUIRE_EQ(scope.Error, "");
    MCRmlMenus& menus = *scope.Menus;
    std::expected<std::unique_ptr<MCRmlMechLab>, std::string> lab = MCRmlMechLab::Open(menus);
    REQUIRE_EQ((lab ? std::string() : lab.error()), "");

    struct MCLayout
    {
        int Width;
        int Height;
        size_t CardsInARow;
        bool SideBySide;
        std::optional<uint32_t> Hash;
    };

    constexpr std::array<MCLayout, 4> Layouts = {{
        {640, 480, 1, false, 0x9ebb6b53u},
        {1280, 720, 2, true, std::nullopt},
        {1920, 1200, 1, true, std::nullopt},
        {2560, 1080, 4, true, std::nullopt},
    }};

    for (const MCLayout& layout : Layouts)
    {
        MCTest::Scope at(std::format("{}x{}", layout.Width, layout.Height));
        REQUIRE(SetWindowPixels(*display, layout.Width, layout.Height));
        menus.SetWindow(layout.Width, layout.Height);
        RunFrames(menus, *display, SettleFrames);
        CHECK_EQ(BoxProblems(menus, layout.Width, layout.Height), "");

        // The cards a row of the list takes, and the force's laid out so.
        Rml::Element* cardList = menus.Document()->get().GetElementById("cards");
        REQUIRE(cardList != nullptr);
        const Rml::ElementList cards = ElementsOf(menus, "cards", "card");
        REQUIRE(!cards.empty());
        const float cardWidth = cards.front()->GetBox().GetSize(Rml::BoxArea::Margin).x;
        const float listWidth = cardList->GetBox().GetSize(Rml::BoxArea::Content).x;
        const auto fit = static_cast<size_t>(std::floor((listWidth + Slack) / cardWidth));
        CHECK_EQ(fit, layout.CardsInARow);
        const auto inFirstRow = std::ranges::count_if(
            cards, [&](Rml::Element* card)
            { return std::fabs(card->GetAbsoluteTop() - cards.front()->GetAbsoluteTop()) < Slack; });
        CHECK_EQ(static_cast<size_t>(inFirstRow), std::min(fit, cards.size()));

        Rml::Element* bay = menus.Document()->get().GetElementById("bay");
        Rml::Element* sparesList = menus.Document()->get().GetElementById("spares");
        REQUIRE(bay != nullptr);
        REQUIRE(sparesList != nullptr);
        const bool beside = sparesList->GetAbsoluteLeft() >= bay->GetAbsoluteLeft() + bay->GetOffsetWidth() - Slack;
        const bool under = sparesList->GetAbsoluteTop() >= bay->GetAbsoluteTop() + bay->GetOffsetHeight() - Slack;
        CHECK_EQ(beside, layout.SideBySide);
        CHECK_EQ(under, !layout.SideBySide);

        if (layout.Hash.has_value())
        {
            const auto window = display->ReadWindow();
            REQUIRE(window.has_value());
            CHECK_EQ(PictureHash(*window), *layout.Hash);
        }

        if (const std::optional<std::string_view> shots = MCTest::Option("shots"); shots.has_value())
        {
            const std::filesystem::path path =
                std::filesystem::path(*shots) / std::format("rml_mechlab_{}x{}.png", layout.Width, layout.Height);
            REQUIRE(display->SaveWindowScreenshot(path).has_value());
        }
    }
}

/// <summary>
/// Tool: <c>--rml-mechbay &lt;folder&gt;</c> shows the mech lab on a new campaign's logistics data at 640x480,
/// 1280x720 and 2560x1080 (or <c>--window WxH</c> alone), HD art on or off and on either renderer as
/// <c>--rml-probe</c>, and saves each window's picture into the folder. Then it times <c>--rml-frames</c> frames at
/// 1280x720 (or the window) with a payload copy being dragged over the spares and back, as <c>--rml-probe</c> times its own.
/// </summary>
TEST_CASE_ISOLATED("tool: --rml-mechbay pictures and times the RmlUi mech lab")
{
    const std::optional<std::string_view> shots = MCTest::Option("rml-mechbay");

    if (!shots.has_value() || !MCTestGame::Available())
    {
        return;
    }

    const MCToolOptions options = ToolOptions();
    const std::shared_ptr<MCDisplay> display = StartTool(options);
    Settle();
    NewCampaign();
    Settle();
    std::vector<std::pair<int, int>> sizes = {{640, 480}, {1280, 720}, {2560, 1080}};

    if (options.Window.has_value())
    {
        sizes = {*options.Window};
    }

    REQUIRE(SetWindowPixels(*display, sizes.front().first, sizes.front().second));
    MCProbeScope scope(*display, options.Hd, std::nullopt);
    REQUIRE_EQ(scope.Error, "");
    MCRmlMenus& menus = *scope.Menus;
    std::expected<std::unique_ptr<MCRmlMechLab>, std::string> lab = MCRmlMechLab::Open(menus);
    REQUIRE_EQ((lab ? std::string() : lab.error()), "");

    for (const std::string_view family :
         {"white10", "dim10", "yellow12", "blue", "gryfnt", "white", "yelldrp", "bluedrp"})
    {
        std::cout << std::format("  {}: {} lines\n", family, menus.CellHeight(family));
    }

    const std::filesystem::path folder(*shots);
    std::filesystem::create_directories(folder);

    for (const auto& [width, height] : sizes)
    {
        REQUIRE(SetWindowPixels(*display, width, height));
        menus.SetWindow(width, height);
        RunFrames(menus, *display, SettleFrames);
        const std::filesystem::path path =
            folder / std::format("rml_mechbay_{}x{}_{}_{}.png", width, height, options.Software ? "software" : "gpu",
                                 options.Hd ? "hd" : "original");
        REQUIRE(display->SaveWindowScreenshot(path).has_value());
        std::cout << std::format("  {}\n  boxes: {}\n", path.string(), BoxProblems(menus, width, height));
    }

    // The timing, at 1280x720 unless a window was given: a payload copy picked up and carried over the spares and
    // back each frame, never let go there (so the data stays as it is).
    const auto [width, height] = options.Window.value_or(DefaultToolWindow);
    REQUIRE(SetWindowPixels(*display, width, height));
    menus.SetWindow(width, height);
    RunFrames(menus, *display, SettleFrames);
    const Rml::ElementList lines = ElementsOf(menus, "payload", "line");
    Rml::Element* sparesList = menus.Document()->get().GetElementById("spares");
    REQUIRE(sparesList != nullptr);
    const auto picked = std::ranges::find_if(lines, [](Rml::Element* line) { return !line->IsClassSet("heading"); });
    REQUIRE(picked != lines.end());
    const Rml::Vector2f from = MiddleOf(**picked);
    const Rml::Vector2f to = MiddleOf(*sparesList);
    MCInput::HandleEvent(MouseAt(*display, from.x, from.y));
    MCInput::HandleEvent(ButtonAt(*display, from.x, from.y, true));
    // The frames a carry across takes, one way.
    constexpr int32_t Across = 20;
    int32_t over = 0;
    const MCFrameTimes times =
        TimeFrames(menus, *display, options.Frames,
                   [&](int32_t frame)
                   {
                       const int32_t step = frame % (2 * Across);
                       const float share =
                           static_cast<float>(step < Across ? step : 2 * Across - step) / static_cast<float>(Across);
                       const Rml::Vector2f at = from + (to - from) * share;
                       MCInput::HandleEvent(MouseAt(*display, at.x, at.y));
                       over += sparesList->IsClassSet("over") ? 1 : 0;
                   });
    PrintTimes(times, width, height);
    // The drag is live: the spares lit as a drop zone while the copy is over them.
    std::cout << std::format("  frames with the copy over the spares: {} of {}\n", over, options.Frames);
    CHECK(over > 0);
    // Let go back where it was picked up, on the lab, where a copy dropped changes nothing.
    MCInput::HandleEvent(MouseAt(*display, from.x, from.y));
    MCInput::HandleEvent(ButtonAt(*display, from.x, from.y, false));
    RunFrames(menus, *display, SettleFrames);
}
