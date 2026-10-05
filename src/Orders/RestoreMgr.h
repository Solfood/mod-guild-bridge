/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_RESTOREMGR_H
#define MOD_GUILD_BRIDGE_RESTOREMGR_H

#include "OrderRunner.h"
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Restores, one bot at a time (spec §4.6). World thread. A restore rewinds the bot, not the world.
//
// Steps: check (snapshot is this bot's; bot free, not a leader, not in a raising or run) -> hold -> safety snapshot
// (pre_restore), confirmed in the database -> log out -> empty its mailbox (so the delete returns no mail to its
// senders: that would duplicate gold and items next to the snapshot's own mailbox) -> delete, confirmed -> load the
// chosen dump with the same guid, name and account, confirmed (BotDumps::ConfirmLoaded); on failure the safety dump
// instead -> back in its guild at its rank -> release (population bots are logged in by the population, others by
// the bridge) -> `restore` event.
//
// Holds and restarts (Task 4 ruling: fork holds are in memory). No hold is persisted, because none is needed across a
// restart: everything recovery needs is in the guildmaster database before anything is changed (the order row, still
// `running`, and the pre_restore snapshot, confirmed before the mailbox is emptied). At the next boot
// RecoverAtStartup runs in OnStartup, before the first world update, so before the population manager can log anyone
// in: each bot of a cut order that has a confirmed safety snapshot and is not settled yet (settled bots are recorded
// in orders.result_data) is held again and put back from that safety snapshot by a recovery job (same steps: mailbox,
// delete if present, load, confirm, guild, release), and the order is failed with "interrupted by a server restart;
// bot put back from its safety snapshot". A bot cut before its safety snapshot was confirmed was never touched; its
// order is failed with "interrupted by a server restart". So a restart never leaves a bot missing or half-restored.
class RestoreMgr
{
public:
    static RestoreMgr& Instance();
    void RecoverAtStartup();  // OnStartup, before OrderRunner::RecoverAtStartup (sync queries: startup only)
    void Begin(GuildBridge::ParsedOrder const& order, std::function<void(OrderResult const&)> finish);
    void Update(uint32 diff);
    bool IsRestoring(uint32 guid) const;
    std::vector<uint32> RestoringGuids() const;
    uint32 Queued() const { return static_cast<uint32>(_jobs.size()); }

private:
    enum class Stage
    {
        Check,          // checks, then reads the chosen snapshot (async)
        Safety,         // holds the bot, takes the pre_restore snapshot (async)
        ConfirmSafety,  // that snapshot is in the database (async read)
        Logout,         // logs the bot out and waits until it is gone
        Purge,          // empties its mailbox (async, confirmed)
        Delete,         // deletes the character
        Deleted,        // the delete is in the database (async read; asked again on a timeout)
        Paused,         // test seam "crash": stays here until the worldserver restarts
        Load,           // loads the chosen dump (or the safety dump)
        Loaded          // the load is in the database (BotDumps::ConfirmLoaded; asked again on a timeout)
    };

    struct Batch
    {
        uint64 orderId = 0;
        bool single = true;
        std::vector<uint64> restored;
        std::vector<std::pair<uint64, std::string>> failed;
        uint32 left = 0;
        std::function<void(OrderResult const&)> finish;
    };

    struct Job
    {
        uint64 id = 0;    // unique per job: async callbacks check it (and seq) before touching the front job
        uint32 seq = 0;   // bumped on every stage change: a late callback of an earlier step is ignored
        std::shared_ptr<Batch> batch;
        uint32 guid = 0;
        uint64 snapshotId = 0;
        std::string testFail;
        bool dryRun = false;
        Stage stage = Stage::Check;
        uint32 stageMs = 0;
        uint32 account = 0;
        std::string name;
        uint32 guildId = 0;
        uint8 rank = 0;
        bool population = false;
        bool held = false;       // we hold it (released in Done)
        bool wasOnline = false;  // logged in before: a non-population bot is logged back in
        bool loggedOut = false;
        std::string target;
        std::string safety;
        bool usedSafety = false;
        bool recovery = false;  // queued by RecoverAtStartup: target and safety are the safety dump
        std::string targetError;
        bool waiting = false;  // an async step is in flight
    };

    Job* Current(uint64 jobId, uint32 seq);
    void Step(Job& job, uint32 diff);
    void Check(Job& job);
    void Delete(Job& job);
    void CheckDeleted(Job& job);
    void Load(Job& job);
    void Confirm(Job& job);
    void Back(Job& job);
    void Done(Job& job, bool ok, std::string const& reason, bool characterExists = true);
    void SetStage(Job& job, Stage stage)
    {
        job.stage = stage;
        job.stageMs = 0;
        job.waiting = false;
        ++job.seq;
    }

    std::deque<Job> _jobs;  // front = the one running
    uint64 _nextJob = 1;
};

#endif
