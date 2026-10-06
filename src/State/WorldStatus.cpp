/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "WorldStatus.h"

#include "BridgeConfig.h"
#include "EventSink.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "PlayerbotsAdapter.h"
#include <ctime>
#include <string>

#ifndef GUILDBRIDGE_VERSION
#define GUILDBRIDGE_VERSION "dev"
#endif

WorldStatus& WorldStatus::Instance()
{
    static WorldStatus instance;
    return instance;
}

void WorldStatus::Update(uint32 diff)
{
    _timer += diff;
    if (_timer >= INTERVAL_MS)
        WriteNow();
}

void WorldStatus::WriteNow()
{
    _timer = 0;
    if (!GuildmasterDatabaseReady)
        return;
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    if (!_bootedAt)
        _bootedAt = now;
    _size = PlayerbotsAdapter::PopulationSize();
    _online = PlayerbotsAdapter::PopulationOnline();
    // Once per boot: the first time enough of the population is awake (spec §2b "412 awake"). Advisory only.
    if (!_readyAt && _size &&
        static_cast<uint64>(_online) * 100 >= static_cast<uint64>(_size) * BridgeConfig::Get().populationReadyShare)
        _readyAt = now;
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_UPD_WORLD_STATUS);
    stmt->SetData(0, std::string(GUILDBRIDGE_VERSION).substr(0, 16));  // world_status.bridge_version VARCHAR(16)
    stmt->SetData(1, _bootedAt);
    stmt->SetData(2, now);
    stmt->SetData(3, _size);
    stmt->SetData(4, _online);
    stmt->SetData(5, _readyAt);
    stmt->SetData(6, GuildRegistry::Instance().GuildIdFor(GuildRole::User));
    stmt->SetData(7, GuildRegistry::Instance().GuildIdFor(GuildRole::Test));
    stmt->SetData(8, EventSink::Instance().Dropped());
    GuildmasterDatabase.Execute(stmt);
}
