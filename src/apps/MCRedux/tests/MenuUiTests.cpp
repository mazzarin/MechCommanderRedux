#include "stdafx.h"
#include "MCScratchFolder.h"
#include "MCMenusPictures.h"
#include "MCTest.h"
#include "TestGame.h"
#include "gui/MCGuiGlobals.h"
#include "platform/MCDisplay.h"
#include "platform/MCFrameLog.h"
#include "platform/MCInput.h"
#include "platform/MCUiSoftware.h"
#include "platform/MCUtf8Path.h"
#include "sprite/MCHdArt.h"
#include "ui/MCRmlArt.h"
#include "ui/MCRmlChecks.h"
#include "ui/MCRmlKit.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlProbe.h"
#include "ui/MCRmlSystem.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <random>

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

    using namespace MCMenusPictures;

    constexpr size_t PixelBytes = 4;

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
            Menus->Kit().KeepPlayed(true);
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

namespace
{
    /// <summary>Where two pictures first differ (their sizes, or a pixel's bytes in each); nothing: the same bytes.</summary>
    std::string PictureDifference(const SDL_Surface& first, const SDL_Surface& second)
    {
        if (first.w != second.w || first.h != second.h)
        {
            return std::format("{}x{} against {}x{}", first.w, first.h, second.w, second.h);
        }

        for (int y = 0; y < first.h; ++y)
        {
            const auto* a = static_cast<const uint8_t*>(first.pixels) + static_cast<ptrdiff_t>(y) * first.pitch;
            const auto* b = static_cast<const uint8_t*>(second.pixels) + static_cast<ptrdiff_t>(y) * second.pitch;

            for (int x = 0; x < first.w; ++x)
            {
                const size_t at = static_cast<size_t>(x) * PixelBytes;

                if (std::memcmp(a + at, b + at, PixelBytes) != 0)
                {
                    return std::format("pixel {},{}: {:02x}{:02x}{:02x}{:02x} against {:02x}{:02x}{:02x}{:02x}", x, y,
                                       a[at], a[at + 1], a[at + 2], a[at + 3], b[at], b[at + 1], b[at + 2], b[at + 3]);
                }
            }
        }

        return {};
    }

    /// <summary>The pixels the last draw of <paramref name="software"/> drew (its boxes' areas, summed).</summary>
    int64_t ChangedArea(const MCUiSoftware& software)
    {
        int64_t area = 0;

        for (const SDL_Rect& box : software.Changed())
        {
            area += static_cast<int64_t>(box.w) * static_cast<int64_t>(box.h);
        }

        return area;
    }

    /// <summary>The middle of <paramref name="element"/>'s border box, in window pixels.</summary>
    Rml::Vector2f MiddleOf(Rml::Element& element)
    {
        return element.GetAbsoluteOffset(Rml::BoxArea::Border) + element.GetBox().GetSize() / 2.0f;
    }
}

