/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "NewGameOrders.h"

#include "BridgeAsync.h"
#include "CharacterCache.h"
#include "CharacterMaker.h"
#include "DatabaseEnv.h"
#include "EventPayloads.h"
#include "EventSink.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Json.h"
#include "Log.h"
#include "NewGameRules.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "QueryCallback.h"
#include "StringFormat.h"
#include "World.h"
#include <algorithm>
#include <ctime>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace GuildBridge;

namespace
{
constexpr uint32 RowWaitStepMs = 2000;
constexpr uint32 RowWaitLimitMs = 60000;
constexpr uint32 PlaceStepMs = 1000;
constexpr uint32 FoundersLimitMs = 15 * 60 * 1000;  // founders normally log in within a minute

// Characters made this tick reach the database a moment later (async save). Wait for all of them.
struct RowWait
{
    std::vector<uint32> guids;
    uint32 waitedMs = 0;
    uint32 stepMs = 0;
    bool querying = false;
    bool finished = false;
    std::function<void(bool ok)> done;
};

// A founder made but not in its guild yet: placed as soon as it has logged in.
struct WaitingFounder
{
    uint32 guid = 0;
    uint32 guildId = 0;
};

// A create_founders order waiting for its founders to be placed.
struct FounderOrder
{
    std::vector<uint32> guids;
    std::vector<std::string> names;
    uint32 guildId = 0;
    uint32 waitedMs = 0;
    NewGameOrders::Finish finish;
};

std::vector<std::shared_ptr<RowWait>> waits;
std::vector<WaitingFounder> unplaced;
std::vector<FounderOrder> founderOrders;
uint32 placeTimerMs = 0;

uint32 Now() { return static_cast<uint32>(std::time(nullptr)); }

void WaitForRows(std::vector<uint32> guids, std::function<void(bool)> done)
{
    auto wait = std::make_shared<RowWait>();
    wait->guids = std::move(guids);
    wait->done = std::move(done);
    waits.push_back(std::move(wait));
}

// The world's own name rules, after the pure shape check: allowed, not reserved.
bool GameNameAllowed(std::string const& name)
{
    return ObjectMgr::CheckPlayerName(name, true) == CHAR_NAME_SUCCESS && !sObjectMgr->IsReservedName(name);
}

std::string GameNameProblem(std::string const& name)
{
    if (!GameNameAllowed(name))
        return "bad name: " + name;
    if (sCharacterCache->GetCharacterGuidByName(name))
        return "name taken: " + name;
    return "";
}

std::string JoinNames(std::vector<std::string> const& names)
{
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i)
        out += (i ? ", " : "") + names[i];
    return out;
}

bool IsPlaced(uint32 guid)
{
    return std::none_of(unplaced.begin(), unplaced.end(), [guid](WaitingFounder const& u) { return u.guid == guid; });
}

// One founder that has logged in (and finished playerbots' login steps) joins its guild; true when it is settled
// (placed, or dropped because its guild is gone).
bool TryPlace(WaitingFounder const& founder)
{
    Guild* guild = GuildRegistry::Instance().RoleOf(founder.guildId) != GuildRole::None
                       ? sGuildMgr->GetGuildById(founder.guildId)
                       : nullptr;
    if (!guild)
    {
        LOG_ERROR("module.guildbridge", "GUILDBRIDGE founder {}: guild {} is gone; it stays a guildless population bot",
                  founder.guid, founder.guildId);
        PlayerbotsAdapter::SetJoinsBotGuild(founder.guid, true);
        return true;
    }
    ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(founder.guid);
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (!player || !player->IsInWorld() || !PlayerbotsAdapter::IsMasterlessLoggedIn(guid))
        return false;
    if (player->GetGuildId() == founder.guildId)
    {
        PlayerbotsAdapter::SetJoinsBotGuild(founder.guid, true);
        return true;
    }
    if (uint32 const strayId = player->GetGuildId())
        if (Guild* stray = sGuildMgr->GetGuildById(strayId))
        {
            // Should not happen (the fork keeps a waiting founder out of bot guilds); never leave it there.
            LOG_WARN("module.guildbridge", "GUILDBRIDGE founder {} was in guild {}; moving it to {}", player->GetName(),
                     strayId, founder.guildId);
            stray->DeleteMember(guid, false, false, true);
        }
    if (player->GetGuildId() || !guild->AddMember(guid, GUILD_RANK_NONE))
    {
        LOG_ERROR("module.guildbridge", "GUILDBRIDGE founder {} could not join guild {}; trying again",
                  player->GetName(), founder.guildId);
        return false;
    }
    PlayerbotsAdapter::SetJoinsBotGuild(founder.guid, true);
    Guild::Member const* member = guild->GetMember(guid);
    EventSink::Instance().Push(EventType::GuildJoin, founder.guid, founder.guildId, 0,
                               GuildJoinPayload(guild->GetName(), member ? member->GetRankId() : 0));
    LOG_INFO("module.guildbridge", "GUILDBRIDGE founder {} joined {}", player->GetName(), guild->GetName());
    return true;
}

