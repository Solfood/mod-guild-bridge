/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "RestoreMgr.h"

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "DumpRules.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "EventPayloads.h"
#include "EventSink.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildRegistry.h"
#include "GuildmasterDatabase.h"
#include "Json.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotsAdapter.h"
#include "QueryCallback.h"
#include "StringFormat.h"
#include <unordered_map>
#include <unordered_set>

using namespace GuildBridge;

namespace
{
constexpr uint32 STEP_TIMEOUT_MS = 120000;  // an async step (snapshot, database round trip)
constexpr uint32 LOGOUT_TIMEOUT_MS = 60000;
constexpr uint32 MAX_TRIES = 3;  // purge and delete attempts
char const* const RECOVERED = "interrupted by a server restart; bot put back from its safety snapshot";

ObjectGuid PlayerGuid(uint32 guid) { return ObjectGuid::Create<HighGuid::Player>(guid); }
}  // namespace

RestoreMgr& RestoreMgr::Instance()
{
    static RestoreMgr instance;
    return instance;
}

bool RestoreMgr::IsRestoring(uint32 guid) const
{
    for (Job const& job : _jobs)
        if (job.guid == guid && !job.dryRun)
            return true;
    return false;
}

std::vector<uint32> RestoreMgr::RestoringGuids() const
{
    std::vector<uint32> guids;
    for (Job const& job : _jobs)
        if (!job.dryRun)
            guids.push_back(job.guid);
    return guids;
}

RestoreMgr::Job* RestoreMgr::Current(uint64 jobId, uint32 seq)
{
    if (_jobs.empty() || _jobs.front().id != jobId || _jobs.front().seq != seq)
        return nullptr;  // that job is finished, or this answers an earlier step that was given up on
    return &_jobs.front();
}

// Startup only (sync queries are allowed before the world runs). Every restore order still `running` was cut by a
// restart. For each bot of such an order that has a confirmed safety snapshot and is not settled yet (Done records
// settled bots in orders.result_data), a recovery job is queued: it puts the bot back from that safety snapshot
// through the same steps as a restore (empty mailbox, delete if present, load, confirm, guild, release). The bot is
// held from here on, so the population manager cannot log it in (or log in a character that is not there yet) before
// the first world update runs the job. Orders with nothing to recover are finished at once.
void RestoreMgr::RecoverAtStartup()
{
    QueryResult orders = GuildmasterDatabase.Query("SELECT id FROM orders WHERE type = 'restore' AND status = 'running'");
    if (!orders)
        return;
    std::unordered_map<uint64, std::shared_ptr<Batch>> batches;
    do
    {
        auto batch = std::make_shared<Batch>();
        batch->orderId = (*orders)[0].Get<uint64>();
        uint64 const orderId = batch->orderId;
        batch->finish = [orderId](OrderResult const& result) { OrderRunner::Finish(orderId, result); };
        batches[orderId] = batch;
    } while (orders->NextRow());

    QueryResult safety = GuildmasterDatabase.Query(
        "SELECT s.order_id, s.id, s.guid, s.account, s.name, s.guild_id, s.guild_rank, s.dump FROM bot_snapshots s "
        "JOIN orders o ON o.id = s.order_id WHERE o.type = 'restore' AND o.status = 'running' AND s.reason = 'pre_restore' "
        "AND NOT JSON_CONTAINS(IFNULL(o.result_data->'$.settled', JSON_ARRAY()), CAST(s.guid AS JSON)) "
        "ORDER BY s.order_id, s.id DESC");
    std::unordered_set<uint32> queued;
    if (safety)
        do
        {
            Field* f = safety->Fetch();
            auto const it = batches.find(f[0].Get<uint64>());
            uint32 const guid = f[2].Get<uint32>();
            if (it == batches.end() || !queued.insert(guid).second)
                continue;
            Job job;
            job.id = _nextJob++;
            job.batch = it->second;
            job.recovery = true;
            job.guid = guid;
            job.snapshotId = f[1].Get<uint64>();
            job.account = f[3].Get<uint32>();
            job.name = f[4].Get<std::string>();
            job.guildId = f[5].Get<uint32>();
            job.rank = f[6].Get<uint8>();
            job.population = PlayerbotsAdapter::IsRandomBot(guid);
            job.target = f[7].Get<std::string>();
            job.safety = job.target;
            job.safetyId = job.snapshotId;
            job.stage = Stage::Logout;  // nobody is online at boot: goes straight on to the mailbox
            PlayerbotsAdapter::Hold(guid);
            job.held = true;
            ++job.batch->left;
            LOG_WARN("module.guildbridge", "restore order {} was cut by a restart: putting {} (guid {}) back from "
                     "safety snapshot {}", job.batch->orderId, job.name, guid, job.snapshotId);
            _jobs.push_back(std::move(job));
        } while (safety->NextRow());

    for (auto const& [orderId, batch] : batches)
        if (!batch->left)  // never got as far as a confirmed safety snapshot: the bot was not touched
            OrderRunner::Finish(orderId, {false, "interrupted by a server restart", ""});
}

