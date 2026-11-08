// Built without the precompiled header: Objective-C++, and Foundation and MCCore's Win32 stand-ins can't meet.
#include "MCWebSocketOpen.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    /// <summary>Our own user agent, as the reports' and the downloads'.</summary>
    NSString* const UserAgent = @"MechCommander Redux";
    /// <summary>The largest message the service sends (a link's 64 KB of bytes, a description's 32 KB), with room.</summary>
    constexpr NSInteger MaxMessageBytes = 1 << 20;
    /// <summary>The bytes arrived and not taken past which the socket closes: the game takes them every frame.</summary>
    constexpr size_t MaxUnreadBytes = size_t{4} << 20;

    /// <summary>What the socket's callbacks and its owner share, under its lock.</summary>
    struct MCShared
    {
        std::mutex Lock;
        MCWebSocket::State State = MCWebSocket::State::Opening;
        std::vector<MCWebSocket::Message> Inbox;
        size_t InboxBytes = 0;
        /// <summary>What was sent before it opened, sent in order as it opens.</summary>
        std::vector<NSURLSessionWebSocketMessage*> Waiting;
        std::string Why;
        NSURLSessionWebSocketTask* Task = nil;
        /// <summary>The bytes sent (or waiting to) whose send hasn't completed.</summary>
        size_t Unsent = 0;

        /// <summary>Ends it (under the lock), with the first reason given.</summary>
        void End(std::string why)
        {
            if (State != MCWebSocket::State::Closed)
            {
                State = MCWebSocket::State::Closed;
                Why = std::move(why);
                Waiting.clear();
            }
        }
    };

    /// <summary>Whether <paramref name="error"/> is the system refusing the server's certificate.</summary>
    bool RefusedCertificate(NSError* error)
    {
        if (![error.domain isEqualToString:NSURLErrorDomain])
        {
            return false;
        }

        switch (error.code)
        {
            case NSURLErrorServerCertificateHasBadDate:
            case NSURLErrorServerCertificateUntrusted:
            case NSURLErrorServerCertificateHasUnknownRoot:
            case NSURLErrorServerCertificateNotYetValid:
                return true;
            default:
                return false;
        }
    }

    std::string ErrorText(NSError* error)
    {
        const std::string text = std::format("{} error {}", error.domain.UTF8String, static_cast<long>(error.code));
        return RefusedCertificate(error) ? std::format("{} ({})", MCWebSocketUntrusted, text) : text;
    }

    size_t MessageBytes(NSURLSessionWebSocketMessage* message)
    {
        return message.type == NSURLSessionWebSocketMessageTypeData
                   ? message.data.length
                   : [message.string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    }

    /// <summary>Sends <paramref name="message"/> (under the lock): its bytes count as unsent until its send completes.</summary>
    void Send(const std::shared_ptr<MCShared>& shared, NSURLSessionWebSocketMessage* message)
    {
        const size_t bytes = MessageBytes(message);
        const std::weak_ptr<MCShared> weak = shared;
        [shared->Task sendMessage:message
                completionHandler:^(NSError* _Nullable error) {
                  if (error != nil)
                  {
                      NSLog(@"MCWebSocket: a send failed: %s", ErrorText(error).c_str());
                  }

                  if (const std::shared_ptr<MCShared> owner = weak.lock())
                  {
                      const std::scoped_lock lock(owner->Lock);
                      owner->Unsent -= std::min(owner->Unsent, bytes);
                  }
                }];
    }

    /// <summary>Takes the next message, and again after each, until the socket ends.</summary>
    void ReceiveNext(std::weak_ptr<MCShared> weak, NSURLSessionWebSocketTask* task)
    {
        [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* _Nullable message,
                                                    NSError* _Nullable error) {
          const std::shared_ptr<MCShared> shared = weak.lock();

          if (shared == nullptr)
          {
              return;
          }

          {
              const std::scoped_lock lock(shared->Lock);

              if (error != nil || message == nil)
              {
                  shared->End(error != nil ? ErrorText(error) : std::string("it ended"));
                  return;
              }

              MCWebSocket::Message got;

              if (message.type == NSURLSessionWebSocketMessageTypeData)
              {
                  got.Binary = true;
                  got.Data.assign(static_cast<const char*>(message.data.bytes), message.data.length);
              }
              else
              {
                  got.Data = message.string.UTF8String != nullptr ? message.string.UTF8String : "";
              }

              if (shared->InboxBytes + got.Data.size() > MaxUnreadBytes)
              {
                  shared->End(std::format("the service sent over {} bytes the game hadn't read", MaxUnreadBytes));
                  [task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeMessageTooBig reason:nil];
                  return;
              }

              shared->InboxBytes += got.Data.size();
              shared->Inbox.push_back(std::move(got));
          }

          ReceiveNext(weak, task);
        }];
    }
}

/// <summary>The session's delegate: the socket's opening and closing (its messages come by ReceiveNext).</summary>
@interface MCWebSocketDelegate : NSObject <NSURLSessionWebSocketDelegate>
- (instancetype)initWithShared:(std::weak_ptr<MCShared>)shared;
@end

@implementation MCWebSocketDelegate
{
    std::weak_ptr<MCShared> _Shared;
}

