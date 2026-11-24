#include "stdafx.h"
#include "logistics/MCOperationVideo.h"
#include "ai/MCMoveGeometry.h"
#include "lib/MCFile.h"
#include "logistics/MCCampaignMenu.h"
#include "main/MCLogistics.h"
#include "main/MCGamePaths.h"
#include "platform/MCSmacker.h"
#include "sound/MCSoundSystem.h"
#include "sprite/MCHdMovie.h"
#include "ui/MCRmlFlow.h"
#include "ui/MCRmlMenus.h"
#include "ui/MCRmlModel.h"
#include "ui/MCRmlRecorder.h"

#include <RmlUi/Core/Input.h>

namespace
{
    constexpr const char* WindowDocument = "video.rml";
    constexpr const char* WindowModel = "video";
    constexpr const char* PauseId = "pause";
    constexpr const char* CloseId = "close";
    constexpr const char* NoHeight = "0dp";
    /// <summary>SmackOpen's flags and extra buffers: the port's player needs neither.</summary>
    constexpr uint32_t NoFlags = 0;
    constexpr int32_t NoExtraBuffers = -1;
    /// <summary>Betty's "incoming transmission" before the operation's video, and the channel she speaks on (1998's).</summary>
    constexpr uint32_t IncomingBetty = 0x1a;
    constexpr int32_t BettyChannel = 0xe;
    /// <summary>A picture's bytes a pixel (RGBA8), and a palette's a colour (RGB8).</summary>
    constexpr size_t PixelBytes = 4;
    constexpr size_t PaletteBytes = 3;
    constexpr uint8_t Opaque = 0xff;
    constexpr double MicrosecondsPerSecond = 1000000.0;
    constexpr int SecondsPerMinute = 60;
    constexpr int WholePercent = 100;

    MCGuiHandle<MCOperationVideoWindow>& TheWindow()
    {
        static MCGuiHandle<MCOperationVideoWindow> window;
        return window;
    }

    /// <summary>The live picture the window shows from now on (none: nothing).</summary>
    void ShowLive(std::shared_ptr<const MCUiTexture> picture)
    {
        if (const MCOptionalRef<MCRmlMenus> menus = MCRmlMenus::Running(); menus.has_value())
        {
            menus->get().ShowLive(std::string(MCVideoMenu::LiveSource), std::move(picture));
        }
    }
}

// MCOperationVideo

MCOperationVideo::MCOperationVideo() = default;

MCOperationVideo::~MCOperationVideo() = default;

std::expected<std::unique_ptr<MCOperationVideo>, std::string> MCOperationVideo::Open(std::string_view cinema,
                                                                                     bool incoming)
{
    const std::string path = GamePath(MoviePath, cinema, ".smk");
    std::unique_ptr<MCSmackTag> movie = SmackOpen(path, NoFlags, NoExtraBuffers);

    if (movie == nullptr || movie->Player == nullptr)
    {
        return std::unexpected(std::format("the operation's video {} can't be read", path));
    }

    std::unique_ptr<MCOperationVideo> video(new MCOperationVideo());
    const MCSmackerPlayer& player = *movie->Player;
    video->_Indices.resize(static_cast<size_t>(player.Width()) * static_cast<size_t>(player.Height()));
    video->_Movie = std::move(movie);
    video->_Hd = MCHdMovie::Over(*video->_Movie);

    if (incoming && SoundSystem() != nullptr)
    {
        SoundSystem()->PlayBettySample(IncomingBetty);
        video->_Incoming = true;
    }

    return video;
}

bool MCOperationVideo::Step()
{
    if (_Over || Paused())
    {
        return false;
    }

    // Betty's line first, as 1998's operation tab waited for it.
    if (_Incoming)
    {
        if (SoundSystem() != nullptr && SoundSystem()->IsChannelPlaying(BettyChannel))
        {
            return false;
        }

        _Incoming = false;
    }

    MCSmackerPlayer& player = *_Movie->Player;

    if (player.Wait())
    {
        return false;
    }

    const uint32_t frame = player.FrameNum();

    if (const std::expected<void, std::string> done = player.DoFrame(); !done)
    {
        SDL_Log("Menus: the operation's video %s stops at frame %u: %s", _Movie->FileName.c_str(), frame,
                done.error().c_str());
        _Over = true;
        return false;
    }

    player.CopyFrame(_Indices, 0, 0, player.Width(), player.Height());
    _Picture = Pictured(frame);
    _Shown = frame + 1;

    if (_Shown >= player.Frames())
    {
        _Over = true;
    }
    else
    {
        player.NextFrame();
    }

    return true;
}

