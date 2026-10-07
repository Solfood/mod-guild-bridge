/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeConfig.h"
#include "BuiltInConfig.h"
#include "Config.h"
#include "DBUpdater.h"
#include "DatabaseScript.h"
#include "GuildmasterDatabase.h"
#include "LegendsTiming.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "World.h"
#include <mysqld_error.h>

// Opens the guildmaster pool at boot, creates the database if missing, applies base + update SQL.
// Same steps as mod-playerbots' PlayerbotsDatabaseScript. A failure stops the boot: running without the
// bridge's database would silently lose events and orders. Then two per-world steps (sync queries are fine
// here, before the world runs): the world-id stamp, and the legends view on this world's playerbots database. On a
// fresh world's first boot that database does not exist yet (mod-playerbots creates it after this hook: modules load
// in name order), so the view is then made once all databases are loaded; a failure there stops the boot too.
class BridgeDatabaseScript : public DatabaseScript
{
public:
    BridgeDatabaseScript()
        : DatabaseScript("BridgeDatabaseScript",
                         {DATABASEHOOK_ON_MODULE_DATABASES_LOADING, DATABASEHOOK_ON_AFTER_DATABASES_LOADED,
                          DATABASEHOOK_ON_MODULE_DATABASES_KEEPALIVE,
                          DATABASEHOOK_ON_MODULE_DATABASES_CLOSING, DATABASEHOOK_ON_DATABASE_WARN_ABOUT_SYNC_QUERIES})
    {
    }

    bool OnModuleDatabasesLoading() override
    {
        std::string const info = sConfigMgr->GetOption<std::string>("GuildmasterDatabaseInfo", "");
        if (info.empty())
        {
            LOG_ERROR("module.guildbridge", "GuildmasterDatabaseInfo is not set");
            return false;
        }
        GuildmasterDatabase.SetConnectionInfo(info, sConfigMgr->GetOption<uint8>("GuildmasterDatabase.WorkerThreads", 1),
                                              sConfigMgr->GetOption<uint8>("GuildmasterDatabase.SynchThreads", 1));
        if (!DBUpdaterUtil::CheckExecutable())
            return false;

        uint32 error = GuildmasterDatabase.Open();
        if (error == ER_BAD_DB_ERROR)
        {
            if (!ModuleDBUpdater::Create(GuildmasterDatabase))
                return false;
            error = GuildmasterDatabase.Open();
        }
        if (error)
        {
            LOG_ERROR("module.guildbridge", "Cannot connect to the guildmaster database, error {}", error);
            return false;
        }

        DBUpdaterInfo const updater = {"Guildmaster", BuiltInConfig::GetSourceDirectory() + "/modules/mod-guild-bridge",
                                       BuiltInConfig::GetSourceDirectory() +
                                           "/modules/mod-guild-bridge/data/sql/guildmaster/base/",
                                       "db_guildmaster"};
        if (!ModuleDBUpdater::Populate(GuildmasterDatabase, updater) ||
            !ModuleDBUpdater::Update(GuildmasterDatabase, updater))
        {
            LOG_ERROR("module.guildbridge", "Could not create or update the guildmaster database");
            return false;
        }
        if (!StampWorld())
            return false;
        _legendsLater = GuildBridge::LegendsViewTiming(PlayerbotsDatabaseExists()) ==
                        GuildBridge::LegendsViewWhen::AfterAllDatabases;
        if (_legendsLater)
            LOG_INFO("module.guildbridge", "fresh world: the legends view on `{}` is made once all databases are loaded",
                     BridgeConfig::Get().playerbotsDb);
        else if (!CreateLegendsView())
            return false;
        GuildmasterDatabaseReady = GuildmasterDatabase.PrepareStatements();
        return GuildmasterDatabaseReady;
    }

    // A fresh world: mod-playerbots has now created and filled its database. The hook cannot fail the boot itself,
    // so a failure prints the same line, turns the bridge's database off (no heartbeat) and stops the world.
    void OnAfterDatabasesLoaded(uint32 /*updateFlags*/) override
    {
        if (!_legendsLater)
            return;
        _legendsLater = false;
        if (CreateLegendsView())
            return;
        GuildmasterDatabaseReady = false;
        World::StopNow(ERROR_EXIT_CODE);
    }