/// <summary>
/// The software path draws again only the boxes a frame changes (MCUiSoftware), and its picture is the one a whole
/// redraw gives, byte for byte: over the mouse moving down the list's rows, the list scrolled, a change of tab, the
/// window opening (its veil over everything) and a hover in it, the window closing and the screen faded (its opacity
/// stepped down and back). A hover draws again only a small part of the window.
/// </summary>
TEST_CASE_ISOLATED("menus: the software path's partial redraw gives a whole redraw's picture, byte for byte")
{
    constexpr MCWindowSize Window = FrameWindows[1];
    constexpr int64_t WindowArea = static_cast<int64_t>(Window.Width) * Window.Height;
    // A hover's redraw: under a tenth of the window (the rows' boxes).
    constexpr int64_t HoverAreaMost = WindowArea / 10;
    // Games enough for the list to scroll.
    constexpr int ScrollingGames = 40;
    MCHeadlessProbe probe(Window.Width, Window.Height);
    REQUIRE_EQ(probe.Error, "");
    probe.Probe->SetGames(MCRmlProbe::SampleGames(ScrollingGames));
    MCUiSoftware kept;
    MCUiSoftware whole;

    // A step's settled frame drawn on the kept picture (only where it changed) and wholly, the two compared; the
    // pixels the kept picture drew.
    const auto drawn = [&](std::string_view step)
    {
        const std::shared_ptr<const MCUiDraws> draws = probe.Frames();
        REQUIRE(draws != nullptr);
        const auto keptDrawn = kept.Draw(*draws);
        REQUIRE_EQ(keptDrawn.has_value() ? std::string() : keptDrawn.error(), "");
        whole.Forget();
        const auto wholeDrawn = whole.Draw(*draws);
        REQUIRE_EQ(wholeDrawn.has_value() ? std::string() : wholeDrawn.error(), "");
        CHECK_EQ(std::format("{}: {}", step, PictureDifference(*kept.Picture(), *whole.Picture())),
                 std::format("{}: ", step));
        return ChangedArea(kept);
    };

    CHECK_EQ(drawn("first"), WindowArea);
    const std::vector<Rml::Element*> rows = probe.Rows();
    REQUIRE(rows.size() > 3);

    // The mouse's first move, onto the first row: the focus ring goes and the mouse's hints show.
    const Rml::Vector2f first = MiddleOf(*rows[0]);
    probe.MouseTo(first.x, first.y);
    CHECK(drawn("the mouse's first move") > 0);

    // On down the rows: each move restyles the row left and the row reached (the picked row looks the same hovered or
    // not), and draws little.
    for (size_t row = 1; row <= 3; ++row)
    {
        const Rml::Vector2f at = MiddleOf(*rows[row]);
        probe.MouseTo(at.x, at.y);
        const int64_t area = drawn(std::format("hover {}", row));
        CHECK(area > 0);
        CHECK(area < HoverAreaMost);
    }

    // The list scrolled a row and a little (the rows cut by its box), then back.
    constexpr float PastRow = 3.0f;
    Rml::Element& list = *probe.Document().GetElementById("list");
    list.SetScrollTop(rows[1]->GetAbsoluteTop() - rows[0]->GetAbsoluteTop() + PastRow);
    CHECK(drawn("scrolled") > 0);
    list.SetScrollTop(0.0f);
    drawn("scrolled back");

    // The next tab.
    probe.Key(SDLK_E);
    CHECK(drawn("tab") > 0);

    // The window, its veil over the whole screen; a hover on its LEAVE; closed.
    probe.Document().GetElementById("window")->Focus(true);
    probe.Key(SDLK_RETURN);
    REQUIRE(static_cast<bool>(probe.Probe->Window()));
    CHECK_EQ(drawn("window"), WindowArea);
    Rml::Element* leave = probe.Probe->Window().Lock()->Document()->get().GetElementById("leave");
    REQUIRE(leave != nullptr);
    const Rml::Vector2f atLeave = MiddleOf(*leave);
    probe.MouseTo(atLeave.x, atLeave.y);
    CHECK(drawn("hover in the window") < HoverAreaMost);
    probe.Key(SDLK_ESCAPE);
    drawn("window closed");

    // A fade: the screen's opacity stepped down, and back.
    for (const char* opacity : {"0.5", "0.25"})
    {
        probe.Document().SetProperty("opacity", opacity);
        drawn(std::format("opacity {}", opacity));
    }

    probe.Document().RemoveProperty("opacity");
    drawn("opacity back");

    // Nothing changed: nothing drawn.
    CHECK_EQ(drawn("again"), 0);
}

