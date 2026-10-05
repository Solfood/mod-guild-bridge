/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_DUNGEONRUNMGR_H
#define MOD_GUILD_BRIDGE_DUNGEONRUNMGR_H

#include "OrderRunner.h"
#include "TravelRules.h"
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// run_dungeon orders (spec §4.3). One state machine per run, world thread. Run id = order id.
// Holds: population members are held (fork Hold) for the run; holds are in memory only. A restart ends every run
// instead of resuming it: LoadAtStartup marks runs left "running" as abandoned (with a run_end event) and the order
// runner fails their orders, so after a boot no bot is held, in a run or waiting for a run (nothing to put back:
// a bot keeps whatever position it had reached, as after any logout).
// At the entrance the party is handed to mod-dungeon-clear (`.dc test start <d> party=...`): the bridge logs the five
// out (population members stay held), dungeon-clear logs them in, clears, revives them and sends them to their home
// inn, then logs them out. Its result line in dc_testruns.jsonl is read off the world thread. One run = one dungeon-clear
// token (a wing for Scarlet Monastery, Dire Maul, ...); wings of one map share the map's outdoor gathering spot.
class DungeonRunMgr
{
public:
    static DungeonRunMgr& Instance();
    void LoadAtStartup();  // outdoor entrances; runs left "running" by a restart become "abandoned"
    void Begin(GuildBridge::ParsedOrder const& order, std::function<void(OrderResult const&)> finish);
    void Update(uint32 diff);
    bool EntranceFor(uint32 mapId, GuildBridge::Spot& out) const;
    void SetMaxRuns(uint32 max) { _maxRuns = max; }  // LoadAtStartup sets it from config; `bridge run cap` for tests
    uint32 Running() const { return static_cast<uint32>(_runs.size()); }

private:
    enum class Stage { Snapshots, Approach, Travel, AtEntrance, Handover, WaitDc, Monitor, ReadResult, Done };

    struct Member
    {
        uint32 guid = 0;
        std::string name;
        std::string role;
        uint8 cls = 0;
        uint8 level = 0;
        bool population = false;
        bool arrived = false;    // at the entrance (travel)
        bool travelled = false;  // got there by travelling, not by a teleport (preflight D15)
        uint32 deadMs = 0;
    };

    struct Run
    {
        uint64 id = 0;
        uint32 guildId = 0;  // the guild the run's events are filed under (user, or test for test parties)
        std::string token;
        std::string dungeonName;
        uint32 mapId = 0;
        bool heroic = false;
        GuildBridge::Spot entrance;
        GuildBridge::ApproachChoice choice;
        std::string approachName;  // travel | teleport | travel_then_teleport
        std::string warningsJson = "[]";
        std::vector<Member> members;
        Stage stage = Stage::Snapshots;
        uint32 stageMs = 0;
        uint32 totalMs = 0;
        uint32 tickMs = 0;
        uint32 startedAt = 0;
        uint32 enteredAt = 0;
        uint32 snapshotsLeft = 5;
        std::string snapshotError;
        std::string testFail;
        std::string dcRunId;
        uint32 readTries = 0;
        bool dcStopAsked = false;  // the overall time limit passed while dungeon-clear had the party
        std::future<std::string> fileRead;
        std::function<void(OrderResult const&)> finish;
    };

    Run* Find(uint64 runId);  // nullptr once the run is over (callbacks capture the id, never a Run*: preflight D3)
    void Step(Run& run, uint32 diff);
    void TickTravel(Run& run);
    void Handover(Run& run, uint32 diff);  // log the five out, start the dungeon-clear run
    void Monitor(Run& run, uint32 diff);   // until dungeon-clear releases the tank, then read its result line
    void End(Run& run, std::string const& result, std::string const& reason, uint32 bossesKilled, uint32 bossesTotal);
    static void SetStage(Run& run, Stage stage)
    {
        run.stage = stage;
        run.stageMs = 0;
    }

    std::vector<std::unique_ptr<Run>> _runs;
    std::unordered_map<uint32, GuildBridge::Spot> _entrances;
    uint32 _maxRuns = 3;
};

#endif