void RestoreMgr::Begin(ParsedOrder const& order, std::function<void(OrderResult const&)> finish)
{
    auto batch = std::make_shared<Batch>();
    batch->orderId = order.id;
    batch->finish = std::move(finish);

    if (!order.guild.empty())
    {
        GuildRole const role = GuildRegistry::RoleFromName(order.guild);
        uint32 const guildId = GuildRegistry::Instance().GuildIdFor(role);
        if (!guildId)
        {
            batch->finish({false, "no " + order.guild + " guild yet", ""});
            return;
        }
        if (!order.testFail.empty() && role != GuildRole::Test)
        {
            batch->finish({false, "test seams only work on test-guild bots", ""});
            return;
        }
        batch->single = false;
        bool const dryRun = order.dryRun;
        std::string const testFail = order.testFail;
        uint32 const takenBefore = order.takenBefore;
        // Two reads, one per database (no statement names another database): the members (the leader, rank 0, is
        // never restored: deleting it would hand the guild to someone else), then each one's newest snapshot.
        BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
            "SELECT guid FROM guild_member WHERE guildid = {} AND `rank` > 0", guildId)).WithCallback(
            [this, batch, dryRun, testFail, takenBefore](QueryResult members) {
                if (!members)
                {
                    batch->finish({false, "the guild has no members to restore", ""});
                    return;
                }
                std::string in;
                std::vector<uint32> guids;
                do
                {
                    guids.push_back((*members)[0].Get<uint32>());
                    in += (in.empty() ? "" : ",") + std::to_string(guids.back());
                } while (members->NextRow());
                BridgeAsync::Add(GuildmasterDatabase.AsyncQuery(Acore::StringFormat(
                    "SELECT guid, MAX(id) FROM bot_snapshots WHERE guid IN ({}) AND taken_at <= {} "
                    "AND reason <> 'pre_restore' GROUP BY guid", in, takenBefore)).WithCallback(
                    [this, batch, dryRun, testFail, guids](QueryResult snaps) {
                        std::unordered_map<uint32, uint64> newest;
                        if (snaps)
                            do
                                newest[(*snaps)[0].Get<uint32>()] = (*snaps)[1].Get<uint64>();
                            while (snaps->NextRow());
                        std::vector<std::pair<uint32, uint64>> todo;
                        for (uint32 guid : guids)
                        {
                            auto const it = newest.find(guid);
                            if (it != newest.end())
                                todo.emplace_back(guid, it->second);
                            else
                                batch->failed.emplace_back(guid, "no snapshot that old");
                        }
                        if (dryRun)
                        {
                            batch->finish({true, "dry run: would restore " + std::to_string(todo.size()) +
                                                     " bot(s), " + std::to_string(batch->failed.size()) +
                                                     " without a snapshot that old", ""});
                            return;
                        }
                        if (todo.empty())
                        {
                            batch->finish({false, "no member has a snapshot that old", ""});
                            return;
                        }
                        batch->left = static_cast<uint32>(todo.size());
                        for (auto const& [guid, snap] : todo)
                        {
                            Job job;
                            job.id = _nextJob++;
                            job.batch = batch;
                            job.guid = guid;
                            job.snapshotId = snap;
                            job.testFail = testFail;
                            _jobs.push_back(std::move(job));
                        }
                    }));
            }));
        return;
    }

    if (!order.dryRun && IsRestoring(order.bot))
    {
        batch->finish({false, "bot is being restored", ""});
        return;
    }
    batch->left = 1;
    Job job;
    job.id = _nextJob++;
    job.batch = batch;
    job.guid = order.bot;
    job.snapshotId = order.snapshotId;
    job.testFail = order.testFail;
    job.dryRun = order.dryRun;
    _jobs.push_back(std::move(job));
}

