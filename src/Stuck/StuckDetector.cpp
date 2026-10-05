/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "StuckDetector.h"

#include "BridgeConfig.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Json.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "RestoreMgr.h"
#include "RunRegistry.h"
#include "StringFormat.h"
#include "WorldSession.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <mutex>
#include <shared_mutex>

using namespace GuildBridge;

namespace
{
// Why a bot is not judged for "no progress" right now, or nullptr. Kept in the details of other kinds.
char const* ExemptReason(Player* bot, uint32 guid)
{
    if (RunRegistry::Instance().RunIdFor(guid))
        return "run";
    if (PlayerbotsAdapter::IsHeld(guid))
        return "held";
    if (RestoreMgr::Instance().IsRestoring(guid))
        return "restoring";
    if (PlayerbotsAdapter::IsRaising(guid))
        return "raising";
    if (Map const* map = bot->GetMap(); map && (map->IsDungeon() || map->IsBattlegroundOrArena()))
        return "instance";
    return nullptr;
}
}  // namespace

StuckDetector& StuckDetector::Instance()
{
    static StuckDetector instance;
    return instance;
}

void StuckDetector::LoadThresholds()
{
    BridgeConfig const& cfg = BridgeConfig::Get();
    _thresholds = {cfg.stuckDeadS, cfg.stuckNoProgressS, cfg.stuckMoveYards, cfg.stuckPathFails, cfg.stuckPathWindowS};
}

void StuckDetector::CloseAllAtStartup()
{
    LoadThresholds();
    CloseAll("restart");
}

void StuckDetector::CloseAll(char const* why)
{
    _tracks.clear();
    _scanning = false;
    _pending.clear();
    if (!GuildmasterDatabaseReady)
        return;
    GuildmasterDatabase.Execute(
        "UPDATE incidents SET closed_at = {}, details = JSON_SET(details, '$.closed_by', '{}') WHERE closed_at IS NULL",
        static_cast<uint32>(std::time(nullptr)), why);
}

void StuckDetector::SetTest(std::string const& name, Thresholds const& thresholds)
{
    CloseAll("test");
    LoadThresholds();
    _thresholds.deadS = std::max<uint32>(1, thresholds.deadS);
    _thresholds.noProgressS = std::max<uint32>(1, thresholds.noProgressS);
    _thresholds.pathFails = std::max<uint32>(1, thresholds.pathFails);
    _onlyName = name;
}

void StuckDetector::Reset()
{
    CloseAll("reset");
    LoadThresholds();
    _onlyName.clear();
}

uint32 StuckDetector::OpenCount() const
{
    uint32 open = 0;
    for (auto const& [guid, entry] : _tracks)
        for (bool o : entry.track.open)
            open += o ? 1 : 0;
    return open;
}

void StuckDetector::Update(uint32 diff)
{
    if (!BridgeConfig::Get().stuckEnable)
        return;
    if (!_scanning)
    {
        _timer += diff;
        if (_timer < BridgeConfig::Get().stuckScanS * 1000)
            return;
        StartScan();
    }
    Step(BridgeConfig::Get().stuckPerTick);
}

void StuckDetector::ScanNow()
{
    StartScan();
    Step(std::numeric_limits<std::size_t>::max());
}

void StuckDetector::StartScan()
{
    auto const started = std::chrono::steady_clock::now();
    _timer = 0;
    _scanning = true;
    ++_pass;
    _pending.clear();
    _next = 0;
    _scanUs = 0;
    _scanTicks = 0;
    _scanBots = 0;
    {
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (player && (_onlyName.empty() || player->GetName() == _onlyName))
                _pending.push_back(guid.GetCounter());
    }
    _scanUs += static_cast<uint64>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
}

void StuckDetector::Step(std::size_t budget)
{
    auto const started = std::chrono::steady_clock::now();
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    for (std::size_t done = 0; done < budget && _next < _pending.size(); ++done)
        Judge(_pending[_next++], now);
    bool const finished = _next >= _pending.size();
    if (finished)
        FinishScan();
    uint32 const us = static_cast<uint32>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    _maxStepUs = std::max(_maxStepUs, us);
    _scanUs += us;
    ++_scanTicks;
    if (finished)
    {
        _lastScanUs = static_cast<uint32>(_scanUs);
        _lastScanBots = _scanBots;
        _lastScanTicks = _scanTicks;
    }
}

void StuckDetector::FinishScan()
{
    _scanning = false;
    _pending.clear();
    _next = 0;
    // Bots no longer online: what they had open closes, and they are forgotten (a relog starts a new track).
    uint32 const now = static_cast<uint32>(std::time(nullptr));
    for (auto it = _tracks.begin(); it != _tracks.end();)
    {
        if (it->second.pass == _pass)
        {
            ++it;
            continue;
        }
        for (int k = 0; k < INCIDENT_KINDS; ++k)
            if (it->second.track.open[k])
                Close(it->first, static_cast<IncidentKind>(k), now, "logout");
        it = _tracks.erase(it);
    }
}

