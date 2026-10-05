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
                     "run_id, held, updated_at) VALUES (?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                     "?, ?, NULLIF(?, 0), ?, ?)",
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
}