void RestoreMgr::Update(uint32 diff)
{
    if (!_jobs.empty())
        Step(_jobs.front(), diff);
}

void RestoreMgr::Step(Job& job, uint32 diff)
{
    job.stageMs += diff;
    if (job.stage == Stage::Paused)
        return;  // test seam "crash": waits here for the worldserver restart
    if (job.waiting && job.stage != Stage::Logout && job.stageMs > STEP_TIMEOUT_MS)
    {
        if (job.stage == Stage::Purged || job.stage == Stage::Deleted || job.stage == Stage::Loaded)
        {
            // From the mailbox purge on (the point of no return) the bot is never given up on: ask the database again
            // (preflight D18: never act blindly; the late answer to the earlier question is ignored).
            LOG_WARN("module.guildbridge", "restore of guid {}: no answer from the characters database, asking again",
                     job.guid);
            SetStage(job, job.stage);
            return;
        }
        Done(job, false, "restore timed out before the bot was deleted; bot unchanged");
        return;
    }
    if (job.waiting && job.stage != Stage::Logout)
        return;

    uint64 const jobId = job.id;
    ObjectGuid const guid = PlayerGuid(job.guid);
    switch (job.stage)
    {
        case Stage::Check:
            Check(job);
            return;
        case Stage::Safety:
            // Held from here on: the population manager leaves it alone and no raising can start on it (fork).
            if (PlayerbotsAdapter::IsRaising(job.guid))
                return Done(job, false, "bot is being raised");
            PlayerbotsAdapter::Hold(job.guid);
            job.held = true;
            job.wasOnline = ObjectAccessor::FindConnectedPlayer(guid) != nullptr;
            job.waiting = true;
            BotDumps::Instance().Request(job.guid, "pre_restore", job.batch->orderId,
                [this, jobId, seq = job.seq](bool ok, std::string const& error, std::string const& dump) {
                    Job* current = Current(jobId, seq);
                    if (!current)
                        return;
                    if (!ok)
                        return Done(*current, false, "could not take the safety snapshot: " + error);
                    current->safety = dump;
                    SetStage(*current, Stage::ConfirmSafety);
                });
            return;
        case Stage::ConfirmSafety:
            // One guildmaster writer: this read runs after the snapshot's insert. Restart recovery needs that row, so
            // nothing is deleted before it is really there. The phase is recorded first (same writer, so it is in
            // before the answer, and so before the purge): from here a cut restore is put back from the safety
            // snapshot by RecoverAtStartup (which reloads every unsettled bot of a cut order; the phase says where).
            GuildmasterDatabase.Execute("UPDATE orders SET result_data = JSON_SET(IFNULL(result_data, JSON_OBJECT()), "
                                        "'$.phase', 'past_safety', '$.guid', {}) WHERE id = {} AND status = 'running'",
                                        job.guid, job.batch->orderId);
            job.waiting = true;
            BridgeAsync::Add(GuildmasterDatabase.AsyncQuery(Acore::StringFormat(
                "SELECT MAX(id) FROM bot_snapshots WHERE order_id = {} AND guid = {} AND reason = 'pre_restore'",
                job.batch->orderId, job.guid)).WithCallback([this, jobId, seq = job.seq](QueryResult result) {
                Job* current = Current(jobId, seq);
                if (!current)
                    return;
                if (!result || !(*result)[0].Get<uint64>())
                    return Done(*current, false, "the safety snapshot did not reach the database; bot unchanged");
                current->safetyId = (*result)[0].Get<uint64>();
                SetStage(*current, Stage::Logout);
            }));
            return;
        case Stage::Logout:
        {
            Player* player = ObjectAccessor::FindConnectedPlayer(guid);
            if (!job.waiting)
            {
                job.waiting = true;
                if (player)
                    PlayerbotsAdapter::Logout(guid);
                return;
            }
            if (player)
            {
                if (job.stageMs > LOGOUT_TIMEOUT_MS)
                    Done(job, false, "bot did not log out");
                return;
            }
            job.loggedOut = job.wasOnline;
            SetStage(job, Stage::Purge);
            return;
        }
        case Stage::Purge:
        {
            // Player::DeleteFromDB returns every player-sent letter in the mailbox to its sender (with its gold and
            // items) while the dump brings its own mailbox back. So the letters that are in the safety dump (the
            // mailbox when the restore began) are emptied first, by id, in one transaction: that commit is the
            // point of no return (they then live only in the dumps), and every later failure either finishes the
            // restore, puts the safety dump back, or says what was lost. A letter that arrived after the dump is
            // in no dump: it is left to the delete, which returns it to its sender.
            std::vector<uint64_t> const ids = DumpMailIds(job.safety);
            job.mailIds = JoinIds(ids);
            job.mailCount = static_cast<uint32>(ids.size());
            if (ids.empty())
            {
                SetStage(job, Stage::Delete);
                return;
            }
            ++job.purgeTries;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            trans->Append(Acore::StringFormat(
                "DELETE FROM item_instance WHERE guid IN (SELECT item_guid FROM mail_items WHERE mail_id IN ({}))",
                job.mailIds));
            trans->Append(Acore::StringFormat("DELETE FROM mail_items WHERE mail_id IN ({})", job.mailIds));
            trans->Append(Acore::StringFormat("DELETE FROM mail WHERE id IN ({})", job.mailIds));
            CharacterDatabase.CommitTransaction(trans);
            SetStage(job, Stage::Purged);
            return;
        }
        case Stage::Purged:
            // CharacterDatabase.WorkerThreads = 1 (pinned): this read runs after the purge transaction.
            job.waiting = true;
            BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
                "SELECT COUNT(*) FROM mail WHERE id IN ({})", job.mailIds)).WithCallback(
                [this, jobId, seq = job.seq](QueryResult result) {
                    Job* current = Current(jobId, seq);
                    if (!current || !result)
                        return;  // no answer: the step timeout asks again
                    if (current->testFail == "purge_timeout" && !current->seamUsed)
                    {
                        current->seamUsed = true;  // test seam: this answer is "lost"; the timeout asks again
                        current->stageMs = STEP_TIMEOUT_MS + 1;
                        return;
                    }
                    if ((*result)[0].Get<uint64>() == 0)
                        return SetStage(*current, Stage::Delete);
                    // The purge is one transaction: letters still there means it did not commit and nothing is
                    // lost yet. Try again, then give up with the bot untouched.
                    if (current->purgeTries < MAX_TRIES)
                        return SetStage(*current, Stage::Purge);
                    Done(*current, false, "could not empty the bot's mailbox; bot unchanged");
                }));
            return;
        case Stage::Delete:
            Delete(job);
            return;
        case Stage::Deleted:
            CheckDeleted(job);
            return;
        case Stage::Load:
            Load(job);
            return;
        case Stage::Loaded:
            Confirm(job);
            return;
        case Stage::Paused:
            return;
    }
}

