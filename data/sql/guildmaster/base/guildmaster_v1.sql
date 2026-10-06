-- The guildmaster data contract, version 1 (docs/superpowers/specs/2026-10-04-bridge-data-contract.md in
-- Solfood/guildmaster). Times are unix seconds (UTC). guid = characters.guid in this world's characters
-- database. 0 means "none". One such database per world: no statement here names another database.

-- This world, one row (id = 1). The bridge stamps world_id at the first boot and refuses to boot against a
-- database stamped with another id; the rest is rewritten every 10 s (Task 15) for the world controller.
CREATE TABLE IF NOT EXISTS `world_status` (
  `id` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `world_id` VARCHAR(32) NOT NULL,
  `bridge_version` VARCHAR(16) NOT NULL DEFAULT '',
  `booted_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `heartbeat_at` INT UNSIGNED NOT NULL DEFAULT 0,
  `population_size` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'bots in the fixed population',
  `population_online` INT UNSIGNED NOT NULL DEFAULT 0,
  `population_ready_at` INT UNSIGNED NULL COMMENT 'first time online >= ReadyShare % of size, this boot',
  `user_guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `test_guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `last_snapshot_all_at` INT UNSIGNED NULL,
  `events_dropped` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'since this boot',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The two guilds the bridge manages. role 'user' = the player's guild, 'test' = throwaway experiments.
CREATE TABLE IF NOT EXISTS `guilds` (
  `guild_id` INT UNSIGNED NOT NULL,
  `role` ENUM('user','test') NOT NULL,
  `name` VARCHAR(24) NOT NULL,
  `faction` ENUM('alliance','horde') NOT NULL COMMENT 'the leader''s faction; founders must match it',
  `leader_guid` INT UNSIGNED NOT NULL,
  `created_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guild_id`),
  UNIQUE KEY `role` (`role`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- What the app generated for a bot and the bridge keeps (founders: from create_founders). The bridge stores
-- traits and backstory as given and never interprets them; Plan 3 owns their meaning.
CREATE TABLE IF NOT EXISTS `bot_profiles` (
  `guid` INT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL,
  `origin` ENUM('founder','recruit') NOT NULL,
  `traits` JSON NOT NULL COMMENT '["Ambitious","Loyal"] as sent by the app',
  `backstory` VARCHAR(255) NOT NULL DEFAULT '',
  `order_id` BIGINT UNSIGNED NULL,
  `created_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`),
  KEY `guild` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- What happened in the world. Written by the bridge only. payload shapes: contract §3.
CREATE TABLE IF NOT EXISTS `events` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `ts` INT UNSIGNED NOT NULL,
  `type` VARCHAR(24) NOT NULL,
  `guid` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'the bot it is about; 0 for group events',
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0,
  `run_id` BIGINT UNSIGNED NULL COMMENT 'dungeon_runs.id when it happened in a run_dungeon order',
  `payload` JSON NOT NULL,
  PRIMARY KEY (`id`),
  KEY `guild_ts` (`guild_id`, `ts`),
  KEY `type_ts` (`type`, `ts`),
  KEY `guid_ts` (`guid`, `ts`),
  KEY `run` (`run_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- What the app or the world controller wants done. They insert (status pending); the bridge sets the rest.
CREATE TABLE IF NOT EXISTS `orders` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `created` INT UNSIGNED NOT NULL,
  `type` VARCHAR(24) NOT NULL,
  `params` JSON NOT NULL,
  `status` ENUM('pending','running','done','failed') NOT NULL DEFAULT 'pending',
  `result` VARCHAR(255) NULL COMMENT 'one plain sentence: what happened or why it failed',
  `result_data` JSON NULL,
  `started_at` INT UNSIGNED NULL,
  `done_at` INT UNSIGNED NULL,
  PRIMARY KEY (`id`),
  KEY `status_id` (`status`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The roster right now: one row per member of the user and test guilds, rewritten every 30 s.
CREATE TABLE IF NOT EXISTS `bot_state` (
  `guid` INT UNSIGNED NOT NULL,
  `name` VARCHAR(12) NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL,
  `online` TINYINT UNSIGNED NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `class` TINYINT UNSIGNED NOT NULL,
  `race` TINYINT UNSIGNED NOT NULL,
  `gender` TINYINT UNSIGNED NOT NULL,
  `map` SMALLINT UNSIGNED NOT NULL,
  `zone` INT UNSIGNED NOT NULL,
  `area` INT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL,
  `y` FLOAT NOT NULL,
  `z` FLOAT NOT NULL,
  `alive` TINYINT UNSIGNED NOT NULL,
  `ghost` TINYINT UNSIGNED NOT NULL,
  `hp_pct` TINYINT UNSIGNED NOT NULL,
  `durability_pct` TINYINT UNSIGNED NOT NULL,
  `money` BIGINT UNSIGNED NOT NULL COMMENT 'copper',
  `activity` VARCHAR(24) NOT NULL COMMENT 'playerbots new-rpg status, e.g. DO_QUEST',
  `focus` VARCHAR(16) NOT NULL DEFAULT 'none',
  `prof1` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'picked or preset primary profession skill id',
  `prof2` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  `in_group` TINYINT UNSIGNED NOT NULL,
  `run_id` BIGINT UNSIGNED NULL,
  `held` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 while held: a population member in a run, or being restored (clones in a run: read run_id)',
  `updated_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`),
  KEY `guild` (`guild_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The standing focus per bot (set by focus orders; re-applied by the bridge after logins and restarts).
CREATE TABLE IF NOT EXISTS `bot_focus` (
  `guid` INT UNSIGNED NOT NULL,
  `focus` VARCHAR(16) NOT NULL,
  `order_id` BIGINT UNSIGNED NULL,
  `set_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Per-bot snapshots (AzerothCore player dump text), in this world's database. Kept 14 days.
-- reason 'scheduled' = taken by a snapshot_all order (the world controller's schedule).
CREATE TABLE IF NOT EXISTS `bot_snapshots` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `guid` INT UNSIGNED NOT NULL,
  `account` INT UNSIGNED NOT NULL COMMENT 'needed to load the dump back',
  `name` VARCHAR(12) NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'membership when taken (restart recovery re-adds it)',
  `guild_rank` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `reason` ENUM('scheduled','pre_order','manual','pre_restore') NOT NULL,
  `order_id` BIGINT UNSIGNED NULL,
  `taken_at` INT UNSIGNED NOT NULL,
  `size_bytes` INT UNSIGNED NOT NULL,
  `dump` MEDIUMTEXT NOT NULL,
  PRIMARY KEY (`id`),
  KEY `guid_time` (`guid`, `taken_at`),
  KEY `taken` (`taken_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One row per run_dungeon order (id = the order id).
CREATE TABLE IF NOT EXISTS `dungeon_runs` (
  `id` BIGINT UNSIGNED NOT NULL,
  `dungeon` VARCHAR(24) NOT NULL COMMENT 'mod-dungeon-clear token, e.g. rfc',
  `dungeon_name` VARCHAR(64) NOT NULL,
  `map_id` INT UNSIGNED NOT NULL,
  `heroic` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `party` JSON NOT NULL COMMENT '[{guid,name,role,class,level}] in role order tank, heal, dps, dps, dps',
  `warnings` JSON NULL COMMENT '["no healer in this party", "party reordered: tank <name>, healer <name>"]',
  `approach` ENUM('travel','teleport','travel_then_teleport') NULL,
  `dc_run_id` VARCHAR(48) NULL,
  `result` ENUM('running','cleared','wiped','abandoned','failed') NOT NULL DEFAULT 'running',
  `fail_reason` VARCHAR(255) NULL,
  `bosses_killed` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `bosses_total` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `furthest_boss` VARCHAR(64) NULL,
  `wipes` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `loot` JSON NULL COMMENT '[{guid,item,quality}]',
  `started_at` INT UNSIGNED NOT NULL,
  `entered_at` INT UNSIGNED NULL COMMENT 'handed to mod-dungeon-clear',
  `ended_at` INT UNSIGNED NULL,
  `duration_s` INT UNSIGNED NULL,
  PRIMARY KEY (`id`),
  KEY `dungeon` (`dungeon`, `started_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Stuck-bot incidents (any population bot). Open while closed_at IS NULL.
CREATE TABLE IF NOT EXISTS `incidents` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `guid` INT UNSIGNED NOT NULL,
  `name` VARCHAR(12) NOT NULL,
  `kind` ENUM('dead_long','no_progress','path_fail') NOT NULL,
  `opened_at` INT UNSIGNED NOT NULL,
  `closed_at` INT UNSIGNED NULL,
  `map` SMALLINT UNSIGNED NOT NULL,
  `zone` INT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL,
  `y` FLOAT NOT NULL,
  `z` FLOAT NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `intent` VARCHAR(24) NOT NULL COMMENT 'new-rpg status when opened',
  `last_dest` VARCHAR(64) NOT NULL DEFAULT '' COMMENT 'map:x:y:z of the last stuck move, if any',
  `details` JSON NOT NULL,
  PRIMARY KEY (`id`),
  KEY `open_kind` (`closed_at`, `kind`),
  KEY `spot` (`map`, `zone`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Server firsts, claimed once (kind 'level' ref = level; kind 'boss' ref = creditEntry * 4 + difficulty).
CREATE TABLE IF NOT EXISTS `firsts` (
  `kind` VARCHAR(16) NOT NULL,
  `ref` INT UNSIGNED NOT NULL,
  `guid` INT UNSIGNED NOT NULL,
  `guild_id` INT UNSIGNED NOT NULL,
  `label` VARCHAR(96) NOT NULL,
  `ts` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`kind`, `ref`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- mod-dungeon-clear test runs (the bench), imported by server/tools/bench-import.sh. The app's estimates.
CREATE TABLE IF NOT EXISTS `bench_runs` (
  `dc_run_id` VARCHAR(48) NOT NULL,
  `dungeon` VARCHAR(24) NOT NULL,
  `level` TINYINT UNSIGNED NOT NULL,
  `heroic` TINYINT UNSIGNED NOT NULL,
  `roster` TINYINT UNSIGNED NOT NULL COMMENT '1 = hand-picked party (our run_dungeon), 0 = bench pool bots',
  `result` VARCHAR(24) NOT NULL COMMENT 'success, wipe, no_progress, stalled_timeout, ...',
  `fail_reason` VARCHAR(255) NOT NULL DEFAULT '',
  `duration_s` INT UNSIGNED NOT NULL,
  `bosses_killed` TINYINT UNSIGNED NOT NULL,
  `bosses_total` TINYINT UNSIGNED NOT NULL,
  `started_at` INT UNSIGNED NOT NULL,
  `imported_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`dc_run_id`),
  KEY `dungeon` (`dungeon`, `level`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The `legends` view (hall of legends) is NOT created here: it reads the fork's playerbots_raisings table,
-- and that database's name differs per world. BridgeDatabaseScript (re)creates it at every boot.