namespace
{
    /// <summary>
    /// The software path's draws drawn the plain way, for the tests: every rectangle a copy by SDL's software renderer (a
    /// box its white texel scaled and coloured, a picture its texels scaled), other triangles by its SDL_RenderGeometry,
    /// cut by the draw's scissor, the whole picture each time.
    /// </summary>
    class MCSdlPlain
    {
    public:
        MCSdlPlain(int width, int height)
            : _Surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32))
            , _Renderer(_Surface != nullptr ? SDL_CreateSoftwareRenderer(_Surface.get()) : nullptr)
        {
            _White = Texture(MCUiTexture{.Id = 0, .Width = 1, .Height = 1, .Pixels = {255, 255, 255, 255}});
        }

        ~MCSdlPlain()
        {
            _Textures.clear();
            _White.reset();
            _Renderer.reset();
        }

        MCSdlPlain(const MCSdlPlain&) = delete;
        MCSdlPlain& operator=(const MCSdlPlain&) = delete;

        const SDL_Surface& Draw(const MCUiDraws& draws)
        {
            SDL_Renderer* renderer = _Renderer.get();
            SDL_SetRenderClipRect(renderer, nullptr);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            for (const MCUiDraw& draw : draws.Draws)
            {
                SDL_Texture* texture = _White.get();

                if (draw.Texture != nullptr)
                {
                    auto& kept = _Textures[draw.Texture->Id];
                    kept = kept != nullptr ? std::move(kept) : Texture(*draw.Texture);
                    texture = kept.get();
                }

                SDL_SetRenderClipRect(renderer, draw.Scissor.has_value() ? &*draw.Scissor : nullptr);

                for (const MCUiPiece& piece : MCUiPieces(draw))
                {
                    if (!piece.Quad.has_value())
                    {
                        constexpr Uint8 Full = 255;
                        MCUiSdlTriangles(draw, piece, _Vertices);
                        SDL_SetTextureColorMod(texture, Full, Full, Full);
                        SDL_SetTextureAlphaMod(texture, Full);
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                        SDL_RenderGeometry(renderer, texture, _Vertices.data(), static_cast<int>(_Vertices.size()),
                                           nullptr, 0);
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
                        continue;
                    }

                    const MCUiQuad& quad = *piece.Quad;
                    SDL_SetTextureColorMod(texture, quad.Color[0], quad.Color[1], quad.Color[2]);
                    SDL_SetTextureAlphaMod(texture, quad.Color[3]);
                    const auto toFloat = [](const SDL_Rect& rect)
                    {
                        return SDL_FRect{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                         static_cast<float>(rect.w), static_cast<float>(rect.h)};
                    };

                    const SDL_FRect target = toFloat(quad.Target);
                    const std::optional<SDL_FRect> source =
                        quad.Source.has_value() ? std::optional(toFloat(*quad.Source)) : std::nullopt;
                    SDL_RenderTexture(renderer, texture, source.has_value() ? &*source : nullptr, &target);
                }
            }

            REQUIRE(SDL_FlushRenderer(renderer));
            return *_Surface;
        }

    private:
        MCSdlHandle<SDL_Texture> Texture(const MCUiTexture& picture)
        {
            MCSdlHandle<SDL_Texture> made(SDL_CreateTexture(_Renderer.get(), SDL_PIXELFORMAT_RGBA32,
                                                            SDL_TEXTUREACCESS_STATIC, picture.Width, picture.Height));
            REQUIRE(made != nullptr);
            REQUIRE(SDL_UpdateTexture(made.get(), nullptr, picture.Pixels.data(),
                                      picture.Width * static_cast<int>(PixelBytes)));
            SDL_SetTextureBlendMode(made.get(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetTextureScaleMode(made.get(), picture.Smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            return made;
        }

        MCSdlHandle<SDL_Surface> _Surface;
        MCSdlHandle<SDL_Renderer> _Renderer;
        MCSdlHandle<SDL_Texture> _White;
        std::unordered_map<uint64_t, MCSdlHandle<SDL_Texture>> _Textures;
        std::vector<SDL_Vertex> _Vertices;
    };

    /// <summary>
    /// Random draws over a window: rectangles as RmlUi draws them (two triangles over four corners), boxes of one colour
    /// and pictures (whole texels, scaled up, down or not, nearest or smooth); and triangles (a gradient's two, a
    /// border's slanted edge, any, and two kinds SDL takes as a rectangle: a box of one colour between triangles, a right
    /// triangle after one sharing two of its corners); some cut by a scissor, some partly outside the window, in
    /// premultiplied colours (some opaque, some nothing).
    /// </summary>
    class MCRandomDraws
    {
    public:
        MCRandomDraws(int width, int height) : _Width(width), _Height(height)
        {
            constexpr int Pictures = 6;
            constexpr int PictureSizeMost = 48;

            for (int i = 0; i < Pictures; ++i)
            {
                auto picture = std::make_shared<MCUiTexture>(MCUiTexture{
                    .Id = static_cast<uint64_t>(i + 1),
                    .Width = Between(1, PictureSizeMost),
                    .Height = Between(1, PictureSizeMost),
                    .Smooth = i % 2 == 0,
                });

                for (int texel = 0; texel < picture->Width * picture->Height; ++texel)
                {
                    const std::array<uint8_t, PixelBytes> color = Color();
                    picture->Pixels.insert(picture->Pixels.end(), color.begin(), color.end());
                }

                _Pictures.push_back(std::move(picture));
            }
        }

        /// <summary>A draw made anew: a rectangle, or (one in three) triangles.</summary>
        MCUiDraw Draw()
        {
            constexpr int Kinds = 3;
            MCUiDraw draw = Between(1, Kinds) == 1 ? Triangles() : Rectangle();
            constexpr int OneIn = 4;

            if (Between(1, OneIn) == 1)
            {
                draw.Scissor = SDL_Rect{Between(0, _Width / 2), Between(0, _Height / 2), Between(1, _Width / 2),
                                        Between(1, _Height / 2)};
            }

            return draw;
        }

        int Between(int low, int high) { return std::uniform_int_distribution<int>(low, high)(_Random); }

    private:
        // Some past the window's edges.
        static constexpr int Beyond = 40;
        static constexpr int SizeMost = 400;

        /// <summary>A place across and one down, some past the window's edges.</summary>
        float Across() { return static_cast<float>(Between(-Beyond, _Width + Beyond)); }
        float Down() { return static_cast<float>(Between(-Beyond, _Height + Beyond)); }

        /// <summary>
        /// Untextured triangles: a gradient's two, a border's slanted edge, any, or two SDL takes as a rectangle.
        /// </summary>
        MCUiDraw Triangles()
        {
            constexpr int Kinds = 5;
            auto geometry = std::make_shared<MCUiGeometry>();
            std::vector<MCUiVertex>& at = geometry->Vertices;
            const auto corner = [this](float x, float y, const std::array<uint8_t, PixelBytes>& color)
            { return MCUiVertex{.X = x, .Y = y, .Color = color}; };
            const float x = Across();
            const float y = Down();
            const auto w = static_cast<float>(Between(1, SizeMost));
            const auto h = static_cast<float>(Between(1, SizeMost));

            switch (Between(1, Kinds))
            {
                case 1:
                {
                    // A vertical gradient: two colours, top and bottom.
                    const std::array<uint8_t, PixelBytes> top = Color();
                    const std::array<uint8_t, PixelBytes> bottom = Color();
                    at = {corner(x, y, top), corner(x + w, y, top), corner(x + w, y + h, bottom),
                          corner(x, y + h, bottom)};
                    geometry->Indices = {0, 1, 2, 0, 2, 3};
                    break;
                }
                case 2:
                {
                    // A border's top edge, its ends slanted, in one colour.
                    constexpr int WidthMost = 8;
                    const auto width = static_cast<float>(Between(1, WidthMost));
                    const std::array<uint8_t, PixelBytes> color = Color();
                    at = {corner(x, y, color), corner(x + w + 2 * width, y, color),
                          corner(x + w + width, y + width, color), corner(x + width, y + width, color)};
                    geometry->Indices = {0, 1, 2, 0, 2, 3};
                    break;
                }
                case 3:
                {
                    // Any triangles, either way round, each corner its own colour.
                    constexpr int Most = 4;

                    for (int i = 0, count = Between(1, Most); i < count; ++i)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            geometry->Indices.push_back(static_cast<int32_t>(at.size()));
                            at.push_back(corner(Across(), Down(), Color()));
                        }
                    }

                    break;
                }
                case 4:
                {
                    // A rectangle's lower left or upper right half after a triangle sharing an edge with its right
                    // angle (SDL fills the whole rectangle and leaves the first triangle out: SDL_SW_RenderGeometryRaw
                    // looks only at the corners it names) or its diagonal (SDL draws both, unless the first's third
                    // corner is a right angle too), that third corner sometimes in the same colour.
                    const std::array<uint8_t, PixelBytes> color = Color();
                    const bool lowerLeft = Between(0, 1) == 1;
                    const bool sameColour = Between(0, 1) == 1;
                    at = {corner(x, y, color), lowerLeft ? corner(x, y + h, color) : corner(x + w, y, color),
                          corner(Across(), Down(), sameColour ? color : Color()), corner(x + w, y + h, color)};
                    geometry->Indices = Between(0, 1) == 1 ? std::vector<int32_t>{0, 1, 2, 0, 3, 1}
                                                           : std::vector<int32_t>{0, 3, 2, 0, 3, 1};
                    break;
                }
                default:
                {
                    // A box of one colour between two triangles: not a rectangle to the pieces, one to SDL.
                    const std::array<uint8_t, PixelBytes> color = Color();
                    at = {corner(Across(), Down(), Color()), corner(Across(), Down(), Color()), corner(x, y, color),
                          corner(x + w, y, color),           corner(x + w, y + h, color),       corner(x, y + h, color),
                          corner(Across(), Down(), Color())};
                    geometry->Indices = {0, 1, 2, 2, 3, 4, 2, 4, 5, 5, 6, 0};
                    break;
                }
            }

            return MCUiDraw{.Geometry = std::move(geometry)};
        }

        /// <summary>A rectangle's draw: a box of one colour or a picture.</summary>
        MCUiDraw Rectangle()
        {
            const int x = Between(-Beyond, _Width);
            const int y = Between(-Beyond, _Height);
            const int w = Between(1, SizeMost);
            const int h = Between(1, SizeMost);
            MCUiDraw draw;
            std::array<float, 2> u{0.0f, 0.0f};
            std::array<float, 2> v{0.0f, 0.0f};

            if (Between(0, 1) == 0)
            {
                draw.Texture = _Pictures[static_cast<size_t>(Between(0, static_cast<int>(_Pictures.size()) - 1))];
                const int across = draw.Texture->Width;
                const int down = draw.Texture->Height;
                const int left = Between(0, across - 1);
                const int top = Between(0, down - 1);
                const auto place = [](int texels, int size)
                { return static_cast<float>(texels) / static_cast<float>(size); };
                u = {place(left, across), place(Between(left + 1, across), across)};
                v = {place(top, down), place(Between(top + 1, down), down)};
            }

            const std::array<uint8_t, PixelBytes> color = Color();
            auto geometry = std::make_shared<MCUiGeometry>();
            geometry->Vertices = {
                {.X = static_cast<float>(x), .Y = static_cast<float>(y), .Color = color, .U = u[0], .V = v[0]},
                {.X = static_cast<float>(x + w), .Y = static_cast<float>(y), .Color = color, .U = u[1], .V = v[0]},
                {.X = static_cast<float>(x + w), .Y = static_cast<float>(y + h), .Color = color, .U = u[1], .V = v[1]},
                {.X = static_cast<float>(x), .Y = static_cast<float>(y + h), .Color = color, .U = u[0], .V = v[1]},
            };

            geometry->Indices = {0, 1, 2, 0, 2, 3};
            draw.Geometry = std::move(geometry);
            return draw;
        }

        /// <summary>A premultiplied colour: opaque, nothing, or any alpha with channels no more than it.</summary>
        std::array<uint8_t, PixelBytes> Color()
        {
            constexpr int Kinds = 4;
            const int kind = Between(1, Kinds);
            const int alpha = kind == 1 ? 255 : kind == 2 ? 0 : Between(0, 255);
            const auto channel = [&] { return static_cast<uint8_t>(Between(0, alpha)); };
            return {channel(), channel(), channel(), static_cast<uint8_t>(alpha)};
        }

        int _Width;
        int _Height;
        std::mt19937 _Random{1};
        std::vector<std::shared_ptr<MCUiTexture>> _Pictures;
    };
}

