// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
// Windows' Credential Manager for MechCommander Online's sign-in (MCMcoKeysStore.h): one generic credential for this
// user on this computer, its blob the sign-in's text (network/MCMcoRooms.h's MCMcoKeptText).
#include "MCMcoKeysStore.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincred.h>

#pragma comment(lib, "advapi32.lib")

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
    /// <summary>The credential's name, as Credential Manager lists it.</summary>
    constexpr const wchar_t* Target = L"MechCommander Redux HD/MechCommander Online";

    class MCWinKeys final : public MCMcoKeys
    {
    public:
        std::optional<MCMcoKept> Load() override
        {
            PCREDENTIALW credential = nullptr;

            if (!CredReadW(Target, CRED_TYPE_GENERIC, 0, &credential))
            {
                if (const DWORD error = GetLastError(); error != ERROR_NOT_FOUND)
                {
                    SDL_Log("MechCommander Online: the Credential Manager's sign-in couldn't be read (%lu)", error);
                }

                return std::nullopt;
            }

            const std::string text(reinterpret_cast<const char*>(credential->CredentialBlob),
                                   credential->CredentialBlobSize);
            CredFree(credential);
            return MCMcoKeptFrom(text);
        }

        bool Save(const MCMcoKept& kept) override
        {
            std::string text = MCMcoKeptText(kept);
            CREDENTIALW credential{};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = const_cast<wchar_t*>(Target);
            credential.CredentialBlobSize = static_cast<DWORD>(text.size());
            credential.CredentialBlob = reinterpret_cast<LPBYTE>(text.data());
            credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
            const bool written = CredWriteW(&credential, 0) != FALSE;

            if (!written)
            {
                SDL_Log("MechCommander Online: the sign-in couldn't be kept in the Credential Manager (%lu)",
                        GetLastError());
            }

            SecureZeroMemory(text.data(), text.size());
            return written;
        }

        void Erase() override
        {
            if (!CredDeleteW(Target, CRED_TYPE_GENERIC, 0))
            {
                if (const DWORD error = GetLastError(); error != ERROR_NOT_FOUND)
                {
                    SDL_Log("MechCommander Online: the Credential Manager's sign-in couldn't be removed (%lu)", error);
                }
            }
        }
    };
}

std::shared_ptr<MCMcoKeys> MCMakeMcoKeys()
{
    return std::make_shared<MCWinKeys>();
}

#endif
