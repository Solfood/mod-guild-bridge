/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_STATEWRITER_H
#define MOD_GUILD_BRIDGE_STATEWRITER_H

#include "Define.h"
#include "OrderRules.h"
#include <unordered_map>

// guildmaster.bot_state every 30 s (spec §4.7, contract §2.6) and the standing focus per bot. World thread only.
// One pass: an async read of our guilds' member list, then (world thread, in the callback) one row per member from
// memory (online = in the world now; never characters.online), written as one async transaction. The same pass
// re-applies each online member's stored focus, because the fork keeps focus in memory only (a relog or a restart
// resets it). Focus is written on the world thread only: world-thread hooks run after the map threads finish.
class StateWriter
{
public:
    static StateWriter& Instance();
    void LoadFocusAtStartup();  // OnStartup (one sync query, startup only)
    void Update(uint32 diff);
    void WriteNow();
    void SetFocus(uint32 guid, GuildBridge::Focus focus, uint64 orderId);
    GuildBridge::Focus FocusOf(uint32 guid) const;
    uint32 LastBuildUs() const { return _lastBuildUs; }  // world-thread cost of the last pass (`bridge status`)
    uint32 MaxBuildUs() const { return _maxBuildUs; }
    uint32 LastRows() const { return _lastRows; }

private:
    void WriteMembers(std::unordered_map<uint32, uint32> const& members);  // guid -> guild id
    static constexpr uint32 INTERVAL_MS = 30000;
    uint32 _timer = 0;
    bool _querying = false;
    uint32 _lastBuildUs = 0;
    uint32 _maxBuildUs = 0;
    uint32 _lastRows = 0;
    std::unordered_map<uint32, GuildBridge::Focus> _focus;
};

#endif
