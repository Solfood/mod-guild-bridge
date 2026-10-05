/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_DCADAPTER_H
#define MOD_GUILD_BRIDGE_DCADAPTER_H

#include "Define.h"
#include <string>

// The ONLY place the bridge calls into mod-dungeon-clear (jrad7, AGPL-3.0, pinned at 60f3d98). World thread.
namespace DcAdapter
{
struct DungeonInfo
{
    bool found = false;
    std::string token;
    std::string name;
    uint32 mapId = 0;
    uint32 recommendedLevel = 0;
    uint32 heroicLevel = 0;  // 0 = no heroic mode
};

// A dungeon (or dungeon wing) row of mod-dungeon-clear's test catalogue, by token; scenario rows are not dungeons.
DungeonInfo Find(std::string const& token);
}  // namespace DcAdapter

#endif
