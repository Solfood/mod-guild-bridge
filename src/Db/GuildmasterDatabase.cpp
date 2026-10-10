/*
 * This file is part of mod-guild-bridge (Solfood/guildmaster). Released under GNU GPL v2 or later.
 */

#include "GuildmasterDatabase.h"

#include "MySQLPreparedStatement.h"

GuildmasterDatabasePool GuildmasterDatabase;
bool GuildmasterDatabaseReady = false;

void GuildmasterDatabaseConnection::DoPrepareStatements()
{
    if (!m_reconnecting)
        m_stmts.resize(MAX_GUILDMASTER_STATEMENTS);

    PrepareStatement(GM_INS_EVENT,
                     "INSERT INTO events (ts, type, guid, guild_id, run_id, payload) VALUES (?, ?, ?, ?, NULLIF(?, 0), ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_REP_BOT_STATE,
                     "REPLACE INTO bot_state (guid, name, guild_id, online, level, class, race, gender, map, zone, area, "
                     "x, y, z, alive, ghost, hp_pct, durability_pct, money, activity, focus, prof1, prof2, in_group, "
                     "run_id, held, route_hub, route_done, route_total, struggling, route_style, updated_at) VALUES "
                     "(?, ?, ?, 1, "
                     "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                     "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                     "NULLIF(?, 0), ?, NULLIF(?, ''), ?, ?, ?, NULLIF(?, ''), ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_ROUTE_HUB,
                     "INSERT INTO route_hubs (hub_id, name, faction, map, zone, area, min_level, level, max_level, "
                     "quest_count, x, y, written_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_QUEST_DROP, "INSERT INTO quest_drops (quest, drops, last_at, updated_at) VALUES (?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_SNAPSHOT,
                     "INSERT INTO bot_snapshots (guid, account, name, level, guild_id, guild_rank, reason, order_id, taken_at, "
                     "size_bytes, dump) VALUES (?, ?, ?, ?, ?, ?, ?, NULLIF(?, 0), ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_UPD_ORDER_FINISH,
                     "UPDATE orders SET status = ?, result = ?, result_data = CAST(NULLIF(?, '') AS JSON), done_at = ? "
                     "WHERE id = ?",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_REP_GUILD,
                     "REPLACE INTO guilds (guild_id, role, name, faction, leader_guid, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_REP_FOCUS, "REPLACE INTO bot_focus (guid, focus, order_id, set_at) VALUES (?, ?, NULLIF(?, 0), ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_FIRST, "INSERT IGNORE INTO firsts (kind, ref, guid, guild_id, label, ts) VALUES (?, ?, ?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_RUN,
                     "INSERT INTO dungeon_runs (id, dungeon, dungeon_name, map_id, heroic, party, warnings, started_at) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_UPD_RUN_END,
                     "UPDATE dungeon_runs SET approach = NULLIF(?, ''), dc_run_id = NULLIF(?, ''), result = ?, "
                     "fail_reason = NULLIF(?, ''), bosses_killed = ?, bosses_total = ?, entered_at = NULLIF(?, 0), "
                     "ended_at = ?, duration_s = ? WHERE id = ?",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_INS_INCIDENT,
                     "INSERT INTO incidents (guid, name, kind, opened_at, map, zone, x, y, z, level, intent, last_dest, "
                     "details) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                     CONNECTION_ASYNC);
    // An offline member: a new row from the character cache, or the last-seen row kept with online = 0.
    // (Row alias, MySQL 8.0.19+: VALUES() here is deprecated in 8.4.)
    PrepareStatement(GM_UPS_BOT_OFFLINE,
                     "INSERT INTO bot_state (guid, name, guild_id, online, level, class, race, gender, map, zone, "
                     "area, x, y, z, alive, ghost, hp_pct, durability_pct, money, activity, focus, in_group, held, "
                     "updated_at) VALUES (?, ?, ?, 0, ?, ?, ?, ?, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-', ?, 0, ?, ?) "
                     "AS new ON DUPLICATE KEY UPDATE online = 0, guild_id = new.guild_id, held = new.held, "
                     "focus = new.focus, in_group = 0, run_id = NULL, struggling = 0, "
                     "updated_at = new.updated_at",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_UPD_WORLD_STATUS,
                     "UPDATE world_status SET bridge_version = ?, booted_at = ?, heartbeat_at = ?, population_size = ?, "
                     "population_online = ?, population_ready_at = NULLIF(?, 0), user_guild_id = ?, test_guild_id = ?, "
                     "events_dropped = ? WHERE id = 1",
                     CONNECTION_ASYNC);
    PrepareStatement(GM_REP_PROFILE,
                     "REPLACE INTO bot_profiles (guid, guild_id, origin, traits, backstory, order_id, created_at) "
                     "VALUES (?, ?, ?, ?, ?, NULLIF(?, 0), ?)",
                     CONNECTION_ASYNC);
}
