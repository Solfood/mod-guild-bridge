// src/Core/EventPayloads.h
/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 * Pure. Every events.payload shape of the data contract (§3) is built here and nowhere else.
 */

#ifndef MOD_GUILD_BRIDGE_CORE_EVENTPAYLOADS_H
#define MOD_GUILD_BRIDGE_CORE_EVENTPAYLOADS_H

#include "Json.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace GuildBridge
{
enum class EventType : uint8_t
{
    Level, Loot, Death, PvpKill, Achievement, BossKill, BossWipe, GuildJoin, GuildLeave, GuildRank, GuildFounded,
    First, RunStart, RunTravel, RunTeleport, RunAmbush, RunEnd, Restore, Fake, Count
};

inline char const* EventTypeName(EventType type)
{
    static char const* const names[] = {"level",      "loot",          "death",      "pvp_kill",   "achievement",
                                        "boss_kill",  "boss_wipe",     "guild_join", "guild_leave", "guild_rank",
                                        "guild_founded", "first",      "run_start",  "run_travel", "run_teleport",
                                        "run_ambush", "run_end",       "restore",    "fake"};
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(EventType::Count));
    return names[static_cast<std::size_t>(type)];
}

struct Where
{
    uint32_t map = 0;
    uint32_t zone = 0;
    float x = 0.f, y = 0.f, z = 0.f;
};

struct DeathInfo
{
    std::string killerType;  // "creature" or "player"
    uint32_t killerEntry = 0;
    uint32_t killerGuid = 0;
    std::string killerName;
    uint32_t killerLevel = 0;
    Where where;
};

inline JsonObject& AddWhere(JsonObject& json, Where const& w)
{
    return json.UInt("map", w.map).UInt("zone", w.zone).Num("x", w.x).Num("y", w.y).Num("z", w.z);
}

inline std::string LevelPayload(uint32_t level, uint32_t oldLevel, uint32_t cls, uint32_t race, Where const& w)
{
    JsonObject j;
    j.UInt("level", level).UInt("old_level", oldLevel).UInt("class", cls).UInt("race", race);
    return AddWhere(j, w).Build();
}

inline std::string LootPayload(uint32_t itemEntry, std::string_view itemName, uint32_t quality, uint32_t itemLevel,
                               uint32_t count, std::string_view source, Where const& w)
{
    JsonObject j;
    j.UInt("item_entry", itemEntry).Str("item_name", itemName).UInt("quality", quality).UInt("item_level", itemLevel)
        .UInt("count", count).Str("source", source);
    return AddWhere(j, w).Build();
}

inline std::string DeathPayload(DeathInfo const& d)
{
    JsonObject j;
    j.Str("killer_type", d.killerType).UInt("killer_entry", d.killerEntry).UInt("killer_guid", d.killerGuid)
        .Str("killer_name", d.killerName).UInt("killer_level", d.killerLevel);
    return AddWhere(j, d.where).Build();
}

inline std::string PvpKillPayload(uint32_t victimGuid, std::string_view victimName, uint32_t victimLevel,
                                  Where const& w)
{
    JsonObject j;
    j.UInt("victim_guid", victimGuid).Str("victim_name", victimName).UInt("victim_level", victimLevel);
    return AddWhere(j, w).Build();
}

inline std::string AchievementPayload(uint32_t id, std::string_view name, uint32_t points)
{
    return JsonObject().UInt("achievement_id", id).Str("name", name).UInt("points", points).Build();
}

inline std::string BossKillPayload(uint32_t map, std::string_view mapName, uint32_t creditEntry,
                                   std::string_view bossName, uint32_t difficulty, bool dungeonCompleted,
                                   std::vector<uint64_t> const& participants)
{
    return JsonObject().UInt("map", map).Str("map_name", mapName).UInt("credit_entry", creditEntry)
        .Str("boss_name", bossName).UInt("difficulty", difficulty).Bool("dungeon_completed", dungeonCompleted)
        .UIntArray("participants", participants).Build();
}

inline std::string BossWipePayload(uint32_t map, uint32_t bossId, std::vector<uint64_t> const& participants)
{
    return JsonObject().UInt("map", map).UInt("boss_id", bossId).UIntArray("participants", participants).Build();
}

inline std::string GuildJoinPayload(std::string_view guildName, uint32_t rank)
{
    return JsonObject().Str("guild_name", guildName).UInt("rank", rank).Build();
}

inline std::string GuildLeavePayload(std::string_view guildName, bool kicked)
{
    return JsonObject().Str("guild_name", guildName).Bool("kicked", kicked).Build();
}

inline std::string GuildRankPayload(uint32_t rank, bool promoted)
{
    return JsonObject().UInt("rank", rank).Bool("promoted", promoted).Build();
}

inline std::string GuildFoundedPayload(std::string_view guildName, std::string_view faction,
                                       std::string_view leaderName)
{
    return JsonObject().Str("guild_name", guildName).Str("faction", faction).Str("leader_name", leaderName).Build();
}

inline std::string FirstPayload(std::string_view kind, uint32_t ref, std::string_view label)
{
    return JsonObject().Str("kind", kind).UInt("ref", ref).Str("label", label).Build();
}

inline std::string RunStartPayload(uint64_t runId, std::string_view dungeon, std::vector<uint64_t> const& party,
                                   std::string_view approach)
{
    return JsonObject().UInt("run_id", runId).Str("dungeon", dungeon).UIntArray("party", party)
        .Str("approach", approach).Build();
}

inline std::string RunTravelPayload(uint64_t runId, float distance)
{
    return JsonObject().UInt("run_id", runId).Num("distance", distance).Build();
}

inline std::string RunTeleportPayload(uint64_t runId, std::string_view reason)
{
    return JsonObject().UInt("run_id", runId).Str("reason", reason).Build();
}

inline std::string RunAmbushPayload(uint64_t runId, std::string_view killerName, bool pvp)
{
    return JsonObject().UInt("run_id", runId).Str("killer_name", killerName).Bool("pvp", pvp).Build();
}

inline std::string RunEndPayload(uint64_t runId, std::string_view result, std::string_view failReason,
                                 uint32_t bossesKilled, uint32_t bossesTotal, uint32_t durationS, uint32_t wipes)
{
    return JsonObject().UInt("run_id", runId).Str("result", result).Str("fail_reason", failReason)
        .UInt("bosses_killed", bossesKilled).UInt("bosses_total", bossesTotal).UInt("duration_s", durationS)
        .UInt("wipes", wipes).Build();
}

inline std::string RestorePayload(uint64_t snapshotId, bool ok, std::string_view reason)
{
    return JsonObject().UInt("snapshot_id", snapshotId).Bool("ok", ok).Str("reason", reason).Build();
}

inline std::string FakePayload(std::string_view note) { return JsonObject().Str("note", note).Build(); }
}  // namespace GuildBridge

#endif
