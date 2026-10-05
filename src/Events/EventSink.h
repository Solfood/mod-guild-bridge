/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_EVENTSINK_H
#define MOD_GUILD_BRIDGE_EVENTSINK_H

#include "Define.h"
#include "EventPayloads.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// The run a bot is in right now (0 = none). Any thread. Defined by the run tracking (Task 11).
uint64 BridgeRunIdFor(uint32 guid);

// Events from any thread into one queue; the world thread writes them as one async transaction.
// Beyond GuildBridge.Events.QueueCap queued events, new ones are counted as dropped instead of queued.
class EventSink
{
public:
    static EventSink& Instance();
    void Push(GuildBridge::EventType type, uint32 guid, uint32 guildId, uint64 runId, std::string payload);
    void Flush();  // world thread
    uint64 Written() const { return _written; }
    uint64 Dropped() const { return _dropped; }
    uint32 Queued() const { return _queued; }
    uint32 LastFlushUs() const { return _lastFlushUs; }
    uint32 MaxFlushUs() const { return _maxFlushUs; }  // the slowest flush since boot (world-thread cost)

private:
    struct Pending
    {
        uint32 ts = 0;
        GuildBridge::EventType type = GuildBridge::EventType::Fake;
        uint32 guid = 0;
        uint32 guildId = 0;
        uint64 runId = 0;
        std::string payload;
    };

    std::mutex _mutex;
    std::vector<Pending> _queue;
    std::atomic<uint64> _written{0};
    std::atomic<uint64> _dropped{0};
    std::atomic<uint32> _queued{0};
    std::atomic<uint32> _lastFlushUs{0};
    std::atomic<uint32> _maxFlushUs{0};
};

#endif
