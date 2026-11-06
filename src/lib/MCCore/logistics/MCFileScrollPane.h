#pragma once

#include "gui/MCScrollPane.h"
#include "logistics/MCLogTextObject.h"

class MCFileScrollPane;

/// <summary>
/// Port: one of a <see cref="MCFileScrollPane"/>'s column headers (the selected save's operation, mission or
/// resource points). The original made them plain lObjects and painted their pictures from the pane; this one draws
/// its column of the pane's selected save each frame (white on black).
/// </summary>
class MCFileColumnHeader : public MCLogObject
{
public:
    /// <summary>Wipes the header and writes the selected save's figure for its column, if it has one.</summary>
    void Draw() override;

    /// <summary>The header draws itself each frame.</summary>
    bool DrawsLive() override { return true; }

    /// <summary>The pane whose selection the header shows.</summary>
    std::weak_ptr<MCFileScrollPane> Pane;
    /// <summary>0 the operation, 1 the mission, 2 the resource points.</summary>
    int32_t Column = 0;
};

/// <summary>
/// The save/load game file list: every <c>.sav</c> (<c>.sol</c>, or <c>.mpk</c> in multiplayer) file in a directory
/// with its operation, mission and resource points, a scroll bar, and (on a save screen) a name entry field.
/// </summary>
/// <remarks>Original source: <c>logistics\loggen.cpp</c> (<c>FileScrollPane</c>).</remarks>
class MCFileScrollPane : public MCScrollPane
{
public:
    /// <summary>A save listed: its name (without the extension) and, in single player, where its campaign is.</summary>
    struct File
    {
        std::string Name;
        /// <summary>The operation number (0 for a multiplayer save).</summary>
        int32_t Operation = 0;
        /// <summary>The mission number.</summary>
        int32_t Mission = 0;
        uint32_t ResourcePoints = 0;
        /// <summary>Whether the user folder has the save (the list and Load use that copy when both have it).</summary>
        bool InUserFolder = false;
        /// <summary>Whether the game's folder has the save (the original kept saves there).</summary>
        bool InGameFolder = false;
    };

    /// <summary>The characters a save's name takes (the name entry's buffer, a game rule).</summary>
    static constexpr int32_t NameBufferSize = 0x20;

    /// <summary>Places the pane, tiles its slider column with the splash art and makes the three column headers.</summary>
    void Init(int32_t xPos, int32_t yPos, int32_t width, int32_t height);

    /// <summary>Frees the file list and the column headers.</summary>
    void OnDestroy() override;

    /// <summary>
    /// Port: the splash arrows have no pressed art. (The original's draw put them back over the pressed art straight
    /// away; see OB-132 for the plain arrows a release left.)
    /// </summary>
    std::shared_ptr<MCLogPort> PressedArrowArt(bool down) override;

    /// <summary>Port: draws the file lines (<see cref="DrawFiles"/>) into the content view.</summary>
    void DrawContent() override;

    /// <summary>Draws the pane when shown, then the column headers and the children (the name entry).</summary>
    void Display() override;

    /// <summary>Selecting a file (an entry field over it on a save pane), scrolling, and double clicks.</summary>
    void HandleEvent(MCGuiEvent& event) override;

    /// <summary>The splash screens' slider edge colour.</summary>
    uint8_t SliderEdgeColor() const override { return 0xc0; }

    /// <summary>The file under (<paramref name="xPos"/>, <paramref name="yPos"/>) of the pane, or -1.</summary>
    int32_t GetFileAtPosition(int32_t xPos, int32_t yPos);

    /// <summary>Sets the directory and lists its files.</summary>
    void SetStartDirectory(std::string_view directory);

    /// <summary>Draws the file lines into the content (a view while the pane draws).</summary>
    void DrawFiles();

    /// <summary>
    /// Port: the first part of the original's drawFiles: sizes the content to the files (at least the pane's height)
    /// and resets the scroll when that changes it. Called when the list changes.
    /// </summary>
    void LayoutFiles();

    /// <summary>
    /// Lists every <paramref name="extension"/> file of the directory, reading each single player save's mission and
    /// resource points; <paramref name="sort"/> sorts them by name (the selection follows its file). Saving
    /// mid-campaign, the first entry is the empty slot.
    /// </summary>
    void GetAllFiles(std::string_view extension, bool sort);

    /// <summary>
    /// Sorts <paramref name="files"/> by name as the original did (an exchange sort on <c>strcmp</c>); the selection
    /// <paramref name="selected"/> follows its file's name (the first file of that name).
    /// </summary>
    static void SortByName(std::vector<File>& files, int32_t& selected);

    /// <summary>Selects file <paramref name="file"/> (or none), scrolls it into view and tells the parent.</summary>
    void SetSelectedFile(int32_t file);

    /// <summary>Switches between single player saves (<c>.sav</c>) and multiplayer ones (<c>.mpk</c>).</summary>
    void SetMultiplayer(bool multiplayer);

    /// <summary>The number of files listed.</summary>
    int32_t NumFiles() const { return static_cast<int32_t>(Files.size()); }

    /// <summary>Whether a listed file is selected.</summary>
    bool HasSelection() const { return SelectedFile > -1 && SelectedFile < NumFiles(); }

    int32_t SelectedFile = -1;
    bool Multiplayer = false;
    /// <summary>The three column headers (the pane's, displayed by it; not children).</summary>
    std::array<MCGuiRef<MCFileColumnHeader>, 3> ColumnHeaders;
    std::string StartDirectory;
    /// <summary>The files listed, in order.</summary>
    std::vector<File> Files;
    /// <summary>The save screen's pane (the ini's SavePane): it has the name entry field.</summary>
    bool SavePane = false;
    /// <summary>The save name entry field while one shows (a child, made by each click on a save pane's file).</summary>
    MCGuiHandle<MCLogTextObject> NameEntry;
    /// <summary>The height of a file line (the large white font's height + 1).</summary>
    int32_t LineHeight = 0;
};

/// <summary>
/// The name of an empty save slot (string 0x381, loaded by the logistics setup; empty until then).
/// </summary>
extern std::string EmptyFile;
