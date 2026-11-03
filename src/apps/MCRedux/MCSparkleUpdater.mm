// Built without the precompiled header: Objective-C++, and Foundation and MCCore's Win32 stand-ins can't meet.
#include "MCSparkleUpdater.h"

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>
#include <SDL3/SDL_log.h>

namespace
{
    /// <summary>The Info.plist keys: the feed, and the versions shown (major.minor.patch and the build).</summary>
    NSString* const FeedKey = @"SUFeedURL";
    NSString* const ShownVersionKey = @"CFBundleShortVersionString";
    NSString* const BuildKey = @"CFBundleVersion";

    /// <summary>A version as the game shows it: "0.2.1 (468)".</summary>
    std::string ShownVersion(NSString* shown, NSString* build)
    {
        return std::string((shown ?: @"?").UTF8String) + " (" + std::string((build ?: @"?").UTF8String) + ")";
    }

    /// <summary><paramref name="text"/> as UTF-8.</summary>
    std::string Utf8(NSString* text)
    {
        return text != nil ? std::string(text.UTF8String) : std::string();
    }

    /// <summary>
    /// Release notes as plain text: the feed's are plain text (generate_appcast's .txt notes); HTML ones lose their
    /// tags, each paragraph and line break a newline.
    /// </summary>
    std::string PlainNotes(NSString* notes, NSString* format)
    {
        if (notes == nil)
        {
            return {};
        }

        if (format == nil || [format isEqualToString:@"html"])
        {
            NSMutableString* text = [notes mutableCopy];
            [text replaceOccurrencesOfString:@"<br>" withString:@"\n" options:NSCaseInsensitiveSearch
                                       range:NSMakeRange(0, text.length)];
            [text replaceOccurrencesOfString:@"</p>" withString:@"\n" options:NSCaseInsensitiveSearch
                                       range:NSMakeRange(0, text.length)];
            [text replaceOccurrencesOfString:@"</li>" withString:@"\n" options:NSCaseInsensitiveSearch
                                       range:NSMakeRange(0, text.length)];
            NSRegularExpression* tags = [NSRegularExpression regularExpressionWithPattern:@"<[^>]*>" options:0
                                                                                    error:nil];
            NSString* stripped = [tags stringByReplacingMatchesInString:text options:0
                                                                  range:NSMakeRange(0, text.length)
                                                           withTemplate:@""];
            return Utf8([stripped stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]);
        }

        return Utf8([notes stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]);
    }
}

/// <summary>
/// Sparkle's user driver: each step it shows goes to the updater instead (main/MCUpdater.h), and the replies wait for
/// the player's answers on the UPDATE page. Sparkle calls it on the main thread, as the frame pumps the window's events.
/// </summary>
@interface MCSparkleDriver : NSObject <SPUUserDriver>
/// <summary>Passes on the player's answer to the step waiting for one.</summary>
- (void)answer:(MCUpdater::MCAnswer)answer;
@end

@implementation MCSparkleDriver
{
    /// <summary>The reply waiting for the player: to the update found, or to its being ready to install.</summary>
    void (^_FoundReply)(SPUUserUpdateChoice);
    void (^_ReadyReply)(SPUUserUpdateChoice);
    uint64_t _Received;
    uint64_t _Expected;
}

- (void)answer:(MCUpdater::MCAnswer)answer
{
    const SPUUserUpdateChoice choice =
        answer == MCUpdater::MCAnswer::Install ? SPUUserUpdateChoiceInstall : SPUUserUpdateChoiceSkip;

    if (_FoundReply != nil)
    {
        void (^reply)(SPUUserUpdateChoice) = _FoundReply;
        _FoundReply = nil;
        reply(choice);
    }
    else if (_ReadyReply != nil && answer == MCUpdater::MCAnswer::Install)
    {
        void (^reply)(SPUUserUpdateChoice) = _ReadyReply;
        _ReadyReply = nil;
        reply(choice);
    }
    else
    {
        SDL_Log("Updater: an answer with no step waiting for one");
    }
}

- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request
                              reply:(void (^)(SUUpdatePermissionResponse*))reply
{
    // The game looks at each start itself: Sparkle schedules nothing.
    reply([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO sendSystemProfile:NO]);
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation
{
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)item
                                 state:(SPUUserUpdateState*)state
                                 reply:(void (^)(SPUUserUpdateChoice))reply
{
    _FoundReply = reply;
    MCUpdater::Found(ShownVersion(item.displayVersionString, item.versionString),
                     PlainNotes(item.itemDescription, item.itemDescriptionFormat));
}

- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData
{
    NSString* notes = [[NSString alloc] initWithData:downloadData.data encoding:NSUTF8StringEncoding];
    const bool html = [downloadData.MIMEType isEqualToString:@"text/html"];
    MCUpdater::ReleaseNotes(PlainNotes(notes, html ? @"html" : @"plain-text"));
}

- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error
{
    SDL_Log("Updater: the release notes didn't download: %s", error.localizedDescription.UTF8String);
}

- (void)showUpdateNotFoundWithError:(NSError*)error acknowledgement:(void (^)(void))acknowledgement
{
    SDL_Log("Updater: no newer version (%s)", error.localizedDescription.UTF8String);
    acknowledgement();
    MCUpdater::Closed();
}

- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))acknowledgement
{
    _FoundReply = nil;
    _ReadyReply = nil;
    MCUpdater::Failed(Utf8(error.localizedDescription));
    acknowledgement();
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation
{
    _Received = 0;
    _Expected = 0;
    MCUpdater::Downloading(_Received, _Expected);
}

- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)expectedContentLength
{
    _Expected = expectedContentLength;
    MCUpdater::Downloading(_Received, _Expected);
}

- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length
{
    _Received += length;
    MCUpdater::Downloading(_Received, _Expected);
}

- (void)showDownloadDidStartExtractingUpdate
{
}

- (void)showExtractionReceivedProgress:(double)progress
{
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply
{
    _ReadyReply = reply;
    MCUpdater::Ready();
}

- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)applicationTerminated
                          retryTerminatingApplication:(void (^)(void))retryTerminatingApplication
{
    // Sparkle asked the app to quit: SDL turns that into the game's own quit, and the new version starts after it.
    MCUpdater::Installing();
}

- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))acknowledgement
{
    acknowledgement();
}

- (void)dismissUpdateInstallation
{
    _FoundReply = nil;
    _ReadyReply = nil;
    MCUpdater::Closed();
}
@end

/// <summary>Sparkle's delegate: no permission asked (the game looks at each start), and the session's end logged.</summary>
@interface MCSparkleDelegate : NSObject <SPUUpdaterDelegate>
@end

@implementation MCSparkleDelegate
- (BOOL)updaterShouldPromptForPermissionToCheckForUpdates:(SPUUpdater*)updater
{
    return NO;
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error
{
    SDL_Log("Updater: Sparkle stopped: %s", error.localizedDescription.UTF8String);
}

- (void)updaterWillRelaunchApplication:(SPUUpdater*)updater
{
    SDL_Log("Updater: the new version starts once the game has quit");
}
@end

namespace
{
    /// <summary>The backend: Sparkle's updater, its driver and its delegate.</summary>
    class MCSparkleBackend final : public MCUpdater::MCBackend
    {
    public:
        MCSparkleBackend(SPUUpdater* updater, MCSparkleDriver* driver, MCSparkleDelegate* delegate)
            : _Updater(updater), _Driver(driver), _Delegate(delegate)
        {
        }

        std::string RunningVersion() override
        {
            NSBundle* bundle = NSBundle.mainBundle;
            return ShownVersion([bundle objectForInfoDictionaryKey:ShownVersionKey],
                                [bundle objectForInfoDictionaryKey:BuildKey]);
        }

        void Check() override { [_Updater checkForUpdatesInBackground]; }

        void Answer(MCUpdater::MCAnswer answer) override { [_Driver answer:answer]; }

    private:
        SPUUpdater* _Updater;
        MCSparkleDriver* _Driver;
        /// <summary>Kept: Sparkle holds its delegate weakly.</summary>
        MCSparkleDelegate* _Delegate;
    };
}

std::unique_ptr<MCUpdater::MCBackend> MCMakeSparkleBackend()
{
    @autoreleasepool
    {
        NSBundle* bundle = NSBundle.mainBundle;

        if ([bundle objectForInfoDictionaryKey:FeedKey] == nil)
        {
            SDL_Log("Updater: the app's Info.plist names no feed (%s): no updates", FeedKey.UTF8String);
            return nullptr;
        }

        MCSparkleDriver* driver = [[MCSparkleDriver alloc] init];
        MCSparkleDelegate* delegate = [[MCSparkleDelegate alloc] init];
        SPUUpdater* updater = [[SPUUpdater alloc] initWithHostBundle:bundle
                                                   applicationBundle:bundle
                                                          userDriver:driver
                                                            delegate:delegate];
        // The game looks once a start (MCUpdater::CheckAtStart): Sparkle's own schedule stays off.
        updater.automaticallyChecksForUpdates = NO;
        NSError* error = nil;

        if (![updater startUpdater:&error])
        {
            SDL_Log("Updater: Sparkle didn't start: %s", error.localizedDescription.UTF8String);
            return nullptr;
        }

        return std::make_unique<MCSparkleBackend>(updater, driver, delegate);
    }
}
