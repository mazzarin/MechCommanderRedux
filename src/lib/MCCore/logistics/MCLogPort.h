#pragma once

#include "gui/MCGuiPort.h"

/// <summary>
/// A logistics-screen drawing port: an <see cref="MCGuiPort"/> that makes a new bitmap on every
/// <see cref="Init(int32_t, int32_t)"/> and loads its art without the planet's variant and without a fatal error for a
/// missing file.
/// </summary>
/// <remarks>Original source: <c>logistics\lport.cpp</c> (<c>lPort</c>).</remarks>
class MCLogPort : public MCGuiPort
{
public:
    /// <summary>Makes a <paramref name="width"/> x <paramref name="height"/> zeroed bitmap, even at the same size.</summary>
    /// <returns>0, or 3 when out of memory.</returns>
    int32_t Init(int32_t width, int32_t height) override;

    /// <summary>
    /// Loads the TGA file <paramref name="fileName"/>, looked for under <c>ArtPath</c> first, then as it is (a full
    /// path). A file that can't be read is fatal (<c>GeneralMsg</c>).
    /// </summary>
    void Load(std::string_view fileName);
};

/// <summary>
/// Port: a block of another port drawn in place: a view whose pixel (0, 0) lies at (xPos, yPos) of the destination,
/// cut to its size. It stands where the original painted a scratch picture and copied it there, so the drawing goes
/// straight to the destination (on the GPU when that is the screen) instead of through a picture made each frame.
/// <c>keyTransparent</c> says the copy was keyed on 0xff (0xff writes are left out).
/// </summary>
class MCLogBlockPort : public MCLogPort
{
public:
    MCLogBlockPort(const MCPane& dest, int32_t xPos, int32_t yPos, int32_t width, int32_t height, bool keyTransparent)
    {
        InitView(width, height);
        OpenViewOn(dest, xPos, yPos, keyTransparent);
    }
};

/// <summary>
/// Port: the logistics art file <paramref name="fileName"/> (as <see cref="MCLogPort::Load"/> takes it), loaded the
/// first time it is asked for and kept until <see cref="ClearLogArt"/>. The screens draw from their state every frame,
/// so they take their art from here instead of loading it again for each draw.
/// </summary>
/// <returns>The art, shared with the cache (never null: a file that can't be read is fatal).</returns>
std::shared_ptr<MCLogPort> LogArt(std::string_view fileName);

/// <summary>Port: <see cref="LogArt"/> of <c>&lt;ArtPath&gt;logart\&lt;name&gt;</c>, the logistics screens' own art.</summary>
std::shared_ptr<MCLogPort> LogScreenArt(std::string_view name);

/// <summary>Port: frees the art <see cref="LogArt"/> loaded.</summary>
void ClearLogArt();
