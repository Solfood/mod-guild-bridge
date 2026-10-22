/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "PlayerbotsAdapter.h"

#include "AiFactory.h"
#include "CraftMgr.h"
#include "GuideMgr.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "PlayerbotMgr.h"
#include "PlayerbotsDatabase.h"
#include "Playerbots.h"
#include "NewRpgInfo.h"
#include "ProfessionPicker.h"
#include "QueryResult.h"
#include "RaisingMgr.h"
#include "RouteMgr.h"
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
    spec.guid = static_cast<uint32_t>(player->GetGUID().GetCounter());
    spec.hasCatForm = player->HasSpell(768);      // Cat Form (AiFactory SPELL_CAT_FORM)
    spec.hasThickHide = player->HasAura(16931);   // Thick Hide (AiFactory SPELL_DRUID_THICK_HIDE)
    return spec;
}

bool PlayerbotsAdapter::RoleHeld(Player* player, GuildBridge::Role role)
{
    if (role == GuildBridge::Role::Dps)
        return true;  // nothing to switch to: the rest play the strategies they have
    return role == GuildBridge::Role::Tank ? PlayerbotAI::IsTank(player, false) : PlayerbotAI::IsHeal(player, false);
}

namespace
{
// Strategy sets, in playerbots' own names (Ai/Base AiFactory::AddDefaultCombatStrategies / AddDefaultNonCombatStrategies
// for the spec that has that role). A '-' first removes what the other specs of the class add, then the role's set is
// added; PlayerbotAI::ChangeStrategy takes "+name,-name" lists. Order: combat engine, non-combat engine.
struct RoleSets
{
    char const* combat;
    char const* nonCombat;
};

RoleSets SetsFor(Player* player, GuildBridge::Role role)
{
    bool const tank = role == GuildBridge::Role::Tank;
    switch (player->getClass())
    {
        case CLASS_WARRIOR:  // tank only
            return {"-behind,-arms,-fury,-dps assist,+tank,+tank assist,+pull,+pull back,+aoe,+tank face",
                    "-dps assist,+tank assist,+pull"};
        case CLASS_PALADIN:
            if (tank)
                return {"-behind,-heal,-dps,-baoe,-bcast,-dps assist,+tank,+tank assist,+pull,+pull back,+bthreat,+barmor,"
                        "+cure,+tank face",
                        player->GetLevel() >= 20
                            ? "-dps assist,-bwisdom,-bcast,-baoe,+bthreat,+tank assist,+pull,+barmor,+bsanc,+cure"
                            : "-dps assist,-bwisdom,-bcast,-baoe,+bthreat,+tank assist,+pull,+barmor,+bmight,+cure"};
            return {"-behind,-tank,-tank assist,-pull,-pull back,-bthreat,-barmor,-tank face,-dps,-baoe,+heal,+dps assist,"
                    "+bcast,+cure,+save mana,+healer dps",
                    "-tank assist,-pull,-bthreat,-barmor,-bsanc,-bmight,-baoe,+dps assist,+bwisdom,+bcast,+cure"};
        case CLASS_DRUID:
            if (tank)
                return {"-behind,-cat,-balance,-resto,-tranquility,-cc,-aoe,-dps assist,+bear,+tank assist,+pull,+pull back,"
                        "+feral charge,+tank face",
                        "-dps assist,+tank assist,+pull"};
            return {"-behind,-cat,-bear,-balance,-tank assist,-pull,-pull back,-tank face,-feral charge,+resto,+cure,"
                    "+dps assist,+tranquility,+save mana,+healer dps",
                    "-tank assist,-pull,+dps assist,+cure"};
        case CLASS_DEATH_KNIGHT:  // tank only
            return {"-behind,-frost,-unholy,-frost aoe,-unholy aoe,-dps assist,+blood,+tank assist,+pull,+pull back,+tank face",
                    "-dps assist,+tank assist,+pull"};
        case CLASS_PRIEST:  // heal only
            return {"-behind,-dps,-shadow debuff,-shadow aoe,-holy dps,+heal,+cure,+dps assist,+save mana,+healer dps", ""};
        case CLASS_SHAMAN:  // heal only
            return {"-behind,-ele,-enh,-strength of earth,-magma,-healing stream,-windfury,-wrath,+resto,+stoneskin,"
                    "+flametongue,+mana spring,+wrath of air,+dps assist,+cure,+aoe,+save mana,+healer dps",
                    ""};
        default:
            return {"", ""};
    }
}
}  // namespace

