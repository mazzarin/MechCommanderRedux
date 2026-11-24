#pragma once

/// <summary>
/// The registry values the game kept under <c>HKEY_LOCAL_MACHINE</c> (its version and language, the multiplayer
/// player name), stored in <c>REGISTRY.CFG</c> in the user folder instead. Keys and value names are matched without
/// regard to case, as the registry does. Every key counts as existing: the installer that created them is gone.
/// </summary>
/// <remarks>
/// Port-only: the values are kept in memory, read from the file once for the user and game folders set (again when
/// they change), and each change is saved to the file on a thread of its own, so a menu's button (NEW CAMPAIGN's, the
/// first to store the version) never waits for the disk. A file just written can't be opened again until the system's
/// scan of it is done: reading it back for the second value of the version's pair froze NEW CAMPAIGN's press for
/// 1.2-1.3 s on the A10, Windows Defender busy (24 Nov). The saves are joined at quit with the other threads let go of
/// (MCThreadKeeper::Process). Threads: any.
/// </remarks>
namespace MCRegistry
{
    /// <summary>
    /// <c>RegQueryValueExA</c>: the string value <paramref name="valueName"/> of key <paramref name="keyName"/>
    /// (a path such as <c>Software\Fasa Interactive\MechCommander Expansion</c>).
    /// </summary>
    /// <returns>The value, or nothing when it was never written.</returns>
    std::optional<std::string> Read(std::string_view keyName, std::string_view valueName);

    /// <summary>
    /// <c>RegSetValueExA</c>: sets string value <paramref name="valueName"/> of <paramref name="keyName"/>, read
    /// back at once; the file is saved behind (a value as stored already isn't saved again; a failed save is logged).
    /// </summary>
    void Write(std::string_view keyName, std::string_view valueName, std::string_view data);

    /// <summary>Waits until every value written is in the file (tests; not while saves are held).</summary>
    void WaitForSaves();

    /// <summary>
    /// Port-only (tests): while one is alive, no save is written, as on a disk that takes its time. The folders stay
    /// set meanwhile: reading another folder's values waits for the saves.
    /// </summary>
    class MCSavesHeld
    {
    public:
        MCSavesHeld();
        ~MCSavesHeld();
        MCSavesHeld(const MCSavesHeld&) = delete;
        MCSavesHeld& operator=(const MCSavesHeld&) = delete;
    };
}