void RestoreMgr::Check(Job& job)
{
    ObjectGuid const guid = PlayerGuid(job.guid);
    CharacterCacheEntry const* cache = sCharacterCache->GetCharacterCacheByGuid(guid);
    if (!cache)
        return Done(job, false, "no such bot");
    // Busy order (preflight D10): run -> restore -> held.
    if (BridgeRunIdFor(job.guid))
        return Done(job, false, "bot is in a dungeon run");
    for (Job const& other : _jobs)
        if (&other != &job && other.guid == job.guid && !other.dryRun)
            return Done(job, false, "bot is being restored");
    if (PlayerbotsAdapter::IsHeld(job.guid))
        return Done(job, false, "bot is busy (lent to a dungeon run or being restored)");
    if (PlayerbotsAdapter::IsRaising(job.guid))
        return Done(job, false, "bot is being raised");
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (player && !PlayerbotsAdapter::IsBot(player))
        return Done(job, false, "a player is logged in on that character");
    Guild* guild = cache->GuildId ? sGuildMgr->GetGuildById(cache->GuildId) : nullptr;
    // Deleting a leader hands its guild to someone else (or disbands it) for good.
    if (guild && guild->GetLeaderGUID() == guid)
        return Done(job, false, "bot leads its guild");
    if (!job.testFail.empty() && GuildRegistry::Instance().RoleOf(cache->GuildId) != GuildRole::Test)
        return Done(job, false, "test seams only work on test-guild bots");
    job.account = cache->AccountId;
    job.name = cache->Name;
    job.guildId = cache->GuildId;
    if (Guild::Member const* member = guild ? guild->GetMember(guid) : nullptr)
        job.rank = member->GetRankId();
    job.population = PlayerbotsAdapter::IsRandomBot(job.guid);

    job.waiting = true;
    uint64 const jobId = job.id;
    BridgeAsync::Add(GuildmasterDatabase.AsyncQuery(Acore::StringFormat(
        "SELECT guid, dump FROM bot_snapshots WHERE id = {}", job.snapshotId)).WithCallback(
        [this, jobId, seq = job.seq](QueryResult result) {
            Job* current = Current(jobId, seq);
            if (!current)
                return;
            if (!result)
                return Done(*current, false, "no such snapshot");
            if ((*result)[0].Get<uint32>() != current->guid)
                return Done(*current, false, "snapshot belongs to another bot");
            if (current->dryRun)
                return Done(*current, true, "dry run: would restore " + current->name + " from snapshot " +
                                                std::to_string(current->snapshotId));
            current->target = (*result)[1].Get<std::string>();
            SetStage(*current, Stage::Safety);
        }));
}

