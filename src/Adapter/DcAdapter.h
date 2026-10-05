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

// Hand a party of five offline characters to mod-dungeon-clear (`.dc test start <token> party=...`), with its
// headless driver standing in for the GM. names: the five character names in role order, comma-separated.
// Retry = the driver is still logging in, a member is still online (logout lag), another run holds a member, or
// dungeon-clear's own concurrent-run cap is reached; try again in a few seconds. Refused = it will not work (the
// driver cannot come up at all, a bad roster, ...); message says why. Started fills runId (dungeon-clear's run id).
enum class StartState { Started, Retry, Refused };
StartState Start(std::string const& token, std::string const& names, bool heroic, std::string& message, std::string& runId);
uint32 ConcurrentCap();        // DungeonClear.TestRun.MaxConcurrent (0 = unlimited); call at startup (no runs yet)
bool IsReserved(uint32 guid);  // true while a dungeon-clear run holds this character
bool Stop(std::string const& runId, std::string& message);  // ask a live run to end now (it still writes its line)
// Where dungeon-clear appends its results: the same rule as dungeon-clear (env DC_TESTRUNS_FILE, else the working
// directory). The compose override points it at a mounted directory, so a redeploy keeps the file (preflight C10).
std::string RunsFilePath();
}  // namespace DcAdapter

#endif