void UpdateRowWaits(uint32 diff)
{
    std::vector<std::shared_ptr<RowWait>> const current = waits;  // a callback may add a wait while we loop
    for (auto const& wait : current)
    {
        if (wait->finished)
            continue;
        wait->waitedMs += diff;
        wait->stepMs += diff;
        if (wait->querying || wait->stepMs < RowWaitStepMs)
            continue;
        wait->stepMs = 0;
        if (wait->waitedMs > RowWaitLimitMs)
        {
            wait->finished = true;
            wait->done(false);
            continue;
        }
        std::ostringstream in;
        for (std::size_t i = 0; i < wait->guids.size(); ++i)
            in << (i ? "," : "") << wait->guids[i];
        wait->querying = true;
        uint64 const want = wait->guids.size();
        std::string const sql = "SELECT COUNT(*) FROM characters WHERE guid IN (" + in.str() + ")";
        BridgeAsync::Add(CharacterDatabase.AsyncQuery(sql)
                             .WithCallback([wait, want](QueryResult result) {
                                 wait->querying = false;
                                 if (wait->finished || !result || (*result)[0].Get<uint64>() != want)
                                     return;
                                 wait->finished = true;
                                 wait->done(true);
                             }));
    }
    waits.erase(std::remove_if(waits.begin(), waits.end(),
                               [](auto const& wait) { return wait->finished && !wait->querying; }),
                waits.end());
}

void UpdatePlacements(uint32 diff)
{
    placeTimerMs += diff;
    if (placeTimerMs < PlaceStepMs)
        return;
    uint32 const step = placeTimerMs;
    placeTimerMs = 0;
    if (!unplaced.empty())
    {
        std::vector<WaitingFounder> const current = unplaced;
        unplaced.clear();
        for (WaitingFounder const& founder : current)
            if (!TryPlace(founder))
                unplaced.push_back(founder);
    }

    std::vector<FounderOrder> pending;
    pending.swap(founderOrders);
    for (FounderOrder& order : pending)
    {
        order.waitedMs += step;
        std::vector<std::string> waiting;
        for (std::size_t i = 0; i < order.guids.size(); ++i)
            if (!IsPlaced(order.guids[i]))
                waiting.push_back(order.names[i]);
        Guild* guild = sGuildMgr->GetGuildById(order.guildId);
        std::string const guildName = guild ? guild->GetName() : "the guild";
        if (waiting.empty())
        {
            std::string list = "[";
            for (std::size_t i = 0; i < order.guids.size(); ++i)
                list += (i ? "," : "") + JsonObject().Str("name", order.names[i]).UInt("guid", order.guids[i]).Build();
            list += "]";
            order.finish({true, std::to_string(order.guids.size()) + " founder(s) joined " + guildName,
                          JsonObject().Raw("founders", list).Build()});
        }
        else if (order.waitedMs > FoundersLimitMs)
            order.finish({false, "founders made but not online yet: " + JoinNames(waiting) + " (they join " +
                                     guildName + " when they log in)", ""});
        else
            founderOrders.push_back(std::move(order));
    }
}
}  // namespace

