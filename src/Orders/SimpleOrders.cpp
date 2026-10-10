/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "SimpleOrders.h"

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "CharacterCache.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "EventPayloads.h"
#include "EventSink.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Json.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "QueryCallback.h"
#include "RestoreMgr.h"
#include "RunRegistry.h"
#include "StateWriter.h"
#include "StringFormat.h"
#include <deque>
#include <memory>
#include <vector>

using namespace GuildBridge;

namespace
{
CharacterCacheEntry const* Cache(uint32 guid)
{
    return sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
}

Guild* OurGuildOf(CharacterCacheEntry const* cache)
{
    if (!cache || GuildRegistry::Instance().RoleOf(cache->GuildId) == GuildRole::None)
        return nullptr;
    return sGuildMgr->GetGuildById(cache->GuildId);
}

// The guild has rank `rank` (core ranks are 0..lowest; a missing rank has no rights). Our guilds keep the core's
// default ranks, each of which has at least the guild-chat rights.
bool HasRank(Guild* guild, int rank)
{
    return rank >= 0 && rank < 10 && guild->GetRankRights(static_cast<uint8>(rank)) != 0;
}

std::string NoRank(int rank) { return "bad value for rank (the guild has no rank " + std::to_string(rank) + ")"; }

TeamId TeamOf(CharacterCacheEntry const* cache) { return Player::TeamIdForRace(cache->Race); }

OrderResult Invite(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    GuildRole const role = GuildRegistry::RoleFromName(order.guild);
    Guild* guild = sGuildMgr->GetGuildById(GuildRegistry::Instance().GuildIdFor(role));
    if (!guild)
        return {false, std::string("no ") + order.guild + " guild yet", ""};
    if (cache->GuildId == guild->GetId())
        return {false, "already in the guild", ""};
    CharacterCacheEntry const* leader = sCharacterCache->GetCharacterCacheByGuid(guild->GetLeaderGUID());
    if (leader && TeamOf(leader) != TeamOf(cache))
        return {false, "wrong faction: " + cache->Name + " cannot join " + guild->GetName(), ""};
    Guild* other = cache->GuildId ? sGuildMgr->GetGuildById(cache->GuildId) : nullptr;
    if (other && other->GetLeaderGUID() == cache->Guid)
        return {false, "bot leads its guild", ""};
    if (other && PlayerbotsAdapter::IsRealGuild(other->GetId()))
        return {false, "bot is in another guild", ""};
    if (order.rank > 0 && !HasRank(guild, order.rank))
        return {false, NoRank(order.rank), ""};
    // Guild::AddMember reads an offline character's stats with a sync query; the world thread makes none.
    if (!ObjectAccessor::FindPlayerByLowGUID(order.bot))
        return {false, "bot is offline", ""};
    if (order.dryRun)
        return {true, "dry run: would invite " + cache->Name + " to " + guild->GetName(), ""};
    if (other)
        other->DeleteMember(cache->Guid, false, false);  // a playerbots bot guild: it just leaves (decided Q11)
    if (!guild->AddMember(cache->Guid, GUILD_RANK_NONE))
        return {false, "the game refused the invite", ""};
    if (order.rank > 0 && !guild->ChangeMemberRank(cache->Guid, static_cast<uint8>(order.rank)))
        return {false, cache->Name + " joined " + guild->GetName() + " at the lowest rank: " + NoRank(order.rank), ""};
    Guild::Member const* member = guild->GetMember(cache->Guid);
    uint8 const finalRank = member ? member->GetRankId() : 0;
    EventSink::Instance().Push(EventType::GuildJoin, order.bot, guild->GetId(), 0,
                               GuildJoinPayload(guild->GetName(), finalRank));
    return {true, cache->Name + " joined " + guild->GetName() + " at rank " + std::to_string(finalRank), ""};
}

OrderResult Remove(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    Guild* guild = OurGuildOf(cache);
    if (!guild)
        return {false, "bot is not in our guild", ""};
    if (guild->GetLeaderGUID() == cache->Guid)
        return {false, "bot leads its guild", ""};
    if (order.dryRun)
        return {true, "dry run: would remove " + cache->Name + " from " + guild->GetName(), ""};
    std::string const guildName = guild->GetName();
    uint32 const guildId = guild->GetId();
    guild->DeleteMember(cache->Guid, false, true);
    StateWriter::Instance().ClearFocus(order.bot);  // a focus order only applies to our members
    StateWriter::Instance().ClearRoute(order.bot);  // so do route_style and head_to
    EventSink::Instance().Push(EventType::GuildLeave, order.bot, guildId, 0, GuildLeavePayload(guildName, true));
    return {true, cache->Name + " left " + guildName, ""};
}

OrderResult Rank(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    Guild* guild = OurGuildOf(cache);
    if (!guild || !guild->GetMember(cache->Guid))
        return {false, "bot is not in our guild", ""};
    if (guild->GetLeaderGUID() == cache->Guid)
        return {false, "bot leads its guild", ""};
    if (!HasRank(guild, order.rank))
        return {false, NoRank(order.rank), ""};
    uint8 const oldRank = guild->GetMember(cache->Guid)->GetRankId();
    if (order.dryRun)
        return {true, "dry run: would set " + cache->Name + "'s rank to " + std::to_string(order.rank), ""};
    if (!guild->ChangeMemberRank(cache->Guid, static_cast<uint8>(order.rank)))
        return {false, NoRank(order.rank), ""};
    EventSink::Instance().Push(EventType::GuildRank, order.bot, guild->GetId(), 0,
                               GuildRankPayload(static_cast<uint32>(order.rank), order.rank < oldRank));
    return {true, cache->Name + " is now rank " + std::to_string(order.rank), ""};
}

OrderResult PresetProfessions(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    if (!OurGuildOf(cache))
        return {false, "bot is not in our guild", ""};
    Player* bot = ObjectAccessor::FindPlayerByLowGUID(order.bot);
    if (!bot)
        return {false, "bot is offline", ""};
    PlayerbotsAdapter::ProfessionState const state = PlayerbotsAdapter::GetProfessionState(bot);
    if (state.knownPrimary >= 2 || (state.stored1 && state.stored2 && bot->GetLevel() >= state.pickLevel))
        return {false, "already picked professions (changing them needs the unlearn flow, later)", ""};
    if (order.dryRun)
        return {true, "dry run: would preset professions for " + cache->Name, ""};
    PlayerbotsAdapter::PresetProfessions(order.bot, order.first, order.second);
    return {true, cache->Name + " will pick professions " + std::to_string(order.first) + " and " +
                      (order.second ? std::to_string(order.second) : std::string("a rolled partner")),
            ""};
}

// Stored in bot_focus and applied at once (world thread); the 30 s bot_state pass re-applies it after relogs.
OrderResult FocusOrder(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    if (!OurGuildOf(cache))
        return {false, "bot is not in our guild", ""};
    if (order.dryRun)
        return {true, "dry run: would set " + cache->Name + "'s focus to " + FocusName(order.focus), ""};
    StateWriter::Instance().SetFocus(order.bot, order.focus, order.id);
    return {true, cache->Name + "'s focus is now " + FocusName(order.focus), ""};
}

std::string ZoneName(uint32 zone)
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(zone);
    return area ? std::string(area->area_name[0]) : "zone " + std::to_string(zone);
}

