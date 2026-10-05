/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_SIMPLEORDERS_H
#define MOD_GUILD_BRIDGE_SIMPLEORDERS_H

#include "OrderRunner.h"
#include <functional>

// The orders that finish at once (focus, invite, remove, rank, preset_professions) and the snapshot orders.
// Holds: none. Focus/invite/remove/rank/preset change the game in one world-thread step; a snapshot only saves the bot
// and dumps it on a worker thread (BotDumps). A restart in the middle of a snapshot batch leaves the order
// "running", and OrderRunner::RecoverAtStartup fails it at the next boot ("interrupted by a server restart").
namespace SimpleOrders
{
// "" when the bot is free for an order; else why not (held = lent to a run or being restored).
// Check order for later tasks (preflight D10): run (RunIdFor) -> restore (IsRestoring) -> held (IsHeld).
std::string BusyReason(uint32 guid);
OrderResult Run(GuildBridge::ParsedOrder const& order);  // focus, invite, remove, rank, preset_professions
void Snapshot(GuildBridge::ParsedOrder const& order, std::function<void(OrderResult const&)> finish);
// Every member of the user and test guilds (reason "scheduled"), then the retention. orderId 0 = console.
void SnapshotAll(uint64 orderId, bool dryRun, std::function<void(OrderResult const&)> finish);
// World thread, every tick: hands queued snapshot requests to BotDumps, GuildBridge.Snapshot.PerTick at a time
// (each one saves an online bot on the world thread, ~0.5-1 ms), so a snapshot of 1000 bots is spread over
// many ticks instead of stalling one.
void Update(uint32 diff);
uint32 QueuedSnapshots();  // requests not yet handed to BotDumps (`bridge status` snapq=)
}  // namespace SimpleOrders

#endif