void PlayerbotsAdapter::ApplyRole(Player* player, GuildBridge::Role role)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
    if (!botAI || role == GuildBridge::Role::Dps)
        return;
    RoleSets const sets = SetsFor(player, role);
    // A shadow priest keeps Shadowform (blocks Holy spells) unless it is taken off: Shadowform 15473.
    if (role == GuildBridge::Role::Heal && player->getClass() == CLASS_PRIEST)
        player->RemoveAurasDueToSpell(15473);
    if (*sets.combat)
        botAI->ChangeStrategy(sets.combat, BOT_STATE_COMBAT);
    if (*sets.nonCombat)
        botAI->ChangeStrategy(sets.nonCombat, BOT_STATE_NON_COMBAT);
}

void PlayerbotsAdapter::ResetStrategiesToDefault(Player* player)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
        botAI->ResetStrategies();
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

std::unordered_set<uint32> PlayerbotsAdapter::WithPopulationRecord(std::vector<uint32> const& guids)
{
    std::unordered_set<uint32> out;
    if (guids.empty())
        return out;
    std::string in;
    for (uint32 guid : guids)
        in += (in.empty() ? "" : ",") + std::to_string(guid);
    // The record GetBots() reads (owner 0, event "add"), expired as FindEvent expires it.
    if (QueryResult result = PlayerbotsDatabase.Query(
            "SELECT bot FROM playerbots_random_bots WHERE owner = 0 AND event = 'add' AND value > 0 AND "
            "(IFNULL(validIn, 0) = 0 OR UNIX_TIMESTAMP() - time < validIn) AND bot IN (" + in + ")"))
        do
            out.insert((*result)[0].Get<uint32>());
        while (result->NextRow());
    return out;
}

std::unordered_set<uint32> PlayerbotsAdapter::InRaisings(std::vector<uint32> const& guids)
{
    std::unordered_set<uint32> const wanted(guids.begin(), guids.end());
    std::unordered_set<uint32> out;
    if (guids.empty())
        return out;
    std::string in;
    for (uint32 guid : guids)
        in += (in.empty() ? "" : ",") + std::to_string(guid);
    if (QueryResult result = PlayerbotsDatabase.Query("SELECT old_guid, new_guid FROM playerbots_raisings WHERE "
                                                      "old_guid IN (" + in + ") OR new_guid IN (" + in + ")"))
        do
            for (int i = 0; i < 2; ++i)
                if (uint32 const guid = (*result)[i].Get<uint32>(); wanted.count(guid))
                    out.insert(guid);
        while (result->NextRow());
    return out;
}

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

void PlayerbotsAdapter::LoadStoredValues(uint32 guid) { sRandomPlayerbotMgr.GetValue(guid, "firstSkill"); }

uint32 PlayerbotsAdapter::StoredProfession(uint32 guid, bool second)
{
    return sRandomPlayerbotMgr.GetValue(guid, second ? "secondSkill" : "firstSkill");
}

