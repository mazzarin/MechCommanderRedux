#pragma once

// Only standard headers: the app's MCGameFolder.cpp, built without MCCore's precompiled header, includes this too.
#include <filesystem>
#include <string>
#include <string_view>

/// <summary>
/// A path held in a string is UTF-8 on every platform: game paths (the retail data's are ASCII), SDL's paths and the
/// command line's. These convert between those and std::filesystem::path; path::string and a path built from a
/// std::string use the ANSI code page on Windows, and string throws on a name the code page can't spell.
/// </summary>
namespace MCFileSystem
{
    /// <summary>The UTF-8 spelling of <paramref name="path"/>.</summary>
    inline std::string ToUtf8(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    /// <summary>The path that the UTF-8 string <paramref name="text"/> spells.</summary>
    inline std::filesystem::path FromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }
}