void RestoreMgr::Delete(Job& job)
{
    // deleteFinally = true: removed for good whatever CharDelete.Method says (Plan 1b unlinks deleted characters for
    // the hall of legends; a restore must free the guid and the name for the reload). DeleteFromDB makes a few sync
    // reads on the world thread (mail, pets, friends): restores only, like the pdump load (preflight D7).
    ++job.deleteTries;
    if (job.testFail == "delete_fail")
        LOG_WARN("module.guildbridge", "restore test seam: the delete of guid {} is not sent", job.guid);
    else
        Player::DeleteFromDB(job.guid, job.account, false, true);
    LOG_INFO("module.guildbridge", "restore: deleted {} (guid {}) for order {}", job.name, job.guid,
             job.batch->orderId);
    if (job.testFail == "crash")
    {
        LOG_WARN("module.guildbridge", "restore test seam: stopping after the delete until the worldserver restarts");
        SetStage(job, Stage::Paused);
        return;
    }
    SetStage(job, Stage::Deleted);
}

void RestoreMgr::CheckDeleted(Job& job)
{
    job.waiting = true;
    uint64 const jobId = job.id;
    // One characters writer: this read runs after the delete transaction. The guid must be free before the load,
    // or the loader silently gives the bot a new guid.
    BridgeAsync::Add(CharacterDatabase.AsyncQuery(Acore::StringFormat(
        "SELECT COUNT(*) FROM characters WHERE guid = {}", job.guid)).WithCallback(
        [this, jobId, seq = job.seq](QueryResult result) {
            Job* current = Current(jobId, seq);
            if (!current)
                return;
            if (!result)
                return;  // no answer: the step timeout asks again
            if ((*result)[0].Get<uint64>() != 0)
            {
                if (current->deleteTries < MAX_TRIES)
                {
                    LOG_WARN("module.guildbridge", "restore: the delete of guid {} did not reach the database, "
                             "trying again", current->guid);
                    return SetStage(*current, Stage::Delete);
                }
                // The safety dump cannot be loaded over a character that is still there (the loader would give it a
                // new guid), so the loss is stated: the purged letters live on in the safety snapshot.
                LOG_ERROR("module.guildbridge", "restore: the delete of guid {} never reached the database; its "
                          "mailbox ({} letters) was emptied: restore pre_restore snapshot {} to get them back",
                          current->guid, current->mailCount, current->safetyId);
                return Done(*current, false, "delete failed: the character is still there but its mailbox was "
                            "emptied (" + std::to_string(current->mailCount) + " letter(s) with their items and "
                            "gold); restore pre_restore snapshot " + std::to_string(current->safetyId) +
                            " to get them back (restart the world first if the bot cannot log in)");
            }
            SetStage(*current, Stage::Load);
        }));
}

