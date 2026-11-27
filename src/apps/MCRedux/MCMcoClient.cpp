#include "stdafx.h"
#include "MCMcoClient.h"

#include "MCVersion.h"
#include "lib/MCRandomId.h"
#include "platform/MCFileSystem.h"

#include <cstring>
#include <format>
#include <fstream>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace
{
    /// <summary>The save folder's file keeping the device's seed, and its line's key.</summary>
    constexpr std::string_view ConfigFile = "mco.cfg";
    constexpr std::string_view SeedKey = "DeviceSeed=";

    struct MCSystem
    {
        std::string Family;
        std::string Release;
        std::string Build;
        std::string SystemCpu;
        std::string AppCpu;
    };

#ifdef __APPLE__
    /// <summary>A text sysctl's value, or empty.</summary>
    std::string SysctlText(const char* name)
    {
        size_t size = 0;

        if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0)
        {
            return {};
        }

        std::string value(size, '\0');

        if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0)
        {
            return {};
        }

        value.resize(std::strlen(value.c_str()));
        return value;
    }
#endif

    /// <summary>This system, named as MCO's launcher names Windows' (its client_os: .NET's CPU names).</summary>
    MCSystem ThisSystem()
    {
#if defined(__aarch64__) || defined(_M_ARM64)
        const std::string appCpu = "Arm64";
#elif defined(__x86_64__) || defined(_M_X64)
        const std::string appCpu = "X64";
#else
        const std::string appCpu = "Unknown";
#endif

#ifdef __APPLE__
        // An Intel build run by Rosetta is on an Arm64 system.
        int translated = 0;
        size_t size = sizeof(translated);
        const bool rosetta =
            sysctlbyname("sysctl.proc_translated", &translated, &size, nullptr, 0) == 0 && translated == 1;
        return {.Family = "macOS",
                .Release = SysctlText("kern.osproductversion"),
                .Build = SysctlText("kern.osversion"),
                .SystemCpu = rosetta ? "Arm64" : appCpu,
                .AppCpu = appCpu};
#else
        // Windows' version needs Windows.h, which this file (built with the precompiled header) can't include.
        return {.Family = "Windows", .SystemCpu = appCpu, .AppCpu = appCpu};
#endif
    }

    /// <summary>The device's seed, kept in the save folder's mco.cfg (made there the first time).</summary>
    std::expected<std::string, std::string> DeviceSeed(const std::filesystem::path& folder)
    {
        const std::filesystem::path file = folder / ConfigFile;

        if (std::ifstream in(file); in)
        {
            std::string line;

            while (std::getline(in, line))
            {
                if (line.starts_with(SeedKey) && MCIsRandomId(line.substr(SeedKey.size())))
                {
                    return line.substr(SeedKey.size());
                }
            }
        }

        std::string seed = MCRandomId();
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << SeedKey << seed << '\n';

        if (!out)
        {
            return std::unexpected(std::format("Can't write {}", MCFileSystem::ToUtf8(file)));
        }

        return seed;
    }

}

std::expected<MCMcoApi::MCClient, std::string> MCMcoThisClient(const std::filesystem::path& userFolder,
                                                               std::string_view site)
{
    const auto seed = DeviceSeed(userFolder);

    if (!seed)
    {
        return std::unexpected(seed.error());
    }

    const MCSystem system = ThisSystem();
    return MCMcoApi::MCClient{
        .DeviceId = MCMcoApi::DeviceId(*seed),
        .UserAgent = std::format("MechCommanderReduxHD/{} ({}{}{})", MC_VERSION_SEMVER, system.Family,
                                 system.Release.empty() ? "" : " ", system.Release),
        .ClientOs = MCMcoApi::ClientOs(system.Family, system.Release, system.Build, system.SystemCpu, system.AppCpu),
        .Chat = std::format("{}{}", site, MCMcoApi::ChatPage),
        .Account = std::format("{}{}", site, MCMcoApi::AccountPage)};
}
