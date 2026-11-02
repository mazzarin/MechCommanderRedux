#include "stdafx.h"
#include "gui/MCGuiSmackerWindow.h"
#include "gui/MCGuiSystem.h"
#include "lib/MCFatal.h"
#include "platform/MCRenderer.h"
#include "platform/MCSmacker.h"
#include "sprite/MCHdArt.h"
#include "vfx/MCVfxFunctions.h"

// Port: a full-screen movie (fullScreen set while the game is full screen) switched the original's display to 16-bit
// and let Smacker draw to the DirectDraw surface in its own colours. The port's display stays 8-bit, so a full-screen
// movie puts up its own palette (CheckSmackerPalette), as the original did in a windowed game; a windowed movie is
// remapped to the game palette. Either way the movie decodes into the window's own pane (the original decoded a
// full-screen movie straight onto the screen), and the window copies the pane to the screen in the frame pass: movie
// frames are the one picture that changes under the renderer every frame.

int MovieOver = 0;

namespace
{
    /// <summary>Clears the whole screen buffer (a full-screen movie's background).</summary>
    void ClearScreen()
    {
        // Port: the original cleared 640x480 bytes (0x96000 in 16-bit mode) of the surface; the port wipes the
        // screen at its real size, through the renderer.
        VfxPaneWipe(MCPane::Over(ScreenPort()->SharedBitmap()), 0);
    }

    /// <summary>Remaps a windowed movie onto the game's current palette (<c>SmackColorRemap</c>).</summary>
    void RemapToGamePalette(MCSmackTag& movie)
    {
        const std::array<MCVfxRgb, 256>& palette = GuiSystem().CurrentPalette;
        movie.Player->ColorRemap(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(palette.data()),
                                                          sizeof(MCVfxRgb) * palette.size()));
    }
}

auto MCGuiSmackerWindow::Init(const tagRECT& area, std::optional<tagPOINT> position) -> int32_t
{
    int32_t left = area.left;
    int32_t top = area.top;

    if (position.has_value())
    {
        left += position->x;
        top += position->y;
    }

    return MCGuiObject::Init(left, top, area.right, area.bottom, {});
}

auto MCGuiSmackerWindow::StartSmackerMovie(std::unique_ptr<MCSmackTag> newMovie, bool fullScreen) -> int32_t
{
    FullScreen = fullScreen;
    Movie = std::move(newMovie);
    MovieOver = 0;
    // (The original returned here for a full-screen movie: it had no pane.)
    MoviePane = std::make_unique<MCPane>(Frame());
    MovieWindow = std::make_shared<MCWindow>();
    MoviePane->Window = MovieWindow;

    if (MoviePane->X1 < 0 || MoviePane->Y1 < 0)
    {
        return NoPane;
    }

    MovieWindow->XMax = MoviePane->X1;
    MovieWindow->YMax = MoviePane->Y1;
    MovieWindow->Pixels = std::make_shared<uint8_t[]>(MovieWindow->Size());
    MCRenderer::CreateTexture(*MovieWindow, MCTextureUse::Stream);

    if (!FullScreen)
    {
        RemapToGamePalette(*Movie);
    }

    OpenHdMovie();
    return 0;
}

auto MCGuiSmackerWindow::OpenHdMovie() -> void
{
    HdMovie.reset();
    HdDropped.reset();
    HdNextFrame = 0;
    const std::optional<std::filesystem::path> path = MCHdArt::FindMovie(Movie->FileName);

    // Off a sharp screen the frames wouldn't be seen: none are decoded.
    if (!path.has_value() || !MCRenderer::ShowsHdPictures())
    {
        return;
    }

    const MCSmackerPlayer& player = *Movie->Player;
    std::expected<std::unique_ptr<MCHdMovie>, std::string> opened = MCHdMovie::Open(*path);

    if (!opened)
    {
        SDL_Log("HD movie: %s; the original plays", opened.error().c_str());
        return;
    }

    const MCHdMovie& movie = **opened;

    if (!movie.ScaleOver(player.Width(), player.Height()).has_value() || movie.Frames() != player.Frames())
    {
        SDL_Log("HD movie: %s's %u frames of %dx%d don't fit the movie's %u of %dx%d; the original plays",
                Movie->FileName.c_str(), movie.Frames(), movie.Width(), movie.Height(), player.Frames(), player.Width(),
                player.Height());
        return;
    }

    HdMovie = std::move(*opened);
}

auto MCGuiSmackerWindow::ShowHdFrame(MCTexture& texture, const MCRect& rect, uint32_t shown) -> void
{
    // The frame shown last, decoded again (a movie at its end whose window is still up), keeps its HD frame.
    if (HdMovie == nullptr || shown + 1 == HdNextFrame)
    {
        return;
    }

    if (shown != HdNextFrame)
    {
        DropHdMovie(texture, std::format("frame {} played where {} was due", shown, HdNextFrame));
        return;
    }

    const MCSmackerPlayer& player = *Movie->Player;
    HdOriginal.resize(static_cast<size_t>(player.Width()) * player.Height());
    player.CopyFrame(HdOriginal, 0, 0, player.Width(), player.Height());
    std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> frame =
        HdMovie->Next(HdOriginal, player.Width(), player.Height(), player.Width());

    if (!frame)
    {
        DropHdMovie(texture, frame.error());
        return;
    }

    ++HdNextFrame;
    MCRenderer::ShowHdPicture(texture, std::move(*frame), rect.X0, rect.Y0);
}

