/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "StateWriter.h"

#include "BridgeAsync.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "EventSink.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "QueryCallback.h"
#include "RestoreMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <string>

using namespace GuildBridge;

StateWriter& StateWriter::Instance()
{
    static StateWriter instance;
    return instance;
}

void StateWriter::LoadFocusAtStartup()
{
    if (QueryResult result = GuildmasterDatabase.Query("SELECT guid, focus FROM bot_focus"))
        do
        {
            Focus focus = Focus::None;
            if (FocusFromName((*result)[1].Get<std::string>(), focus) && focus != Focus::None)
                _focus[(*result)[0].Get<uint32>()] = focus;
        } while (result->NextRow());
    LOG_INFO("module.guildbridge", "GUILDBRIDGE focus loaded for {} bot(s)", _focus.size());
}

Focus StateWriter::FocusOf(uint32 guid) const
{
    auto const it = _focus.find(guid);
    return it == _focus.end() ? Focus::None : it->second;
}

void StateWriter::SetFocus(uint32 guid, Focus focus, uint64 orderId)
{
    if (focus == Focus::None)
        _focus.erase(guid);
    else
        _focus[guid] = focus;
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_REP_FOCUS);
    stmt->SetData(0, guid);
    stmt->SetData(1, std::string(FocusName(focus)));
    stmt->SetData(2, orderId);
    stmt->SetData(3, static_cast<uint32>(std::time(nullptr)));
    GuildmasterDatabase.Execute(stmt);
    // An offline bot gets it from the next pass after it logs in.
    if (Player* bot = ObjectAccessor::FindPlayerByLowGUID(guid))
        PlayerbotsAdapter::SetFocus(bot, static_cast<uint8>(focus));
}

void StateWriter::ClearFocus(uint32 guid)
{
    _focus.erase(guid);
    GuildmasterDatabase.Execute("DELETE FROM bot_focus WHERE guid = {}", guid);
    if (Player* bot = ObjectAccessor::FindPlayerByLowGUID(guid))
        PlayerbotsAdapter::SetFocus(bot, static_cast<uint8>(Focus::None));
}

void StateWriter::Update(uint32 diff)
{
    _timer += diff;
    if (_timer >= INTERVAL_MS)
        WriteNow();
}

void StateWriter::WriteNow()
{
    _timer = 0;
    uint32 const user = GuildRegistry::Instance().GuildIdFor(GuildRole::User);
    uint32 const test = GuildRegistry::Instance().GuildIdFor(GuildRole::Test);
    if (_querying || !GuildmasterDatabaseReady || (!user && !test))
        return;
    _querying = true;
    // Guild ids come from the registry, so this query names no other database (spec §2b: per world).
    BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
        "SELECT guid, guildid FROM guild_member WHERE guildid IN ({}, {})", user, test)).WithCallback(
        [this](QueryResult result) {
            _querying = false;
            // A registered guild always has its leader, so no rows means the read failed: skip this pass rather
            // than take an empty list for "everyone left" and wipe the roster.
            if (!result)
            {
                LOG_WARN("module.guildbridge", "bot_state: the guild member list could not be read; pass skipped");
                return;
            }
            std::unordered_map<uint32, uint32> members;
            do
                members[(*result)[0].Get<uint32>()] = (*result)[1].Get<uint32>();
            while (result->NextRow());
            WriteMembers(members);
        }));
}