- (instancetype)initWithShared:(std::weak_ptr<MCShared>)shared
{
    if ((self = [super init]))
    {
        _Shared = std::move(shared);
    }

    return self;
}

- (void)URLSession:(NSURLSession*)session
       webSocketTask:(NSURLSessionWebSocketTask*)webSocketTask
    didOpenWithProtocol:(NSString*)protocol
{
    const std::shared_ptr<MCShared> shared = _Shared.lock();

    if (shared == nullptr)
    {
        return;
    }

    const std::scoped_lock lock(shared->Lock);

    if (shared->State == MCWebSocket::State::Opening)
    {
        shared->State = MCWebSocket::State::Open;

        for (NSURLSessionWebSocketMessage* message : shared->Waiting)
        {
            Send(shared, message);
        }

        shared->Waiting.clear();
    }
}

- (void)URLSession:(NSURLSession*)session
       webSocketTask:(NSURLSessionWebSocketTask*)webSocketTask
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
              reason:(NSData*)reason
{
    const std::shared_ptr<MCShared> shared = _Shared.lock();

    if (shared != nullptr)
    {
        const std::scoped_lock lock(shared->Lock);
        const std::string said = reason != nil ? std::string(static_cast<const char*>(reason.bytes), reason.length) : "";
        shared->End(std::format("closed ({}){}{}", static_cast<long>(closeCode), said.empty() ? "" : ": ", said));
    }
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error
{
    const std::shared_ptr<MCShared> shared = _Shared.lock();

    if (shared != nullptr)
    {
        const std::scoped_lock lock(shared->Lock);
        shared->End(error != nil ? ErrorText(error) : std::string("it ended"));
    }
}
@end

namespace
{
    class MCWebSocketMac final : public MCWebSocket
    {
    public:
        explicit MCWebSocketMac(const std::string& url) : _Shared(std::make_shared<MCShared>())
        {
            NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];

            if (address == nil)
            {
                _Shared->End(std::format("\"{}\" isn't an address", url));
                return;
            }

            NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
            configuration.HTTPAdditionalHeaders = @{@"User-Agent" : UserAgent};
            _Session = [NSURLSession sessionWithConfiguration:configuration
                                                     delegate:[[MCWebSocketDelegate alloc] initWithShared:_Shared]
                                                delegateQueue:nil];
            NSURLSessionWebSocketTask* task = [_Session webSocketTaskWithURL:address];
            task.maximumMessageSize = MaxMessageBytes;
            _Shared->Task = task;
            ReceiveNext(_Shared, task);
            [task resume];
        }

        ~MCWebSocketMac() override
        {
            Close();
            // The session keeps its delegate until it's invalidated.
            [_Session invalidateAndCancel];
        }

        void SendText(std::string_view text) override
        {
            Queue([[NSURLSessionWebSocketMessage alloc]
                initWithString:[[NSString alloc] initWithBytes:text.data()
                                                        length:text.size()
                                                      encoding:NSUTF8StringEncoding]]);
        }

        void SendBinary(std::span<const uint8_t> bytes) override
        {
            Queue([[NSURLSessionWebSocketMessage alloc] initWithData:[NSData dataWithBytes:bytes.data()
                                                                                    length:bytes.size()]]);
        }

        std::vector<Message> Take() override
        {
            const std::scoped_lock lock(_Shared->Lock);
            _Shared->InboxBytes = 0;
            return std::exchange(_Shared->Inbox, {});
        }

        State GetState() const override
        {
            const std::scoped_lock lock(_Shared->Lock);
            return _Shared->State;
        }

        std::string WhyClosed() const override
        {
            const std::scoped_lock lock(_Shared->Lock);
            return _Shared->Why;
        }

        size_t Unsent() const override
        {
            const std::scoped_lock lock(_Shared->Lock);
            // Nothing waits to go once it's closed, whatever its cancelled sends' handlers have yet to say.
            return _Shared->State == State::Closed ? 0 : _Shared->Unsent;
        }

        void Close() override
        {
            CloseWith(NSURLSessionWebSocketCloseCodeNormalClosure);
        }

        void CloseAway() override
        {
            CloseWith(static_cast<NSURLSessionWebSocketCloseCode>(MCWebSocketAwayCode));
        }

    private:
        /// <summary>Closes it with <paramref name="code"/>, once (the destructor's Close keeps an earlier one's).</summary>
        void CloseWith(NSURLSessionWebSocketCloseCode code)
        {
            const std::scoped_lock lock(_Shared->Lock);

            if (_Shared->Task != nil && _Shared->State != State::Closed)
            {
                [_Shared->Task cancelWithCloseCode:code reason:nil];
            }

            _Shared->End("this game closed it");
        }

        void Queue(NSURLSessionWebSocketMessage* message)
        {
            const std::scoped_lock lock(_Shared->Lock);

            if (_Shared->State == State::Open)
            {
                _Shared->Unsent += MessageBytes(message);
                Send(_Shared, message);
            }
            else if (_Shared->State == State::Opening)
            {
                _Shared->Unsent += MessageBytes(message);
                _Shared->Waiting.push_back(message);
            }
        }

        std::shared_ptr<MCShared> _Shared;
        NSURLSession* _Session = nil;
    };
}

std::unique_ptr<MCWebSocket> MCOpenWebSocket(const std::string& url)
{
    return std::make_unique<MCWebSocketMac>(url);
}
