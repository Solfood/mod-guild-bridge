/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_FIRSTS_H
#define MOD_GUILD_BRIDGE_FIRSTS_H

#include "Define.h"
#include <mutex>
#include <set>
#include <string>
#include <utility>

// Server firsts ("first to level 20", "first kill of X"). Claim() is true exactly once per (kind, ref); it then
// writes the firsts row and a "first" event. Any thread. Callers never pass test-guild or test-account bots.
class Firsts
{
public:
    static Firsts& Instance();
    void LoadAtStartup();  // OnStartup only (sync query before the world runs)
    bool Claim(std::string const& kind, uint32 ref, uint32 guid, uint32 guildId, std::string const& label);

private:
    std::mutex _mutex;
    std::set<std::pair<std::string, uint32>> _claimed;
};

#endif