/// <summary>
/// The software path's draws come out as SDL's software renderer draws them, byte for byte: boxes of one colour
/// (MCUiFill, SDL's blend), pictures scaled once and copied after (SDL's scale) and untextured triangles (MCUiTriangle,
/// SDL's), cut by scissors and the window's edges, drawn whole and then, as draws are taken away, changed and added,
/// only where they changed.
/// </summary>
TEST_CASE_ISOLATED("menus: the software path's draws come out as SDL's renderer draws them, byte for byte")
{
    constexpr int Width = 640;
    constexpr int Height = 480;
    constexpr int Draws = 400;
    constexpr int Changes = 12;
    MCRandomDraws random(Width, Height);
    MCUiDraws draws{.Width = Width, .Height = Height};

    for (int i = 0; i < Draws; ++i)
    {
        draws.Draws.push_back(random.Draw());
    }

    MCUiSoftware software;
    MCSdlPlain plain(Width, Height);
    REQUIRE(software.Draw(draws).has_value());
    CHECK_EQ(PictureDifference(*software.Picture(), plain.Draw(draws)), "");

    for (int change = 0; change < Changes; ++change)
    {
        // A few draws taken away, a few changed, a few added.
        constexpr int Each = 3;

        for (int i = 0; i < Each; ++i)
        {
            draws.Draws.erase(draws.Draws.begin() + random.Between(0, static_cast<int>(draws.Draws.size()) - 1));
            draws.Draws[static_cast<size_t>(random.Between(0, static_cast<int>(draws.Draws.size()) - 1))] =
                random.Draw();
            draws.Draws.insert(draws.Draws.begin() + random.Between(0, static_cast<int>(draws.Draws.size())),
                               random.Draw());
        }

        REQUIRE(software.Draw(draws).has_value());
        CHECK(software.Changed().size() > 0);
        CHECK_EQ(std::format("change {}: {}", change, PictureDifference(*software.Picture(), plain.Draw(draws))),
                 std::format("change {}: ", change));
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

    // Escape: the screen's way back, once however long it's held.
    probe.Key(SDLK_ESCAPE);
    CHECK_EQ(backs, 1);
    SDL_Event held = KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_ESCAPE, SDL_KMOD_NONE);
    held.key.repeat = true;
    probe.Menus->HandleEvent(held);
    CHECK_EQ(backs, 1);

    // The mouse's first move takes the ring away.
    probe.Key(SDLK_TAB);
    CHECK(probe.Ringed());
    probe.MouseTo(1.0f, 1.0f);
    CHECK(!probe.Ringed());
}

