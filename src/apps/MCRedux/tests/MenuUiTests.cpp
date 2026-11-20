#include "stdafx.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "platform/MCDisplay.h"
#include "platform/MCInput.h"
#include "sprite/MCHdArt.h"
#include "ui/MCRmlMenus.h"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>

// Port-only (SPEC-menus.md, "How it's built"): RmlUi drawing a screen written in RML and RCSS with the game's art and
// fonts (MCCore's ui/), over the game's picture, on both renderers: the probe screen (src/res/ui/probe.rml).

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
    /// that stays at 0), the probe shown and the menus' events layer on; all undone at the end.
    /// </summary>
    class MCProbeScope
    {
    public:
        MCProbeScope(MCDisplay& display, bool hd) : _Display(display)
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

            if (auto shown = Menus->Show(Probe); !shown)
            {
                Error = shown.error();
                return;
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
        {640, 480, false, 0xb29815ed},
        {1280, 720, true, 0x38c49621},
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

    int width = 1280;
    int height = 720;

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
    int32_t frames = 200;

    if (const std::optional<std::string_view> option = MCTest::Option("rml-frames"); option.has_value())
    {
        std::from_chars(option->data(), option->data() + option->size(), frames);
    }

    Rml::Element* list = menus.Document()->get().GetElementById("list");
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
        const std::shared_ptr<const MCUiDraws> draws = menus.Frame();
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
}
