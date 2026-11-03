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
    ~MCGenericScreen() override;

    /// <summary>
    /// The palette of the TGA <paramref name="fileName"/> (under <c>ArtPath</c>, else as it is) as 6-bit RGB, 0x300
    /// bytes. A file that can't be read is fatal.
    /// </summary>
    static std::vector<uint8_t> PaletteFromArt(std::string_view fileName);

    /// <summary>Makes the elements the ini file describes (background, buttons, text fields, a file pane).</summary>
    int32_t Init(MCFitIniFile& screenFile);

    /// <summary>Removes and frees the elements (element 0 is the screen itself), the palette and the art.</summary>
    void Destroy() override;

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

    /// <summary>Port: adds <paramref name="child"/> to the screen, which owns it (the preferences' drop-downs and boxes).</summary>
    template <typename T> T& AdoptChild(MCGuiOwned<T> child) { return AdoptChildOf(*this, std::move(child)); }

    /// <summary>
    /// Port: adds <paramref name="child"/> to <paramref name="parent"/> (the screen or one of its objects); the screen
    /// owns it (the preferences' panel and what is on it).
    /// </summary>
    template <typename T> T& AdoptChildOf(MCGuiObject& parent, MCGuiOwned<T> child)
    {
        T& adopted = *child;
        parent.AddChild(child);
        _Adopted.push_back(std::move(child));
        return adopted;
    }

    /// <summary>The elements, by number; element 0 is the screen itself, an element of a type not made is empty.</summary>
    std::vector<std::weak_ptr<MCGuiObject>> Elements;
    /// <summary>The background art's palette (0x300 bytes) when the background has UseBackPalette, else empty.</summary>
    std::vector<uint8_t> Palette;
    /// <summary>The file pane element, if any.</summary>
    MCGuiOwned<MCFileScrollPane> FilePane;
    /// <summary>The load or save button (callbacks 8 and 9).</summary>
    MCGuiOwned<MCLogButton> LoadSaveButton;
    /// <summary>The delete button (callback 10).</summary>
    MCGuiOwned<MCLogButton> DeleteButton;
    /// <summary>The cancel button (callback 11).</summary>
    MCGuiOwned<MCLogButton> CancelButton;
    /// <summary>
    /// Port: the main menu's ORIGINAL CINEMA (callback 5), which becomes UPDATE in a build that updates itself
    /// (logistics/MCUpdatePage.h).
    /// </summary>
    MCGuiOwned<MCLogButton> CinemaButton;
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

    /// <summary>Makes element <paramref name="index"/>'s object, owned by the screen.</summary>
    template <typename T> MCGuiOwned<T> MakeElement(int32_t index)
    {
        MCGuiOwned<T> element = MCMakeGui<T>();
        Elements[static_cast<size_t>(index)] = element;
        _Made[static_cast<size_t>(index)] = element;
        return element;
    }

    /// <summary>A text entry field element (type 4): black text on colour 0x1f.</summary>
    MCGuiOwned<MCLogTextObject> MakeTextEntry(int32_t index, const ElementHeader& header);

    /// <summary>A file pane element (type 5), with its SavePane flag.</summary>
    MCGuiOwned<MCFileScrollPane> MakeFilePane(MCFitIniFile& file, int32_t index, const ElementHeader& header);

    /// <summary>
    /// Loads the background (element 0): its palette when UseBackPalette, and places the screen. The art itself is
    /// the caller's.
    /// </summary>
    void InitBackground(MCFitIniFile& file, const ElementHeader& header);

private:
    /// <summary>The element objects the screen made (by element number; null for the screen and types not made).</summary>
    std::vector<MCGuiOwned<MCGuiObject>> _Made;
    /// <summary>Port: the children added with <see cref="AdoptChild"/>.</summary>
    std::vector<MCGuiOwned<MCGuiObject>> _Adopted;
};

/// <summary>
/// Port: a picture element of a generic screen (element type 6), which the original made as a plain lObject with the
/// art loaded into its port. It draws the art each frame.
/// </summary>
class MCLogImage : public MCLogObject
{
public:
    /// <summary>Frees the art.</summary>
    void Destroy() override;

    /// <summary>Copies the art (opaque, as the picture was copied).</summary>
    void Draw() override;

    /// <summary>The image draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The picture.</summary>
    std::shared_ptr<MCLogPort> Art;
};
