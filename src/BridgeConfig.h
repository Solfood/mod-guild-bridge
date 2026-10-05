/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_CONFIG_H
#define MOD_GUILD_BRIDGE_CONFIG_H

#include "Define.h"
#include <string>

// Every GuildBridge.* option, read once at startup (a change needs a worldserver restart).
struct BridgeConfig
{
    static BridgeConfig& Get();
    void Load();

    bool enable = true;
};

#endif