void RestoreMgr::Load(Job& job)
{
    bool const safetyLoad = job.usedSafety || job.recovery;
    std::string const& dump = safetyLoad ? job.safety : job.target;
    std::string error;
    bool loaded = false;
    if (!safetyLoad && job.testFail == "load")
        error = "test seam: the chosen snapshot does not load";
    else
        loaded = BotDumps::LoadOnWorldThread(dump, job.account, job.name, job.guid, error);
    if (loaded)
    {
        SetStage(job, Stage::Loaded);
        return;
    }
    if (!safetyLoad)
    {
        LOG_WARN("module.guildbridge", "restore of {} failed ({}), loading its safety snapshot", job.name, error);
        job.usedSafety = true;
        job.targetError = error;
        SetStage(job, Stage::Load);
        return;
    }
    LOG_ERROR("module.guildbridge", "RESTORE LOST guid {} ({}): {} / {}; load pre_restore snapshot {} by hand",
              job.guid, job.name, job.targetError, error, job.recovery ? job.snapshotId : 0);
    Done(job, false, "BOT LOST: load its pre_restore snapshot by hand (" + error + ")", false);
}

void RestoreMgr::Confirm(Job& job)
{
    job.waiting = true;
    uint64 const jobId = job.id;
    // Only now is the load really in the database (Task 5 ruling: ConfirmLoaded before any success).
    BotDumps::ConfirmLoaded(job.name, [this, jobId, seq = job.seq](bool ok, uint32 loadedGuid) {
        Job* current = Current(jobId, seq);
        if (!current)
            return;
        if (!ok)
        {
            // The load's transaction was refused: nothing of it is in the database and the name is free again.
            if (current->usedSafety || current->recovery)
            {
                LOG_ERROR("module.guildbridge", "RESTORE LOST guid {} ({}): the safety load did not reach the database",
                          current->guid, current->name);
                return Done(*current, false, "BOT LOST: load its pre_restore snapshot by hand (the load did not "
                                             "reach the database)", false);
            }
            current->usedSafety = true;
            current->targetError = "the load did not reach the database";
            return SetStage(*current, Stage::Load);
        }
        if (loadedGuid != current->guid)
        {
            LOG_ERROR("module.guildbridge", "restore: {} came back as guid {} instead of {}", current->name,
                      loadedGuid, current->guid);
            return Done(*current, false, "the bot came back under guid " + std::to_string(loadedGuid) +
                                             " instead of " + std::to_string(current->guid) + "; check it by hand");
        }
        Back(*current);
    });
}

