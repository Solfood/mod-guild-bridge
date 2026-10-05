/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BotDumps.h"
#include "BridgeAsync.h"
#include "BridgeConfig.h"
#include "Log.h"
#include "ScriptMgr.h"
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
        if (BridgeConfig::Get().enable)
            BotDumps::Instance().Start();
    }

    void OnUpdate(uint32 diff) override
    {
        if (!BridgeConfig::Get().enable)
            return;
        BridgeAsync::Process();
        BotDumps::Instance().Update(diff);
    }

    void OnShutdown() override { BotDumps::Instance().Stop(); }
};

void AddBridgeWorldScripts() { new BridgeWorldScript(); }
