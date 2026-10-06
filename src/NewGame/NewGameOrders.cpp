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
#include "RestoreMgr.h"
#include "QueryCallback.h"
#include "StringFormat.h"
#include "World.h"
#include <algorithm>
#include <ctime>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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
// Founder profiles (bot_profiles origin founder): guid -> guild. Loaded at startup, kept as orders write them.
std::unordered_map<uint32, uint32> founderProfiles;
// Founders to add to the population once the fork has loaded it (its first update: GetBots() loads the "add"
// records only while its set is empty, so nothing may be added before that).
std::vector<uint32> toAdd;
uint32 addWaitMs = 0;
// create_founders orders still running when the server went down (read before the order runner fails them). Only
// the founders they journaled in bot_profiles are settled at boot (re-reviews N1).
std::unordered_set<uint64> cutOrders;

uint32 Now() { return static_cast<uint32>(std::time(nullptr)); }

// Written right after the founder's character is made (final review I1): from then on a restart or a late save can
// no longer lose it; the next boot or the order sent again finishes it (SettleFounderAtBoot, CanReuseFounder).
void WriteProfile(uint32 guid, uint32 guildId, Founder const& f, uint64 orderId)
{
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_REP_PROFILE);
    stmt->SetData(0, guid);
    stmt->SetData(1, guildId);
    stmt->SetData(2, std::string("founder"));
    stmt->SetData(3, f.traitsJson);
    stmt->SetData(4, f.backstory);
    stmt->SetData(5, orderId);
    stmt->SetData(6, Now());
    GuildmasterDatabase.Execute(stmt);
    founderProfiles[guid] = guildId;
}

// A population bot from now on (logged in and kept by playerbots); it waits guildless at login until it is placed.
void JoinPopulation(uint32 guid)
{
    if (PlayerbotsAdapter::IsRandomBot(guid) || std::find(toAdd.begin(), toAdd.end(), guid) != toAdd.end())
        return;
    if (PlayerbotsAdapter::PopulationSize())
        PlayerbotsAdapter::AddToPopulation(guid, false);
    else
        toAdd.push_back(guid);
}

void Unplace(uint32 guid, uint32 guildId)
{
    if (std::none_of(unplaced.begin(), unplaced.end(), [guid](WaitingFounder const& u) { return u.guid == guid; }))
        unplaced.push_back({guid, guildId});
}

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

// The world's checks on a founder's name ("bad name", "name taken"), except that a character an earlier send of
// create_founders made for this guild is taken back (`reuse` = its guid) instead of "name taken" (final review I1).
std::string FounderNameProblem(Founder const& f, uint32 guildId, uint32& reuse)
{
    reuse = 0;
    if (!GameNameAllowed(f.name))
        return "bad name: " + f.name;
    ObjectGuid const taken = sCharacterCache->GetCharacterGuidByName(f.name);
    if (!taken)
        return "";
    CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(taken);
    if (!cache)
        return "name taken: " + f.name;
    std::vector<uint32> const accounts = GuildRegistry::Instance().FounderAccounts();
    ExistingCharacter existing;
    existing.onFounderAccount = std::find(accounts.begin(), accounts.end(), cache->AccountId) != accounts.end();
    existing.guildId = cache->GuildId;
    existing.race = cache->Race;
    existing.cls = cache->Class;
    existing.gender = cache->Sex;
    auto const profile = founderProfiles.find(taken.GetCounter());
    existing.profileGuild = profile == founderProfiles.end() ? 0 : profile->second;
    if (!CanReuseFounder(existing, f, guildId))
        return "name taken: " + f.name;
    reuse = taken.GetCounter();
    return "";
}

std::string JoinNames(std::vector<std::string> const& names)
{
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i)
        out += (i ? ", " : "") + names[i];
    return out;
}