/// <summary>
/// A press that closes its own screen (the window's CANCEL, clicked or pressed by Enter, and Escape, its Back): the
/// screen is destroyed at once, but it and the press's action are let go only once the press is done, so what the
/// action does after the close is still there; then both are gone. The kit keeps no list of its sounds in the game.
/// </summary>
TEST_CASE_ISOLATED("menus: a press that closes its own screen finishes before the screen goes")
{
    // One menus at a time (RmlUi's): the probe's, then the game's.
    {
        MCHeadlessProbe probe(1280, 720);
        REQUIRE_EQ(probe.Error, "");
        probe.Frames();
        MCRmlProbe& screen = *probe.Probe;

        enum class MCPress
        {
            Click,
            Enter,
            Escape
        };

        for (const MCPress press : {MCPress::Click, MCPress::Enter, MCPress::Escape})
        {
            probe.Document().GetElementById("window")->Focus(true);
            probe.Key(SDLK_RETURN);
            const std::weak_ptr<MCRmlScreen> window = screen.Window().Lock();
            REQUIRE(!window.expired());
            REQUIRE_EQ(probe.Focused(), "cancel");
            Rml::Element* cancel = window.lock()->Document()->get().GetElementById("cancel");
            REQUIRE(cancel != nullptr);

            // The window's Back (CANCEL's and Escape's) closes it, then looks at itself and at what it holds.
            auto held = std::make_shared<int>(0);
            const std::weak_ptr<int> heldWatch = held;
            bool destroyedInside = false;
            bool heldInside = false;
            window.lock()->Back =
                [&closing = *window.lock(), held = std::move(held), &heldWatch, &destroyedInside, &heldInside]
            {
                closing.Close();
                destroyedInside = closing.WasDestroyed();
                heldInside = !heldWatch.expired() && *held == 0;
            };

            switch (press)
            {
                case MCPress::Click:
                    cancel->Click();
                    break;
                case MCPress::Enter:
                    probe.Key(SDLK_RETURN);
                    break;
                case MCPress::Escape:
                    probe.Key(SDLK_ESCAPE);
                    break;
            }

            CHECK(destroyedInside);
            CHECK(heldInside);
            CHECK(window.expired());
            CHECK(heldWatch.expired());
            probe.Frames(1);
            REQUIRE(probe.Menus->Menus().Top().has_value());
            CHECK_EQ(&probe.Menus->Menus().Top()->get(), static_cast<MCRmlScreen*>(&screen));
        }
    }

    // In the game (no test asking to keep them) the sounds are played, not kept.
    int played = 0;
    auto made = MCRmlMenus::Create(MCRmlMenus::MCOptions{.Width = 1280,
                                                         .Height = 720,
                                                         .MissionUnit = MCRmlMenus::MenusUnit,
                                                         .Sound = [&played](uint32_t) { ++played; }});
    REQUIRE(made.has_value());
    const std::unique_ptr<MCRmlMenus> menus = std::move(*made);
    menus->SetClock([] { return 0.0; });
    const MCGuiHandle<MCRmlProbe> quiet = menus->Menus().CreateChild<MCRmlProbe>([](MCRmlProbe&) {});
    REQUIRE(quiet->Open().has_value());
    menus->Frame();
    menus->HandleEvent(KeyEvent(SDL_EVENT_KEY_DOWN, SDLK_E, SDL_KMOD_NONE));
    menus->HandleEvent(KeyEvent(SDL_EVENT_KEY_UP, SDLK_E, SDL_KMOD_NONE));
    menus->Frame();
    CHECK_EQ(played, 1);
    CHECK(menus->Kit().Played().empty());
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

    // The mouse on a button with a hint, a screen on each layer: the hint shows, and unchanged frames keep their draws
    // (each layer keeps up its own documents' hint lines).
    Rml::Element* join = probe.Document().GetElementById("join");
    REQUIRE(join != nullptr);
    probe.MouseTo(join->GetAbsoluteLeft() + 2.0f, join->GetAbsoluteTop() + 2.0f);
    const Rml::Element* hints = probe.Document().QuerySelector(".mouse-hints");
    REQUIRE(hints != nullptr);
    CHECK(hints->GetInnerRML().find("Join") != std::string::npos);
    const std::shared_ptr<const MCUiDraws> hovered = probe.Frames();
    CHECK_EQ(probe.Frames().get(), hovered.get());

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
    /// <summary>A PNG of <paramref name="width"/> x <paramref name="height"/> pixels in one colour, RGB (no alpha) as the menus' opaque art is.</summary>
    void WriteRgbPng(const std::filesystem::path& path, int width, int height, SDL_Color color)
    {
        std::filesystem::create_directories(path.parent_path());
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGB24);
        REQUIRE(surface != nullptr);
        SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGB(surface, color.r, color.g, color.b));
        const bool saved = SDL_SavePNG(surface, MCFileSystem::ToUtf8(path).c_str());
        SDL_DestroySurface(surface);
        REQUIRE(saved);
    }
}