void StateWriter::WriteMembers(std::unordered_map<uint32, uint32> const& members)
{
    auto const started = std::chrono::steady_clock::now();
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    GuildmasterTransaction trans = GuildmasterDatabase.BeginTransaction();
    std::string keep;
    uint32 rows = 0;
    for (auto const& [guid, guildId] : members)
    {
        keep += (keep.empty() ? "" : ",") + std::to_string(guid);
        Focus const focus = FocusOf(guid);
        uint8 const held = PlayerbotsAdapter::IsHeld(guid) ? 1 : 0;
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
        if (bot && !bot->IsInWorld())
            continue;  // connected but between maps (a far teleport): its last row stays as it was
        if (!bot)
        {
            CharacterCacheEntry const* cache =
                sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
            if (!cache)
                continue;
            GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_UPS_BOT_OFFLINE);
            stmt->SetData(0, guid);
            stmt->SetData(1, cache->Name);
            stmt->SetData(2, guildId);
            stmt->SetData(3, cache->Level);
            stmt->SetData(4, cache->Class);
            stmt->SetData(5, cache->Race);
            stmt->SetData(6, cache->Sex);
            stmt->SetData(7, std::string(FocusName(focus)));
            stmt->SetData(8, held);
            stmt->SetData(9, now);
            trans->Append(stmt);
            ++rows;
            continue;
        }
        if (PlayerbotsAdapter::GetFocus(bot) != static_cast<uint8>(focus))
            PlayerbotsAdapter::SetFocus(bot, static_cast<uint8>(focus));  // the fork forgets focus at logout

        GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_REP_BOT_STATE);
        stmt->SetData(0, guid);
        stmt->SetData(1, bot->GetName());
        stmt->SetData(2, guildId);
        stmt->SetData(3, bot->GetLevel());
        stmt->SetData(4, bot->getClass());
        stmt->SetData(5, bot->getRace());
        stmt->SetData(6, bot->getGender());
        stmt->SetData(7, static_cast<uint16>(bot->GetMapId()));
        stmt->SetData(8, bot->GetZoneId());
        stmt->SetData(9, bot->GetAreaId());
        stmt->SetData(10, bot->GetPositionX());
        stmt->SetData(11, bot->GetPositionY());
        stmt->SetData(12, bot->GetPositionZ());
        stmt->SetData(13, static_cast<uint8>(bot->IsAlive() ? 1 : 0));
        stmt->SetData(14, static_cast<uint8>(bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? 1 : 0));
        stmt->SetData(15, static_cast<uint8>(bot->GetHealthPct()));
        stmt->SetData(16, PlayerbotsAdapter::DurabilityPct(bot));
        stmt->SetData(17, static_cast<uint64>(bot->GetMoney()));
        stmt->SetData(18, PlayerbotsAdapter::RpgStatusName(bot));
        stmt->SetData(19, std::string(FocusName(focus)));
        // The fork's stored picks: a one-time playerbots-DB load per bot on first use (preflight D7), cached after.
        stmt->SetData(20, static_cast<uint16>(PlayerbotsAdapter::StoredProfession(guid, false)));
        stmt->SetData(21, static_cast<uint16>(PlayerbotsAdapter::StoredProfession(guid, true)));
        stmt->SetData(22, static_cast<uint8>(bot->GetGroup() ? 1 : 0));
        stmt->SetData(23, BridgeRunIdFor(guid));
        stmt->SetData(24, held);
        // Quest routes (Plan 5a): its hub, progress there and the struggling flag (NULL / 0 while it is not routed),
        // and its style (always written: the bridge's route_style, else the fork's pick from its guid).
        PlayerbotsAdapter::RouteInfo const route = PlayerbotsAdapter::RouteOf(bot);
        stmt->SetData(25, route.hub);
        stmt->SetData(26, static_cast<uint16>(std::min<uint32>(route.done, 65535)));
        stmt->SetData(27, static_cast<uint16>(std::min<uint32>(route.total, 65535)));
        stmt->SetData(28, static_cast<uint8>(route.struggling ? 1 : 0));
        stmt->SetData(29, route.style);
        stmt->SetData(30, now);
        trans->Append(stmt);
        ++rows;
    }
    // A bot being restored is out of guild_member (deleted, then loaded back) for a moment: its row stays, marked
    // held and offline, instead of being dropped and written again.
    std::string restoring;
    for (uint32 guid : RestoreMgr::Instance().RestoringGuids())
        if (!members.count(guid))
            restoring += (restoring.empty() ? "" : ",") + std::to_string(guid);
    if (!restoring.empty())
    {
        trans->Append(Acore::StringFormat("UPDATE bot_state SET online = 0, held = 1, updated_at = {} WHERE guid IN ({})",
                                          now, restoring));
        keep += (keep.empty() ? "" : ",") + restoring;
        ++rows;
    }
    if (rows)
        GuildmasterDatabase.CommitTransaction(trans);
    // Ex-members leave the roster (members are kept even while offline). Same single writer: runs after the commit.
    GuildmasterDatabase.Execute(keep.empty() ? std::string("DELETE FROM bot_state")
                                             : "DELETE FROM bot_state WHERE guid NOT IN (" + keep + ")");
    _lastRows = rows;
    _lastBuildUs = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    if (_lastBuildUs > _maxBuildUs)
        _maxBuildUs = _lastBuildUs;
}
