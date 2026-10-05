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
bool IsBotAccount(uint32 accountId);    // in AiPlayerbot.RandomBotAccounts (any thread after startup)
bool IsBotAccountName(std::string const& accountName);  // starts with AiPlayerbot.RandomBotAccountPrefix
void ValidateGuildCache();              // world thread: playerbots re-reads which guilds are "real"
bool IsRealGuild(uint32 guildId);       // leader on a non-bot account
bool IsHeld(uint32 guid);               // any thread
void SetRaisingsUserGuild(uint32 guildId);  // world thread: the fork's raisings cap the user's guild per wave
uint32 RaisingsUserGuild();
}  // namespace PlayerbotsAdapter

#endif
