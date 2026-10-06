/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_PLAYERBOTSADAPTER_H
#define MOD_GUILD_BRIDGE_PLAYERBOTSADAPTER_H

#include "Define.h"
#include "ObjectGuid.h"
#include "TravelRules.h"
#include <string>
#include <unordered_set>
#include <vector>

class Player;

// The ONLY place the bridge calls into mod-playerbots (spec §8 risk 3: upstream churn). World thread unless noted.
namespace PlayerbotsAdapter
{
bool IsBot(Player* player);             // has a PlayerbotAI (any thread)
bool IsRandomBot(uint32 guid);          // a population bot (an "add" record on a population account)
// What playerbots' strategy assignment reads off a character (class, talent tab, level, Cat Form, Thick Hide), for
// GuildBridge::ReadsAsTank/ReadsAsHealer: the roles mod-dungeon-clear sees after it resets the strategies. Not the
// current form or strategies. World thread; player in the world.
GuildBridge::SeatSpec SeatSpecOf(Player* player);
void LoginMasterless(ObjectGuid guid);  // log a character in as a bot with no master (clones, guild master)
void Logout(ObjectGuid guid);           // log a bot out through whichever holder owns it
bool IsMasterlessLoggedIn(ObjectGuid guid);  // the masterless login finished (playerbots' login steps done)
bool IsBotAccount(uint32 accountId);    // in AiPlayerbot.RandomBotAccounts (any thread after startup)
bool IsBotAccountName(std::string const& accountName);  // starts with AiPlayerbot.RandomBotAccountPrefix
void ValidateGuildCache();              // world thread: playerbots re-reads which guilds are "real"
bool IsRealGuild(uint32 guildId);       // leader on a non-bot account
bool IsHeld(uint32 guid);               // any thread
void Hold(uint32 guid);                 // fork RandomPlayerbotMgr::Hold: the population (and raisings) leave it alone
void Release(uint32 guid);
bool IsRaising(uint32 guid);            // world thread: the original or death knight of an unfinished raising
// The fixed population (Task 15). World thread.
void AddPopulationAccount(uint32 accountId);  // startup only: a founder account's characters may be population bots
// fork RandomPlayerbotMgr::AddToPopulation (as raisings do): logged in and kept by the population manager. A founder
// is added with joinsBotGuild = false: it waits guildless at login until the bridge places it (SetJoinsBotGuild).
void AddToPopulation(uint32 guid, bool joinsBotGuild);
void SetJoinsBotGuild(uint32 guid, bool joins);
bool JoinsBotGuild(uint32 guid);        // false while a founder waits for the bridge to place it (fork event value)
uint32 PopulationSize();                // fork PopulationSize: bots with an "add" record (held ones included)
// Startup only (a sync read of the playerbots database, as the fork's own GetBots makes): which of `guids` have a
// live "add" record. The fork's in-memory population is not loaded yet at OnStartup (its first update does that).
std::unordered_set<uint32> WithPopulationRecord(std::vector<uint32> const& guids);
// Startup only (sync): which of `guids` appear in the fork's raisings table (old or new guid, any state).
std::unordered_set<uint32> InRaisings(std::vector<uint32> const& guids);
uint32 PopulationOnline();              // population bots in the world with a bot brain (no clones; preflight D22)
void SetRaisingsUserGuild(uint32 guildId);  // world thread: the fork's raisings cap the user's guild per wave
uint32 RaisingsUserGuild();

// Professions (fork ProfessionPicker, honest world): a bot picks two primary professions once, at its pick level.
struct ProfessionState
{
    uint8 pickLevel = 0;
    uint32 knownPrimary = 0;  // primary professions the bot already knows
    uint32 stored1 = 0;       // the stored pick (firstSkill / secondSkill values; 0 = none)
    uint32 stored2 = 0;
};
// World thread: the stored values load from the playerbots DB on a bot's first use (preflight D7 allows it).
ProfessionState GetProfessionState(Player* bot);
// World thread. The picker keeps a stored pair; a lone first is kept and only its partner is rolled.
void PresetProfessions(uint32 guid, uint32 first, uint32 second);
uint32 StoredProfession(uint32 guid, bool second);  // world thread; for `bridge bot` and bot_state
// Startup only (sync): the fork reads the bot's stored values (presets, ...) into its cache now. Its first write of
// a value for a bot whose cache is not loaded yet marks the cache loaded without reading, which would hide values
// stored by an earlier boot (AddToPopulation is such a write).
void LoadStoredValues(uint32 guid);

// Roster snapshot (bot_state) and focus. World thread: the fork's focus is a plain byte the map threads read, and
// world-thread hooks run after the map threads have finished their update (preflight / Task 4 ruling).
std::string RpgStatusName(Player* bot);  // "DO_QUEST", "IDLE", ... (contract §5) or "-" when it has no bot AI
uint8 DurabilityPct(Player* bot);        // 0-100 (playerbots "durability" value), 100 without AI
uint8 GetFocus(Player* bot);             // fork NewRpgInfo::focus (0-5), 0 without AI
void SetFocus(Player* bot, uint8 focus);  // the fork forgets it at logout: the bridge re-applies it

// Stuck-bot incidents (Task 13). World thread. The fork's counter lives in memory: it starts again at 0 at login.
uint32 StuckTeleports(Player* bot);      // fork NewRpgInfo::stuckTeleports (0 without AI)
std::string LastStuckDest(Player* bot);  // "map:x:y:z" of the last stuck move, or ""

// Dungeon-run travel (Task 11). World thread; call GoTo again every few seconds until the bot is there.
// Population bots use playerbots' own "go to camp" activity (paths, mounts, fights what attacks it); clones, which
// have no new-RPG brain, walk with pathfinding in legs of at most 150 yards.
void GoTo(Player* bot, GuildBridge::Spot const& spot);
void Park(Player* bot);      // arrived: stay put
void ClearGoTo(Player* bot);  // back to its normal life
}  // namespace PlayerbotsAdapter

#endif