std::shared_ptr<const MCUiTexture> MCOperationVideo::Pictured(uint32_t frame)
{
    const MCSmackerPlayer& player = *_Movie->Player;
    const int32_t width = player.Width();
    const int32_t height = player.Height();

    if (_Hd != nullptr)
    {
        std::expected<std::shared_ptr<const MCHdArt::Frame>, std::string> hd =
            _Hd->Next(_Indices, width, height, width);

        if (!hd)
        {
            DropHd(std::format("frame {}: {}", frame, hd.error()));
        }
        else if (std::expected<std::vector<uint8_t>, std::string> colors = MCHdMovie::Rgba(**hd); colors)
        {
            return MCRmlRecorder::MakeTexture((*hd)->Width, (*hd)->Height, std::move(*colors), true);
        }
        else
        {
            DropHd(std::format("frame {}: {}", frame, colors.error()));
        }
    }

    // The movie's own colours (1998's window remapped them to the game's palette, its screen being 8-bit), its pixels
    // drawn as the 1998 art's are; an interlaced movie's rows between its own black, as 1998's window left them (its
    // pane wiped to the game palette's black).
    const std::array<uint8_t, 768>& palette = player.Palette();
    const uint32_t flags = _Movie->Player->Decoder().Flags();
    const bool gaps = (flags & MCSmacker::FlagYInterlaced) != 0 && (flags & MCSmacker::FlagYDoubled) == 0;
    std::vector<uint8_t> pixels(_Indices.size() * PixelBytes);

    for (size_t at = 0; at < _Indices.size(); ++at)
    {
        uint8_t* pixel = pixels.data() + at * PixelBytes;
        pixel[3] = Opaque;

        if (gaps && (at / static_cast<size_t>(width)) % 2 != 0)
        {
            continue;
        }

        const size_t colour = static_cast<size_t>(_Indices[at]) * PaletteBytes;
        pixel[0] = palette[colour];
        pixel[1] = palette[colour + 1];
        pixel[2] = palette[colour + 2];
    }

    return MCRmlRecorder::MakeTexture(width, height, std::move(pixels), false);
}

void MCOperationVideo::DropHd(std::string_view why)
{
    SDL_Log("HD movie: %s: %.*s; the original plays on", _Movie->FileName.c_str(), static_cast<int>(why.size()),
            why.data());
    _HdDropped = std::move(_Hd);
}

void MCOperationVideo::Pause(bool paused)
{
    _Movie->Player->Pause(paused);
}

bool MCOperationVideo::Paused() const
{
    return _Movie->Player->Paused();
}

uint32_t MCOperationVideo::Frames() const
{
    return _Movie->Player->Frames();
}

double MCOperationVideo::Seconds() const
{
    return Frames() * static_cast<double>(_Movie->Player->Decoder().MicrosecondsPerFrame()) / MicrosecondsPerSecond;
}

double MCOperationVideo::SecondsShown() const
{
    return _Shown * static_cast<double>(_Movie->Player->Decoder().MicrosecondsPerFrame()) / MicrosecondsPerSecond;
}

int32_t MCOperationVideo::Width() const
{
    return _Movie->Player->Width();
}

int32_t MCOperationVideo::Height() const
{
    return _Movie->Player->Height();
}

// MCOperationVideoWindow

MCOperationVideoWindow::MCOperationVideoWindow()
{
    SetDocument(WindowDocument);
    Modal = true;
    Back = [this] { Close(); };
    // No picture and no height until a video plays (the bound styles are read as the document loads).
    _Still = MCCampaignScreen::NoPicture;
    _Height = NoHeight;
}

MCOperationVideoWindow::~MCOperationVideoWindow() = default;

void MCOperationVideoWindow::MakeModels(Rml::Context& context)
{
    _Model = std::make_unique<MCRmlModel>(context, WindowModel);
    MCRmlModel& model = *_Model;
    model.Value("eyebrow", _Eyebrow);
    model.Value("still", _Still);
    model.Value("height", _Height);
    model.Value("playing", _Playing);
    model.Value("paused", _Paused);
    model.Value("percent", _Percent);
    model.Value("time", _Time);
}

void MCOperationVideoWindow::OnLoaded(Rml::ElementDocument& /*document*/)
{
    OnClick(PauseId, [this] { TogglePause(); });
    OnClick(CloseId, [this] { Close(); });
    // Space pauses wherever the focus is (RmlUi's own would press the button with it).
    TakeKeysFirst();
}

void MCOperationVideoWindow::OnDestroy()
{
    _Video.reset();
    ShowLive(nullptr);
    _Model.reset();
    MCRmlScreen::OnDestroy();
}

void MCOperationVideoWindow::OnHide()
{
    // The video goes with the window (its sound stopped), and its last frame's picture.
    _Video.reset();
    ShowLive(nullptr);
}

