/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_WORLDSTATUS_H
#define MOD_GUILD_BRIDGE_WORLDSTATUS_H

#include "Define.h"

// world_status every 10 s: what the world controller polls (heartbeat, population size and online, versions, guild
// ids). population_ready_at is advisory (preflight C7): the controller decides readiness from online / size.
// World thread.
class WorldStatus
{
public:
    static WorldStatus& Instance();
    void Update(uint32 diff);
    void WriteNow();
    bool Ready() const { return _readyAt != 0; }
    uint32 Size() const { return _size; }
    uint32 Online() const { return _online; }

private:
    static constexpr uint32 INTERVAL_MS = 10000;
    uint32 _timer = 0;
    uint32 _bootedAt = 0;
    uint32 _readyAt = 0;
    uint32 _size = 0;
    uint32 _online = 0;
};

#endif
