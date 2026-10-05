/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "DungeonRunMgr.h"

#include "BotDumps.h"
#include "BridgeConfig.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "DcAdapter.h"
#include "DcRecord.h"
#include "EventPayloads.h"
#include "EventSink.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Json.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "RestoreMgr.h"
#include "RunRegistry.h"
#include "StringFormat.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>

using namespace GuildBridge;

namespace
{
char const* const ROLES[5] = {"tank", "heal", "dps", "dps", "dps"};
uint32 Now() { return static_cast<uint32>(std::time(nullptr)); }

// A run member as the run sees it: nullptr = offline (logged out). A bot between maps (a far teleport) is still
// connected but not in the world: callers wait for it (Task 9 M1: never call that offline).
Player* Connected(uint32 guid)
{
    return ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
}

// Off the world thread: the last 4 MB of the results file (a run's line is well under that).
std::string ReadTail(std::string path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return "";
    std::streamoff const size = in.tellg();
    std::streamoff const from = size > (4 << 20) ? size - (4 << 20) : 0;
    in.seekg(from);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool NearSpot(Player* bot, Spot const& spot, float radius)
{
    return bot->IsInWorld() && bot->GetMapId() == spot.map && bot->GetExactDist2d(spot.x, spot.y) < radius;
}
}  // namespace

DungeonRunMgr& DungeonRunMgr::Instance()
{
    static DungeonRunMgr instance;
    return instance;
}

// Startup only (sync queries are allowed before the world runs; preflight D7).
void DungeonRunMgr::LoadAtStartup()
{
    // The outdoor side of each instance portal: the areatrigger that teleports into the map, placed on a continent.
    if (QueryResult result = WorldDatabase.Query("SELECT ID, target_map FROM areatrigger_teleport ORDER BY ID"))
        do
        {
            uint32 const triggerId = (*result)[0].Get<uint32>();
            uint32 const target = (*result)[1].Get<uint32>();
            AreaTrigger const* trigger = sObjectMgr->GetAreaTrigger(triggerId);
            if (trigger && IsContinent(trigger->map) && !_entrances.count(target))
                _entrances[target] = Spot{trigger->map, trigger->x, trigger->y, trigger->z};
        } while (result->NextRow());
    _maxRuns = BridgeConfig::Get().runMaxConcurrent;
    // Never more runs than mod-dungeon-clear itself starts at once (preflight C9); a cap hit is still retried.
    uint32 const dcCap = DcAdapter::ConcurrentCap();
    if (dcCap && _maxRuns > dcCap)
    {
        LOG_WARN("module.guildbridge", "GuildBridge.Dungeon.MaxConcurrentRuns {} is above mod-dungeon-clear's "
                 "DungeonClear.TestRun.MaxConcurrent {}: using {}", _maxRuns, dcCap, dcCap);
        _maxRuns = dcCap;
    }
    LOG_INFO("module.guildbridge", "GUILDBRIDGE dungeon runs: at most {} at once (mod-dungeon-clear cap: {})", _maxRuns,
             dcCap ? std::to_string(dcCap) : std::string("unlimited"));
    LOG_INFO("module.guildbridge", "GUILDBRIDGE {} dungeon entrances known", _entrances.size());
    if (!GuildmasterDatabaseReady)
        return;

    // A restart ends every run (holds and run tracking are in memory): runs left "running" are abandoned, each
    // with its run_end event (filed under the guild of its run_start event). The order runner fails their orders.
    uint32 const now = Now();
    if (QueryResult cut = GuildmasterDatabase.Query(
            "SELECT r.id, r.started_at, IFNULL((SELECT e.guild_id FROM events e WHERE e.run_id = r.id AND "
            "e.type = 'run_start' LIMIT 1), 0) FROM dungeon_runs r WHERE r.result = 'running'"))
        do
        {
            uint64 const runId = (*cut)[0].Get<uint64>();
            uint32 const startedAt = (*cut)[1].Get<uint32>();
            uint32 const guildId = (*cut)[2].Get<uint32>();
            EventSink::Instance().Push(EventType::RunEnd, 0, guildId, runId,
                                       RunEndPayload(runId, "abandoned", "server restarted", 0, 0,
                                                     now > startedAt ? now - startedAt : 0, 0));
            LOG_WARN("module.guildbridge", "dungeon run {} was cut by a restart: abandoned", runId);
        } while (cut->NextRow());
    GuildmasterDatabase.DirectExecute(Acore::StringFormat(
        "UPDATE dungeon_runs SET result = 'abandoned', fail_reason = 'server restarted', ended_at = {}, "
        "duration_s = IF({} > started_at, {} - started_at, 0) WHERE result = 'running'",
        now, now, now));
}

bool DungeonRunMgr::EntranceFor(uint32 mapId, Spot& out) const
{
    auto const it = _entrances.find(mapId);
    if (it == _entrances.end())
        return false;
    out = it->second;
    return true;
}

DungeonRunMgr::Run* DungeonRunMgr::Find(uint64 runId)
{
    for (auto& run : _runs)
        if (run->id == runId && run->stage != Stage::Done)
            return run.get();
    return nullptr;
}

void DungeonRunMgr::Begin(ParsedOrder const& order, std::function<void(OrderResult const&)> finish)
{
    BridgeConfig const& cfg = BridgeConfig::Get();
    DcAdapter::DungeonInfo const info = DcAdapter::Find(order.dungeon);
    if (!info.found)
        return finish({false, "no such dungeon", ""});
    if (order.heroic && !info.heroicLevel)
        return finish({false, "bad value for heroic (this dungeon has no heroic mode)", ""});
    auto run = std::make_unique<Run>();
    if (!EntranceFor(info.mapId, run->entrance))
        return finish({false, "no outdoor entrance known for this dungeon", ""});
    if (_runs.size() >= _maxRuns)
        return finish({false, "too many dungeon runs in progress (max " + std::to_string(_maxRuns) + ")", ""});

    // Every check before anyone is touched. Membership comes from the character cache, so a non-member is named
    // as such even when it is offline. Busy order (preflight D10): run -> restore -> held -> raising.
    std::vector<Spot> spots;
    std::vector<std::pair<std::string, uint32_t>> levels;
    std::array<uint8_t, 5> classes{};
    std::vector<std::pair<std::string, std::string>> seats;  // name, role by talent spec (as dungeon-clear reads it)
    TeamId team = TEAM_NEUTRAL;
    bool allTest = true;
    for (std::size_t i = 0; i < 5; ++i)
    {
        uint32 const guid = order.party[i];
        CharacterCacheEntry const* cache =
            sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
        if (!cache)
            return finish({false, "no such bot", ""});
        std::string const& name = cache->Name;
        GuildRole const role = GuildRegistry::Instance().RoleOf(cache->GuildId);
        if (role == GuildRole::None)
            return finish({false, "bot is not in our guild: " + name, ""});
        allTest = allTest && role == GuildRole::Test;
        if (RunRegistry::Instance().RunIdFor(guid))  // one run per bot (Q8)
            return finish({false, "bot is in a dungeon run: " + name, ""});
        if (RestoreMgr::Instance().IsRestoring(guid))
            return finish({false, "bot is being restored: " + name, ""});
        if (PlayerbotsAdapter::IsHeld(guid))
            return finish({false, "bot is busy: " + name, ""});
        if (PlayerbotsAdapter::IsRaising(guid))
            return finish({false, "bot is being raised: " + name, ""});
        Player* bot = Connected(guid);
        if (!bot || !bot->IsInWorld())
            return finish({false, "bot is offline: " + name, ""});
        if (!PlayerbotsAdapter::IsBot(bot))
            return finish({false, "a player is logged in on that character: " + name, ""});
        if (!bot->IsAlive())
            return finish({false, "bot is dead: " + name, ""});
        if (bot->IsInCombat())
            return finish({false, "bot is in combat: " + name, ""});
        if (bot->InBattleground() || bot->GetMap()->IsBattlegroundOrArena())
            return finish({false, "bot is in a battleground: " + name, ""});
        if (bot->IsInFlight())
            return finish({false, "bot is on a flight: " + name, ""});
        if (i == 0)
            team = bot->GetTeamId();
        else if (bot->GetTeamId() != team)
            return finish({false, "party spans both factions", ""});
        spots.push_back({bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()});
        levels.emplace_back(name, bot->GetLevel());
        classes[i] = bot->getClass();
        seats.emplace_back(name, PlayerbotsAdapter::SpecRole(bot));
        Member member;
        member.guid = guid;
        member.name = name;
        member.role = ROLES[i];
        member.cls = bot->getClass();
        member.level = bot->GetLevel();
        member.population = PlayerbotsAdapter::IsRandomBot(guid);
        run->members.push_back(member);
    }
    if (!order.testFail.empty() && !allTest)
        return finish({false, "test seams only work on test-guild bots", ""});
    std::string const levelProblem = LevelProblem(
        levels, BandFor(order.heroic ? info.heroicLevel : info.recommendedLevel, cfg.runLevelBelow, cfg.runLevelAbove));
    if (!levelProblem.empty())
        return finish({false, levelProblem, ""});
    // dungeon-clear elects its leader among tanks by talent spec: without one, `dc on` silently does nothing and the
    // run would end "dc on did not take" after the whole trip. Refuse here, with a reason the player can act on.
    std::string const roleProblem = RoleProblem(seats);
    if (!roleProblem.empty())
        return finish({false, roleProblem, ""});

    std::vector<std::string> const warnings = RoleWarnings(classes);
    run->warningsJson = "[";
    for (std::size_t i = 0; i < warnings.size(); ++i)
    {
        if (i)
            run->warningsJson += ",";
        JsonObject::AppendEscaped(run->warningsJson, warnings[i]);
    }
    run->warningsJson += "]";

    run->choice = ChooseApproach(spots, run->entrance, cfg.runTravelMaxDistance, order.approach);
    run->approachName = run->choice.approach == Approach::Travel ? "travel" : "teleport";
    if (order.dryRun)
        return finish({true, "dry run: would run " + info.name + " by " + run->approachName,
                       JsonObject().Str("approach", run->approachName).Raw("warnings", run->warningsJson).Build()});

    run->id = order.id;
    run->token = info.token;
    run->dungeonName = info.name;
    run->mapId = info.mapId;
    run->heroic = order.heroic;
    run->testFail = order.testFail;
    run->startedAt = Now();
    run->finish = std::move(finish);

    std::string party = "[";
    std::vector<uint64> guids;
    for (Member const& m : run->members)
    {
        party += (guids.empty() ? "" : ",") + JsonObject().UInt("guid", m.guid).Str("name", m.name).Str("role", m.role)
                                                  .UInt("class", m.cls).UInt("level", m.level).Build();
        guids.push_back(m.guid);
        RunRegistry::Instance().Set(m.guid, run->id);
        if (m.population)
            PlayerbotsAdapter::Hold(m.guid);  // the population leaves it alone (no logout, relocation) until End
    }
    party += "]";
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_RUN);
    stmt->SetData(0, run->id);
    stmt->SetData(1, run->token);
    stmt->SetData(2, run->dungeonName);
    stmt->SetData(3, run->mapId);
    stmt->SetData(4, static_cast<uint8>(run->heroic ? 1 : 0));
    stmt->SetData(5, party);
    stmt->SetData(6, run->warningsJson);
    stmt->SetData(7, run->startedAt);
    GuildmasterDatabase.Execute(stmt);
    run->guildId = GuildRegistry::Instance().GuildIdFor(allTest ? GuildRole::Test : GuildRole::User);
    EventSink::Instance().Push(EventType::RunStart, 0, run->guildId, run->id,
                               RunStartPayload(run->id, run->token, guids, run->approachName));
    LOG_INFO("module.guildbridge", "dungeon run {}: {} by {} ({} yd)", run->id, run->dungeonName, run->approachName,
             run->choice.distance);

