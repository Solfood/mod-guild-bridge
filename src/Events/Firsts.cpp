/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "Firsts.h"

#include "BridgeConfig.h"
#include "EventSink.h"
#include "GuildmasterDatabase.h"
#include <ctime>

Firsts& Firsts::Instance()
{
    static Firsts instance;
    return instance;
}

void Firsts::LoadAtStartup()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (QueryResult result = GuildmasterDatabase.Query("SELECT kind, ref FROM firsts"))
        do
            _claimed.emplace((*result)[0].Get<std::string>(), (*result)[1].Get<uint32>());
        while (result->NextRow());
}

bool Firsts::Claim(std::string const& kind, uint32 ref, uint32 guid, uint32 guildId, std::string const& label)
{
    if (!BridgeConfig::Get().enable)
        return false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_claimed.emplace(kind, ref).second)
            return false;
    }
    // Async; INSERT IGNORE keeps the first row if two worlds' bookkeeping ever disagreed.
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_FIRST);
    stmt->SetData(0, kind);
    stmt->SetData(1, ref);
    stmt->SetData(2, guid);
    stmt->SetData(3, guildId);
    stmt->SetData(4, label);
    stmt->SetData(5, static_cast<uint32>(std::time(nullptr)));
    GuildmasterDatabase.Execute(stmt);
    EventSink::Instance().Push(GuildBridge::EventType::First, guid, guildId, 0,
                               GuildBridge::FirstPayload(kind, ref, label));
    return true;
}
