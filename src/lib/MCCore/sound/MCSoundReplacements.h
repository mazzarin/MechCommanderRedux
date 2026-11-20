#pragma once

#include "sound/MCHdAudio.h"

/// <summary>
/// Port-only: replacement sounds, for whoever works on the game's sound and music. While the user folder has a folder
/// named <see cref="FolderName"/> (no player has one unless they make it), a file in it named as the sound it
/// replaces plays instead of that sound, whatever its length, rate or channels, over the HD audio pack's and whether HD
/// audio is on or not: <c>SOUND-003.wav</c> or <c>PILOTQ-021.flac</c> for a PAK's packet (the PAK's name, then the packet's
/// number), <c>MUSIC14.wav</c> for a music track. A WAV may be any PCM or float WAV SDL reads (8 to 32 bits, more than
/// two channels mixed to two); a FLAC any dr_flac reads. A recording kept in several packets (the same PCM) is replaced
/// in all of them by a file for any one.
///
/// Every sound the game plays is listed in the folder's <see cref="HeardFile"/>, newest last, by those names, with the
/// replacement that played and what was wrong with any file that couldn't be used: hear a sound, find its name.
///
/// The folder is looked for as the game starts and as each mission loads (<see cref="Look"/>); while it is there its
/// files are looked at again at most once a second as sounds play, so a file put in, changed or taken out is heard from
/// the next time its sound plays (a music track's from its next start). Replacements are heard on the machine that has
/// them only, in multiplayer too: sounds change nothing a game sends or simulates.
/// </summary>
namespace MCSoundReplacements
{
    /// <summary>The folder, in the user folder (MCFileSystem::UserRoot).</summary>
    inline constexpr std::string_view FolderName = "Sound replacements";
    /// <summary>The list of the sounds heard, in the folder (made again each time the folder is found).</summary>
    inline constexpr std::string_view HeardFile = "heard.txt";

    /// <summary>
    /// Looks for the folder: found, the sound packets' names are read (once) and the list of sounds heard begun; gone,
    /// nothing is replaced or listed. The sound system calls it as it loads and as each mission loads.
    /// </summary>
    void Look();
    /// <summary>Whether the folder was there when last looked for.</summary>
    bool Active();
    /// <summary>
    /// The replacement for the wave image <paramref name="wave"/> (of <paramref name="id"/>, its MCHdAudio::SampleId,
    /// when the caller knows it), decoded to signed 16-bit PCM, or null; the sound is listed as heard. Null at once
    /// while the folder isn't there.
    /// </summary>
    std::shared_ptr<const MCHdAudio::Decoded> Sample(std::span<const uint8_t> wave, std::optional<uint64_t> id);
    /// <summary>
    /// The replacement for the music file <paramref name="original"/> (its name is MUSICnn), decoded whole, or null;
    /// the track is listed as heard.
    /// </summary>
    std::shared_ptr<const MCHdAudio::Decoded> Music(std::string_view original);
    /// <summary>The catalogue name of a sound packet: its PAK's name in capitals, a dash and its number in 3 digits.</summary>
    std::string PacketName(std::string_view pak, int32_t packet);
    /// <summary>Forgets the folder, the names and the replacements (the tests make and remove the folder).</summary>
    void Forget();
}