// Founders this send made that are not finished (D26 and late saves): their profiles are written, so they are not lost.
std::string FinishLater(std::vector<std::string> const& made)
{
    return made.empty() ? ""
                        : " (made: " + JoinNames(made) + "; send the order again to finish them)";
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

void NewGameOrders::NoteCutOrders()
{
    cutOrders.clear();
    if (!GuildmasterDatabaseReady)
        return;
    // Startup only (sync), before OrderRunner::RecoverAtStartup fails them as interrupted.
    if (QueryResult result = GuildmasterDatabase.Query(
            "SELECT id FROM orders WHERE status = 'running' AND type = 'create_founders'"))
        do
            cutOrders.insert((*result)[0].Get<uint64>());
        while (result->NextRow());
}

void NewGameOrders::LoadAtStartup()
{
    unplaced.clear();
    founderProfiles.clear();
    toAdd.clear();
    addWaitMs = 0;
    if (!GuildmasterDatabaseReady)
        return;
    // Startup only (sync), after restore recovery has queued its jobs (RestoreMgr::RecoverAtStartup). Founder
    // profiles, the characters on this world's founder accounts, their "add" records and raisings: the founders a
    // create_founders order the boot found running journaled in bot_profiles are finished, or their profile dropped
    // if the character never saved (final review I1); everything else that looks unfinished (a raised founder, a
    // restore, a raising's new death knight, a late save, a character cut before its profile) only gets a warning.
    // No character is ever deleted here (re-reviews N1, SettleFounderAtBoot). The fork's "no_bot_guild" value says which finished founders still wait
    // for their guild; a founder placed and later removed by the player is not put back.
    struct Profile
    {
        uint32 guid, guildId;
        uint64 orderId;
    };
    std::vector<Profile> profiles;
    if (QueryResult result = GuildmasterDatabase.Query(
            "SELECT guid, guild_id, IFNULL(order_id, 0) FROM bot_profiles WHERE origin = 'founder'"))
        do
            profiles.push_back({(*result)[0].Get<uint32>(), (*result)[1].Get<uint32>(), (*result)[2].Get<uint64>()});
        while (result->NextRow());
    struct OnAccount
    {
        uint32 guid, account;
        std::string name;
    };
    std::vector<OnAccount> onAccounts;
    std::vector<uint32> const accounts = GuildRegistry::Instance().FounderAccounts();
    if (!accounts.empty())
    {
        std::string in;
        for (uint32 account : accounts)
            in += (in.empty() ? "" : ",") + std::to_string(account);
        if (QueryResult result = CharacterDatabase.Query(
                "SELECT guid, account, name FROM characters WHERE account IN (" + in + ")"))
            do
                onAccounts.push_back({(*result)[0].Get<uint32>(), (*result)[1].Get<uint32>(),
                                      (*result)[2].Get<std::string>()});
            while (result->NextRow());
    }
    std::vector<uint32> guids;
    for (Profile const& p : profiles)
        guids.push_back(p.guid);
    for (OnAccount const& c : onAccounts)
        guids.push_back(c.guid);
    std::unordered_set<uint32> const added = PlayerbotsAdapter::WithPopulationRecord(guids);
    std::unordered_set<uint32> const raised = PlayerbotsAdapter::InRaisings(guids);
    auto facts = [&](uint32 guid, bool hasProfile, bool exists, bool fromCut) {
        FounderBootFacts f;
        f.hasProfile = hasProfile;
        f.characterExists = exists;
        f.hasAddRecord = added.count(guid) != 0;
        f.fromCutOrder = fromCut;
        f.raising = raised.count(guid) != 0;
        f.restoring = RestoreMgr::Instance().IsRestoring(guid);
        return f;
    };

    for (Profile const& p : profiles)
    {
        uint32 const guid = p.guid;
        CharacterCacheEntry const* cache =
            sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
        founderProfiles[guid] = p.guildId;  // a raised founder's profile follows its legend: always kept
        switch (SettleFounderAtBoot(facts(guid, true, cache != nullptr, cutOrders.count(p.orderId) != 0)))
        {
            case FounderBootAction::DropProfile:
                // Its cut order's character never reached the database. The guid may be handed out again: forget
                // the profile and the profession preset made for it.
                founderProfiles.erase(guid);
                GuildmasterDatabase.Execute(Acore::StringFormat(
                    "DELETE FROM bot_profiles WHERE guid = {} AND origin = 'founder'", guid));
                PlayerbotsAdapter::PresetProfessions(guid, 0, 0);
                LOG_WARN("module.guildbridge", "GUILDBRIDGE founder guid {} of cut order {} was never saved: profile "
                         "removed", guid, p.orderId);
                break;
            case FounderBootAction::Adopt:
                PlayerbotsAdapter::LoadStoredValues(guid);  // keeps its profession preset visible after the add
                toAdd.push_back(guid);  // added once the population is loaded (Update)
                if (!cache->GuildId)
                    unplaced.push_back({guid, p.guildId});
                LOG_WARN("module.guildbridge", "GUILDBRIDGE founder {} was made but not finished: added to the population "
                         "and placed in guild {} once it logs in", cache->Name, p.guildId);
                break;
            case FounderBootAction::Keep:
                if (cache && !cache->GuildId && !PlayerbotsAdapter::JoinsBotGuild(guid))
                    unplaced.push_back({guid, p.guildId});
                break;
            default:
                LOG_WARN("module.guildbridge", "GUILDBRIDGE founder guid {} looks unfinished (character {}, add record "
                         "{}, raising {}, restoring {}); not from a cut create_founders order, so it is left as it is",
                         guid, cache ? 1 : 0, added.count(guid) ? 1 : 0, raised.count(guid) ? 1 : 0,
                         RestoreMgr::Instance().IsRestoring(guid) ? 1 : 0);
                break;
        }
    }
    for (OnAccount const& c : onAccounts)
    {
        if (std::any_of(profiles.begin(), profiles.end(), [&c](Profile const& p) { return p.guid == c.guid; }))
            continue;
        // Never deleted (re-review 2): a character without a profile may be a restored founder under a new guid or a
        // founder cut before its profile was written; the order sent again takes the latter back by name.
        if (!added.count(c.guid))
            LOG_WARN("module.guildbridge", "GUILDBRIDGE founder account character {} (guid {}) has no founder profile "
                     "and no add record; left as it is (send create_founders again to take it back)", c.name, c.guid);
    }
    if (!unplaced.empty())
        LOG_INFO("module.guildbridge", "GUILDBRIDGE {} founder(s) still to be placed in their guild", unplaced.size());
}

std::size_t NewGameOrders::Unplaced() { return unplaced.size(); }

void NewGameOrders::Update(uint32 diff)
{
    if (!toAdd.empty())
    {
        addWaitMs += diff;
        if (PlayerbotsAdapter::PopulationSize())
        {
            for (uint32 guid : toAdd)
                if (!PlayerbotsAdapter::IsRandomBot(guid))
                    PlayerbotsAdapter::AddToPopulation(guid, false);
            LOG_INFO("module.guildbridge", "GUILDBRIDGE {} founder(s) added to the population", toAdd.size());
            toAdd.clear();
        }
        else if (addWaitMs > 300000)
        {
            LOG_WARN("module.guildbridge", "GUILDBRIDGE {} founder(s) still wait for the population to load",
                     toAdd.size());
            addWaitMs = 0;
        }
    }
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
    // A plain query: the core prepares LOGIN_GET_ACCOUNT_ID_BY_USERNAME for sync connections only.
    std::string escaped = accountName;
    LoginDatabase.EscapeString(escaped);
    std::string const guildName = order.guildName;
    std::string const leaderName = order.leaderName;
    std::string const factionName = order.faction;
    bool const dryRun = order.dryRun;
    std::string const sql = Acore::StringFormat("SELECT id FROM account WHERE username = '{}'", escaped);
    BridgeAsync::Add(LoginDatabase.AsyncQuery(sql).WithCallback([=](QueryResult result) {
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
    bool const cut = order.testFail == "cut";
    if (!order.testFail.empty() && GuildRegistry::Instance().RoleOf(guildId) != GuildRole::Test)
        return finish({false, "test seams only work on test-guild bots", ""});
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
        // reuse[i]: the guid of a founder an earlier send already made (taken back, not made again).
        std::vector<uint32> reuse(founders.size(), 0);
        for (std::size_t i = 0; problem.empty() && i < founders.size(); ++i)
            problem = FounderNameProblem(founders[i], guildId, reuse[i]);
        if (!problem.empty())
            return finish({false, problem, ""});
        std::size_t const toMake = static_cast<std::size_t>(std::count(reuse.begin(), reuse.end(), 0u));

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
            if (!PickFounderAccounts(list, perAccount, toMake, picked))
            {
                auto const free = std::count_if(list.begin(), list.end(),
                                                [perAccount](auto const& a) { return a.second < perAccount; });
                return finish({false, "no free founder account slot (" + std::to_string(free) + " free, " +
                                          std::to_string(toMake) + " needed)", ""});
            }
            Guild* target = sGuildMgr->GetGuildById(guildId);
            if (!target)
                return finish({false, "no such guild (it was disbanded meanwhile)", ""});
            std::size_t const taken = founders.size() - toMake;
            if (dryRun)
                return finish({true, "dry run: would create " + std::to_string(founders.size()) + " founder(s) in " +
                                         target->GetName() +
                                         (taken ? " (" + std::to_string(taken) + " made before, taken back)" : ""),
                               ""});
            // The names again: one may have been taken while we read the accounts. Still nothing made.
            for (std::size_t i = 0; i < founders.size(); ++i)
            {
                uint32 again = 0;
                if (std::string const why = FounderNameProblem(founders[i], guildId, again); !why.empty())
                    return finish({false, why, ""});
                if (again != reuse[i])
                    return finish({false, "name taken: " + founders[i].name, ""});
            }

            std::vector<uint32> guids;
            std::vector<std::string> made;  // made by this send
            std::size_t nextAccount = 0;
            for (std::size_t i = 0; i < founders.size(); ++i)
            {
                Founder const& f = founders[i];
                uint32 guid = reuse[i];
                if (!guid)
                {
                    std::string error;
                    guid = CharacterMaker::Create(picked[nextAccount++], f.name, f.race, f.cls, f.gender, error);
                    if (!guid)
                    {
                        // Every check passed, so this is unexpected: stop, and say exactly what exists (preflight D26).
                        LOG_ERROR("module.guildbridge", "create_founders {}: {}", orderId, error);
                        return finish({false, "could not create " + f.name + ": " + error + FinishLater(made), ""});
                    }
                    made.push_back(f.name);
                    if (cut && i)
                        continue;  // test seam: the restart lands before this founder's profile is written
                }
                else
                    LOG_INFO("module.guildbridge", "create_founders {}: taking back {} (made by an earlier send)",
                             orderId, f.name);
                guids.push_back(guid);
                // Durable at once (final review I1): a restart from here on cannot lose the founder.
                WriteProfile(guid, guildId, f, orderId);
                if (f.prof1)
                    PlayerbotsAdapter::PresetProfessions(guid, f.prof1, f.prof2);
            }
            if (cut)
            {
                LOG_WARN("module.guildbridge", "create_founders {} test seam: stopping before the founders are finished "
                         "until the worldserver restarts", orderId);
                return;
            }
            WaitForRows(guids, [=](bool ok) {
                if (!ok)
                    return finish({false, "the founders' characters were not saved in time" + FinishLater(made), ""});
                FounderOrder pending;
                pending.guildId = guildId;
                pending.finish = finish;
                for (std::size_t i = 0; i < founders.size(); ++i)
                {
                    uint32 const guid = guids[i];
                    JoinPopulation(guid);
                    CharacterCacheEntry const* cache =
                        sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
                    if (!cache || cache->GuildId != guildId)
                        Unplace(guid, guildId);
                    pending.guids.push_back(guid);
                    pending.names.push_back(founders[i].name);
                }
                founderOrders.push_back(std::move(pending));
            });
        }));
    }));
}
