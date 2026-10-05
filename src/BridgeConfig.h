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
    std::string worldId;       // GuildBridge.WorldId: this world's id, stamped into world_status
    std::string charactersDb;  // database names of THIS world, from the core's *DatabaseInfo strings
    std::string playerbotsDb;
    std::string testAccount;  // GuildBridge.TestAccount: the normal account that holds clones (test bots)
    uint32 snapshotKeepDays = 14;  // GuildBridge.Snapshot.KeepDays (0 = keep every snapshot)

    // "host;port;user;password;database" -> "database" ("" when the string has fewer than 5 fields).
    static std::string DatabaseNameOf(std::string const& info);
    static bool IsValidWorldId(std::string const& id);
};

#endif
