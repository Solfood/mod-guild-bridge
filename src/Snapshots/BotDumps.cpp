/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BotDumps.h"

#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildmasterDatabase.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerDump.h"
#include "QueryCallback.h"
#include <ctime>

BotDumps& BotDumps::Instance()
{
    static BotDumps instance;
    return instance;
}

void BotDumps::Start()
{
    _worker = std::thread([this] { WorkerLoop(); });
    RunRetention();  // a world that was off for a while drops what aged out meanwhile
}

void BotDumps::Stop()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
    }
    _wake.notify_all();
    if (_worker.joinable())
        _worker.join();
}

void BotDumps::Request(uint32 guid, char const* reason, uint64 orderId, Done done)
{
    CharacterCacheEntry const* cache =
        sCharacterCache->GetCharacterCacheByGuid(ObjectGuid::Create<HighGuid::Player>(guid));
    if (!cache)
    {
        if (done)
            done(false, "no such bot", "");
        return;
    }
    Job job;
    job.token = _nextToken++;
    job.guid = guid;
    job.account = cache->AccountId;
    job.name = cache->Name;
    job.level = cache->Level;
    job.guildId = cache->GuildId;
    if (Guild* guild = cache->GuildId ? sGuildMgr->GetGuildById(cache->GuildId) : nullptr)
        if (Guild::Member const* member = guild->GetMember(cache->Guid))
            job.guildRank = member->GetRankId();
    job.reason = reason;
    job.orderId = orderId;
    if (done)
        _waiting.emplace_back(job.token, std::move(done));

    if (Player* bot = ObjectAccessor::FindPlayerByLowGUID(guid))
    {
        job.level = bot->GetLevel();
        bot->SaveToDB(false, false);
        // CharacterDatabase.WorkerThreads = 1 (pinned in the server's worldserver overlay): one async writer, so
        // this query runs after the save above has been written.
        BridgeAsync::Add(CharacterDatabase.AsyncQuery("SELECT 1").WithCallback(
            [this, job](QueryResult) mutable { Enqueue(std::move(job)); }));
    }
    else
        Enqueue(std::move(job));
}

void BotDumps::Enqueue(Job job)
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _jobs.push_back(std::move(job));
    }
    _wake.notify_one();
}

void BotDumps::WorkerLoop()
{
    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stopping || !_jobs.empty(); });
            if (_stopping && _jobs.empty())
                return;
            job = std::move(_jobs.front());
            _jobs.pop_front();
        }

        Completion completion;
        completion.token = job.token;
        std::string dump;
        // Sync reads on this worker's own thread through the core's pool (CharacterDatabase.SynchThreads = 2
        // keeps the world thread from waiting for the only sync connection).
        DumpReturn const result = PlayerDumpWriter().WriteDumpToString(dump, job.guid);
        if (result == DUMP_SUCCESS)
        {
            GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_INS_SNAPSHOT);
            stmt->SetData(0, job.guid);
            stmt->SetData(1, job.account);
            stmt->SetData(2, job.name);
            stmt->SetData(3, job.level);
            stmt->SetData(4, job.guildId);
            stmt->SetData(5, job.guildRank);
            stmt->SetData(6, job.reason);
            stmt->SetData(7, job.orderId);
            stmt->SetData(8, static_cast<uint32>(std::time(nullptr)));
            stmt->SetData(9, static_cast<uint32>(dump.size()));
            stmt->SetData(10, dump);
            GuildmasterDatabase.Execute(stmt);
            completion.ok = true;
            completion.dump = std::move(dump);
            ++_taken;
        }
        else
        {
            completion.error = "player dump failed (code " + std::to_string(static_cast<int>(result)) + ")";
            ++_failed;
            LOG_ERROR("module.guildbridge", "Snapshot of guid {} failed: {}", job.guid, completion.error);
        }
        std::lock_guard<std::mutex> lock(_mutex);
        _completions.push_back(std::move(completion));
    }
}

bool BotDumps::LoadOnWorldThread(std::string const& dump, uint32 account, std::string const& name, uint32 guid,
                                 std::string& error)
{
    DumpReturn const result = PlayerDumpReader().LoadDumpFromString(dump, account, name, guid);
    if (result != DUMP_SUCCESS)
    {
        error = "loading the player dump failed (code " + std::to_string(static_cast<int>(result)) + ")";
        return false;
    }
    // A dump of an online bot carries characters.online = 1. Queued after the load's transaction (one async
    // writer), so the loaded character is not marked online while it is not in the world.
    if (ObjectGuid const loaded = sCharacterCache->GetCharacterGuidByName(name))
        CharacterDatabase.Execute("UPDATE characters SET online = 0 WHERE guid = {}", loaded.GetCounter());
    return true;
}

void BotDumps::RunRetention()
{
    uint32 const keepDays = BridgeConfig::Get().snapshotKeepDays;
    if (!keepDays)
        return;  // 0 = keep every snapshot
    uint32 const cutoff = static_cast<uint32>(std::time(nullptr)) - keepDays * 86400u;
    GuildmasterDatabase.Execute("DELETE FROM bot_snapshots WHERE taken_at < {}", cutoff);
}

void BotDumps::Update(uint32 /*diff*/)
{
    std::vector<Completion> ready;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        ready.swap(_completions);
    }
    for (Completion& completion : ready)
        for (auto it = _waiting.begin(); it != _waiting.end(); ++it)
            if (it->first == completion.token)
            {
                Done done = std::move(it->second);
                _waiting.erase(it);
                done(completion.ok, completion.error, completion.dump);
                break;
            }
}