void RestoreMgr::Back(Job& job)
{
    std::string note;
    if (Guild* guild = job.guildId ? sGuildMgr->GetGuildById(job.guildId) : nullptr)
        if (!guild->GetMember(PlayerGuid(job.guid)))
            // The bot is offline: AddMember reads its stats with one sync query (restores only, preflight D7).
            if (!guild->AddMember(PlayerGuid(job.guid), job.rank) &&
                !guild->AddMember(PlayerGuid(job.guid), GUILD_RANK_NONE))
                note = "; could not put it back in " + guild->GetName();
    if (job.recovery)
        return Done(job, false, RECOVERED + note);
    if (job.usedSafety)
        return Done(job, false, "restore failed; bot put back as it was (" + job.targetError + ")" + note);
    if (!note.empty())
        return Done(job, false, "restored from snapshot " + std::to_string(job.snapshotId) + note);
    Done(job, true, job.name + " restored from snapshot " + std::to_string(job.snapshotId));
}

void RestoreMgr::Done(Job& job, bool ok, std::string const& reason, bool characterExists)
{
    ObjectGuid const guid = PlayerGuid(job.guid);
    // A lost bot stays held: the population manager would keep trying to log in a character that is not there.
    if (job.held && characterExists)
        PlayerbotsAdapter::Release(job.guid);  // a population bot: the population manager logs it back in
    if (characterExists && job.loggedOut && !job.population && !ObjectAccessor::FindConnectedPlayer(guid))
        PlayerbotsAdapter::LoginMasterless(guid);  // clones and other bots outside the population
    if (!job.dryRun)
        EventSink::Instance().Push(EventType::Restore, job.guid, job.guildId, 0,
                                   RestorePayload(job.snapshotId, ok, reason));
    LOG_INFO("module.guildbridge", "restore order {} guid {}: {} {}", job.batch->orderId, job.guid,
             ok ? "ok" : "failed", reason);

    std::shared_ptr<Batch> batch = job.batch;
    if (ok)
        batch->restored.push_back(job.guid);
    else
        batch->failed.emplace_back(job.guid, reason);
    std::string const text = reason;
    bool const dryRun = job.dryRun;
    _jobs.pop_front();  // `job` is gone from here on
    if (--batch->left)
    {
        // Settled bots, for restart recovery: it must not rewind a bot this order already finished with.
        std::vector<uint64> settled = batch->restored;
        for (auto const& [failedGuid, why] : batch->failed)
            settled.push_back(failedGuid);
        GuildmasterDatabase.Execute("UPDATE orders SET result_data = '{}' WHERE id = {} AND status = 'running'",
                                    JsonObject().UIntArray("settled", settled).Build(), batch->orderId);
        return;
    }
    std::string failed = "[";
    for (std::size_t i = 0; i < batch->failed.size(); ++i)
        failed += (i ? "," : "") +
                  JsonObject().UInt("guid", batch->failed[i].first).Str("reason", batch->failed[i].second).Build();
    failed += "]";
    std::string const data = JsonObject().UIntArray("restored", batch->restored).Raw("failed", failed).Build();
    if (batch->single)
    {
        batch->finish({ok, text, dryRun ? "" : data});
        return;
    }
    batch->finish({batch->failed.empty(),
                   std::to_string(batch->restored.size()) + " bot(s) restored, " +
                       std::to_string(batch->failed.size()) + " failed",
                   data});
}
