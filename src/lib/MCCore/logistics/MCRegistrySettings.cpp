#include "stdafx.h"
#include "logistics/MCRegistrySettings.h"
#include "main/MCGameStrings.h"
#include "platform/MCRegistry.h"
#include "platform/MCFrameLog.h"

namespace
{
    /// <summary>How many characters of the version are compared (the build string's length).</summary>
    constexpr size_t VersionLength = 13;

    /// <summary>This build's version string (string 0x282).</summary>
    std::string BuildVersion()
    {
        return LoadGameString(0x282, 99);
    }
}

auto RegistryVersionMatches(std::string_view stored, std::string_view build) -> bool
{
    // The original compared 13 characters with strncmp: a shorter string ends the compare at its end.
    const std::string_view storedHead = stored.substr(0, std::min(VersionLength, stored.find('\0')));
    const std::string_view buildHead = build.substr(0, std::min(VersionLength, build.find('\0')));
    return storedHead == buildHead && stored.size() > VersionLength && stored[VersionLength] == '.';
}

auto CheckRegistryVersionNumber() -> bool
{
    // The version is "<string 0x282>." (the build string and a dot at character 13); the original read it into 100
    // bytes.
    const std::optional<std::string> stored = MCRegistry::Read(GameRegistryKey, "Version");

    if (!stored.has_value())
    {
        return false;
    }

    return RegistryVersionMatches(std::string_view(*stored).substr(0, 99), BuildVersion());
}

auto WriteRegistryVersionNumber() -> void
{
    std::string version;
    {
        MCFrameLog::Scope probe("probe.reg strings");
        version = BuildVersion() + ".";
    }
    {
        MCFrameLog::Scope probe("probe.reg write version");
        MCRegistry::Write(GameRegistryKey, "Version", version);
    }
    std::string language;
    {
        MCFrameLog::Scope probe("probe.reg strings");
        language = LoadGameString(900, 99);
    }
    MCFrameLog::Scope probe("probe.reg write language");
    MCRegistry::Write(GameRegistryKey, "Language", language);
}

auto EnsureRegistryVersion() -> void
{
    bool stored = false;
    {
        MCFrameLog::Scope probe("probe.reg check");
        stored = CheckRegistryVersionNumber();
    }

    if (!stored)
    {
        WriteRegistryVersionNumber();
    }
}

auto SaveUserName(std::string_view name) -> void
{
    MCRegistry::Write(GameRegistryKey, "Player Name", name);
}
