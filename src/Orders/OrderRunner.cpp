/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "OrderRunner.h"

#include "BridgeAsync.h"
#include "DatabaseEnv.h"
#include "GuildmasterDatabase.h"
#include "Log.h"
#include "QueryCallback.h"
#include "SimpleOrders.h"
#include "StringFormat.h"
#include <ctime>

uint64 OrderRunner::_finished = 0;

namespace
{
// orders.result is VARCHAR(255) utf8mb4: cut on a character boundary, never inside one (MySQL would refuse
// the whole update and the order would never finish).
std::string FitResult(std::string const& text)
{
    if (text.size() <= 255)
        return text;
    std::size_t cut = 255;
    while (cut && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return text.substr(0, cut);
}

// A JSON null comes back from ->> as the text "null": the same as absent.
std::string FieldText(Field const& field)
{
    std::string value = field.Get<std::string>();
    return value == "null" ? "" : value;
}
}  // namespace

OrderRunner& OrderRunner::Instance()
{
    static OrderRunner instance;
    return instance;
}

void OrderRunner::RecoverAtStartup()
{
    GuildmasterDatabase.DirectExecute(Acore::StringFormat(
        "UPDATE orders SET status = 'failed', result = 'interrupted by a server restart', done_at = {} "
        "WHERE status = 'running' AND type NOT IN ('restore')",
        static_cast<uint32>(std::time(nullptr))));
    // Restores are recovered by RestoreMgr (Task 10), which may have to put a deleted character back first.
}

void OrderRunner::Finish(uint64 id, OrderResult const& result)
{
    GuildmasterPreparedStatement* stmt = GuildmasterDatabase.GetPreparedStatement(GM_UPD_ORDER_FINISH);
    stmt->SetData(0, std::string(result.ok ? "done" : "failed"));
    stmt->SetData(1, FitResult(result.text));
    stmt->SetData(2, result.data);
    stmt->SetData(3, static_cast<uint32>(std::time(nullptr)));
    stmt->SetData(4, id);
    GuildmasterDatabase.Execute(stmt);
    Instance()._running.erase(id);
    ++_finished;
    LOG_INFO("module.guildbridge", "order {} {}: {}", id, result.ok ? "done" : "failed", result.text);
}

void OrderRunner::Update(uint32 diff)
{
    _timer += diff;
    if (_timer < POLL_MS || _polling || !GuildmasterDatabaseReady)
        return;
    _timer = 0;
    Poll();
}

void OrderRunner::Poll()
{
    _polling = true;
    // One async writer (GuildmasterDatabase.WorkerThreads = 1): this read runs after every claim and finish
    // queued before it, so a claimed or finished order is never read as pending again.
    BridgeAsync::Add(GuildmasterDatabase.AsyncQuery(
        "SELECT id, type, IFNULL(params->>'$.bot',''), IFNULL(params->>'$.focus',''), IFNULL(params->>'$.guild',''), "
        "IFNULL(params->>'$.rank',''), IFNULL(params->>'$.first',''), IFNULL(params->>'$.second',''), "
        "IFNULL(params->>'$.dungeon',''), IFNULL(params->>'$.party[0]',''), IFNULL(params->>'$.party[1]',''), "
        "IFNULL(params->>'$.party[2]',''), IFNULL(params->>'$.party[3]',''), IFNULL(params->>'$.party[4]',''), "
        "IFNULL(CAST(JSON_LENGTH(params->'$.party') AS CHAR),''), IFNULL(params->>'$.heroic',''), "
        "IFNULL(params->>'$.approach',''), IFNULL(params->>'$.snapshot_id',''), IFNULL(params->>'$.taken_before',''), "
        "IFNULL(params->>'$.dry_run',''), IFNULL(params->>'$.test_fail','') "
        "FROM orders WHERE status = 'pending' ORDER BY id LIMIT 20")
                         .WithCallback([this](QueryResult result) {
                             _polling = false;
                             if (!result)
                                 return;
                             do
                             {
                                 Field* f = result->Fetch();
                                 GuildBridge::OrderRow row;
                                 row.id = f[0].Get<uint64>();
                                 row.type = f[1].Get<std::string>();
                                 row.bot = FieldText(f[2]);
                                 row.focus = FieldText(f[3]);
                                 row.guild = FieldText(f[4]);
                                 row.rank = FieldText(f[5]);
                                 row.first = FieldText(f[6]);
                                 row.second = FieldText(f[7]);
                                 row.dungeon = FieldText(f[8]);
                                 for (int i = 0; i < 5; ++i)
                                     row.party[i] = FieldText(f[9 + i]);
                                 row.partyLength = FieldText(f[14]);
                                 row.heroic = FieldText(f[15]);
                                 row.approach = FieldText(f[16]);
                                 row.snapshotId = FieldText(f[17]);
                                 row.takenBefore = FieldText(f[18]);
                                 row.dryRun = FieldText(f[19]);
                                 row.testFail = FieldText(f[20]);
                                 if (!_running.count(row.id))
                                     Handle(row);
                             } while (result->NextRow());
                         }));
}

void OrderRunner::Handle(GuildBridge::OrderRow const& row)
{
    GuildBridge::ParsedOrder order;
    std::string const error = GuildBridge::ParseOrder(row, order);
    if (!error.empty())
    {
        Finish(row.id, {false, error, ""});
        return;
    }
    GuildmasterDatabase.Execute(
        "UPDATE orders SET status = 'running', started_at = {} WHERE id = {} AND status = 'pending'",
        static_cast<uint32>(std::time(nullptr)), row.id);
    _running.insert(row.id);

    uint64 const id = row.id;
    switch (order.type)
    {
        case GuildBridge::OrderType::Focus:
        case GuildBridge::OrderType::Invite:
        case GuildBridge::OrderType::Remove:
        case GuildBridge::OrderType::Rank:
        case GuildBridge::OrderType::PresetProfessions:
            Finish(id, SimpleOrders::Run(order));
            break;
        case GuildBridge::OrderType::Snapshot:
            SimpleOrders::Snapshot(order, [id](OrderResult const& result) { Finish(id, result); });
            break;
        case GuildBridge::OrderType::SnapshotAll:
            SimpleOrders::SnapshotAll(id, order.dryRun, [id](OrderResult const& result) { Finish(id, result); });
            break;
        default:
            // restore: Task 10; run_dungeon: 11; create_guild, create_founders: 15
            Finish(id, {false, "order type not available yet", ""});
            break;
    }
}
