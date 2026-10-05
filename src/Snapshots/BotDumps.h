/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_BOTDUMPS_H
#define MOD_GUILD_BRIDGE_BOTDUMPS_H

#include "Define.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Per-bot snapshots as player-dump text in guildmaster.bot_snapshots. Dumps are written on a worker thread
// (sync reads of the characters DB, never on the world thread); loads happen on the world thread.
// Sync-query rule (preflight D7): none on map threads; on the world thread only at startup, plus the pdump load
// here (it hands out new item/mail/pet ids, which only the world thread may do).
// No bot is held while it is dumped or cloned (the bot is only saved), so nothing here needs to survive a
// restart: a dump in flight at shutdown is finished by Stop(); a queued Done callback is simply not run.
class BotDumps
{
public:
    using Done = std::function<void(bool ok, std::string const& error, std::string const& dump)>;

    static BotDumps& Instance();
    void Start();              // also runs the retention once
    void Stop();
    void Update(uint32 diff);  // world thread: completions

    // World thread. Saves an online bot first; the dump is taken after that save reached the database.
    // `reason`: scheduled | pre_order | manual | pre_restore (the bot_snapshots ENUM).
    void Request(uint32 guid, char const* reason, uint64 orderId, Done done = {});
    // World thread. Loads a dump onto `account` as `name`; guid 0 = a new guid.
    static bool LoadOnWorldThread(std::string const& dump, uint32 account, std::string const& name, uint32 guid,
                                  std::string& error);
    // World thread, right after a successful LoadOnWorldThread: the load is one async transaction, so it can
    // still fail (for example a duplicate key). `done(ok)` runs on the world thread once that transaction has
    // been applied (one async writer: a query queued after it returns after it). On failure the character
    // cache entry the load added is dropped again, so the name is free.
    static void ConfirmLoaded(std::string const& name, std::function<void(bool ok, uint32 guid)> done);
    void RunRetention();  // world thread
    uint64 Taken() const { return _taken; }
    uint64 Failed() const { return _failed; }

private:
    struct Job
    {
        uint64 token = 0;
        uint32 guid = 0;
        uint32 account = 0;
        std::string name;
        uint8 level = 0;
        uint32 guildId = 0;
        uint8 guildRank = 0;
        std::string reason;
        uint64 orderId = 0;
    };
    struct Completion
    {
        uint64 token = 0;
        bool ok = false;
        std::string error;
        std::string dump;
    };

    void Enqueue(Job job);
    void WorkerLoop();

    std::thread _worker;
    std::mutex _mutex;
    std::condition_variable _wake;
    std::deque<Job> _jobs;
    std::vector<Completion> _completions;
    bool _stopping = false;
    std::vector<std::pair<uint64, Done>> _waiting;  // world thread only
    uint64 _nextToken = 1;                          // world thread only
    std::atomic<uint64> _taken{0};
    std::atomic<uint64> _failed{0};
};

#endif