void StuckDetector::Judge(uint32 guid, uint32 now)
{
    Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(guid));
    if (!bot)
        return;  // logged out since the look started
    auto it = _tracks.find(guid);
    if (!bot->IsInWorld() || bot->IsBeingTeleported())
    {
        if (it != _tracks.end())
            it->second.pass = _pass;  // between maps: keep the track, judge it next time
        return;
    }
    if (it == _tracks.end())
    {
        if (!PlayerbotsAdapter::IsBot(bot))
            return;  // a real player
        // The test account's clones are test fixtures, not the population: judged only in test mode.
        if (_onlyName.empty() && bot->GetSession() &&
            GuildRegistry::Instance().IsTestAccount(bot->GetSession()->GetAccountId()))
            return;
        it = _tracks.emplace(guid, Entry{}).first;
    }
    Entry& entry = it->second;
    entry.pass = _pass;
    ++_scanBots;

    char const* const exempt = ExemptReason(bot, guid);
    BotSample sample;
    sample.nowS = now;
    sample.dead = !bot->IsAlive();
    sample.exempt = exempt != nullptr;
    sample.map = bot->GetMapId();
    sample.x = bot->GetPositionX();
    sample.y = bot->GetPositionY();
    sample.xp = bot->GetUInt32Value(PLAYER_XP);
    sample.level = bot->GetLevel();
    sample.money = bot->GetMoney();
    sample.stuckTeleports = PlayerbotsAdapter::StuckTeleports(bot);
    sample.loginS = static_cast<uint32>(bot->m_logintime);
    // Where the bot last made progress, before Evaluate moves the anchor (for the details of a new incident).
    ProgressSpot const before{entry.track.anchorS, entry.track.anchorMap, entry.track.anchorX, entry.track.anchorY};
    for (Change const& change : Evaluate(entry.track, sample, _thresholds))
    {
        if (change.open)
            Open(bot, change.kind, sample, entry.track, before, exempt);
        else if (change.kind == IncidentKind::NoProgress && (sample.dead || exempt))
            Close(guid, change.kind, now, sample.dead ? "died" : exempt);  // no longer judged for it
        else
            Close(guid, change.kind, now, "cleared");
    }
}

void StuckDetector::Open(Player* bot, IncidentKind kind, BotSample const& sample, BotTrack const& track,
                         ProgressSpot const& before, char const* exempt)
{
    uint32 const now = sample.nowS;
    // Evidence for GM-BOTAI's later fixes: how long, how far from the last progress, the fork's counter, who.
    JsonObject details;
    details.UInt("dead_s", sample.dead ? now - track.deadSinceS : 0)
        .UInt("since_progress_s", now - before.sinceS)
        .Num("moved_yards", before.map == sample.map ? std::round(std::hypot(sample.x - before.x, sample.y - before.y))
                                                      : -1.0)
        .UInt("stuck_teleports", sample.stuckTeleports)
        .UInt("stuck_teleports_in_window", track.stuckTimes.size())
        .UInt("online_s", sample.loginS && now > sample.loginS ? now - sample.loginS : 0)
        .UInt("area", bot->GetAreaId())
        .UInt("class", bot->getClass())
        .Bool("ghost", bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        .Bool("in_group", bot->GetGroup() != nullptr)
        .Bool("population", PlayerbotsAdapter::IsRandomBot(bot->GetGUID().GetCounter()))
        .UInt("guild", bot->GetGuildId())
        .UInt("focus", PlayerbotsAdapter::GetFocus(bot))
        .UInt("durability_pct", PlayerbotsAdapter::DurabilityPct(bot));
    if (exempt)
        details.Str("exempt", exempt);
    LOG_INFO("module.guildbridge", "GUILDBRIDGE incident open {} {} map={} zone={} intent={}", IncidentKindName(kind),
             bot->GetName(), bot->GetMapId(), bot->GetZoneId(), PlayerbotsAdapter::RpgStatusName(bot));
    if (!GuildmasterDatabaseReady)
        return;
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_INCIDENT);
    stmt->SetData(0, bot->GetGUID().GetCounter());
    stmt->SetData(1, bot->GetName());
    stmt->SetData(2, std::string(IncidentKindName(kind)));
    stmt->SetData(3, now);
    stmt->SetData(4, static_cast<uint16>(bot->GetMapId()));
    stmt->SetData(5, bot->GetZoneId());
    stmt->SetData(6, bot->GetPositionX());
    stmt->SetData(7, bot->GetPositionY());
    stmt->SetData(8, bot->GetPositionZ());
    stmt->SetData(9, static_cast<uint8>(bot->GetLevel()));
    stmt->SetData(10, PlayerbotsAdapter::RpgStatusName(bot));
    stmt->SetData(11, PlayerbotsAdapter::LastStuckDest(bot));
    stmt->SetData(12, details.Build());
    GuildmasterDatabase.Execute(stmt);
}

void StuckDetector::Close(uint32 guid, IncidentKind kind, uint32 now, char const* why)
{
    if (!GuildmasterDatabaseReady)
        return;
    GuildmasterDatabase.Execute("UPDATE incidents SET closed_at = {}, details = JSON_SET(details, '$.closed_by', '{}') "
                                "WHERE guid = {} AND kind = '{}' AND closed_at IS NULL",
                                now, why, guid, IncidentKindName(kind));
}
