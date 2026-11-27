// The Mac's Keychain for MechCommander Online's sign-in (MCMcoKeysStore.h): one generic password item, its data the
// sign-in's text (network/MCMcoRooms.h's MCMcoKeptText).
#include "MCMcoKeysStore.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
    /// <summary>The item's service and account, as Keychain Access lists it.</summary>
    NSString* const Service = @"MechCommander Redux HD: MechCommander Online";
    NSString* const Account = @"sign-in";

    NSMutableDictionary* Query()
    {
        return [NSMutableDictionary dictionaryWithDictionary:@{
            (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : Service,
            (__bridge id)kSecAttrAccount : Account,
        }];
    }

    class MCMacKeys final : public MCMcoKeys
    {
    public:
        std::optional<MCMcoKept> Load() override
        {
            @autoreleasepool
            {
                NSMutableDictionary* query = Query();
                query[(__bridge id)kSecReturnData] = @YES;
                query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
                CFTypeRef found = nullptr;
                const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &found);

                if (status != errSecSuccess)
                {
                    if (status != errSecItemNotFound)
                    {
                        SDL_Log("MechCommander Online: the Keychain's sign-in couldn't be read (%d)",
                                static_cast<int>(status));
                    }

                    return std::nullopt;
                }

                NSData* data = (__bridge_transfer NSData*)found;
                return MCMcoKeptFrom(
                    std::string_view(static_cast<const char*>(data.bytes), static_cast<size_t>(data.length)));
            }
        }

        bool Save(const MCMcoKept& kept) override
        {
            @autoreleasepool
            {
                const std::string text = MCMcoKeptText(kept);
                NSData* data = [NSData dataWithBytes:text.data() length:text.size()];
                const OSStatus updated = SecItemUpdate((__bridge CFDictionaryRef)Query(),
                                                       (__bridge CFDictionaryRef) @{(__bridge id)kSecValueData : data});

                if (updated == errSecSuccess)
                {
                    return true;
                }

                NSMutableDictionary* item = Query();
                item[(__bridge id)kSecValueData] = data;
                item[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;
                const OSStatus added = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);

                if (added != errSecSuccess)
                {
                    SDL_Log("MechCommander Online: the sign-in couldn't be kept in the Keychain (%d)",
                            static_cast<int>(added));
                }

                return added == errSecSuccess;
            }
        }

        void Erase() override
        {
            @autoreleasepool
            {
                const OSStatus status = SecItemDelete((__bridge CFDictionaryRef)Query());

                if (status != errSecSuccess && status != errSecItemNotFound)
                {
                    SDL_Log("MechCommander Online: the Keychain's sign-in couldn't be removed (%d)",
                            static_cast<int>(status));
                }
            }
        }
    };
}

std::shared_ptr<MCMcoKeys> MCMakeMcoKeys()
{
    return std::make_shared<MCMacKeys>();
}