    // Is the world's playerbots database there yet? (An empty name falls through to CreateLegendsView's own error.)
    static bool PlayerbotsDatabaseExists()
    {
        std::string const& pb = BridgeConfig::Get().playerbotsDb;
        if (pb.empty())
            return true;
        QueryResult const r = GuildmasterDatabase.Query(Acore::StringFormat(
            "SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = '{}'", pb));
        return !r || (*r)[0].Get<uint64>() != 0;
    }

    // One world per database (spec §2b saves). First boot: stamp it. Later boots: it must be the same world.
    static bool StampWorld()
    {
        BridgeConfig::Get().Load();  // OnAfterConfigLoad has not run yet when module databases load
        std::string const& worldId = BridgeConfig::Get().worldId;
        if (!BridgeConfig::IsValidWorldId(worldId))
        {
            LOG_ERROR("module.guildbridge", "GuildBridge.WorldId '{}' is not valid (a-z, 0-9, _ and -, at most 32)",
                      worldId);
            return false;
        }
        // INSERT IGNORE then read back: if two worlds first-boot against the same empty database at once, only
        // one stamp lands and the other world reads the winner's id and refuses (DirectExecute reports nothing).
        GuildmasterDatabase.DirectExecute(Acore::StringFormat(
            "INSERT IGNORE INTO world_status (id, world_id, booted_at) VALUES (1, '{}', UNIX_TIMESTAMP())", worldId));
        QueryResult result = GuildmasterDatabase.Query("SELECT world_id FROM world_status WHERE id = 1");
        if (!result)
        {
            LOG_ERROR("module.guildbridge", "could not stamp the guildmaster database with world {}", worldId);
            return false;
        }
        std::string const stamped = (*result)[0].Get<std::string>();
        if (stamped != worldId)
        {
            LOG_ERROR("module.guildbridge", "guildmaster database belongs to world {}, this server is world {}",
                      stamped, worldId);
            return false;
        }
        return true;
    }

    // The hall of legends reads the fork's raisings table in THIS world's playerbots database. Only death knights
    // that exist are legends: 'created' (made, finishing its first login) and 'done'. In-flight steps (logout,
    // unlink), rollbacks, failures and test rows are left out (contract §2, legends).
    static bool CreateLegendsView()
    {
        std::string const& pb = BridgeConfig::Get().playerbotsDb;
        if (pb.empty())
        {
            LOG_ERROR("module.guildbridge", "PlayerbotsDatabaseInfo has no database name");
            return false;
        }
        GuildmasterDatabase.DirectExecute(Acore::StringFormat(
            "CREATE OR REPLACE VIEW legends AS SELECT id, old_guid, new_guid, account, name, race, gender, team, "
            "old_class, old_level, guild_id, state, raised_at FROM `{}`.`playerbots_raisings` "
            "WHERE state IN ('created', 'done')",
            pb));
        // DirectExecute reports nothing: prove the view exists and reads (no grant, no CREATE VIEW privilege, a
        // table already named legends or a missing playerbots_raisings all fail here and stop the boot).
        QueryResult const isView = GuildmasterDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.VIEWS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'legends'");
        if (!isView || (*isView)[0].Get<uint64>() != 1 || !GuildmasterDatabase.Query("SELECT COUNT(*) FROM legends"))
        {
            LOG_ERROR("module.guildbridge", "could not create the legends view on `{}`.`playerbots_raisings`", pb);
            return false;
        }
        return true;
    }

    void OnModuleDatabasesKeepAlive() override { GuildmasterDatabase.KeepAlive(); }
    void OnModuleDatabasesClosing() override { GuildmasterDatabase.Close(); }
    void OnDatabaseWarnAboutSyncQueries(bool apply) override { GuildmasterDatabase.WarnAboutSyncQueries(apply); }

private:
    bool _legendsLater = false;
};

void AddBridgeDatabaseScripts() { new BridgeDatabaseScript(); }