    // pre_order snapshots of all five. The callbacks capture the run id and look the run up (preflight D3): a run
    // that has already ended (timed out) is gone, and its late callbacks do nothing.
    uint64 const runId = run->id;
    std::vector<std::pair<uint32, std::string>> toSnapshot;
    for (Member const& m : run->members)
        toSnapshot.emplace_back(m.guid, m.name);
    _runs.push_back(std::move(run));
    for (auto const& [guid, name] : toSnapshot)
        BotDumps::Instance().Request(guid, "pre_order", runId,
                                     [runId, name = name](bool ok, std::string const&, std::string const&) {
                                         Run* current = DungeonRunMgr::Instance().Find(runId);
                                         if (!current || current->stage != Stage::Snapshots)
                                             return;
                                         if (!ok && current->snapshotError.empty())
                                             current->snapshotError = "could not snapshot " + name;
                                         if (current->snapshotsLeft)
                                             --current->snapshotsLeft;
                                     });
}

void DungeonRunMgr::Update(uint32 diff)
{
    for (std::size_t i = 0; i < _runs.size(); ++i)
        Step(*_runs[i], diff);
    _runs.erase(std::remove_if(_runs.begin(), _runs.end(), [](auto const& run) { return run->stage == Stage::Done; }),
                _runs.end());
}