void MCOperationVideoWindow::Play(std::unique_ptr<MCOperationVideo> video, std::string eyebrow, std::string still)
{
    _Video = std::move(video);
    ShowLive(nullptr);
    _Eyebrow = std::move(eyebrow);
    _Still = std::move(still);
    const float height =
        _Video->Width() > 0 ? MCVideoMenu::VideoUnits * static_cast<float>(_Video->Height()) / _Video->Width() : 0.0f;
    _Height = std::format("{:.1f}dp", height);
    Fill(true);
}

MCOptionalRef<MCOperationVideo> MCOperationVideoWindow::Video() const
{
    return _Video != nullptr ? MCOptionalRef<MCOperationVideo>(*_Video) : std::nullopt;
}

void MCOperationVideoWindow::TogglePause()
{
    if (_Video == nullptr)
    {
        return;
    }

    _Video->Pause(!_Video->Paused());
    Fill(false);
}

void MCOperationVideoWindow::Close()
{
    Hide();
}

void MCOperationVideoWindow::Fill(bool all)
{
    const bool playing = _Video != nullptr && _Video->Picture() != nullptr;
    const bool paused = _Video != nullptr && _Video->Paused();
    const int percent = _Video != nullptr && _Video->Frames() > 0
                            ? static_cast<int>(_Video->Shown() * static_cast<uint64_t>(WholePercent) / _Video->Frames())
                            : 0;
    const std::string time = _Video != nullptr
                                 ? std::format(MCVideoMenu::AlongFormat, MCVideoMenu::TimeText(_Video->SecondsShown()),
                                               MCVideoMenu::TimeText(std::round(_Video->Seconds())))
                                 : std::string();
    const bool keys = all || paused != _Paused;
    const auto keep = [this, all](auto& value, auto made, const char* name)
    {
        if (all || value != made)
        {
            value = std::move(made);

            if (_Model != nullptr && !all)
            {
                _Model->Changed(name);
            }
        }
    };

    keep(_Playing, playing, "playing");
    keep(_Paused, paused, "paused");
    keep(_Percent, percent, "percent");
    keep(_Time, time, "time");

    if (all && _Model != nullptr)
    {
        _Model->Changed();
    }

    if (keys)
    {
        Keys = {{"Space", _Paused ? "Play" : "Pause"}, {"Esc", "Close"}};
        ShowKeys();
    }
}

bool MCOperationVideoWindow::OnKey(int key, int /*modifiers*/)
{
    if (key != Rml::Input::KI_SPACE)
    {
        return false;
    }

    TogglePause();
    return true;
}

void MCOperationVideoWindow::OnFrame()
{
    if (!Shown() || _Video == nullptr)
    {
        return;
    }

    if (_Video->Step())
    {
        ShowLive(_Video->Picture());
    }

    // At its end, back to the briefing (1998's window closed with the movie).
    if (_Video->Over())
    {
        Close();
        return;
    }

    Fill(false);
}

// MCVideoMenu

bool MCVideoMenu::Open(std::string_view cinema, std::string eyebrow)
{
    const MCOptionalRef<MCOperationVideoWindow> made = MCRmlFlow::MadeNow(TheWindow(), "the operation's video");

    if (!made.has_value())
    {
        return false;
    }

    // 1998's operation tab: Betty's "incoming transmission" over the operation's picture, when the mission names an
    // operation, then the movie.
    const std::shared_ptr<MCLogistics> logistics = GlobalLog();
    const int32_t operation = logistics != nullptr ? logistics->Operation : 0;
    std::expected<std::unique_ptr<MCOperationVideo>, std::string> video =
        MCOperationVideo::Open(cinema, operation != 0);

    if (!video)
    {
        SDL_Log("Menus: %s", video.error().c_str());
        return false;
    }

    std::string still(MCCampaignScreen::NoPicture);

    if (operation != 0)
    {
        still = std::format("image(art:logart/{}{} contain center center)", CurPlanet == 0 ? "lsb_op" : "mcxcard",
                            operation);
    }

    MCOperationVideoWindow& window = made->get();
    window.Play(std::move(*video), std::move(eyebrow), std::move(still));
    window.Show();
    return true;
}

void MCVideoMenu::Close()
{
    if (const MCOptionalRef<MCOperationVideoWindow> window = Window(); window.has_value() && window->get().Shown())
    {
        window->get().Close();
    }
}

MCOptionalRef<MCOperationVideoWindow> MCVideoMenu::Window()
{
    return MCRmlFlow::Alive(TheWindow());
}

bool MCVideoMenu::Shown()
{
    const MCOptionalRef<MCOperationVideoWindow> window = Window();
    return window.has_value() && window->get().Shown();
}

std::string MCVideoMenu::TimeText(double seconds)
{
    const auto whole = static_cast<int>(std::max(seconds, 0.0));
    return std::format(TimeFormat, whole / SecondsPerMinute, whole % SecondsPerMinute);
}