// head_to (contract §4): walk toward a zone on its route. Stored in bot_route; the 30 s pass re-applies it after a
// relog until the bot reaches the zone or outlevels it. With routes off it is refused and nothing changes.
OrderResult HeadToOrder(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    if (!OurGuildOf(cache))
        return {false, "bot is not in our guild", ""};
    Player* bot = ObjectAccessor::FindPlayerByLowGUID(order.bot);
    if (!bot)
        return {false, PlayerbotsAdapter::RoutesEnabled() ? "bot is offline" : "routes are off", ""};
    std::string const problem = PlayerbotsAdapter::HeadToProblem(bot, order.zone);  // "routes are off" first
    if (!problem.empty())
        return {false, problem, ""};
    if (order.dryRun)
        return {true, "dry run: " + cache->Name + " would head for " + ZoneName(order.zone), ""};
    StateWriter::Instance().SetHeadTo(order.bot, order.zone, order.id);
    PlayerbotsAdapter::SetHeadTo(bot, order.zone);
    return {true, cache->Name + " heads for " + ZoneName(order.zone), ""};
}

// route_style (contract §4): stored in bot_route and applied now, or after its next login. Refused while routes are
// off (globally, and not switched on for this bot by the fork's test seam), so nothing changes then.
OrderResult RouteStyleOrder(ParsedOrder const& order, CharacterCacheEntry const* cache)
{
    if (!OurGuildOf(cache))
        return {false, "bot is not in our guild", ""};
    Player* bot = ObjectAccessor::FindPlayerByLowGUID(order.bot);
    if (!PlayerbotsAdapter::RoutesEnabled() && !(bot && PlayerbotsAdapter::IsRouted(bot)))
        return {false, "routes are off", ""};
    if (order.dryRun)
        return {true, "dry run: would set " + cache->Name + "'s route style to " + order.style, ""};
    StateWriter::Instance().SetRouteStyle(order.bot, order.style, order.id);
    if (bot)
        PlayerbotsAdapter::SetRouteStyle(bot, order.style);
    return {true, cache->Name + "'s route style is now " + order.style, ""};
}

