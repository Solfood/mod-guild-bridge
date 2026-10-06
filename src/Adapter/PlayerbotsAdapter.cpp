/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "PlayerbotsAdapter.h"

#include "AiFactory.h"
#include "MotionMaster.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "NewRpgInfo.h"
#include "ProfessionPicker.h"
#include "RaisingMgr.h"
#include "RandomPlayerbotMgr.h"
#include "StringFormat.h"
#include <algorithm>
#include <cctype>

bool PlayerbotsAdapter::IsBot(Player* player) { return player && GET_PLAYERBOT_AI(player); }

bool PlayerbotsAdapter::IsRandomBot(uint32 guid) { return sRandomPlayerbotMgr.IsRandomBot(guid); }

GuildBridge::SeatSpec PlayerbotsAdapter::SeatSpecOf(Player* player)
{
    GuildBridge::SeatSpec spec;
    spec.name = player->GetName();
    spec.cls = player->getClass();
    spec.tab = AiFactory::GetPlayerSpecTab(player);
    spec.level = player->GetLevel();
    spec.hasCatForm = player->HasSpell(768);      // Cat Form (AiFactory SPELL_CAT_FORM)
    spec.hasThickHide = player->HasAura(16931);   // Thick Hide (AiFactory SPELL_DRUID_THICK_HIDE)
    return spec;
}

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

void PlayerbotsAdapter::Hold(uint32 guid) { sRandomPlayerbotMgr.Hold(guid); }

void PlayerbotsAdapter::Release(uint32 guid) { sRandomPlayerbotMgr.Release(guid); }

bool PlayerbotsAdapter::IsRaising(uint32 guid) { return sRaisingMgr.IsRaising(guid); }

void PlayerbotsAdapter::AddPopulationAccount(uint32 accountId) { sRandomPlayerbotMgr.AddPopulationAccount(accountId); }

void PlayerbotsAdapter::AddToPopulation(uint32 guid, bool joinsBotGuild)
{
    sRandomPlayerbotMgr.AddToPopulation(guid, joinsBotGuild);
}

void PlayerbotsAdapter::SetJoinsBotGuild(uint32 guid, bool joins) { sRandomPlayerbotMgr.SetJoinsBotGuild(guid, joins); }

bool PlayerbotsAdapter::JoinsBotGuild(uint32 guid) { return sRandomPlayerbotMgr.JoinsBotGuild(guid); }

uint32 PlayerbotsAdapter::PopulationSize() { return sRandomPlayerbotMgr.PopulationSize(); }

uint32 PlayerbotsAdapter::PopulationOnline()
{
    uint32 online = 0;
    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        if (bot && GET_PLAYERBOT_AI(bot) && sRandomPlayerbotMgr.IsRandomBot(bot))
            ++online;
    return online;
}

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

// From the status enum (no ToString() text per bot: the 30 s pass reads every member; preflight D14).
std::string PlayerbotsAdapter::RpgStatusName(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return "-";
    switch (botAI->rpgInfo.GetStatus())
    {
        case RPG_IDLE: return "IDLE";
        case RPG_GO_GRIND: return "GO_GRIND";
        case RPG_GO_CAMP: return "GO_CAMP";
        case RPG_WANDER_RANDOM: return "WANDER_RANDOM";
        case RPG_WANDER_NPC: return "WANDER_NPC";
        case RPG_DO_QUEST: return "DO_QUEST";
        case RPG_TRAVEL_FLIGHT: return "TRAVEL_FLIGHT";
        case RPG_REST: return "REST";
        case RPG_OUTDOOR_PVP: return "OUTDOOR_PVP";
        case RPG_DO_GATHER: return "DO_GATHER";
        default: return "-";
    }
}

uint8 PlayerbotsAdapter::DurabilityPct(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI ? botAI->GetAiObjectContext()->GetValue<uint8>("durability")->Get() : 100;
}

uint8 PlayerbotsAdapter::GetFocus(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI ? botAI->rpgInfo.focus : 0;
}

void PlayerbotsAdapter::SetFocus(Player* bot, uint8 focus)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        botAI->rpgInfo.focus = focus;
}

void PlayerbotsAdapter::GoTo(Player* bot, GuildBridge::Spot const& spot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (botAI && sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        // New-RPG "go to camp": MoveFarTo with mounts, paths and the stuck fallback; it fights what attacks it.
        // Re-set when the bot dropped it or is on a camp trip of its own (a town errand).
        auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&botAI->rpgInfo.data);
        if (!camp || camp->pos.GetMapId() != spot.map || camp->pos.GetExactDist2d(spot.x, spot.y) > 5.f)
            botAI->rpgInfo.ChangeToGoCamp(WorldPosition(spot.map, spot.x, spot.y, spot.z));
        return;
    }
    // Clones: pathfinding toward the spot, in legs of at most 150 yards (long paths are cut by the path generator).
    float const dist = bot->GetExactDist2d(spot.x, spot.y);
    float const step = std::min(dist, 150.f) / std::max(dist, 0.1f);
    float const nx = bot->GetPositionX() + (spot.x - bot->GetPositionX()) * step;
    float const ny = bot->GetPositionY() + (spot.y - bot->GetPositionY()) * step;
    float nz = step >= 1.f ? spot.z : bot->GetPositionZ();
    if (step < 1.f)
        bot->UpdateAllowedPositionZ(nx, ny, nz);
    bot->SetStandState(UNIT_STAND_STATE_STAND);
    bot->GetMotionMaster()->MovePoint(0, nx, ny, nz);
}

void PlayerbotsAdapter::Park(Player* bot)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        if (sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            if (botAI->rpgInfo.GetStatus() != RPG_REST)
                botAI->rpgInfo.ChangeToRest();
            return;
        }
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
    {
        bot->GetMotionMaster()->Clear();
        bot->GetMotionMaster()->MoveIdle();
    }
}

void PlayerbotsAdapter::ClearGoTo(Player* bot)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        if (sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            botAI->rpgInfo.ChangeToIdle();
            return;
        }
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();
}

uint32 PlayerbotsAdapter::StuckTeleports(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    return botAI ? botAI->rpgInfo.stuckTeleports : 0;
}

std::string PlayerbotsAdapter::LastStuckDest(Player* bot)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || !botAI->rpgInfo.stuckTeleports)
        return "";
    WorldPosition const& d = botAI->rpgInfo.lastStuckDest;
    return Acore::StringFormat("{}:{:.0f}:{:.0f}:{:.0f}", d.GetMapId(), d.GetPositionX(), d.GetPositionY(),
                               d.GetPositionZ());
}
