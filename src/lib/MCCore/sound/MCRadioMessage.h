#pragma once

#include "gui/MCGuiRef.h"
#include "sound/MCRadio.h"

class MCGuiSmackerWindow;
class MCMechWarrior;
struct MCSmackTag;

/// <summary>
/// A radio message on its way to the speakers: its speech fragments, the static played before the first one, and
/// the pilot's video.
/// </summary>
/// <remarks>Original source: <c>sound\radio.cpp</c>, <c>sound\soundsys.cpp</c> (<c>RadioData</c>, 16 fragment and
/// noise slots, of which a message filled at most two fragments and the first noise).</remarks>
struct MCRadioMessage
{
    /// <summary>A message without data.</summary>
    MCRadioMessage();
    /// <summary>Ends and deletes the video window.</summary>
    ~MCRadioMessage();

    MCRadioMessage(const MCRadioMessage&) = delete;
    MCRadioMessage& operator=(const MCRadioMessage&) = delete;

    /// <summary>Speech fragment <paramref name="index"/>'s wave, or none past the last.</summary>
    std::span<const uint8_t> FragmentAt(uint32_t index) const;
    /// <summary>The static played before fragment <paramref name="index"/> (only the first has one), or none.</summary>
    std::span<const uint8_t> NoiseAt(uint32_t index) const;
    /// <summary>Ends and deletes the pilot's video window, if there is one, and forgets its video.</summary>
    void CloseMovie();

    /// <summary>The packet played (msgId plus the variation).</summary>
    uint32_t MsgId = 0;
    /// <summary>The message type.</summary>
    MCRadioMessageType MsgType{};
    /// <summary>The noise file packet played before it.</summary>
    uint32_t NoiseId = 0;
    /// <summary>The speech fragments' waves, in the order they play.</summary>
    std::vector<std::vector<uint8_t>> Fragments;
    /// <summary>The static's wave; empty for none.</summary>
    std::vector<uint8_t> Noise;
    /// <summary>The turn it was queued.</summary>
    int32_t TurnQueued = 0;
    /// <summary>The pilot's video window, if the message has a movie.</summary>
    MCGuiHandle<MCGuiSmackerWindow> MovieWindow;
    /// <summary>The open Smacker video, until <see cref="MovieWindow"/> starts it.</summary>
    std::unique_ptr<MCSmackTag> Movie;
    /// <summary>The message type's priority: lower plays first.</summary>
    uint8_t Priority = 0;
    /// <summary>Scenario time after which it isn't worth playing.</summary>
    float ExpirationDate = 0.0f;
    /// <summary>Who speaks (the scenario owns the pilots).</summary>
    std::weak_ptr<MCMechWarrior> Pilot;
};

/// <summary>
/// The radio messages waiting to play, by priority (lower first, then the order they came). At most
/// <see cref="MaxWaiting"/> wait.
/// </summary>
/// <remarks>Original source: <c>sound\soundsys.cpp</c> (<c>SoundSystem</c>'s queue array and its methods).</remarks>
class MCRadioQueue
{
public:
    /// <summary>
    /// Messages that can wait: a game rule kept from the original (it bounds how late a message is heard, as nothing
    /// drops a message whose shelf life has passed).
    /// </summary>
    static constexpr size_t MaxWaiting = 8;

    /// <summary>
    /// Whether <paramref name="pilot"/> may say a message of <paramref name="priority"/> and <paramref name="type"/>
    /// now: not while one of his of a higher priority waits, nor while a message of the type with priority 2 or more
    /// waits.
    /// </summary>
    bool MayQueue(const MCMechWarrior& pilot, uint8_t priority, MCRadioMessageType type) const;
    /// <summary>Drops waiting messages queued on the same turn with the same packet as <paramref name="message"/>.
    /// </summary>
    void RemoveDuplicates(const MCRadioMessage& message);
    /// <summary>Drops every waiting message of <paramref name="pilot"/>.</summary>
    void RemovePilot(const MCMechWarrior& pilot);
    /// <summary>
    /// Puts <paramref name="message"/> ahead of the first one of a lower priority, or last. A full queue takes no
    /// message that would go last.
    /// </summary>
    /// <remarks>
    /// Original behaviour (OB-061): inserting ahead of 7 or 8 waiting messages leaves 7 (the lowest one or two go),
    /// although the queue had room for 8.
    /// </remarks>
    /// <returns>Null when queued, else the message back.</returns>
    std::unique_ptr<MCRadioMessage> Push(std::unique_ptr<MCRadioMessage> message);
    /// <summary>Takes the first waiting message (null when none waits).</summary>
    std::unique_ptr<MCRadioMessage> PopFront();
    /// <summary>Drops every waiting message.</summary>
    void Clear() { _Waiting.clear(); }
    /// <summary>How many messages wait.</summary>
    size_t Size() const { return _Waiting.size(); }
    /// <summary>Waiting message <paramref name="index"/>, first to play first.</summary>
    const MCRadioMessage& At(size_t index) const { return *_Waiting[index]; }

private:
    /// <summary>The waiting messages, first to play first.</summary>
    std::vector<std::unique_ptr<MCRadioMessage>> _Waiting;
};