void DungeonRunMgr::Step(Run& run, uint32 diff)
{
    BridgeConfig const& cfg = BridgeConfig::Get();
    run.stageMs += diff;
    run.totalMs += diff;
    if (run.stage == Stage::Done)
        return;
    if (run.totalMs > cfg.runOverallTimeoutS * 1000 && run.stage != Stage::ReadResult)
    {
        if (run.stage != Stage::Monitor)
            return End(run, "abandoned", "no result in time", 0, 0);
        // dungeon-clear has the party: ask it to stop (it still revives, sends home, logs out and writes its line),
        // then read the result as usual. Only if it never lets go are the bots taken back without its line.
        if (!run.dcStopAsked)
        {
            run.dcStopAsked = true;
            std::string message;
            DcAdapter::Stop(run.dcRunId, message);
            LOG_WARN("module.guildbridge", "dungeon run {}: time limit, asked mod-dungeon-clear to stop {}: {}", run.id,
                     run.dcRunId, message);
        }
        else if (run.totalMs > cfg.runOverallTimeoutS * 1000 + 300000)
            return End(run, "abandoned", "no result in time", 0, 0);
    }

    switch (run.stage)
    {
        case Stage::Snapshots:
            if (run.snapshotsLeft == 0)
            {
                if (!run.snapshotError.empty())
                    return End(run, "failed", run.snapshotError, 0, 0);
                SetStage(run, Stage::Approach);
            }
            else if (run.stageMs > 180000)
                return End(run, "failed", "snapshots timed out", 0, 0);
            return;
        case Stage::Approach:
        {
            if (run.choice.approach == Approach::Travel)
            {
                EventSink::Instance().Push(EventType::RunTravel, 0, run.guildId, run.id,
                                           RunTravelPayload(run.id, run.choice.distance));
                SetStage(run, Stage::Travel);
                run.tickMs = 5000;  // first orders at once
                return;
            }
            EventSink::Instance().Push(EventType::RunTeleport, 0, run.guildId, run.id,
                                       RunTeleportPayload(run.id, run.choice.reason));
            for (std::size_t i = 0; i < run.members.size(); ++i)
                if (Player* bot = Connected(run.members[i].guid))
                    bot->TeleportTo(run.entrance.map, run.entrance.x + static_cast<float>(i), run.entrance.y,
                                    run.entrance.z + 1.f, bot->GetOrientation());
            SetStage(run, Stage::AtEntrance);
            return;
        }
        case Stage::Travel:
            run.tickMs += diff;
            if (run.tickMs >= 5000)
            {
                run.tickMs = 0;
                TickTravel(run);
            }
            return;
        case Stage::AtEntrance:
        {
            bool allThere = true;
            for (Member const& m : run.members)
            {
                Player* bot = Connected(m.guid);
                if (!bot)
                    return End(run, "abandoned", m.name + " went offline", 0, 0);
                if (!bot->IsAlive() || !NearSpot(bot, run.entrance, 40.f))
                    allThere = false;
                else
                    PlayerbotsAdapter::Park(bot);
            }
            if (!allThere)
            {
                if (run.stageMs > 60000)
                    return End(run, "abandoned", "the party did not gather at the entrance", 0, 0);
                return;
            }
            if (run.testFail == "entrance")
                return End(run, "abandoned", "stopped at the entrance (test)", 0, 0);
            SetStage(run, Stage::Handover);
            return;
        }
        case Stage::Handover:
        case Stage::WaitDc:
            Handover(run, diff);
            return;
        case Stage::Monitor:
        case Stage::ReadResult:
            Monitor(run, diff);
            return;
        case Stage::Done:
            return;
    }
}

