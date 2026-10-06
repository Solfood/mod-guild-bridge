/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeConfig.h"

#include "Config.h"
#include "OrderRules.h"
#include <algorithm>
#include <cctype>

BridgeConfig& BridgeConfig::Get()
{
    static BridgeConfig config;
    return config;
}

void BridgeConfig::Load()
{
    enable = sConfigMgr->GetOption<bool>("GuildBridge.Enable", true);
    // Restores, snapshots and the order runner read back what they just wrote on the same pool and trust the answer:
    // true only with one async writer per pool. Any other setting keeps the bridge off (the world still runs).
    disabledReason = GuildBridge::WriterThreadsProblem(
        sConfigMgr->GetOption<uint32>("CharacterDatabase.WorkerThreads", 1),
        sConfigMgr->GetOption<uint32>("GuildmasterDatabase.WorkerThreads", 1));
    if (!disabledReason.empty())
        enable = false;
    worldId = sConfigMgr->GetOption<std::string>("GuildBridge.WorldId", "w1");
    charactersDb = DatabaseNameOf(sConfigMgr->GetOption<std::string>("CharacterDatabaseInfo", ""));
    playerbotsDb = DatabaseNameOf(sConfigMgr->GetOption<std::string>("PlayerbotsDatabaseInfo", ""));
    testAccount = sConfigMgr->GetOption<std::string>("GuildBridge.TestAccount", "GMTEST");
    leaderAccount = sConfigMgr->GetOption<std::string>("GuildBridge.LeaderAccount", "GMGUILD");
    founderAccounts = AccountList(sConfigMgr->GetOption<std::string>("GuildBridge.FounderAccounts", ""));
    populationReadyShare =
        std::min<uint32>(100, sConfigMgr->GetOption<uint32>("GuildBridge.Population.ReadyShare", 95));
    snapshotKeepDays = sConfigMgr->GetOption<uint32>("GuildBridge.Snapshot.KeepDays", 14);
    snapshotPerTick = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildBridge.Snapshot.PerTick", 1));
    eventFlushMs = std::max<uint32>(100, sConfigMgr->GetOption<uint32>("GuildBridge.Events.FlushMs", 1000));
    eventQueueCap = sConfigMgr->GetOption<uint32>("GuildBridge.Events.QueueCap", 10000);
    firstsLevelStep = sConfigMgr->GetOption<uint32>("GuildBridge.Firsts.LevelStep", 10);
    runTravelMaxDistance = sConfigMgr->GetOption<float>("GuildBridge.Dungeon.TravelMaxDistance", 1500.f);
    runTravelTimeoutS = std::max<uint32>(60, sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.TravelTimeoutS", 1800));
    runDeadWaitS = sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.DeadWaitS", 600);
    runLevelBelow = sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.LevelBelow", 5);
    runLevelAbove = sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.LevelAbove", 5);
    runMaxConcurrent = sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.MaxConcurrentRuns", 3);
    runOverallTimeoutS =
        std::max<uint32>(600, sConfigMgr->GetOption<uint32>("GuildBridge.Dungeon.OverallTimeoutS", 9000));
    stuckEnable = sConfigMgr->GetOption<bool>("GuildBridge.Stuck.Enable", true);
    stuckDeadS = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.DeadS", 600));
    stuckNoProgressS = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.NoProgressS", 900));
    stuckMoveYards = sConfigMgr->GetOption<float>("GuildBridge.Stuck.MoveYards", 40.f);
    stuckPathFails = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.PathFails", 3));
    stuckPathWindowS = std::max<uint32>(60, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.PathWindowS", 900));
    stuckScanS = std::max<uint32>(10, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.ScanS", 60));
    stuckPerTick = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("GuildBridge.Stuck.PerTick", 100));
}

std::string BridgeConfig::DatabaseNameOf(std::string const& info)
{
    std::size_t pos = 0;
    for (int field = 0; field < 4; ++field)
    {
        pos = info.find(';', pos);
        if (pos == std::string::npos)
            return "";
        ++pos;
    }
    std::size_t const end = info.find(';', pos);
    return info.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

bool BridgeConfig::IsValidWorldId(std::string const& id)
{
    return !id.empty() && id.size() <= 32 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '_' || c == '-';
    });
}

std::vector<std::string> BridgeConfig::AccountList(std::string const& text)
{
    std::vector<std::string> out;
    std::string name;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == ',')
        {
            if (!name.empty() && std::find(out.begin(), out.end(), name) == out.end())
                out.push_back(name);
            name.clear();
        }
        else if (!std::isspace(static_cast<unsigned char>(text[i])))
            name += static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
    }
    return out;
}
