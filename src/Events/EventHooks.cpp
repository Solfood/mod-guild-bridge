/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Hooks run on map threads: cheap reads of the objects they are given, one Push, nothing else.
 * Scope (contract §3): every type for members of our guilds (user and test); for other bots only level 80,
 * boss_kill and first. Test-guild and test-account bots never claim firsts.
 */

#include "AchievementMgr.h"
#include "BridgeConfig.h"
#include "Creature.h"
#include "EventSink.h"
#include "Firsts.h"
#include "GlobalScript.h"
#include "Group.h"
#include "GuildRegistry.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "RunRegistry.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

using namespace GuildBridge;

namespace
{
Where WhereOf(Player* player)
{
    return {player->GetMapId(), player->GetZoneId(), player->GetPositionX(), player->GetPositionY(),
            player->GetPositionZ()};
}

GuildRole RoleOfPlayer(Player* player) { return GuildRegistry::Instance().RoleOf(player->GetGuildId()); }

bool IsOurs(Player* player) { return RoleOfPlayer(player) != GuildRole::None; }

bool IsTestCharacter(Player* player)
{
    return RoleOfPlayer(player) == GuildRole::Test ||
           GuildRegistry::Instance().IsTestAccount(player->GetSession() ? player->GetSession()->GetAccountId() : 0);
}

void PushFor(Player* player, EventType type, std::string payload)
{
    uint32 const guid = player->GetGUID().GetCounter();
    EventSink::Instance().Push(type, guid, player->GetGuildId(), BridgeRunIdFor(guid), std::move(payload));
}

// Everyone on an instance map (boss kills and wipes are group events, guid 0).
struct Party
{
    std::vector<uint64> guids;
    bool anyOurs = false;
    uint32 ourGuild = 0;     // the guild of one of our members, if any
    uint32 anyGuild = 0;     // the first guild seen (for rival boss kills)
    uint32 claimGuid = 0;    // the first participant allowed to claim a first (not a test character); 0 = none
    uint32 claimGuild = 0;
};

Party PartyOf(Map* map)
{
    Party party;
    for (auto const& ref : map->GetPlayers())
        if (Player* player = ref.GetSource())
        {
            party.guids.push_back(player->GetGUID().GetCounter());
            if (IsOurs(player))
            {
                party.anyOurs = true;
                party.ourGuild = player->GetGuildId();
            }
            if (!party.anyGuild)
                party.anyGuild = player->GetGuildId();
            if (!party.claimGuid && !IsTestCharacter(player))
            {
                party.claimGuid = player->GetGUID().GetCounter();
                party.claimGuild = player->GetGuildId();
            }
        }
    return party;
}
}  // namespace

class BridgePlayerHooks : public PlayerScript
{
public:
    BridgePlayerHooks()
        : PlayerScript("BridgePlayerHooks",
                       {PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM,
                        PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE, PLAYERHOOK_ON_PVP_KILL, PLAYERHOOK_ON_ACHI_COMPLETE})
    {
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        uint8 const level = player->GetLevel();
        if (level != oldLevel + 1)
            return;  // created or set several levels at once (e.g. a raised Death Knight): not a ding
        if (IsOurs(player) || level == 80)
            PushFor(player, EventType::Level, LevelPayload(level, oldLevel, player->getClass(), player->getRace(),
                                                           WhereOf(player)));
        uint32 const step = BridgeConfig::Get().firstsLevelStep;
        if (step && level % step == 0 && !IsTestCharacter(player))
            Firsts::Instance().Claim("level", level, player->GetGUID().GetCounter(), player->GetGuildId(),
                                     "First to level " + std::to_string(level));
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootguid*/) override
    {
        PushLoot(player, item, count, "loot");
    }

    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count, RollVote /*vote*/, Roll* /*roll*/) override
    {
        PushLoot(player, item, count, "roll");
    }

    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!killer || !IsOurs(killed))
            return;
        PushFor(killed, EventType::Death,
                DeathPayload({"creature", killer->GetEntry(), 0, killer->GetName(), killer->GetLevel(), WhereOf(killed)}));
        // A run member's death: the run turns it into a run_ambush event while travelling (Task 11).
        RunRegistry::Instance().NoteDeath(killed->GetGUID().GetCounter(), killer->GetName(), false);
    }

    void OnPlayerPVPKill(Player* killer, Player* killed) override
    {
        if (!killer || !killed || killer == killed)
            return;
        if (IsOurs(killed))
        {
            PushFor(killed, EventType::Death,
                    DeathPayload({"player", 0, killer->GetGUID().GetCounter(), killer->GetName(), killer->GetLevel(),
                                  WhereOf(killed)}));
            RunRegistry::Instance().NoteDeath(killed->GetGUID().GetCounter(), killer->GetName(), true);
        }
        if (IsOurs(killer))
            PushFor(killer, EventType::PvpKill,
                    PvpKillPayload(killed->GetGUID().GetCounter(), killed->GetName(), killed->GetLevel(), WhereOf(killer)));
    }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (!achievement || !IsOurs(player))
            return;
        PushFor(player, EventType::Achievement,
                AchievementPayload(achievement->ID, achievement->name[0] ? achievement->name[0] : "", achievement->points));
    }

