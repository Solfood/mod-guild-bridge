-- Plan 5a (quest routes): route state on the roster, the hubs, and the safety net's drop counts (contract §2.6, §2.15,
-- §2.16). The bridge writes all three; the app only reads them.
ALTER TABLE `bot_state`
  ADD COLUMN `route_hub` VARCHAR(64) NULL COMMENT 'the hub it is bound for or working (NULL: none, or routes off)' AFTER `held`,
  ADD COLUMN `route_done` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'quests it turned in at that hub' AFTER `route_hub`,
  ADD COLUMN `route_total` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'route_done + the quests still doable there for it' AFTER `route_done`,
  ADD COLUMN `struggling` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 while retreating or after 5+ deaths in the last hour' AFTER `route_total`,
  ADD COLUMN `route_style` VARCHAR(12) NULL COMMENT 'steady, curious or easygoing' AFTER `struggling`;

-- Rewritten once per boot while AiPlayerbot.QuestRoutes = 1; empty while routes are off.
CREATE TABLE IF NOT EXISTS `route_hubs` (
  `hub_id` INT UNSIGNED NOT NULL,
  `name` VARCHAR(64) NOT NULL COMMENT 'the area most of its quest givers stand in',
  `faction` ENUM('alliance','horde') NOT NULL,
  `map` SMALLINT UNSIGNED NOT NULL,
  `zone` INT UNSIGNED NOT NULL,
  `area` INT UNSIGNED NOT NULL,
  `min_level` TINYINT UNSIGNED NOT NULL COMMENT 'lowest quest MinLevel',
  `level` TINYINT UNSIGNED NOT NULL COMMENT 'median quest level; outlevelled at level + 3',
  `max_level` TINYINT UNSIGNED NOT NULL COMMENT 'highest quest level',
  `quest_count` SMALLINT UNSIGNED NOT NULL,
  `x` FLOAT NOT NULL,
  `y` FLOAT NOT NULL,
  `written_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`hub_id`),
  KEY `faction_map` (`faction`, `map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The safety net's drops per quest across bots (fork playerbots_dropped_quests), rewritten every 5 minutes.
CREATE TABLE IF NOT EXISTS `quest_drops` (
  `quest` INT UNSIGNED NOT NULL,
  `drops` INT UNSIGNED NOT NULL,
  `last_at` INT UNSIGNED NOT NULL,
  `updated_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`quest`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