/// <summary>
/// The app's own art (SPEC-menus.md, "The art"): an RGB PNG under the <c>ui</c> folder's <c>art</c>, read whole and
/// opaque; with HD art on, its HD copy in <c>art/hd</c> drawn in its place, smoothly, while RmlUi is told the 1998 size;
/// with HD art off, or no copy, the 1998 picture.
/// </summary>
TEST_CASE("menus: the app's art reads as RGB, its HD copy in its place at the 1998 size")
{
    constexpr int Width = 6;
    constexpr int Height = 4;
    constexpr int HdScale = 4;
    constexpr SDL_Color Painted{40, 80, 120, 255};
    constexpr SDL_Color HdPainted{60, 90, 130, 255};
    const MCScratchFolder scratch("mc_menu_ui_tests", "app-art");
    const std::filesystem::path& folder = scratch.Path();
    WriteRgbPng(folder / "art" / "painting.png", Width, Height, Painted);
    WriteRgbPng(folder / "art" / "hd" / "painting.png", Width * HdScale, Height * HdScale, HdPainted);
    WriteRgbPng(folder / "art" / "plain.png", Width, Height, Painted);
    const MCRmlFiles files(folder);
    CHECK_EQ(MCRmlArt::HdPath("art/painting.png"), "art/hd/painting.png");

    for (const bool hd : {false, true})
    {
        const auto loaded = MCRmlArt::Load("art/painting.png", hd, files);
        REQUIRE(loaded.has_value());
        CHECK_EQ(loaded->Width, Width);
        CHECK_EQ(loaded->Height, Height);
        const MCUiTexture& texture = *loaded->Texture;
        CHECK_EQ(texture.Width, hd ? Width * HdScale : Width);
        CHECK_EQ(texture.Height, hd ? Height * HdScale : Height);
        CHECK_EQ(texture.Smooth, true);
        const SDL_Color want = hd ? HdPainted : Painted;
        REQUIRE(texture.Pixels.size() == static_cast<size_t>(texture.Width * texture.Height) * PixelBytes);
        CHECK_EQ(std::format("{},{},{},{}", texture.Pixels[0], texture.Pixels[1], texture.Pixels[2], texture.Pixels[3]),
                 std::format("{},{},{},255", want.r, want.g, want.b));
    }

    // No HD copy: the 1998 picture with HD art on too; a picture that isn't there, or isn't a PNG, an error.
    const auto plain = MCRmlArt::Load("art/plain.png", true, files);
    REQUIRE(plain.has_value());
    CHECK_EQ(plain->Texture->Width, Width);
    CHECK(!MCRmlArt::Load("art/missing.png", true, files).has_value());
    CHECK(!MCRmlArt::Load("../painting.png", false, files).has_value());
}

