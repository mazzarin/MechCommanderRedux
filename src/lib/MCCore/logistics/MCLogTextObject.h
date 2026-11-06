#pragma once

#include "logistics/MCLogObject.h"

class MCGuiFont;

/// <summary>Which characters a <see cref="MCLogTextObject"/> takes.</summary>
enum class MCLogInputType : int32_t
{
    /// <summary>Not editable.</summary>
    None = -1,
    /// <summary>Printable ASCII (' '..'~').</summary>
    Text = 0,
    /// <summary>Digits only.</summary>
    Digits = 1,
    /// <summary>Anything.</summary>
    Any = 2,
    /// <summary>'2'..'6' only (serial/modem port numbers).</summary>
    Port = 3
};

/// <summary>
/// A one-line text entry field: text of at most <see cref="BufferSize"/> - 1 characters edited with the keyboard, a
/// copy of the text as last set, a blinking cursor, and a filter on the characters it accepts.
/// </summary>
/// <remarks>
/// Original source: <c>logistics\loggen.cpp</c> (<c>lTextObject</c>). Made with <see cref="MCLogObject::Init"/> and
/// <see cref="InitBuffer"/>; it has no init of its own. The text is kept as the original's character buffer held it
/// (<see cref="Buffer"/>), since what typing shows depends on what an earlier, longer text left behind its end.
/// </remarks>
class MCLogTextObject : public MCLogObject
{
public:
    /// <summary>Frees the text.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Wipes the field, writes the text and draws the cursor when it has a valid position (lit, or in the field's
    /// colour; the original drew the cursor in display, over the picture).
    /// </summary>
    void Draw() override;

    /// <summary>Port: the field draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>Port: an edit puts the cursor back to its lit blink phase (the original's paint after an edit did).</summary>
    void RestartBlink() { CursorLit = true; }

    /// <summary>Moves the cursor to character <paramref name="pos"/> and works out its pixel position.</summary>
    void SetCursorPos(int32_t pos);

    /// <summary>Whether <paramref name="key"/> may be typed into this field (<see cref="AllowedInput"/>).</summary>
    bool IsValid(char key) const;

    /// <summary>
    /// Typing, backspace, enter (tells the parent), escape (<c>Cancel</c>), focus and the cursor blink timer.
    /// </summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>
    /// Makes the field take <paramref name="size"/> - 1 characters (0: none), empties it and sets what it accepts.
    /// </summary>
    void InitBuffer(int32_t size, MCLogInputType type);

    /// <summary>
    /// Sets the text and its copy, and puts the cursor after it. A text too long is cut to the field (its length is
    /// then left as it was, as the original's).
    /// </summary>
    /// <returns>0, or -1 when the text had to be cut.</returns>
    int32_t SetStringBuffer(std::string_view text);

    /// <summary>The text: <see cref="Buffer"/> up to its first NUL.</summary>
    std::string_view Text() const;

    /// <summary>The size of the field's buffer: it takes one character less (a game rule: the names it edits).</summary>
    int32_t BufferSize = 0;
    /// <summary>
    /// The field's characters, <see cref="BufferSize"/> of them: the text up to the first NUL, and what earlier texts
    /// left behind it (typing writes over the NUL, so that shows again).
    /// </summary>
    std::string Buffer;
    /// <summary>The text as last set (backspace on unchanged text clears it all).</summary>
    std::string OriginalText;
    /// <summary>The length of the text.</summary>
    int32_t TextLength = 0;
    /// <summary>The cursor, in characters (-1 = none).</summary>
    int32_t CursorPos = -1;
    /// <summary>The cursor, in pixels from the left.</summary>
    int32_t CursorPixel = 0;
    /// <summary>The characters the field takes.</summary>
    MCLogInputType AllowedInput = MCLogInputType::None;
    /// <summary>The cursor blink phase (toggled by the blink timer): lit, or in the field's colour.</summary>
    bool CursorLit = true;
    std::shared_ptr<MCGuiFont> Font;
    /// <summary>On focus, a text equal to <see cref="EmptyFile"/> is cleared so the player can type a name.</summary>
    bool ClearEmptyOnFocus = false;
};