auto MCGuiSmackerWindow::DropHdMovie(MCTexture& texture, std::string_view why) -> void
{
    SDL_Log("HD movie: %s: %.*s; the original plays on", Movie->FileName.c_str(), static_cast<int>(why.size()),
            why.data());
    HdDropped = std::move(HdMovie);
    MCRenderer::ShowHdPicture(texture, nullptr, 0, 0);
}

auto MCGuiSmackerWindow::Destroy() -> void
{
    Movie.reset();
    HdMovie.reset();
    HdDropped.reset();

    if (MovieWindow != nullptr)
    {
        MCRenderer::DestroyTexture(*MovieWindow);
    }

    MoviePane.reset();
    MovieWindow.reset();
    MCGuiObject::Destroy();
    ScreenWindow()->RemoveChild(*this);
}

auto MCGuiSmackerWindow::EndSmackerMovie() -> void
{
    Movie.reset();
    MovieOver = 1;
    Destroy();
}

auto MCGuiSmackerWindow::CheckSmackerPalette() -> void
{
    // Port fix: the original reads the movie unguarded.
    if (Movie == nullptr || !Movie->Player->NewPalette())
    {
        return;
    }

    if (!FullScreen)
    {
        RemapToGamePalette(*Movie);
        return;
    }

    GuiSystem().ActivateSmackerPalette(Movie->Player->Palette());
}

auto MCGuiSmackerWindow::Display() -> void
{
    if (!ShowWindow || (IsHidden() && HideOffset == 0))
    {
        return;
    }

    if (Movie == nullptr)
    {
        if (FullScreen)
        {
            ClearScreen();
        }

        if (MoviePane != nullptr)
        {
            VfxPaneWipe(*MoviePane.get(), 0);
        }

        // (The original left the screen as it was.) An escaped movie has destroyed the window already
        // (EndSmackerMovie) while the GUI system still names it until its owner clears it, so there is no pane to draw.
        if (FramePane != nullptr)
        {
            DrawInFramePass(*DisplayPort);
        }

        return;
    }

    MCSmackerPlayer& player = *Movie->Player;

    if (static_cast<uint32_t>(Width()) < static_cast<uint32_t>(player.Width()) ||
        static_cast<uint32_t>(Height()) < static_cast<uint32_t>(player.Height()))
    {
        Fatal(0, "Movie is too big for its window");
    }

    if (FirstFrame)
    {
        VfxPaneWipe(*MoviePane.get(), 0);

        if (FullScreen)
        {
            ClearScreen();
        }

        FirstFrame = false;
    }

    // (The original set SmackToBuffer every display; the port locks the movie's rectangle of the texture only while
    // a frame is decoded into it, so a display with no new frame sends nothing up.)
    if (!player.Wait())
    {
        const std::shared_ptr<MCTexture> held = MovieWindow->Texture.lock();
        MCTexture& texture = *held;
        const MCRect rect{MoviePane->X0, MoviePane->Y0, MoviePane->X0 + player.Width() - 1,
                          MoviePane->Y0 + player.Height() - 1};
        // The locked rows are Width bytes apart from the rectangle's top-left pixel.
        const std::span<uint8_t> locked = MCRenderer::LockTexture(texture, rect);
        const size_t lockedSize =
            static_cast<size_t>(player.Height() - 1) * texture.Width + static_cast<size_t>(texture.Width - rect.X0);
        const uint32_t shown = player.FrameNum();
        const bool more = NextFrame(locked.first(lockedSize), texture.Width, player.Height());
        MCRenderer::UnlockTexture(texture);
        ShowHdFrame(texture, rect, shown);

        if (!more)
        {
            MovieOver = 1;
        }
    }

    DrawInFramePass(*DisplayPort);
}

auto MCGuiSmackerWindow::Draw() -> void
{
    // (The original drew the window's own picture, and only while the movie played.)
    if (MoviePane != nullptr && MovieWindow != nullptr && MovieWindow->Pixels != nullptr)
    {
        VfxPaneCopy(*MoviePane.get(), 0, 0, Port().Frame(), 0, 0, -1);
    }
}

auto MCGuiSmackerWindow::NextFrame(std::span<uint8_t> buffer, int32_t pitch, int32_t height) const -> bool
{
    MCSmackerPlayer& player = *Movie->Player;
    // Port: SmackToBufferRect (collecting the changed rectangle) has no use here; the whole frame is copied.
    static_cast<void>(player.DoFrame());
    player.CopyFrame(buffer, 0, 0, pitch, height);

    if (player.FrameNum() == player.Frames() - 1)
    {
        return false;
    }

    player.NextFrame();
    return true;
}

auto MCGuiSmackerWindow::FindObject(int32_t, int32_t) -> std::shared_ptr<MCGuiObject>
{
    return nullptr;
}
