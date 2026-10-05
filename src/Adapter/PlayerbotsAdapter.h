/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#ifndef MOD_GUILD_BRIDGE_PLAYERBOTSADAPTER_H
#define MOD_GUILD_BRIDGE_PLAYERBOTSADAPTER_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>
#include <vector>

class Player;

// The ONLY place the bridge calls into mod-playerbots (spec §8 risk 3: upstream churn). World thread unless noted.
namespace PlayerbotsAdapter
{
bool IsBot(Player* player);             // has a PlayerbotAI (any thread)
bool IsRandomBot(uint32 guid);          // a population bot (RNDBOT account)
void LoginMasterless(ObjectGuid guid);  // log a character in as a bot with no master (clones, guild master)
void Logout(ObjectGuid guid);           // log a bot out through whichever holder owns it
bool IsMasterlessLoggedIn(ObjectGuid guid);  // the masterless login finished (playerbots' login steps done)
bool IsBotAccount(uint32 accountId);    // in AiPlayerbot.RandomBotAccounts (any thread after startup)
bool IsBotAccountName(std::string const& accountName);  // starts with AiPlayerbot.RandomBotAccountPrefix
void ValidateGuildCache();              // world thread: playerbots re-reads which guilds are "real"
bool IsRealGuild(uint32 guildId);       // leader on a non-bot account
bool IsHeld(uint32 guid);               // any thread
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
uint32 StoredProfession(uint32 guid, bool second);  // world thread; for `bridge bot`
}  // namespace PlayerbotsAdapter

#endif
