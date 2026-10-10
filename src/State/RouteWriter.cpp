/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "RouteWriter.h"

#include "GuildmasterDatabase.h"
#include "Log.h"
#include "PlayerbotsAdapter.h"
#include <algorithm>
#include <ctime>

RouteWriter& RouteWriter::Instance()
{
    static RouteWriter instance;
    return instance;
}

void RouteWriter::Update(uint32 diff)
{
    if (!GuildmasterDatabaseReady)
        return;
    if (!_bootDone)
    {
        _bootDone = true;
        if (PlayerbotsAdapter::RoutesEnabled())
            WriteHubs();
        else
            ClearHubs();  // routes off: the app offers no "Head to..."
    }
    _timer += diff;
    if (_timer < DROPS_INTERVAL_MS)
        return;
    _timer = 0;
    WriteDrops();
}

void RouteWriter::WriteHubs()
{
    std::vector<PlayerbotsAdapter::RouteHubRow> const hubs = PlayerbotsAdapter::RouteHubs();
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    GuildmasterTransaction trans = GuildmasterDatabase.BeginTransaction();
    trans->Append("DELETE FROM route_hubs");
    for (PlayerbotsAdapter::RouteHubRow const& h : hubs)
    {
        GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_ROUTE_HUB);
        stmt->SetData(0, h.id);
        stmt->SetData(1, h.name.substr(0, 64));
        stmt->SetData(2, h.faction);
        stmt->SetData(3, static_cast<uint16>(h.map));
        stmt->SetData(4, h.zone);
        stmt->SetData(5, h.area);
        stmt->SetData(6, h.minLevel);
        stmt->SetData(7, h.level);
        stmt->SetData(8, h.maxLevel);
        stmt->SetData(9, static_cast<uint16>(std::min<uint32>(h.quests, 65535)));
        stmt->SetData(10, h.x);
        stmt->SetData(11, h.y);
        stmt->SetData(12, now);
        trans->Append(stmt);
    }
    GuildmasterDatabase.CommitTransaction(trans);
    _hubsWritten = static_cast<uint32>(hubs.size());
    LOG_INFO("module.guildbridge", "GUILDBRIDGE route_hubs: {} hubs written", hubs.size());
}

void RouteWriter::ClearHubs()
{
    GuildmasterDatabase.Execute("DELETE FROM route_hubs");
    _hubsWritten = 0;
}

void RouteWriter::WriteDrops()
{
    std::vector<PlayerbotsAdapter::QuestDropRow> const drops = PlayerbotsAdapter::QuestDrops();
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    GuildmasterTransaction trans = GuildmasterDatabase.BeginTransaction();
    trans->Append("DELETE FROM quest_drops");
    for (PlayerbotsAdapter::QuestDropRow const& d : drops)
    {
        GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_QUEST_DROP);
        stmt->SetData(0, d.quest);
        stmt->SetData(1, d.drops);
        stmt->SetData(2, d.lastAt);
        stmt->SetData(3, now);
        trans->Append(stmt);
    }
    GuildmasterDatabase.CommitTransaction(trans);
    _dropRows = static_cast<uint32>(drops.size());
}