// Every 5 s while travelling: ambush events, arrivals, the dead wait and the time limit.
void DungeonRunMgr::TickTravel(Run& run)
{
    BridgeConfig const& cfg = BridgeConfig::Get();
    for (RunRegistry::Death const& death : RunRegistry::Instance().TakeDeaths(run.id))
        EventSink::Instance().Push(EventType::RunAmbush, death.guid, run.guildId, run.id,
                                   RunAmbushPayload(run.id, death.killer, death.pvp));

    bool allArrived = true;
    for (Member& m : run.members)
    {
        Player* bot = Connected(m.guid);
        if (!bot)
            return End(run, "abandoned", m.name + " went offline", 0, 0);
        if (!bot->IsInWorld())
        {
            allArrived = false;  // between maps (a far teleport): wait
            continue;
        }
        if (!bot->IsAlive())
        {
            m.deadMs += 5000;
            m.arrived = false;
            allArrived = false;
            if (m.deadMs > cfg.runDeadWaitS * 1000)
                return End(run, "abandoned", m.name + " died on the way and did not get back up", 0, 0);
            continue;  // corpse run: playerbots handles it (honest death, no free revive)
        }
        m.deadMs = 0;
        if (NearSpot(bot, run.entrance, 25.f))
        {
            if (!m.arrived)
                LOG_INFO("module.guildbridge", "dungeon run {}: {} arrived by travel", run.id, m.name);
            m.arrived = true;
            m.travelled = true;
            PlayerbotsAdapter::Park(bot);
            continue;
        }
        m.arrived = false;
        allArrived = false;
        PlayerbotsAdapter::GoTo(bot, run.entrance);
    }
    if (allArrived)
    {
        SetStage(run, Stage::AtEntrance);
        return;
    }
    if (run.stageMs > cfg.runTravelTimeoutS * 1000)
    {
        EventSink::Instance().Push(EventType::RunTeleport, 0, run.guildId, run.id,
                                   RunTeleportPayload(run.id, "travel_timeout"));
        run.approachName = "travel_then_teleport";
        std::size_t i = 0;
        for (Member const& m : run.members)
        {
            ++i;
            if (Player* bot = Connected(m.guid))
                if (bot->IsInWorld() && bot->IsAlive() && !m.arrived)
                {
                    LOG_INFO("module.guildbridge", "dungeon run {}: {} teleported (travel time limit)", run.id, m.name);
                    bot->TeleportTo(run.entrance.map, run.entrance.x + static_cast<float>(i), run.entrance.y,
                                    run.entrance.z + 1.f, bot->GetOrientation());
                }
        }
        SetStage(run, Stage::AtEntrance);
    }
}

