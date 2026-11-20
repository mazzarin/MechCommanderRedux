#pragma once

#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/SystemInterface.h>

/// <summary>
/// Port-only (SPEC-menus.md, "How it's built"): what RmlUi asks of the system: the time (the game's clock unless a
/// test gives its own, so a test's frames are the same each run), its messages in the game's log, and the clipboard
/// (SDL's).
/// </summary>
class MCRmlSystem final : public Rml::SystemInterface
{
public:
    /// <summary>A clock in seconds.</summary>
    using MCClock = std::function<double()>;

    MCRmlSystem();
    ~MCRmlSystem() override;
    MCRmlSystem(const MCRmlSystem&) = delete;
    MCRmlSystem& operator=(const MCRmlSystem&) = delete;

    /// <summary>Reads the time from <paramref name="clock"/> from now on (none: the game's clock).</summary>
    void SetClock(MCClock clock);

    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void SetClipboardText(const Rml::String& text) override;
    void GetClipboardText(Rml::String& text) override;

private:
    MCClock _Clock;
};

/// <summary>
/// Port-only: the menus' documents and style sheets, read from a folder of the app's own (its <c>ui</c> folder: beside
/// the program, or in the Mac app's resources). A path is a document's, relative to that folder; one reaching out of it
/// (<c>..</c>, an absolute path) is refused.
/// </summary>
class MCRmlFiles final : public Rml::FileInterface
{
public:
    /// <summary>The files under <paramref name="folder"/>.</summary>
    explicit MCRmlFiles(std::filesystem::path folder);
    ~MCRmlFiles() override;
    MCRmlFiles(const MCRmlFiles&) = delete;
    MCRmlFiles& operator=(const MCRmlFiles&) = delete;

    /// <summary>The app's own <c>ui</c> folder.</summary>
    static std::filesystem::path AppFolder();

    const std::filesystem::path& Folder() const { return _Folder; }

    Rml::FileHandle Open(const Rml::String& path) override;
    void Close(Rml::FileHandle file) override;
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override;
    bool Seek(Rml::FileHandle file, long offset, int origin) override;
    size_t Tell(Rml::FileHandle file) override;
    size_t Length(Rml::FileHandle file) override;

    /// <summary>An open file: its bytes, read whole, and where reading has got to.</summary>
    struct MCOpenFile
    {
        std::vector<char> Bytes;
        size_t At = 0;
    };

private:
    /// <summary>The open file <paramref name="file"/> stands for; none for a handle this didn't give.</summary>
    MCOpenFile* Find(Rml::FileHandle file);

    std::filesystem::path _Folder;
    std::map<Rml::FileHandle, std::unique_ptr<MCOpenFile>> _Open;
};
