// Built without the precompiled header: Objective-C++, and Foundation and MCCore's Win32 stand-ins can't meet.
#include "MCHttpGet.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace
{
    /// <summary>A wait this long for the host's next bytes ends the transfer.</summary>
    constexpr NSTimeInterval IdleSeconds = 30.0;
    /// <summary>Our own user agent, as the reports': the system's names the macOS version, which isn't the host's.</summary>
    NSString* const UserAgent = @"MechCommander Redux";
    NSString* const ContentRangeHeader = @"Content-Range";
    /// <summary>
    /// The bytes arrived and not yet taken past which the transfer is paused (the session reads no more from the
    /// connection), and the bytes it's taken down to before it goes on.
    /// </summary>
    constexpr size_t QueuedMost = size_t{16} << 20;
    constexpr size_t QueuedResume = QueuedMost / 2;
    constexpr std::string_view StoppedError = "stopped";

    /// <summary>
    /// NSURLSession's errors (NSURLErrorDomain) for a host out of reach: timed out, its name not found, a connection
    /// refused, a connection lost, no name lookup, no connection to the internet, roaming off, a call on, no data allowed.
    /// </summary>
    constexpr std::array<NSInteger, 9> UnreachableErrors = {NSURLErrorTimedOut,
                                                            NSURLErrorCannotFindHost,
                                                            NSURLErrorCannotConnectToHost,
                                                            NSURLErrorNetworkConnectionLost,
                                                            NSURLErrorDNSLookupFailed,
                                                            NSURLErrorNotConnectedToInternet,
                                                            NSURLErrorInternationalRoamingOff,
                                                            NSURLErrorCallIsActive,
                                                            NSURLErrorDataNotAllowed};

    /// <summary>Whether <paramref name="error"/> says the host couldn't be reached.</summary>
    bool IsUnreachable(NSError* error)
    {
        return [error.domain isEqualToString:NSURLErrorDomain] && std::ranges::contains(UnreachableErrors, error.code);
    }

    /// <summary>A transfer's failure that isn't the host out of reach.</summary>
    MCHttpSource::MCGetError Failed(std::string reason)
    {
        return {.Unreachable = false, .Reason = std::move(reason)};
    }

    /// <summary>What the session's delegate passes to the thread that asked, in order.</summary>
    struct MCEvent
    {
        enum class MCKind : uint8_t
        {
            Status,
            Body,
            Done,
        };

        MCKind Kind = MCKind::Done;
        int Status = 0;
        std::string ContentRange;
        NSData* Bytes = nil;
        /// <summary>For Done: what failed (empty: the answer ended), and whether the host was out of reach.</summary>
        std::string Error;
        bool Unreachable = false;
    };

    /// <summary>
    /// One transfer's events: the delegate adds them, the thread that asked takes them. Its task is paused while more
    /// than <see cref="QueuedMost"/> bytes wait, and a cancel ends the wait at once.
    /// </summary>
    struct MCTransfer
    {
        NSURLSessionDataTask* Task = nil;
        NSUInteger TaskId = 0;
        std::mutex Lock;
        std::condition_variable Arrived;
        std::deque<MCEvent> Events;
        size_t Queued = 0;
        bool Paused = false;
        bool Cancelled = false;

        void Add(MCEvent event)
        {
            const std::scoped_lock lock(Lock);

            if (event.Kind == MCEvent::MCKind::Body)
            {
                Queued += event.Bytes.length;

                if (Queued > QueuedMost && !Paused && !Cancelled)
                {
                    Paused = true;
                    [Task suspend];
                }
            }

            Events.push_back(std::move(event));
            Arrived.notify_one();
        }

        /// <summary>The next event, or none once cancelled.</summary>
        std::optional<MCEvent> Take()
        {
            std::unique_lock lock(Lock);
            Arrived.wait(lock, [&] { return Cancelled || !Events.empty(); });

            if (Cancelled)
            {
                return std::nullopt;
            }

            MCEvent event = std::move(Events.front());
            Events.pop_front();

            if (event.Kind == MCEvent::MCKind::Body)
            {
                Queued -= event.Bytes.length;

                if (Paused && Queued <= QueuedResume)
                {
                    Paused = false;
                    [Task resume];
                }
            }

            return event;
        }

        /// <summary>From any thread: the task ends, and the thread waiting for its events wakes.</summary>
        void Cancel()
        {
            const std::scoped_lock lock(Lock);
            Cancelled = true;
            [Task cancel];
            Arrived.notify_all();
        }
    };
}

/// <summary>The session's delegate: passes the followed task's answer to its transfer; a stopped task's is dropped.</summary>
@interface MCHttpDelegate : NSObject <NSURLSessionDataDelegate>
- (void)follow:(std::shared_ptr<MCTransfer>)transfer;
@end

@implementation MCHttpDelegate
{
    std::mutex _Lock;
    std::shared_ptr<MCTransfer> _Transfer;
}

- (void)follow:(std::shared_ptr<MCTransfer>)transfer
{
    const std::scoped_lock lock(_Lock);
    _Transfer = std::move(transfer);
}

