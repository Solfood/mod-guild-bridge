/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_ROUTEWRITER_H
#define MOD_GUILD_BRIDGE_ROUTEWRITER_H

#include "Define.h"

// route_hubs (once per boot: written when the fork's quest routes are on, emptied when off) and quest_drops (every
// 5 minutes, rewritten whole from the fork's memory). Contract §2.15, §2.16. World thread.
class RouteWriter
{
public:
    static RouteWriter& Instance();
    void Update(uint32 diff);
    void WriteHubs();   // also the `bridge routes hubs` test seam (with routes off it builds them in memory first)
    void ClearHubs();
    void WriteDrops();
    uint32 HubsWritten() const { return _hubsWritten; }
    uint32 DropRows() const { return _dropRows; }

private:
    static constexpr uint32 DROPS_INTERVAL_MS = 5 * 60 * 1000;
    uint32 _timer = 0;
    bool _bootDone = false;
    uint32 _hubsWritten = 0;
    uint32 _dropRows = 0;
};

#endif