void NewGameOrders::LoadAtStartup()
{
    unplaced.clear();
    if (!GuildmasterDatabaseReady)
        return;
    // Startup only (sync): founders whose guild placement a restart cut short. The fork's "no_bot_guild" value says
    // which still wait; a founder placed and later removed by the player is not put back.
    if (QueryResult result =
            GuildmasterDatabase.Query("SELECT guid, guild_id FROM bot_profiles WHERE origin = 'founder'"))
        do
        {
            WaitingFounder founder{(*result)[0].Get<uint32>(), (*result)[1].Get<uint32>()};
            ObjectGuid const guid = ObjectGuid::Create<HighGuid::Player>(founder.guid);
            CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(guid);
            if (cache && !cache->GuildId && !PlayerbotsAdapter::JoinsBotGuild(founder.guid))
                unplaced.push_back(founder);
        } while (result->NextRow());
    if (!unplaced.empty())
        LOG_INFO("module.guildbridge", "GUILDBRIDGE {} founder(s) still to be placed in their guild", unplaced.size());
}

std::size_t NewGameOrders::Unplaced() { return unplaced.size(); }

void NewGameOrders::Update(uint32 diff)
{
    UpdateRowWaits(diff);
    UpdatePlacements(diff);
}

void NewGameOrders::CreateGuild(ParsedOrder const& order, Finish finish)
{
    GuildRole const role = GuildRegistry::RoleFromName(order.role);
    if (GuildRegistry::Instance().GuildIdFor(role))
        return finish({false, "a " + order.role + " guild already exists", ""});
    if (!ObjectMgr::IsValidCharterName(order.guildName))
        return finish({false, "bad value for guild_name", ""});
    if (sGuildMgr->GetGuildByName(order.guildName))
        return finish({false, "a guild with that name exists", ""});
    if (!GameNameAllowed(order.leaderName))
        return finish({false, "bad name: " + order.leaderName, ""});
    Faction faction = Faction::Alliance;
    FactionFromName(order.faction, faction);
    uint8 const race = order.leaderRace ? order.leaderRace : DefaultLeaderRace(faction);

    std::string const accountName = GuildRegistry::Instance().LeaderAccountName(role);
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT_ID_BY_USERNAME);
    stmt->SetData(0, accountName);
    std::string const guildName = order.guildName;
    std::string const leaderName = order.leaderName;
    std::string const factionName = order.faction;
    bool const dryRun = order.dryRun;
    BridgeAsync::Add(LoginDatabase.AsyncQuery(stmt).WithCallback([=](PreparedQueryResult result) {
        if (!result)
            return finish({false, "no leader account " + accountName, ""});
        uint32 const account = (*result)[0].Get<uint32>();
        // A leader this order already made, left guildless by a restart before the guild was founded, is used again
        // (so the controller can simply send create_guild again). Anything else with that name is taken.
        uint32 reuse = 0;
        if (ObjectGuid const taken = sCharacterCache->GetCharacterGuidByName(leaderName))
        {
            CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(taken);
            Faction leaderFaction = Faction::Alliance;
            if (!cache || cache->AccountId != account || cache->GuildId || cache->Level != 1 ||
                cache->Class != CLASS_WARRIOR || !RaceFaction(cache->Race, leaderFaction) || leaderFaction != faction)
                return finish({false, "name taken: " + leaderName, ""});
            reuse = taken.GetCounter();
        }
        if (dryRun)
            return finish({true, "dry run: would create " + guildName + " led by " + leaderName, ""});

        auto found = [=](uint32 leader) {
            std::string const refused = GuildRegistry::Instance().BeginCreate(
                role, guildName, leaderName, [=](uint32 guildId, std::string const& why) {
                    if (!guildId)
                        return finish({false, why, ""});
                    EventSink::Instance().Push(EventType::GuildFounded, leader, guildId, 0,
                                               GuildFoundedPayload(guildName, factionName, leaderName));
                    finish({true, guildName + " founded, led by " + leaderName,
                            JsonObject().UInt("guild_id", guildId).UInt("leader_guid", leader).Build()});
                });
            if (!refused.empty())
                finish({false, refused, ""});
        };
        if (reuse)
        {
            LOG_INFO("module.guildbridge", "GUILDBRIDGE create_guild {}: using the leader {} made earlier", guildName,
                     leaderName);
            return found(reuse);
        }
        std::string error;
        uint32 const leader = CharacterMaker::Create(account, leaderName, race, CLASS_WARRIOR, 2, error);
        if (!leader)
            return finish({false, "could not create the leader: " + error, ""});
        WaitForRows({leader}, [=](bool ok) {
            if (!ok)
                return finish({false, "the leader's character was not saved in time", ""});
            found(leader);
        });
    }));
}