PlayerbotsAdapter::CraftInfo PlayerbotsAdapter::CraftOf(Player* bot, uint32 prof1, uint32 prof2)
{
    CraftInfo info;
    info.skill1 = prof1 ? static_cast<uint16>(bot->GetSkillValue(prof1)) : 0;
    info.skill2 = prof2 ? static_cast<uint16>(bot->GetSkillValue(prof2)) : 0;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    auto const* trip = botAI ? std::get_if<NewRpgInfo::CraftTrip>(&botAI->rpgInfo.data) : nullptr;
    if (!trip || trip->step >= trip->batch.size())
        return info;
    CraftRecipeInfo const* recipe = CraftMgr::instance().Recipe(trip->batch[trip->step].spell);
    ItemTemplate const* proto = recipe && recipe->product ? sObjectMgr->GetItemTemplate(recipe->product) : nullptr;
    info.craft = proto ? proto->Name1.substr(0, 48) : recipe && recipe->enchant ? "an enchant" : "";
    return info;
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
        case RPG_FOLLOW_ROUTE: return "FOLLOW_ROUTE";
        case RPG_FOLLOW_GUIDE: return "FOLLOW_GUIDE";  // Plan 5c: a guided bot on its levelling guide
        case RPG_CRAFT: return "CRAFT";  // Plan 6a: a craft trip (bot_state.activity; the app says "crafting")
        case RPG_FISH: return "FISH";    // a fishing trip (the app says "fishing")
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

PlayerbotsAdapter::RouteInfo PlayerbotsAdapter::RouteOf(Player* bot)
{
    RouteInfo info;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return info;
    RouteMgr const& routes = RouteMgr::instance();
    info.style = Routes::StyleName(routes.StyleOf(bot));
    info.headTo = RouteMgr::HeadTo(bot);
    // Guides (Plan 5c) win over routes: the same columns, holding the guide's name and the bot's place in it.
    GuideMgr::Progress const guide = GuideMgr::instance().ProgressOf(bot);
    if (guide.guided)
    {
        info.guided = true;
        info.hub = guide.name;
        info.done = guide.done;
        info.total = guide.total;
        info.struggling = RouteMgr::instance().Struggling(bot);
        return info;
    }
    info.routed = routes.Routed(bot);
    if (!info.routed)
        return info;
    if (Routes::Hub const* hub = routes.HubById(botAI->rpgInfo.route.hubId))
    {
        RouteMgr::HubWork const work = routes.WorkAt(bot, *hub);
        info.hub = hub->name;
        info.done = work.done;
        info.total = work.done + work.remaining;
    }
    info.struggling = routes.Struggling(bot);
    return info;
}

bool PlayerbotsAdapter::RoutesEnabled()
{
    return sPlayerbotAIConfig.questRoutes.enabled && RouteMgr::instance().Built();
}

bool PlayerbotsAdapter::GuidesEnabled()
{
    return sPlayerbotAIConfig.guideBots.enabled && GuideMgr::instance().Built();
}

std::vector<PlayerbotsAdapter::RouteHubRow> PlayerbotsAdapter::RouteHubs()
{
    RouteMgr& routes = RouteMgr::instance();
    routes.Build();
    std::vector<RouteHubRow> rows;
    for (Routes::Hub const& h : routes.Hubs())
    {
        RouteHubRow r;
        r.id = h.id;
        r.name = h.name;
        r.faction = h.team == Routes::TEAM_HORDE_ID ? "horde" : "alliance";
        r.map = h.map;
        r.zone = h.zone;
        r.area = h.area;
        r.minLevel = h.minLevel;
        r.level = h.level;
        r.maxLevel = h.maxLevel;
        r.quests = static_cast<uint32>(h.quests.size());
        r.x = h.x;
        r.y = h.y;
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<PlayerbotsAdapter::QuestDropRow> PlayerbotsAdapter::QuestDrops()
{
    std::vector<QuestDropRow> rows;
    for (RouteMgr::DropCount const& c : RouteMgr::instance().DropCounts())
        rows.push_back({c.quest, c.drops, c.lastAt});
    return rows;
}

bool PlayerbotsAdapter::IsRouted(Player* bot) { return RouteMgr::instance().Routed(bot); }

std::string PlayerbotsAdapter::HeadToProblem(Player* bot, uint32 zone)
{
    return RouteMgr::instance().HeadToProblem(bot, zone);
}

void PlayerbotsAdapter::SetHeadTo(Player* bot, uint32 zone) { RouteMgr::SetHeadTo(bot, zone); }

uint32 PlayerbotsAdapter::HeadTo(Player* bot) { return RouteMgr::HeadTo(bot); }

void PlayerbotsAdapter::SetRouteStyle(Player* bot, std::string const& style)
{
    Routes::Style s = Routes::Style::Steady;
    if (Routes::StyleFromName(style, s))
        RouteMgr::SetStyle(bot, s);
}
