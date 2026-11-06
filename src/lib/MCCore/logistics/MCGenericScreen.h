#pragma once

#include "lib/MCFitIniFile.h"
#include "logistics/MCLogButton.h"

class MCFileScrollPane;
class MCLogTextObject;

/// <summary>
/// A logistics screen built from an ini file: a list of elements (background, buttons, text fields, file panes, ...)
/// read from the <c>[Element#]</c> blocks. Element 0 is the screen itself (its background art).
/// </summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>GenericScreen</c>).</remarks>
class MCGenericScreen : public MCLogObject
{
public:
    /// <summary>
    /// The palette of the TGA <paramref name="fileName"/> (under <c>ArtPath</c>, else as it is) as 6-bit RGB, 0x300
    /// bytes. A file that can't be read is fatal.
    /// </summary>
    static std::vector<uint8_t> PaletteFromArt(std::string_view fileName);

    /// <summary>Makes the elements the ini file describes (background, buttons, text fields, a file pane).</summary>
    int32_t Init(MCFitIniFile& screenFile);

    /// <summary>Forgets the elements (its children, destroyed with it), and frees the palette and the art.</summary>
    void OnDestroy() override;

    /// <summary>Escape cancels; then the event routine runs.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>Port: the screen draws its background art each frame (its port was the art).</summary>
    void Draw() override;

    /// <summary>Port: the screen draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>
    /// Shows or hides the screen; showing the main menu or the load/save screen grays the buttons that have nothing to
    /// act on, and the screen's palette (or the game's) comes back.
    /// </summary>
    void ShowGuiWindow(bool show) override;

    /// <summary>Element <paramref name="index"/> as a <typeparamref name="T"/> (null for a place left empty).</summary>
    template <typename T> std::shared_ptr<T> Element(int32_t index) const
    {
        return std::static_pointer_cast<T>(Elements[static_cast<size_t>(index)].lock());
    }

    /// <summary>The number of elements.</summary>
    int32_t NumElements() const { return static_cast<int32_t>(Elements.size()); }

    /// <summary>
    /// Port: takes element <paramref name="index"/> down and makes it again from <paramref name="file"/> (the screen's
    /// own ini) on <paramref name="parent"/>, one of the screen's objects (the preferences panel's boxes): nothing moves
    /// between parents.
    /// </summary>
    void RemakeElementOn(MCGuiObject& parent, MCFitIniFile& file, int32_t index);

    /// <summary>
    /// The elements, by number; element 0 is the screen itself, an element of a type not made is empty. The others are
    /// children of the screen (or of one of its objects, see <see cref="RemakeElementOn"/>).
    /// </summary>
    std::vector<std::weak_ptr<MCGuiObject>> Elements;
    /// <summary>The background art's palette (0x300 bytes) when the background has UseBackPalette, else empty.</summary>
    std::vector<uint8_t> Palette;
    /// <summary>The file pane element, if any.</summary>
    MCGuiHandle<MCFileScrollPane> FilePane;
    /// <summary>The load or save button (callbacks 8 and 9).</summary>
    MCGuiHandle<MCLogButton> LoadSaveButton;
    /// <summary>The delete button (callback 10).</summary>
    MCGuiHandle<MCLogButton> DeleteButton;
    /// <summary>The cancel button (callback 11).</summary>
    MCGuiHandle<MCLogButton> CancelButton;
    /// <summary>
    /// Port: the main menu's ORIGINAL CINEMA (callback 5), which becomes UPDATE in a build that updates itself
    /// (logistics/MCUpdatePage.h).
    /// </summary>
    MCGuiHandle<MCLogButton> CinemaButton;
    /// <summary>
    /// Port: the background art, which the original loaded into the screen's own port (a splash screen's is the art
    /// all splash screens share).
    /// </summary>
    std::shared_ptr<MCLogPort> ArtPort;

protected:
    /// <summary>An element's <c>[Element#]</c> header.</summary>
    struct ElementHeader
    {
        int32_t Type = -1;
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Width = 0;
        int32_t Height = 0;
        /// <summary>The NormalArt entry.</summary>
        std::string Art;
    };

    /// <summary>Reads the Elements block and makes room for that many elements.</summary>
    void ReadElementCount(MCFitIniFile& file);

    /// <summary>Reads element <paramref name="index"/>'s header: type, rectangle and NormalArt.</summary>
    static ElementHeader ReadElement(MCFitIniFile& file, int32_t index);

    /// <summary>
    /// Reads a button's pictures (the up picture from <paramref name="art"/> when <paramref name="loadUp"/>; "NONE"
    /// skips a picture), its sounds and its callback number.
    /// </summary>
    /// <returns>The Callback entry, when there is one.</returns>
    static std::optional<int32_t> ReadButton(MCFitIniFile& file, MCLogButton& button, std::string_view art,
                                             bool loadUp);

    /// <summary>
    /// Sets a button's callback from the numbers both screen kinds share (0..11). The load/save, delete and cancel
    /// buttons are remembered; the first two start disabled.
    /// </summary>
    /// <returns>Whether the number was one of these.</returns>
    bool SetScreenCallback(MCLogButton& button, int32_t callback);

    /// <summary>
    /// Makes element <paramref name="index"/> (described by <paramref name="header"/>, its block the file's current
    /// one) on <paramref name="parent"/>: a button, text field or file pane.
    /// </summary>
    /// <returns>Whether the screen makes elements of that type.</returns>
    virtual bool MakeElementOn(MCGuiObject& parent, MCFitIniFile& file, int32_t index, const ElementHeader& header);

    /// <summary>
    /// Makes element <paramref name="index"/>'s object as a child of <paramref name="parent"/>, set up by
    /// <paramref name="setup"/> before it is added (as the original set an element up, then added it).
    /// </summary>
    template <typename T, typename Setup> MCGuiHandle<T> MakeElement(MCGuiObject& parent, int32_t index, Setup&& setup)
    {
        return parent.CreateChild<T>(
            [&](T& element)
            {
                Elements[static_cast<size_t>(index)] = element.weak_from_this();
                setup(element);
            });
    }

    /// <summary>A text entry field element (type 4): black text on colour 0x1f, shown first when <paramref name="show"/>.</summary>
    MCGuiHandle<MCLogTextObject> MakeTextEntry(MCGuiObject& parent, int32_t index, const ElementHeader& header,
                                               bool show);

    /// <summary>A file pane element (type 5), with its SavePane flag.</summary>
    MCGuiHandle<MCFileScrollPane> MakeFilePane(MCGuiObject& parent, MCFitIniFile& file, int32_t index,
                                               const ElementHeader& header);

    /// <summary>
    /// Loads the background (element 0): its palette when UseBackPalette, and places the screen. The art itself is
    /// the caller's.
    /// </summary>
    void InitBackground(MCFitIniFile& file, const ElementHeader& header);
};

/// <summary>
/// Port: a picture element of a generic screen (element type 6), which the original made as a plain lObject with the
/// art loaded into its port. It draws the art each frame.
/// </summary>
class MCLogImage : public MCLogObject
{
public:
    /// <summary>Frees the art.</summary>
    void OnDestroy() override;

    /// <summary>Copies the art (opaque, as the picture was copied).</summary>
    void Draw() override;

    /// <summary>The image draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The picture.</summary>
    std::shared_ptr<MCLogPort> Art;
};