// One snapshot batch (preflight D11: the bookkeeping lives here once). Requests are paced by Update().
struct Batch
{
    std::vector<uint32> guids;
    std::size_t next = 0;  // the next guid to hand to BotDumps
    std::size_t answered = 0;
    std::vector<uint64> bots;
    std::vector<uint64> failed;
    char const* reason = "manual";
    uint64 orderId = 0;
    std::function<void()> after;  // runs before finish (snapshot_all: the retention)
    std::function<void(OrderResult const&)> finish;
    uint32 ticks = 0;  // world ticks that handed out this batch's requests (logged: the pacing evidence)
    uint64 lastTick = 0;
};

std::deque<std::shared_ptr<Batch>> g_batches;  // world thread only
uint64 g_tick = 0;                             // world thread only

void Complete(Batch& batch)
{
    LOG_INFO("module.guildbridge", "GUILDBRIDGE snapshot batch order={} bots={} ticks={} failed={}", batch.orderId,
             batch.guids.size(), batch.ticks, batch.failed.size());
    if (batch.after)
        batch.after();
    std::string const data = JsonObject().UIntArray("bots", batch.bots).UIntArray("failed", batch.failed).Build();
    batch.finish({batch.failed.empty(),
                  std::to_string(batch.bots.size()) + " snapshot(s) taken, " + std::to_string(batch.failed.size()) +
                      " failed",
                  data});
}

void SnapshotBatch(std::vector<uint32> guids, char const* reason, uint64 orderId,
                   std::function<void(OrderResult const&)> finish, std::function<void()> after = {})
{
    auto batch = std::make_shared<Batch>();
    batch->guids = std::move(guids);
    batch->reason = reason;
    batch->orderId = orderId;
    batch->finish = std::move(finish);
    batch->after = std::move(after);
    if (batch->guids.empty())
        Complete(*batch);
    else
        g_batches.push_back(std::move(batch));
}

std::vector<uint32> Guids(QueryResult const& result)
{
    std::vector<uint32> guids;
    if (result)
        do
            guids.push_back((*result)[0].Get<uint32>());
        while (result->NextRow());
    return guids;
}
}  // namespace

std::string SimpleOrders::BusyReason(uint32 guid)
{
    if (RunRegistry::Instance().RunIdFor(guid))
        return "bot is in a dungeon run";
    if (RestoreMgr::Instance().IsRestoring(guid))
        return "bot is being restored";
    if (PlayerbotsAdapter::IsHeld(guid))
        return "bot is busy (lent to a dungeon run or being restored)";
    return "";
}