namespace
{
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

/// <summary>
/// On the GPU renderer the menus' draws are on the window itself, and in its recording (MCDisplay::RecordWindow, the
/// tour's whole-window pictures): JOIN GAME's blue where the probe's layout puts it, not the logistics screen under it.
/// Skipped where the GPU renderer isn't there (it fell back to the software one).
/// </summary>
TEST_CASE_ISOLATED("game: the probe screen shows on the GPU renderer's window and in its recording")
{
    if (!MCTestGame::Available())
    {
        return;
    }

    GRenderer = static_cast<int>(MCRendererKind::Vulkan);
    REQUIRE(MCTestGame::StartLogistics());
    const std::shared_ptr<MCDisplay> display = MCInput::Display();
    REQUIRE(display != nullptr);

    if (display->RendererKind() != MCRendererKind::Vulkan)
    {
        return;
    }

    constexpr MCWindowSize Window{1280, 720};
    REQUIRE(SetWindowPixels(*display, Window.Width, Window.Height));
    MCGameProbe probe(*display, false);
    REQUIRE_EQ(probe.Error, "");
    RunFrames(SettleFrames);

    std::optional<MCWindowFrame> taken;
    REQUIRE(display->RecordWindow([&taken](MCWindowFrame&& frame) { taken = std::move(frame); }));
    MCTestGame::RunFrame(FrameSeconds);
    display->ShowPresented();
    display->RecordWindow({});
    REQUIRE(taken.has_value());
    REQUIRE_EQ(taken->Width, Window.Width);
    REQUIRE_EQ(taken->Height, Window.Height);

    // JOIN GAME's middle, in window pixels: the look's main button blue (.button.main, #0c346c).
    Rml::Element* join = probe.Probe->Document()->get().GetElementById("join");
    REQUIRE(join != nullptr);
    const Rml::Vector2f at = join->GetAbsoluteOffset(Rml::BoxArea::Border) + join->GetBox().GetSize() / 2.0f;
    const SDL_Color& pixel = taken->Pixels[static_cast<size_t>(std::lround(at.y)) * static_cast<size_t>(Window.Width) +
                                           static_cast<size_t>(std::lround(at.x))];
    CHECK_EQ(std::format("{:02x}{:02x}{:02x}", pixel.r, pixel.g, pixel.b), "0c346c");
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

    /// <summary>The present's parts in <paramref name="tally"/> (the frame log's), in ms a frame of
    /// <paramref name="frames"/>.</summary>
    std::string PresentParts(const MCFrameLog::MCTally& tally, int32_t frames)
    {
        std::string parts;

        for (const auto& [name, ms] : tally.PartsMs)
        {
            if (name.starts_with("present"))
            {
                parts += std::format("{}{} {:.3f}", parts.empty() ? "" : ", ", name, ms / frames);
            }
        }

        return parts.empty() ? std::string("none") : parts;
    }
}

/// <summary>
/// Tool: <c>--rml-probe &lt;folder&gt;</c> shows the probe screen at <c>--window WxH</c> (1280x720 by default) over the
/// logistics screen, HD art on with the pack (<c>--hd-pack</c>, else the user folder's) or off with <c>--hd-original
/// 1</c>, on the GPU renderer or with <c>--rml-software 1</c> the software one, and saves the whole window's picture
/// into the folder. Then it times <c>--rml-frames</c> frames (200 by default) without vsync, the mouse moving down the
/// list's rows (each frame restyles two): the menus' layout and recording a frame, the present with their draws and
/// without, and their draws drawn alone and read back; and the present's parts from a frame log beside the picture.
/// On the software path also a redraw's time (the boxes changed) and a whole picture's (the median of
/// <c>--rml-whole</c>, 20 by default).
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
    const uint64_t redrawsBefore = display->OverlayRedraws();
    // The present's parts, each loop's tallied apart, from a frame log beside the picture.
    const std::filesystem::path log = std::filesystem::path(path).replace_extension(".framelog");
    REQUIRE(MCFrameLog::Open(log));
    MCFrameLog::NextFrame();
    MCFrameLog::StartTally();