// At the entrance: log the five out (dungeon-clear only takes offline characters; population members stay held, so
// the population does not log them back in), then start the run, retrying while dungeon-clear says "not yet".
void DungeonRunMgr::Handover(Run& run, uint32 diff)
{
    if (run.stage == Stage::Handover)
    {
        for (Member const& m : run.members)
            PlayerbotsAdapter::Logout(ObjectGuid::Create<HighGuid::Player>(m.guid));
        SetStage(run, Stage::WaitDc);
        return;
    }
    run.tickMs += diff;
    if (run.tickMs < 5000)
        return;
    run.tickMs = 0;
    for (Member const& m : run.members)
        if (Connected(m.guid))
        {
            if (run.stageMs > 60000)
                return End(run, "abandoned", m.name + " did not log out for the handover", 0, 0);
            return;
        }
    if (run.testFail == "dcfail")
        return End(run, "abandoned", "mod-dungeon-clear refused: test", 0, 0);

    std::string names;
    for (Member const& m : run.members)
        names += (names.empty() ? "" : ",") + m.name;
    std::string message;
    std::string runId;
    switch (DcAdapter::Start(run.token, names, run.heroic, message, runId))
    {
        case DcAdapter::StartState::Started:
            run.dcRunId = runId;
            run.enteredAt = Now();
            // dungeon-clear ids look like tr-20261005-143000-1; anything else is not written into the SQL text
            // here (End stores it through a prepared statement).
            if (runId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
                std::string::npos)
                GuildmasterDatabase.Execute("UPDATE dungeon_runs SET entered_at = {}, dc_run_id = '{}' WHERE id = {}",
                                            run.enteredAt, runId, run.id);
            else
                GuildmasterDatabase.Execute("UPDATE dungeon_runs SET entered_at = {} WHERE id = {}", run.enteredAt,
                                            run.id);
            LOG_INFO("module.guildbridge", "dungeon run {}: handed to mod-dungeon-clear as {}", run.id, runId);
            SetStage(run, Stage::Monitor);
            return;
        case DcAdapter::StartState::Retry:
            if (run.stageMs > 120000)
                return End(run, "abandoned", "mod-dungeon-clear refused: " + message, 0, 0);
            return;
        case DcAdapter::StartState::Refused:
            return End(run, "abandoned", "mod-dungeon-clear refused: " + message, 0, 0);
    }
}

