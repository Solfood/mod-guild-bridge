/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "RunRegistry.h"

RunRegistry& RunRegistry::Instance()
{
    static RunRegistry instance;
    return instance;
}

void RunRegistry::Set(uint32 guid, uint64 runId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _runOf[guid] = runId;
}

void RunRegistry::Clear(uint32 guid)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _runOf.erase(guid);
}

uint64 RunRegistry::RunIdFor(uint32 guid) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const it = _runOf.find(guid);
    return it == _runOf.end() ? 0 : it->second;
}

std::size_t RunRegistry::Bots() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _runOf.size();
}

void RunRegistry::NoteDeath(uint32 guid, std::string killer, bool pvp)
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const it = _runOf.find(guid);
    if (it != _runOf.end())
        _deaths[it->second].push_back({guid, std::move(killer), pvp});
}

std::vector<RunRegistry::Death> RunRegistry::TakeDeaths(uint64 runId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Death> out;
    auto const it = _deaths.find(runId);
    if (it != _deaths.end())
    {
        out.swap(it->second);
        _deaths.erase(it);
    }
    return out;
}

void RunRegistry::NoteWipe(uint64 runId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ++_wipes[runId];
}

uint32 RunRegistry::Wipes(uint64 runId) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    auto const it = _wipes.find(runId);
    return it == _wipes.end() ? 0 : it->second;
}

void RunRegistry::Forget(uint64 runId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _wipes.erase(runId);
    _deaths.erase(runId);
}