    std::shared_ptr<const MCUiDraws> lastDraws;

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        MCFrameLog::NextFrame();
        const float y = rowsTop + std::fmod(static_cast<float>(frame) * RowStep, rowsHeight);
        MCInput::HandleEvent(MouseAt(*display, rowsX, y));
        const uint64_t start = SDL_GetPerformanceCounter();
        const std::shared_ptr<const MCUiDraws> draws = menus.FrameFor(width, height);
        lastDraws = draws;
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

    // The software presenter's redraws of the menus' picture (none on the GPU: it draws them every frame).
    const uint64_t redraws = display->OverlayRedraws() - redrawsBefore;
    MCFrameLog::NextFrame();
    const MCFrameLog::MCTally withParts = MCFrameLog::TakeTally();

    // The same without the menus: a present alone, and an empty window's read back.
    const auto empty = std::make_shared<MCUiDraws>(MCUiDraws{.Width = width, .Height = height, .Draws = {}});
    display->SetOverlay(nullptr);
    double withoutMs = 0.0;
    double readEmptyMs = 0.0;
    MCFrameLog::StartTally();

    for (int32_t frame = 0; frame < frames; ++frame)
    {
        MCFrameLog::NextFrame();
        const uint64_t start = SDL_GetPerformanceCounter();
        REQUIRE(display->Present(false).has_value());
        const uint64_t shown = SDL_GetPerformanceCounter();
        REQUIRE(display->ReadOverlay(*empty).has_value());
        withoutMs += Milliseconds(start, shown);
        readEmptyMs += Milliseconds(shown, SDL_GetPerformanceCounter());
    }

    std::cout << std::format(
        "  {} frames at {}x{}, no vsync, ms a frame: the menus' layout and recording {:.3f}; a present {:.3f} with "
        "their draws ({} redrawn by the software presenter), {:.3f} without (+{:.3f}); drawn alone and read back "
        "{:.3f}, "
        "an empty window {:.3f} (+{:.3f})\n",
        frames, width, height, menusMs / frames, withMs / frames, redraws, withoutMs / frames,
        (withMs - withoutMs) / frames, readMs / frames, readEmptyMs / frames, (readMs - readEmptyMs) / frames);
    MCFrameLog::NextFrame();
    const MCFrameLog::MCTally withoutParts = MCFrameLog::TakeTally();
    MCFrameLog::Close();
    std::cout << std::format("  the present's parts, ms a frame, with the menus: {}; without: {}\n",
                             PresentParts(withParts, frames), PresentParts(withoutParts, frames));

    // The software path: a redraw's time (only the boxes changed), and a whole picture's drawn on the CPU.
    if (software && redraws > 0 && lastDraws != nullptr)
    {
        const auto drawPart = std::ranges::find(withParts.PartsMs, std::string("present.menus.draw"),
                                                &std::pair<std::string, double>::first);
        const double redrawMs = drawPart != withParts.PartsMs.end() ? drawPart->second / redraws : 0.0;
        constexpr int32_t DefaultWholeRedraws = 20;
        int32_t wholeRedraws = DefaultWholeRedraws;

        if (const std::optional<std::string_view> option = MCTest::Option("rml-whole"); option.has_value())
        {
            std::from_chars(option->data(), option->data() + option->size(), wholeRedraws);
        }

        MCUiSoftware whole;
        std::vector<double> wholeMs;

        for (int32_t i = 0; i < std::max(wholeRedraws, 1); ++i)
        {
            whole.Forget();
            const uint64_t start = SDL_GetPerformanceCounter();
            REQUIRE(whole.Draw(*lastDraws).has_value());
            wholeMs.push_back(Milliseconds(start, SDL_GetPerformanceCounter()));
        }

        std::ranges::sort(wholeMs);
        std::cout << std::format(
            "  the software path: a redraw {:.3f} ms (of {}, the boxes changed, uploaded); a whole "
            "picture drawn {:.3f} ms (the median of {})\n",
            redrawMs, redraws, wholeMs[wholeMs.size() / 2], wholeMs.size());
    }

    GHdArt = 0;
    MCHdArt::SetPackFolder({});
}
