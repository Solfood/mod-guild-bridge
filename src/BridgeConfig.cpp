/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "BridgeConfig.h"

#include "Config.h"

BridgeConfig& BridgeConfig::Get()
{
    static BridgeConfig config;
    return config;
}

void BridgeConfig::Load()
{
    enable = sConfigMgr->GetOption<bool>("GuildBridge.Enable", true);
}