// While dungeon-clear holds the tank the run is on. Then: wait for its logouts (so End can log the clones back in),
// read its result line off the world thread and end the run with it.
void DungeonRunMgr::Monitor(Run& run, uint32 diff)
{
    run.tickMs += diff;
    if (run.tickMs < 5000)
        return;
    run.tickMs = 0;
    if (run.stage == Stage::Monitor)
    {
        if (DcAdapter::IsReserved(run.members[0].guid))
            return;  // dungeon-clear still has the party
        SetStage(run, Stage::ReadResult);
    }
    if (run.stageMs < 60000)
        for (Member const& m : run.members)
            if (Connected(m.guid))
                return;  // dungeon-clear's logout still in flight
    if (!run.fileRead.valid())
    {
        run.fileRead = std::async(std::launch::async, ReadTail, DcAdapter::RunsFilePath());
        return;
    }
    if (run.fileRead.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    DcOutcome const outcome = FindDcRun(run.fileRead.get(), run.dcRunId);
    if (outcome.partial && ++run.partialTries < 12)
        return;  // dungeon-clear is still writing the line (no '\n' yet): read again on the next poll, never final
    if (!outcome.found)
    {
        if (++run.readTries < 3)
            return;  // the line is flushed at teardown; try again in 5 s
        return End(run, "abandoned", "result missing from dc_testruns.jsonl", 0, 0);
    }
    std::string reason = outcome.result == "success" ? "" : outcome.failReason;
    if (run.dcStopAsked)
        reason = "no result in time";
    else if (reason.empty() && outcome.result != "success")
        reason = outcome.result;
    End(run, RunResultFromDc(outcome.result), reason, outcome.bossesKilled, outcome.bossesTotal);
}

// Every outcome ends here: the bots go back to their normal lives, the run row, the run_end event, the order.
void DungeonRunMgr::End(Run& run, std::string const& result, std::string const& reason, uint32 bossesKilled,
                        uint32 bossesTotal)
{
    uint32 travelled = 0;
    for (Member const& m : run.members)
    {
        travelled += m.travelled ? 1 : 0;
        RunRegistry::Instance().Clear(m.guid);
        Player* bot = Connected(m.guid);
        if (m.population)
            PlayerbotsAdapter::Release(m.guid);  // offline after a dungeon-clear run: the population logs it back in
        else if (!bot)
            PlayerbotsAdapter::LoginMasterless(ObjectGuid::Create<HighGuid::Player>(m.guid));
        if (bot && bot->IsInWorld())
            PlayerbotsAdapter::ClearGoTo(bot);
    }
    uint32 const now = Now();
    uint32 const wipes = RunRegistry::Instance().Wipes(run.id);
    RunRegistry::Instance().Forget(run.id);  // deaths inside the dungeon are already death events
    EventSink::Instance().Push(EventType::RunEnd, 0, run.guildId, run.id,
                               RunEndPayload(run.id, result, reason, bossesKilled, bossesTotal, now - run.startedAt,
                                             wipes));
    EventSink::Instance().Flush();  // the aggregate below must see this run's events (same writer thread, in order)

    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_UPD_RUN_END);
    stmt->SetData(0, run.approachName);
    stmt->SetData(1, run.dcRunId);
    stmt->SetData(2, result);
    stmt->SetData(3, reason.substr(0, 255));
    stmt->SetData(4, static_cast<uint8>(bossesKilled));
    stmt->SetData(5, static_cast<uint8>(bossesTotal));
    stmt->SetData(6, run.enteredAt);
    stmt->SetData(7, now);
    stmt->SetData(8, now - run.startedAt);
    stmt->SetData(9, run.id);
    GuildmasterDatabase.Execute(stmt);
    GuildmasterDatabase.Execute(
        "UPDATE dungeon_runs r SET "
        "wipes = (SELECT COUNT(*) FROM events e WHERE e.run_id = r.id AND e.type = 'boss_wipe'), "
        "loot = (SELECT IFNULL(JSON_ARRAYAGG(JSON_OBJECT('guid', e.guid, 'item', CAST(e.payload->>'$.item_entry' AS "
        "UNSIGNED), 'quality', CAST(e.payload->>'$.quality' AS UNSIGNED))), JSON_ARRAY()) FROM events e WHERE "
        "e.run_id = r.id AND e.type = 'loot'), furthest_boss = (SELECT e.payload->>'$.boss_name' FROM events e WHERE "
        "e.run_id = r.id AND e.type = 'boss_kill' ORDER BY e.id DESC LIMIT 1) WHERE r.id = {}",
        run.id);
    LOG_INFO("module.guildbridge", "dungeon run {} ended: {} ({}), {} of {} arrived by travel", run.id, result, reason,
             travelled, run.members.size());

    bool const ok = result != "failed";
    std::string const data = JsonObject().UInt("run_id", run.id).Str("result", result).Str("approach", run.approachName)
                                 .UInt("arrived_by_travel", travelled).Raw("warnings", run.warningsJson).Build();
    SetStage(run, Stage::Done);
    run.finish({ok, run.dungeonName + ": " + result + (reason.empty() ? "" : " (" + reason + ")"), data});
}
