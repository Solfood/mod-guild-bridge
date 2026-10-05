/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "PlayerbotsAdapter.h"

#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "ProfessionPicker.h"
#include "RandomPlayerbotMgr.h"
#include <cctype>

bool PlayerbotsAdapter::IsBot(Player* player) { return player && GET_PLAYERBOT_AI(player); }

bool PlayerbotsAdapter::IsRandomBot(uint32 guid) { return sRandomPlayerbotMgr.IsRandomBot(guid); }

// Masterless logins are owned by sRandomPlayerbotMgr (the same path mod-dungeon-clear's headless driver uses).
// A clone is not a random bot: its account is not in AiPlayerbot.RandomBotAccounts, so the population manager
// never re-rolls or relocates it, and (fork, preflight C3) it does not use up the population's login budget.
void PlayerbotsAdapter::LoginMasterless(ObjectGuid guid) { sRandomPlayerbotMgr.AddPlayerBot(guid, 0); }

// The holder records the bot and runs its login steps (bot-guild assignment among them) in one call, after the
// player is already in the world: only from then on can Logout find it.
bool PlayerbotsAdapter::IsMasterlessLoggedIn(ObjectGuid guid) { return sRandomPlayerbotMgr.GetPlayerBot(guid); }

void PlayerbotsAdapter::Logout(ObjectGuid guid)
{
    if (sRandomPlayerbotMgr.GetPlayerBot(guid))
        sRandomPlayerbotMgr.LogoutPlayerBot(guid);
}

bool PlayerbotsAdapter::IsBotAccount(uint32 accountId) { return sPlayerbotAIConfig.IsInRandomAccountList(accountId); }

bool PlayerbotsAdapter::IsBotAccountName(std::string const& accountName)
{
    std::string const& prefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    if (prefix.empty() || accountName.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        auto const upper = [](char c) { return std::toupper(static_cast<unsigned char>(c)); };
        if (upper(accountName[i]) != upper(prefix[i]))
            return false;
    }
    return true;
}

void PlayerbotsAdapter::ValidateGuildCache() { PlayerbotGuildMgr::instance().ValidateGuildCache(); }

bool PlayerbotsAdapter::IsRealGuild(uint32 guildId) { return PlayerbotGuildMgr::instance().IsRealGuild(guildId); }

bool PlayerbotsAdapter::IsHeld(uint32 guid) { return sRandomPlayerbotMgr.IsHeld(guid); }

// The user's guild is created at New Game, so its id cannot sit in playerbots.conf
// (AiPlayerbot.Raisings.UserGuildId stays 0 there); the bridge sets the live value from the registry.
void PlayerbotsAdapter::SetRaisingsUserGuild(uint32 guildId) { sPlayerbotAIConfig.raisingsUserGuildId = guildId; }

uint32 PlayerbotsAdapter::RaisingsUserGuild() { return sPlayerbotAIConfig.raisingsUserGuildId; }

PlayerbotsAdapter::ProfessionState PlayerbotsAdapter::GetProfessionState(Player* bot)
{
    ProfessionState state;
    uint32 const guid = bot->GetGUID().GetCounter();
    state.pickLevel = ProfessionPicker::PickLevel(bot);
    for (uint32 skill : PlayerbotFactory::tradeSkills)
        if (PlayerbotFactory::IsPrimaryTradeSkill(static_cast<uint16>(skill)) && bot->HasSkill(skill))
            ++state.knownPrimary;
    state.stored1 = sRandomPlayerbotMgr.GetValue(guid, "firstSkill");
    state.stored2 = sRandomPlayerbotMgr.GetValue(guid, "secondSkill");
    return state;
}

void PlayerbotsAdapter::PresetProfessions(uint32 guid, uint32 first, uint32 second)
{
    // ProfessionPicker keeps a stored pick; a lone first is kept and only its partner is rolled (fork Task 7, F11).
    sRandomPlayerbotMgr.SetValue(guid, "firstSkill", first);
    sRandomPlayerbotMgr.SetValue(guid, "secondSkill", second);
}

uint32 PlayerbotsAdapter::StoredProfession(uint32 guid, bool second)
{
    return sRandomPlayerbotMgr.GetValue(guid, second ? "secondSkill" : "firstSkill");
}
