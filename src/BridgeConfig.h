/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_CONFIG_H
#define MOD_GUILD_BRIDGE_CONFIG_H

#include "Define.h"
#include <string>

// Every GuildBridge.* option, read once at startup (a change needs a worldserver restart).
struct BridgeConfig
{
    static BridgeConfig& Get();
    void Load();

    bool enable = true;
    std::string worldId;       // GuildBridge.WorldId: this world's id, stamped into world_status
    std::string charactersDb;  // database names of THIS world, from the core's *DatabaseInfo strings
    std::string playerbotsDb;
    std::string testAccount;  // GuildBridge.TestAccount: the normal account that holds clones (test bots)
    uint32 snapshotKeepDays = 14;  // GuildBridge.Snapshot.KeepDays (0 = keep every snapshot)
    uint32 snapshotPerTick = 1;    // GuildBridge.Snapshot.PerTick: batch snapshot requests started per world tick
    uint32 eventFlushMs = 1000;     // GuildBridge.Events.FlushMs: how often queued events are written
    uint32 eventQueueCap = 10000;   // GuildBridge.Events.QueueCap: queued events beyond this are dropped (counted)
    uint32 firstsLevelStep = 10;    // GuildBridge.Firsts.LevelStep: level firsts at 10, 20, ... (0 = none)
    // GuildBridge.Dungeon.* (run_dungeon orders, Task 11)
    float runTravelMaxDistance = 1500.f;  // .TravelMaxDistance: travel (not teleport) when everyone is this close
    uint32 runTravelTimeoutS = 1800;      // .TravelTimeoutS: whoever has not arrived by then is teleported
    uint32 runDeadWaitS = 600;            // .DeadWaitS: how long the group waits for a member who died on the way
    uint32 runLevelBelow = 5;             // .LevelBelow / .LevelAbove: the level band around the recommended level
    uint32 runLevelAbove = 5;
    uint32 runMaxConcurrent = 3;          // .MaxConcurrentRuns: runs at the same time (each costs world tick time)
    uint32 runOverallTimeoutS = 9000;     // .OverallTimeoutS: a run with no result by then is abandoned
    // GuildBridge.Stuck.* (stuck-bot incidents, Task 13)
    bool stuckEnable = true;          // .Enable
    uint32 stuckDeadS = 600;          // .DeadS: dead this long
    uint32 stuckNoProgressS = 900;    // .NoProgressS: no move beyond MoveYards and no XP/level/money change
    float stuckMoveYards = 40.f;      // .MoveYards
    uint32 stuckPathFails = 3;        // .PathFails: the fork's stuck -> teleport fallback this many times ...
    uint32 stuckPathWindowS = 900;    // .PathWindowS: ... within this window
    uint32 stuckScanS = 60;           // .ScanS: one look at every online bot this often
    uint32 stuckPerTick = 100;        // .PerTick: bots judged per world tick (the look is spread over ticks)

    // "host;port;user;password;database" -> "database" ("" when the string has fewer than 5 fields).
    static std::string DatabaseNameOf(std::string const& info);
    static bool IsValidWorldId(std::string const& id);
};

#endif