OrderResult SimpleOrders::Run(ParsedOrder const& order)
{
    CharacterCacheEntry const* cache = Cache(order.bot);
    if (!cache)
        return {false, "no such bot", ""};
    std::string const busy = BusyReason(order.bot);
    if (!busy.empty())
        return {false, busy, ""};
    switch (order.type)
    {
        case OrderType::Invite: return Invite(order, cache);
        case OrderType::Remove: return Remove(order, cache);
        case OrderType::Rank: return Rank(order, cache);
        case OrderType::PresetProfessions: return PresetProfessions(order, cache);
        case OrderType::Focus: return FocusOrder(order, cache);
        case OrderType::HeadTo: return HeadToOrder(order, cache);
        case OrderType::RouteStyle: return RouteStyleOrder(order, cache);
        default: return {false, "unknown order type", ""};
    }
}

void SimpleOrders::Snapshot(ParsedOrder const& order, std::function<void(OrderResult const&)> finish)
{
    uint64 const orderId = order.id;
    if (!order.guild.empty())
    {
        uint32 const guildId = GuildRegistry::Instance().GuildIdFor(GuildRegistry::RoleFromName(order.guild));
        if (!guildId)
        {
            finish({false, "no " + order.guild + " guild yet", ""});
            return;
        }
        if (order.dryRun)
        {
            finish({true, "dry run: would snapshot every member of the " + order.guild + " guild", ""});
            return;
        }
        BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
            "SELECT guid FROM guild_member WHERE guildid = {}", guildId)).WithCallback(
            [orderId, finish = std::move(finish)](QueryResult result) {
                SnapshotBatch(Guids(result), "manual", orderId, finish);
            }));
        return;
    }
    if (!Cache(order.bot))
    {
        finish({false, "no such bot", ""});
        return;
    }
    if (order.dryRun)
    {
        finish({true, "dry run: would snapshot the bot", ""});
        return;
    }
    SnapshotBatch({order.bot}, "manual", orderId, std::move(finish));
}

void SimpleOrders::SnapshotAll(uint64 orderId, bool dryRun, std::function<void(OrderResult const&)> finish)
{
    uint32 const user = GuildRegistry::Instance().GuildIdFor(GuildRole::User);
    uint32 const test = GuildRegistry::Instance().GuildIdFor(GuildRole::Test);
    if (dryRun)
    {
        finish({true, "dry run: would snapshot every member of our guilds", ""});
        return;
    }
    auto after = []() {
        // Queued after every snapshot insert (one guildmaster writer), so it sees this batch's rows.
        BotDumps::Instance().RunRetention();
        GuildmasterDatabase.Execute("UPDATE world_status SET last_snapshot_all_at = UNIX_TIMESTAMP() WHERE id = 1");
    };
    // Guild ids come from the registry, so this query names no other database (spec §2b: per world).
    BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
        "SELECT guid FROM guild_member WHERE guildid IN ({}, {})", user, test)).WithCallback(
        [orderId, after, finish = std::move(finish)](QueryResult result) {
            // No guilds yet: nothing to take, the retention still runs.
            SnapshotBatch(Guids(result), "scheduled", orderId, finish, after);
        }));
}

void SimpleOrders::Update(uint32 /*diff*/)
{
    ++g_tick;
    uint32 budget = BridgeConfig::Get().snapshotPerTick;
    while (budget && !g_batches.empty())
    {
        std::shared_ptr<Batch> batch = g_batches.front();
        if (batch->lastTick != g_tick)
        {
            batch->lastTick = g_tick;
            ++batch->ticks;
        }
        uint32 const guid = batch->guids[batch->next++];
        if (batch->next == batch->guids.size())
            g_batches.pop_front();
        --budget;
        BotDumps::Instance().Request(guid, batch->reason, batch->orderId,
                                     [batch, guid](bool ok, std::string const&, std::string const&) {
                                         (ok ? batch->bots : batch->failed).push_back(guid);
                                         if (++batch->answered == batch->guids.size())
                                             Complete(*batch);
                                     });
    }
}

uint32 SimpleOrders::QueuedSnapshots()
{
    std::size_t queued = 0;
    for (auto const& batch : g_batches)
        queued += batch->guids.size() - batch->next;
    return static_cast<uint32>(queued);
}
