// Built without the precompiled header on Windows (MCCore.vcxproj): windows.h must not meet the game's Win32
// stand-ins.

#include "update/MCSyncedFolder.h"

#ifdef _WIN32
// Not lean: the Cloud Files API uses types the lean windows.h leaves out.
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cfapi.h>
#include <cwchar>

#pragma comment(lib, "cldapi.lib")

namespace
{
    /// <summary>
    /// The variables OneDrive sets to its folders (the player's own, a work or school one): a folder synced without
    /// Files On-Demand may have no sync root.
    /// </summary>
    constexpr std::array<const wchar_t*, 3> OneDriveVariables = {L"OneDrive", L"OneDriveConsumer",
                                                                 L"OneDriveCommercial"};
    constexpr const char* OneDrive = "OneDrive";
    constexpr const char* SomeClient = "A sync client";

    std::string Narrow(std::wstring_view wide)
    {
        if (wide.empty())
        {
            return {};
        }

        const int length =
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string narrow(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), length, nullptr,
                            nullptr);
        return narrow;
    }

    /// <summary>Where <paramref name="folder"/> resolves, or the nearest folder holding it that exists.</summary>
    std::filesystem::path Resolved(const std::filesystem::path& folder)
    {
        std::error_code error;
        std::filesystem::path at = std::filesystem::absolute(folder, error);

        while (!std::filesystem::exists(at, error) && at.has_relative_path())
        {
            at = at.parent_path();
        }

        const std::filesystem::path real = std::filesystem::canonical(at, error);
        return error ? at : real;
    }

    /// <summary>Whether <paramref name="path"/> is <paramref name="folder"/> or in it, ignoring case as Windows does.</summary>
    bool Within(const std::filesystem::path& path, const std::filesystem::path& folder)
    {
        auto part = path.begin();

        for (const std::filesystem::path& wanted : folder)
        {
            if (wanted.empty())
            {
                continue;
            }

            if (part == path.end() || _wcsicmp(part->c_str(), wanted.c_str()) != 0)
            {
                return false;
            }

            ++part;
        }

        return true;
    }
}

std::optional<std::string> MCSyncedBy(const std::filesystem::path& folder)
{
    const std::filesystem::path real = Resolved(folder);
    CF_SYNC_ROOT_PROVIDER_INFO provider{};
    DWORD length = 0;
    const HRESULT result =
        CfGetSyncRootInfoByPath(real.c_str(), CF_SYNC_ROOT_INFO_PROVIDER, &provider, sizeof(provider), &length);

    if (SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_MORE_DATA))
    {
        const std::string name = Narrow(provider.ProviderName);
        return name.empty() ? std::string(SomeClient) : name;
    }

    for (const wchar_t* variable : OneDriveVariables)
    {
        std::array<wchar_t, MAX_PATH> value{};
        const DWORD written = GetEnvironmentVariableW(variable, value.data(), static_cast<DWORD>(value.size()));

        if (written > 0 && written < value.size() && Within(real, Resolved(std::filesystem::path(value.data()))))
        {
            return std::string(OneDrive);
        }
    }

    return std::nullopt;
}
#else
std::optional<std::string> MCSyncedBy(const std::filesystem::path&)
{
    return std::nullopt;
}
#endif
