#pragma once

// Port-only (HD weather): how many of the HD weather's falling pieces a machine's frames keep up with. When the frames
// fall behind the display's for a second, fewer pieces are thrown; once they keep up for a while, more come back a
// step at a time, and a step that makes them fall behind goes back at once and doubles the wait before the next. A cut
// shows as the pieces in the air land and fewer new ones are thrown, so it is judged again only once they have. Only
// the drawing changes: the pieces are the weather's own (their own dice), so nothing in play or on the network does.
// The app follows its frames (MCWeatherPacing::Follow); the tests and the battle recorder don't, so theirs fall the
// same every run.

/// <summary>The share of the HD weather's falling pieces a machine's frames keep up with.</summary>
class MCWeatherPace
{
public:
    /// <summary>The least share: a machine that can't keep up at all still shows a fifth of the pieces.</summary>
    static constexpr float LeastShare = 0.2f;
    /// <summary>A frame longer than this is a load's or a hitch's, not the pace's, and isn't counted.</summary>
    static constexpr double MostFrameSeconds = 0.25;
    /// <summary>The frames are judged by their mean over this long.</summary>
    static constexpr double WindowSeconds = 0.5;
    /// <summary>How long the pieces in the air take to follow a change (the flakes' longest fall and rest).</summary>
    static constexpr double SettleSeconds = 3.0;
    /// <summary>How long the frames keep up before the first step back up.</summary>
    static constexpr double FirstRaiseWait = 5.0;
    /// <summary>The longest wait between steps back up.</summary>
    static constexpr double LongestRaiseWait = 40.0;

    /// <summary>
    /// Takes a frame <paramref name="seconds"/> long, the display showing one every <paramref name="refreshSeconds"/>.
    /// Only a frame that drew the weather's pieces (<paramref name="drewPieces"/>) counts: a cut helps nothing else.
    /// </summary>
    void Frame(double seconds, double refreshSeconds, bool drewPieces);

    /// <summary>The share of the pieces thrown now: 1 throws them all.</summary>
    float Share() const { return _Share; }

private:
    float _Share = 1.0f;
    /// <summary>The window's counted seconds and frames.</summary>
    double _Window = 0.0;
    int _WindowFrames = 0;
    /// <summary>Seconds counted since the share last changed (settled at first).</summary>
    double _SinceChange = SettleSeconds;
    /// <summary>Windows in a row behind the display.</summary>
    int _BehindWindows = 0;
    /// <summary>Seconds the frames have kept up since the share last changed or they last fell behind.</summary>
    double _KeptUp = 0.0;
    /// <summary>How long they must keep up before the next step back up.</summary>
    double _RaiseWait = FirstRaiseWait;
    /// <summary>Whether the last change was a step up.</summary>
    bool _Raised = false;
};

/// <summary>Port-only (HD weather): the app's own pace, which MCWeather throws its pieces by.</summary>
namespace MCWeatherPacing
{
    /// <summary>Has the share follow the frames (the app's; never the tests' or the recorder's).</summary>
    void Follow(bool on);

    /// <summary>
    /// Ends a frame: its time since the last, against the window's display; nothing unless followed. Frames with the
    /// frame rate locked (LockFrameRate) or without the weather's pieces don't count.
    /// </summary>
    void Frame(bool drewPieces);

    /// <summary>The share of the HD weather's falling pieces to throw: 1 unless followed.</summary>
    float Share();
}
