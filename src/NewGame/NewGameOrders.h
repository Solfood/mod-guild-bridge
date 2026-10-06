/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_NEWGAMEORDERS_H
#define MOD_GUILD_BRIDGE_NEWGAMEORDERS_H

#include "OrderRunner.h"
#include <cstddef>
#include <functional>

// create_guild and create_founders (spec §2b), sent by the world controller at New Game. World thread.
// Holds: none. Founders are population bots on this world's founder accounts (preflight C1). A founder joins its
// guild once it has logged in (an offline Guild::AddMember is a sync query, D7); until then it waits guildless
// (fork "no_bot_guild"), across restarts too: LoadAtStartup finds the founders not placed yet.
// Restarts (final review I1): each founder's bot_profiles row is written right after its character is made, so a
// restart between making and finishing loses nothing. For the founders of a create_founders order the boot finds
// running (and only those; never a guid in a raising or a restore, re-review N1), LoadAtStartup adds a profiled
// founder with no "add" record to the population, drops a profile whose character never reached the database, and
// deletes a character with neither profile nor "add" record. A create_founders sent again takes back the founders
// an earlier send made (no "name taken"); that is also how a "not saved in time" failure is finished.
namespace NewGameOrders
{
using Finish = std::function<void(OrderResult const&)>;
void NoteCutOrders();  // OnStartup, before OrderRunner::RecoverAtStartup: create_founders orders a restart cut
void LoadAtStartup();  // OnStartup, after RestoreMgr::RecoverAtStartup (sync reads and the startup deletes allowed)
void CreateGuild(GuildBridge::ParsedOrder const& order, Finish finish);
void CreateFounders(GuildBridge::ParsedOrder const& order, Finish finish);
void Update(uint32 diff);  // waits for new characters to reach the database; places logged-in founders
std::size_t Unplaced();    // founders made but not in their guild yet (`bridge status`)
}  // namespace NewGameOrders

#endif
