/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "EventSink.h"
#include "Firsts.h"
#include "GuildRegistry.h"
#include "Log.h"
#include "OrderRunner.h"
#include "RestoreMgr.h"
#include "ScriptMgr.h"
#include "SimpleOrders.h"
#include "StateWriter.h"
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

    void OnAfterConfigLoad(bool /*reload*/) override { BridgeConfig::Get().Load(); }

    void OnStartup() override
    {
        LOG_INFO("module.guildbridge", "GUILDBRIDGE loaded (enabled={})", BridgeConfig::Get().enable ? 1 : 0);
        if (!BridgeConfig::Get().enable)
            return;
        GuildRegistry::Instance().LoadAtStartup();  // first: everything after it may ask for guild roles
        Firsts::Instance().LoadAtStartup();
        BotDumps::Instance().Start();
        RestoreMgr::Instance().RecoverAtStartup();  // first: it decides what happens to cut restores
        OrderRunner::Instance().RecoverAtStartup();
        StateWriter::Instance().LoadFocusAtStartup();
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
        StateWriter::Instance().Update(diff);
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
