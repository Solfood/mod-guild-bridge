/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "EventSink.h"

#include "BridgeConfig.h"
#include "GuildmasterDatabase.h"
#include <chrono>
#include <ctime>

uint64 BridgeRunIdFor(uint32 /*guid*/) { return 0; }  // replaced in Task 11

EventSink& EventSink::Instance()
{
    static EventSink instance;
    return instance;
}

void EventSink::Push(GuildBridge::EventType type, uint32 guid, uint32 guildId, uint64 runId, std::string payload)
{
    if (!BridgeConfig::Get().enable)
        return;  // no guildmaster database to flush into
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.size() >= BridgeConfig::Get().eventQueueCap)
    {
        ++_dropped;
        return;
    }
    _queue.push_back({static_cast<uint32>(std::time(nullptr)), type, guid, guildId, runId, std::move(payload)});
    _queued = static_cast<uint32>(_queue.size());
}

void EventSink::Flush()
{
    std::vector<Pending> batch;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        batch.swap(_queue);
        _queued = 0;
    }
    if (batch.empty())
        return;
    auto const start = std::chrono::steady_clock::now();
    GuildmasterTransaction trans = GuildmasterDatabase.BeginTransaction();
    for (Pending& event : batch)
    {
        GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_EVENT);
        stmt->SetData(0, event.ts);
        stmt->SetData(1, std::string(GuildBridge::EventTypeName(event.type)));
        stmt->SetData(2, event.guid);
        stmt->SetData(3, event.guildId);
        stmt->SetData(4, event.runId);
        stmt->SetData(5, std::move(event.payload));
        trans->Append(stmt);
    }
    GuildmasterDatabase.CommitTransaction(trans);  // async: the guildmaster pool's writer thread runs it
    _written += batch.size();
    _lastFlushUs = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
}
