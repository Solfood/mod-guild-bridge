/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "DungeonRunMgr.h"
#include "EventSink.h"
#include "Firsts.h"
#include "GuildmasterDatabase.h"
#include "GuildRegistry.h"
#include "Log.h"
#include "NewGameOrders.h"
#include "OrderRunner.h"
#include "RestoreMgr.h"
#include "RouteWriter.h"
#include "ScriptMgr.h"
#include "SimpleOrders.h"
#include "StateWriter.h"
#include "StuckDetector.h"
#include "WorldStatus.h"
#include "WorldScript.h"

// Startup and the one world-thread tick that drives every bridge part (later tasks add calls here).
class BridgeWorldScript : public WorldScript
{
public:
    BridgeWorldScript()
        : WorldScript("BridgeWorldScript", {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE,
                                            WORLDHOOK_ON_SHUTDOWN})
    {
    }

    // Startup only (final review M1): `.reload config` would rewrite settings the map threads read, and turning the
    // bridge off at run time would freeze every run and restore with its bots held and stop the heartbeat.
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
        {
            LOG_INFO("module.guildbridge", "GUILDBRIDGE settings are read at startup only: restart the worldserver to "
                     "change them");
            return;
        }
        BridgeConfig::Get().Load();
    }

    void OnStartup() override
    {
        LOG_INFO("module.guildbridge", "GUILDBRIDGE loaded (enabled={})", BridgeConfig::Get().enable ? 1 : 0);
        if (!BridgeConfig::Get().disabledReason.empty())
            LOG_ERROR("module.guildbridge", "GUILDBRIDGE disabled: {}. The bridge needs one async writer per pool "
                      "(restores, snapshots and orders read back their own writes); set both to 1 and restart",
                      BridgeConfig::Get().disabledReason);
        if (!BridgeConfig::Get().enable)
            return;
        if (!GuildmasterDatabaseReady)  // a fresh world whose legends view failed: the world is already stopping
            return;
        GuildRegistry::Instance().LoadAtStartup();  // first: everything after it may ask for guild roles
        Firsts::Instance().LoadAtStartup();
        BotDumps::Instance().Start();
        RestoreMgr::Instance().RecoverAtStartup();  // first: it decides what happens to cut restores
        DungeonRunMgr::Instance().LoadAtStartup();  // entrances; runs cut by the restart end as abandoned
        NewGameOrders::NoteCutOrders();  // before the runner fails the create_founders orders a restart cut
        OrderRunner::Instance().RecoverAtStartup();
        StateWriter::Instance().LoadFocusAtStartup();
        StateWriter::Instance().LoadRoutesAtStartup();
        StuckDetector::Instance().CloseAllAtStartup();
        NewGameOrders::LoadAtStartup();  // after restore recovery: founders a restart cut short or left guildless
        WorldStatus::Instance().WriteNow();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!BridgeConfig::Get().enable)
            return;
        BridgeAsync::Process();
        BotDumps::Instance().Update(diff);
        GuildRegistry::Instance().Update(diff);
        SimpleOrders::Update(diff);
        OrderRunner::Instance().Update(diff);
        RestoreMgr::Instance().Update(diff);
        DungeonRunMgr::Instance().Update(diff);
        StateWriter::Instance().Update(diff);
        RouteWriter::Instance().Update(diff);
        StuckDetector::Instance().Update(diff);
        NewGameOrders::Update(diff);
        WorldStatus::Instance().Update(diff);
        _flushInMs = _flushInMs > diff ? _flushInMs - diff : 0;
        if (!_flushInMs)
        {
            EventSink::Instance().Flush();
            _flushInMs = BridgeConfig::Get().eventFlushMs;
        }
    }

    void OnShutdown() override
    {
        if (BridgeConfig::Get().enable)
            EventSink::Instance().Flush();  // the last events in
        BotDumps::Instance().Stop();
    }

private:
    uint32 _flushInMs = 0;
};

void AddBridgeWorldScripts() { new BridgeWorldScript(); }
