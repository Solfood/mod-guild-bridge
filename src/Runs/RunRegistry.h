/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_RUNREGISTRY_H
#define MOD_GUILD_BRIDGE_RUNREGISTRY_H

#include "Define.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Which bot is in which run, plus deaths and wipes noticed by the hooks. Thread-safe (hooks run on map threads).
// Deaths and wipes are kept per run (preflight D4: several runs at once must not take each other's deaths).
// In memory only: a restart ends every run (DungeonRunMgr::LoadAtStartup), so nothing here needs to survive it.
class RunRegistry
{
public:
    struct Death
    {
        uint32 guid = 0;
        std::string killer;
        bool pvp = false;
    };

    static RunRegistry& Instance();
    void Set(uint32 guid, uint64 runId);
    void Clear(uint32 guid);
    uint64 RunIdFor(uint32 guid) const;
    std::size_t Bots() const;
    void NoteDeath(uint32 guid, std::string killer, bool pvp);  // kept only when the bot is in a run
    std::vector<Death> TakeDeaths(uint64 runId);
    void NoteWipe(uint64 runId);
    uint32 Wipes(uint64 runId) const;
    void Forget(uint64 runId);  // the run is over: its deaths and wipes go

private:
    mutable std::mutex _mutex;
    std::unordered_map<uint32, uint64> _runOf;
    std::unordered_map<uint64, std::vector<Death>> _deaths;
    std::unordered_map<uint64, uint32> _wipes;
};

#endif
