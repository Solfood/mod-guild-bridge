/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_DATABASE_H
#define MOD_GUILD_BRIDGE_DATABASE_H

#include "DatabaseEnvFwd.h"
#include "ModuleDatabasePool.h"
#include "MySQLConnection.h"
#include "PreparedStatement.h"
#include "Transaction.h"
#include <memory>

// Prepared statements for every write that carries text (names, JSON, dumps): no hand-escaping anywhere.
// Numeric-only updates use plain Execute("...{}...") strings. Later tasks append ids here (before MAX_).
enum GuildmasterStatements : uint32
{
    GM_INS_EVENT,
    GM_REP_BOT_STATE,
    GM_INS_SNAPSHOT,
    GM_UPD_ORDER_FINISH,
    GM_REP_GUILD,
    GM_REP_FOCUS,
    GM_INS_FIRST,
    GM_INS_RUN,
    GM_UPD_RUN_END,
    GM_INS_INCIDENT,
    GM_UPS_BOT_OFFLINE,
    GM_UPD_WORLD_STATUS,
    GM_REP_PROFILE,
    GM_INS_ROUTE_HUB,
    GM_INS_QUEST_DROP,
    MAX_GUILDMASTER_STATEMENTS
};

class GuildmasterDatabaseConnection : public MySQLConnection
{
public:
    typedef GuildmasterStatements Statements;

    GuildmasterDatabaseConnection(MySQLConnectionInfo& connInfo) : MySQLConnection(connInfo) {}
    GuildmasterDatabaseConnection(ProducerConsumerQueue<SQLOperation*>* queue, MySQLConnectionInfo& connInfo)
        : MySQLConnection(queue, connInfo)
    {
    }

    void DoPrepareStatements() override;
};

using GuildmasterPreparedStatement = PreparedStatement<GuildmasterDatabaseConnection>;
using GuildmasterTransaction = std::shared_ptr<Transaction<GuildmasterDatabaseConnection>>;

class GuildmasterDatabasePool : public ModuleDatabasePool
{
public:
    GuildmasterPreparedStatement* GetPreparedStatement(GuildmasterStatements index)
    {
        return new GuildmasterPreparedStatement(index, GetPreparedStatementParamCount(index));
    }

    using ModuleDatabasePool::Execute;
    using ModuleDatabasePool::Query;

    GuildmasterTransaction BeginTransaction() { return std::make_shared<Transaction<GuildmasterDatabaseConnection>>(); }
    void CommitTransaction(GuildmasterTransaction transaction) { ModuleDatabasePool::CommitTransaction(transaction); }

protected:
    MySQLConnection* CreateConnection(MySQLConnectionInfo& connInfo) override
    {
        return new GuildmasterDatabaseConnection(connInfo);
    }

    MySQLConnection* CreateConnection(ProducerConsumerQueue<SQLOperation*>* queue,
                                      MySQLConnectionInfo& connInfo) override
    {
        return new GuildmasterDatabaseConnection(queue, connInfo);
    }
};

extern GuildmasterDatabasePool GuildmasterDatabase;

// True once the pool is open, migrated, stamped and prepared (`bridge status` db=1). A failure stops the boot.
extern bool GuildmasterDatabaseReady;

#endif
