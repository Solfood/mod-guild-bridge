/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "PlayerbotsAdapter.h"

#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

bool PlayerbotsAdapter::IsBot(Player* player) { return player && GET_PLAYERBOT_AI(player); }

bool PlayerbotsAdapter::IsRandomBot(uint32 guid) { return sRandomPlayerbotMgr.IsRandomBot(guid); }

// Masterless logins are owned by sRandomPlayerbotMgr (the same path mod-dungeon-clear's headless driver uses).
// A clone is not a random bot: its account is not in AiPlayerbot.RandomBotAccounts, so the population manager
// never re-rolls or relocates it, and (fork, preflight C3) it does not use up the population's login budget.
void PlayerbotsAdapter::LoginMasterless(ObjectGuid guid) { sRandomPlayerbotMgr.AddPlayerBot(guid, 0); }

void PlayerbotsAdapter::Logout(ObjectGuid guid)
{
    if (sRandomPlayerbotMgr.GetPlayerBot(guid))
        sRandomPlayerbotMgr.LogoutPlayerBot(guid);
}