- (std::shared_ptr<MCTransfer>)transferFor:(NSURLSessionTask*)task
{
    const std::scoped_lock lock(_Lock);
    return _Transfer != nullptr && _Transfer->TaskId == task.taskIdentifier ? _Transfer : nullptr;
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler
{
    if (const std::shared_ptr<MCTransfer> transfer = [self transferFor:dataTask])
    {
        MCEvent event{.Kind = MCEvent::MCKind::Status};

        if ([response isKindOfClass:NSHTTPURLResponse.class])
        {
            auto* http = static_cast<NSHTTPURLResponse*>(response);
            event.Status = static_cast<int>(http.statusCode);
            NSString* range = [http valueForHTTPHeaderField:ContentRangeHeader];
            event.ContentRange = range != nil ? std::string(range.UTF8String) : std::string();
        }

        transfer->Add(std::move(event));
    }

    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)dataTask didReceiveData:(NSData*)data
{
    if (const std::shared_ptr<MCTransfer> transfer = [self transferFor:dataTask])
    {
        transfer->Add({.Kind = MCEvent::MCKind::Body, .Bytes = data});
    }
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    if (const std::shared_ptr<MCTransfer> transfer = [self transferFor:task])
    {
        transfer->Add(
            {.Kind = MCEvent::MCKind::Done,
             .Error = error != nil ? std::format("{} ({} {})", error.localizedDescription.UTF8String,
                                                 error.domain.UTF8String, static_cast<long>(error.code))
                                   : std::string(),
             .Unreachable = error != nil && IsUnreachable(error)});
    }
}
@end

namespace
{
    class MCMacGet final : public MCHttpSource::MCGet
    {
    public:
        MCMacGet()
        {
            // Nothing kept between runs: no cookies, no cache (release.json must be the host's latest).
            NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
            configuration.HTTPShouldSetCookies = NO;
            configuration.URLCache = nil;
            configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
            configuration.timeoutIntervalForRequest = IdleSeconds;
            // The bytes as stored: a compressed answer's ranges would count the compressed bytes.
            configuration.HTTPAdditionalHeaders = @{@"User-Agent" : UserAgent, @"Accept-Encoding" : @"identity"};
            NSOperationQueue* queue = [NSOperationQueue new];
            queue.maxConcurrentOperationCount = 1;
            _Delegate = [MCHttpDelegate new];
            _Session = [NSURLSession sessionWithConfiguration:configuration delegate:_Delegate delegateQueue:queue];
        }

        ~MCMacGet() override { [_Session invalidateAndCancel]; }

        MCMacGet(const MCMacGet&) = delete;
        MCMacGet& operator=(const MCMacGet&) = delete;

        std::expected<void, MCHttpSource::MCGetError> Get(const std::string& url, uint64_t from,
                                                          MCHttpSource::MCReply& reply) override
        {
            NSURL* address = [NSURL URLWithString:@(url.c_str())];

            if (address == nil)
            {
                return std::unexpected(Failed(std::format("\"{}\" isn't an address", url)));
            }

            NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:address];

            if (from != 0)
            {
                [request setValue:@(std::format("bytes={}-", from).c_str()) forHTTPHeaderField:@"Range"];
            }

            const auto transfer = std::make_shared<MCTransfer>();
            transfer->Task = [_Session dataTaskWithRequest:request];
            transfer->TaskId = transfer->Task.taskIdentifier;

            {
                const std::scoped_lock lock(_Lock);

                if (_Cancelled)
                {
                    return std::unexpected(Failed(std::string(StoppedError)));
                }

                _Current = transfer;
            }

            [_Delegate follow:transfer];
            [transfer->Task resume];
            const auto done = Follow(*transfer, reply);
            [_Delegate follow:nullptr];
            [transfer->Task cancel];
            const std::scoped_lock lock(_Lock);
            _Current.reset();

            if (_Cancelled)
            {
                return std::unexpected(Failed(std::string(StoppedError)));
            }

            return done;
        }

        void Cancel() override
        {
            const std::scoped_lock lock(_Lock);
            _Cancelled = true;

            if (_Current != nullptr)
            {
                _Current->Cancel();
            }
        }

    private:
        /// <summary>Passes the transfer's events to the reply until the answer ends, the reply stops it or it's cancelled.</summary>
        std::expected<void, MCHttpSource::MCGetError> Follow(MCTransfer& transfer, MCHttpSource::MCReply& reply)
        {
            for (;;)
            {
                std::optional<MCEvent> event = transfer.Take();

                if (!event)
                {
                    return std::unexpected(Failed(std::string(StoppedError)));
                }

                __block bool goOn = true;

                switch (event->Kind)
                {
                    case MCEvent::MCKind::Status:
                        goOn = reply.Status(event->Status, event->ContentRange);
                        break;
                    case MCEvent::MCKind::Body:
                        [event->Bytes enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL* stop) {
                          if (!reply.Body({static_cast<const char*>(bytes), range.length}))
                          {
                              goOn = false;
                              *stop = YES;
                          }
                        }];
                        break;
                    case MCEvent::MCKind::Done:
                        if (event->Error.empty())
                        {
                            return {};
                        }

                        return std::unexpected(
                            MCHttpSource::MCGetError{.Unreachable = event->Unreachable, .Reason = event->Error});
                }

                if (!goOn)
                {
                    return {};
                }
            }
        }

        MCHttpDelegate* _Delegate;
        NSURLSession* _Session;
        std::mutex _Lock;
        bool _Cancelled = false;
        std::shared_ptr<MCTransfer> _Current;
    };
}

std::unique_ptr<MCHttpSource::MCGet> MCMakeHttpGet()
{
    return std::make_unique<MCMacGet>();
}
