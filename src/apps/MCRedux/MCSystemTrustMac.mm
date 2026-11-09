// The Mac's trust in a server's certificates (MCSystemTrust.h): Security's SSL policy for the host, over the chain the
// server sent, against the authorities the Mac trusts.
#include "MCSystemTrust.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <string>
#include <vector>

namespace
{
    class MCMacTrust final : public MCTlsLink::MCTrust
    {
    public:
        std::string Check(std::span<const std::span<const uint8_t>> chain, std::string_view host) const override
        {
            @autoreleasepool
            {
                NSMutableArray* certificates = [NSMutableArray arrayWithCapacity:chain.size()];

                for (const std::span<const uint8_t> der : chain)
                {
                    NSData* data = [NSData dataWithBytes:der.data() length:der.size()];
                    SecCertificateRef certificate = SecCertificateCreateWithData(nullptr, (__bridge CFDataRef)data);

                    if (certificate == nullptr)
                    {
                        return "a certificate the server sent can't be read";
                    }

                    [certificates addObject:(__bridge_transfer id)certificate];
                }

                NSString* name = [[NSString alloc] initWithBytes:host.data()
                                                          length:host.size()
                                                        encoding:NSUTF8StringEncoding];

                if (name == nil)
                {
                    return "the server's name isn't text";
                }

                SecPolicyRef policy = SecPolicyCreateSSL(true, (__bridge CFStringRef)name);
                SecTrustRef trust = nullptr;
                const OSStatus made =
                    SecTrustCreateWithCertificates((__bridge CFArrayRef)certificates, policy, &trust);
                CFRelease(policy);

                if (made != errSecSuccess || trust == nullptr)
                {
                    return std::string("the Mac couldn't check the certificates (") + std::to_string(made) + ")";
                }

                CFErrorRef error = nullptr;
                const bool trusted = SecTrustEvaluateWithError(trust, &error);
                CFRelease(trust);

                if (trusted)
                {
                    return {};
                }

                std::string why = "the Mac doesn't trust it";

                if (error != nullptr)
                {
                    NSString* description = [(__bridge NSError*)error localizedDescription];

                    if (description != nil)
                    {
                        why = description.UTF8String;
                    }

                    CFRelease(error);
                }

                return why;
            }
        }
    };
}

std::unique_ptr<MCTlsLink::MCTrust> MCMakeSystemTrust()
{
    return std::make_unique<MCMacTrust>();
}
