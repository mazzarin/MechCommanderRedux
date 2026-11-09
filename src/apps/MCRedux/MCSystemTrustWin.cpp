// Built without the precompiled header: Windows.h and MCCore's Win32 stand-ins can't meet.
// Windows' trust in a server's certificates (MCSystemTrust.h): the certificate chain engine over the chain the server
// sent (its others as a store beside it), then the SSL policy for the host.
#include "MCSystemTrust.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

#include <format>
#include <string>

namespace
{
    /// <summary>The host's name as Windows takes it (UTF-16).</summary>
    std::wstring Wide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int size =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

        if (size <= 0)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(),
                            size);
        return wide;
    }

    class MCWindowsTrust final : public MCTlsLink::MCTrust
    {
    public:
        std::string Check(std::span<const std::span<const uint8_t>> chain, std::string_view host) const override
        {
            std::wstring name = Wide(host);

            if (name.empty() || chain.empty())
            {
                return "nothing to check";
            }

            HCERTSTORE others = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);

            if (others == nullptr)
            {
                return std::format("Windows couldn't check the certificates ({})", GetLastError());
            }

            PCCERT_CONTEXT leaf =
                CertCreateCertificateContext(X509_ASN_ENCODING, chain[0].data(), static_cast<DWORD>(chain[0].size()));

            if (leaf == nullptr)
            {
                CertCloseStore(others, 0);
                return "a certificate the server sent can't be read";
            }

            for (size_t i = 1; i < chain.size(); i++)
            {
                CertAddEncodedCertificateToStore(others, X509_ASN_ENCODING, chain[i].data(),
                                                 static_cast<DWORD>(chain[i].size()), CERT_STORE_ADD_ALWAYS, nullptr);
            }

            // Asked for as a server's certificate, its revocation checked where Windows can.
            LPSTR serverUse = const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH);
            CERT_CHAIN_PARA para{};
            para.cbSize = sizeof(para);
            para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
            para.RequestedUsage.Usage.cUsageIdentifier = 1;
            para.RequestedUsage.Usage.rgpszUsageIdentifier = &serverUse;
            PCCERT_CHAIN_CONTEXT built = nullptr;
            std::string why;

            if (!CertGetCertificateChain(nullptr, leaf, nullptr, others, &para,
                                         CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, nullptr, &built))
            {
                why = std::format("Windows couldn't build the chain ({})", GetLastError());
            }
            else
            {
                SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
                ssl.cbSize = sizeof(ssl);
                ssl.dwAuthType = AUTHTYPE_SERVER;
                ssl.pwszServerName = name.data();
                // A revocation check that couldn't be made (no answer from the authority, offline) passes, as the
                // browsers let it; a certificate found revoked doesn't.
                CERT_CHAIN_POLICY_PARA policy{};
                policy.cbSize = sizeof(policy);
                policy.dwFlags = CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;
                policy.pvExtraPolicyPara = &ssl;
                CERT_CHAIN_POLICY_STATUS status{};
                status.cbSize = sizeof(status);

                if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, built, &policy, &status))
                {
                    why = std::format("Windows couldn't check the chain ({})", GetLastError());
                }
                else if (status.dwError != 0)
                {
                    why = std::format("Windows doesn't trust it (0x{:08x})", static_cast<unsigned>(status.dwError));
                }

                CertFreeCertificateChain(built);
            }

            CertFreeCertificateContext(leaf);
            CertCloseStore(others, 0);
            return why;
        }
    };
}

std::unique_ptr<MCTlsLink::MCTrust> MCMakeSystemTrust()
{
    return std::make_unique<MCWindowsTrust>();
}

#endif