void NewGameOrders::CreateFounders(ParsedOrder const& order, Finish finish)
{
    uint32 const guildId = order.guildId;
    if (GuildRegistry::Instance().RoleOf(guildId) == GuildRole::None || !sGuildMgr->GetGuildById(guildId))
        return finish({false, "no such guild (it must be the user or the test guild)", ""});
    uint64 const orderId = order.id;
    bool const dryRun = order.dryRun;
    // Each founder's fields as text, read with JSON_TABLE. A field of the wrong JSON type reads as "" (then
    // ParseFounders refuses it); traits come back as MySQL prints the array, and ParseFounders checks them.
    auto text = [](char const* column) {
        return Acore::StringFormat("IF(JSON_TYPE(f.{0}) IN ('STRING', 'INTEGER', 'UNSIGNED INTEGER'), "
                                   "JSON_UNQUOTE(f.{0}), '')",
                                   column);
    };
    std::string const sql = Acore::StringFormat(
        "SELECT {}, {}, {}, {}, "
        "CASE WHEN f.professions IS NULL OR JSON_TYPE(f.professions) = 'NULL' THEN '0' "
        "WHEN JSON_TYPE(f.professions) = 'ARRAY' THEN CAST(JSON_LENGTH(f.professions) AS CHAR) ELSE 'x' END, "
        "IFNULL(JSON_UNQUOTE(JSON_EXTRACT(f.professions, '$[0]')), ''), "
        "IFNULL(JSON_UNQUOTE(JSON_EXTRACT(f.professions, '$[1]')), ''), IFNULL(JSON_TYPE(f.traits), ''), "
        "IFNULL(CAST(f.traits AS CHAR), ''), {} "
        "FROM orders o, JSON_TABLE(o.params, '$.founders[*]' COLUMNS (idx FOR ORDINALITY, "
        "name JSON PATH '$.name', race JSON PATH '$.race', class JSON PATH '$.class', gender JSON PATH '$.gender', "
        "professions JSON PATH '$.professions', traits JSON PATH '$.traits', backstory JSON PATH '$.backstory')) f "
        "WHERE o.id = {} ORDER BY f.idx",
        text("name"), text("race"), text("class"), text("gender"), text("backstory"), orderId);
    BridgeAsync::Add(GuildmasterDatabase.AsyncQuery(sql).WithCallback([=](QueryResult result) {
        Guild* guild = sGuildMgr->GetGuildById(guildId);
        if (!guild)
            return finish({false, "no such guild (it must be the user or the test guild)", ""});
        std::vector<FounderRow> rows;
        if (result)
            do
            {
                Field* f = result->Fetch();
                rows.push_back({f[0].Get<std::string>(), f[1].Get<std::string>(), f[2].Get<std::string>(),
                                f[3].Get<std::string>(), f[4].Get<std::string>(), f[5].Get<std::string>(),
                                f[6].Get<std::string>(), f[7].Get<std::string>(), f[8].Get<std::string>(),
                                f[9].Get<std::string>()});
            } while (result->NextRow());
        CharacterCacheEntry const* leader = sCharacterCache->GetCharacterCacheByGuid(guild->GetLeaderGUID());
        Faction faction = Faction::Alliance;
        if (!leader || !RaceFaction(leader->Race, faction))
            return finish({false, "no such guild (its leader is missing)", ""});
        std::vector<Founder> founders;
        std::string problem = ParseFounders(rows, faction, founders);
        for (std::size_t i = 0; problem.empty() && i < founders.size(); ++i)
            problem = GameNameProblem(founders[i].name);
        if (!problem.empty())
            return finish({false, problem, ""});

        // This world's founder accounts (preflight C1), one founder each, the emptiest first.
        std::vector<uint32> const accounts = GuildRegistry::Instance().FounderAccounts();
        if (accounts.empty())
            return finish({false, "no founder accounts (GuildBridge.FounderAccounts is empty)", ""});
        std::ostringstream in;
        for (std::size_t i = 0; i < accounts.size(); ++i)
            in << (i ? "," : "") << accounts[i];
        BridgeAsync::Add(CharacterDatabase.AsyncQuery("SELECT account, COUNT(*) FROM characters WHERE account IN (" +
                                                      in.str() + ") GROUP BY account")
                             .WithCallback([=](QueryResult counts) {
            std::unordered_map<uint32, uint32> used;
            if (counts)
                do
                    used[(*counts)[0].Get<uint32>()] = static_cast<uint32>((*counts)[1].Get<uint64>());
                while (counts->NextRow());
            std::vector<std::pair<uint32, uint32>> list;
            for (uint32 account : accounts)
                list.emplace_back(account, used[account]);
            std::vector<uint32> picked;
            uint32 const perAccount = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_REALM);
            if (!PickFounderAccounts(list, perAccount, founders.size(), picked))
            {
                auto const free = std::count_if(list.begin(), list.end(),
                                                [perAccount](auto const& a) { return a.second < perAccount; });
                return finish({false, "no free founder account slot (" + std::to_string(free) + " free, " +
                                          std::to_string(founders.size()) + " needed)", ""});
            }
            Guild* target = sGuildMgr->GetGuildById(guildId);
            if (!target)
                return finish({false, "no such guild (it was disbanded meanwhile)", ""});
            if (dryRun)
                return finish({true, "dry run: would create " + std::to_string(founders.size()) + " founder(s) in " +
                                         target->GetName(), ""});
            // The names again: one may have been taken while we read the accounts. Still nothing made.
            for (Founder const& f : founders)
                if (std::string const taken = GameNameProblem(f.name); !taken.empty())
                    return finish({false, taken, ""});

            std::vector<uint32> guids;
            std::vector<std::string> names;
            for (std::size_t i = 0; i < founders.size(); ++i)
            {
                Founder const& f = founders[i];
                std::string error;
                uint32 const guid = CharacterMaker::Create(picked[i], f.name, f.race, f.cls, f.gender, error);
                if (!guid)
                {
                    // Every check passed, so this is unexpected: stop, and say exactly what exists (preflight D26).
                    LOG_ERROR("module.guildbridge", "create_founders {}: {}", orderId, error);
                    return finish({false, "could not create " + f.name + ": " + error +
                                              (names.empty() ? "" : " (made, not in the population: " +
                                                                        JoinNames(names) + ")"),
                                   ""});
                }
                guids.push_back(guid);
                names.push_back(f.name);
            }
            WaitForRows(guids, [=](bool ok) {
                if (!ok)
                    return finish({false, "the founders' characters were not saved in time", ""});
                FounderOrder pending;
                pending.guildId = guildId;
                pending.finish = finish;
                for (std::size_t i = 0; i < founders.size(); ++i)
                {
                    Founder const& f = founders[i];
                    uint32 const guid = guids[i];
                    // A population bot from now on (logged in and kept by playerbots); it waits guildless at login
                    // and joins the guild as soon as it is in the world.
                    PlayerbotsAdapter::AddToPopulation(guid, false);
                    if (f.prof1)
                        PlayerbotsAdapter::PresetProfessions(guid, f.prof1, f.prof2);
                    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_REP_PROFILE);
                    stmt->SetData(0, guid);
                    stmt->SetData(1, guildId);
                    stmt->SetData(2, std::string("founder"));
                    stmt->SetData(3, f.traitsJson);
                    stmt->SetData(4, f.backstory);
                    stmt->SetData(5, orderId);
                    stmt->SetData(6, Now());
                    GuildmasterDatabase.Execute(stmt);
                    unplaced.push_back({guid, guildId});
                    pending.guids.push_back(guid);
                    pending.names.push_back(f.name);
                }
                founderOrders.push_back(std::move(pending));
            });
        }));
    }));
}
