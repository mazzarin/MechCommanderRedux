#pragma once

// Port-only (SPEC-menus.md's stage 8, BRIEFING): the operation's video in a window over the remade briefing. PLAY (or
// Space) on BRIEFING opens it over the dimmed briefing, 448 units wide: the mission's OperationCinema (a Smacker movie
// in data\movies\, with its sound) after Betty's "incoming transmission" over the operation's picture, as 1998's
// operation tab played it, each frame shown from the HD art pack's when the 1998 window would show it; PAUSE (Space)
// holds it, how far along under it, CLOSE (Escape) or its end goes back to the briefing as it was.

#include "ui/MCRmlScreen.h"

class MCHdMovie;
class MCRmlModel;
struct MCSmackTag;
struct MCUiTexture;

/// <summary>The operation's video played: its frames as the menus draw them (a live picture), held or going.</summary>
class MCOperationVideo
{
public:
    /// <summary>
    /// The movie <paramref name="cinema"/> (the mission's OperationCinema) opened, from its first frame; with
    /// <paramref name="incoming"/>, after Betty's "incoming transmission" (1998's, with the operation's picture); why
    /// not, when it can't be read.
    /// </summary>
    static std::expected<std::unique_ptr<MCOperationVideo>, std::string> Open(std::string_view cinema, bool incoming);

    ~MCOperationVideo();
    MCOperationVideo(const MCOperationVideo&) = delete;
    MCOperationVideo& operator=(const MCOperationVideo&) = delete;

    /// <summary>The video moved along: Betty's line first, then each frame as it is due; whether a frame was shown.</summary>
    bool Step();

    /// <summary>Holds it where it is (its clock and sound stopped), or lets it go on.</summary>
    void Pause(bool paused);
    bool Paused() const;
    /// <summary>Whether Betty's line still plays before the first frame.</summary>
    bool Incoming() const { return _Incoming; }
    /// <summary>Whether its last frame has shown.</summary>
    bool Over() const { return _Over; }

    /// <summary>The frames shown, and the movie's.</summary>
    uint32_t Shown() const { return _Shown; }
    uint32_t Frames() const;
    /// <summary>How long it is, and how far along, in seconds.</summary>
    double Seconds() const;
    double SecondsShown() const;
    /// <summary>The movie's size (its 1998 pixels).</summary>
    int32_t Width() const;
    int32_t Height() const;
    /// <summary>Whether its frames are the HD art pack's.</summary>
    bool Hd() const { return _Hd != nullptr; }

    /// <summary>The frame shown last as a picture (premultiplied RGBA8); none before the first.</summary>
    const std::shared_ptr<const MCUiTexture>& Picture() const { return _Picture; }

private:
    MCOperationVideo();

    /// <summary>The frame just decoded (<paramref name="frame"/>, in <see cref="_Indices"/>) as a picture.</summary>
    std::shared_ptr<const MCUiTexture> Pictured(uint32_t frame);
    /// <summary>The HD frames dropped, saying why (the 1998 frames show on).</summary>
    void DropHd(std::string_view why);

    std::unique_ptr<MCSmackTag> _Movie;
    std::unique_ptr<MCHdMovie> _Hd;
    /// <summary>Once dropped, kept until the video closes: closing it waits for the frame it is decoding.</summary>
    std::unique_ptr<MCHdMovie> _HdDropped;
    /// <summary>The frame decoded last, its palette indices.</summary>
    std::vector<uint8_t> _Indices;
    std::shared_ptr<const MCUiTexture> _Picture;
    uint32_t _Shown = 0;
    bool _Incoming = false;
    bool _Over = false;
};

/// <summary>The window: the video, its eyebrow and title, PAUSE, how far along, CLOSE.</summary>
class MCOperationVideoWindow final : public MCRmlScreen
{
public:
    MCOperationVideoWindow();
    ~MCOperationVideoWindow() override;

    /// <summary>
    /// Plays <paramref name="video"/> in the window, shown over the briefing: <paramref name="eyebrow"/> names the
    /// operation; <paramref name="still"/> is the operation's picture while Betty speaks (a decorator; none: black).
    /// </summary>
    void Play(std::unique_ptr<MCOperationVideo> video, std::string eyebrow, std::string still);

    /// <summary>PAUSE (Space), and CLOSE (Escape): the window gone, the video with it.</summary>
    void TogglePause();
    void Close();

    /// <summary>The video playing (the tests read it); none once closed.</summary>
    MCOptionalRef<MCOperationVideo> Video() const;
    /// <summary>How far along it shows ("0:24 of 0:49"), and as a percentage.</summary>
    const std::string& TimeText() const { return _Time; }
    int Percent() const { return _Percent; }

    bool OnKey(int key, int modifiers) override;
    void OnFrame() override;
    void OnDestroy() override;

protected:
    void MakeModels(Rml::Context& context) override;
    void OnLoaded(Rml::ElementDocument& document) override;
    void OnHide() override;

private:
    /// <summary>How far along, the button's word and the keys as the video stands; the values that changed told.</summary>
    void Fill(bool all);

    std::unique_ptr<MCRmlModel> _Model;
    std::unique_ptr<MCOperationVideo> _Video;

    // The model's values.
    std::string _Eyebrow;
    std::string _Still;
    std::string _Height;
    bool _Playing = false;
    bool _Paused = false;
    int _Percent = 0;
    std::string _Time;
};

namespace MCVideoMenu
{
    /// <summary>The live picture the window's video draws (MCRmlRecorder::LiveScheme).</summary>
    inline constexpr std::string_view LiveSource = "live:video";
    /// <summary>The video's width in the window (units: the HD video's 704 pixels 1.4 times at 1080p).</summary>
    inline constexpr float VideoUnits = 448.0f;
    /// <summary>How far along: "0:24 of 0:49" (each a time as <see cref="TimeFormat"/> writes it).</summary>
    inline constexpr std::string_view AlongFormat = "{} of {}";
    inline constexpr std::string_view TimeFormat = "{}:{:02}";

    /// <summary>
    /// Opens the window over the briefing playing the operation's video <paramref name="cinema"/>, named by
    /// <paramref name="eyebrow"/>; false (the log says why) when it can't be read or the menus can't show it.
    /// </summary>
    bool Open(std::string_view cinema, std::string eyebrow);

    /// <summary>The window closed, the video with it (the briefing hidden, a mission begun).</summary>
    void Close();

    /// <summary>The window, while the menus have it.</summary>
    MCOptionalRef<MCOperationVideoWindow> Window();

    /// <summary>Whether it shows.</summary>
    bool Shown();

    /// <summary>A time in seconds as the window writes it ("0:49").</summary>
    std::string TimeText(double seconds);
}