private:
    static void PushLoot(Player* player, Item* item, uint32 count, char const* source)
    {
        if (!item || !IsOurs(player))
            return;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->Quality < ITEM_QUALITY_RARE)
            return;
        PushFor(player, EventType::Loot,
                LootPayload(proto->ItemId, proto->Name1, proto->Quality, proto->ItemLevel, count, source, WhereOf(player)));
    }
};

class BridgeGlobalHooks : public GlobalScript
{
public:
    BridgeGlobalHooks()
        : GlobalScript("BridgeGlobalHooks", {GLOBALHOOK_ON_AFTER_UPDATE_ENCOUNTER_STATE, GLOBALHOOK_ON_BEFORE_SET_BOSS_STATE})
    {
    }

    // Every boss kill on the server, ours or not: a notable world event (scope), so always written.
    void OnAfterUpdateEncounterState(Map* map, EncounterCreditType type, uint32 creditEntry, Unit* /*source*/,
                                     Difficulty difficulty, std::list<DungeonEncounter const*> const* /*encounters*/,
                                     uint32 dungeonCompleted, bool updated) override
    {
        if (!updated || !map)
            return;
        Party const party = PartyOf(map);
        if (party.guids.empty())
            return;
        std::string bossName = "encounter " + std::to_string(creditEntry);
        if (type == ENCOUNTER_CREDIT_KILL_CREATURE)
            if (CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(creditEntry))
                bossName = proto->Name;
        uint32 const guildId = party.anyOurs ? party.ourGuild : party.anyGuild;
        uint64 const runId = BridgeRunIdFor(static_cast<uint32>(party.guids.front()));
        EventSink::Instance().Push(EventType::BossKill, 0, guildId, runId,
                                   BossKillPayload(map->GetId(), map->GetMapName(), creditEntry, bossName,
                                                   static_cast<uint32>(difficulty), dungeonCompleted != 0, party.guids));
        if (party.claimGuid)
            Firsts::Instance().Claim("boss", creditEntry * 4 + static_cast<uint32>(difficulty), party.claimGuid,
                                     party.claimGuild, "First to defeat " + bossName);
    }

    // A boss fight that ends without a kill (in progress -> fail / not started): ours only.
    void OnBeforeSetBossState(uint32 id, EncounterState newState, EncounterState oldState, Map* instance) override
    {
        if (!instance || oldState != IN_PROGRESS || (newState != FAIL && newState != NOT_STARTED))
            return;
        Party const party = PartyOf(instance);
        if (!party.anyOurs || party.guids.empty())
            return;
        uint64 const runId = BridgeRunIdFor(static_cast<uint32>(party.guids.front()));
        EventSink::Instance().Push(EventType::BossWipe, 0, party.ourGuild, runId,
                                   BossWipePayload(instance->GetId(), id, party.guids));
        if (runId)
            RunRegistry::Instance().NoteWipe(runId);
    }
};

void AddBridgeEventHooks()
{
    new BridgePlayerHooks();
    new BridgeGlobalHooks();
}
