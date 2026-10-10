-- Plan 5a: each member's route_style and head_to, stored by the orders and re-applied by the bridge after every login
-- (the fork keeps them in memory only). Contract §2.17.
CREATE TABLE IF NOT EXISTS `bot_route` (
  `guid` INT UNSIGNED NOT NULL,
  `style` VARCHAR(12) NULL COMMENT 'steady, curious or easygoing (NULL: the fork picks from the guid)',
  `head_to_zone` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 = none; cleared once reached or outlevelled',
  `style_order_id` BIGINT UNSIGNED NULL,
  `head_to_order_id` BIGINT UNSIGNED NULL,
  `set_at` INT UNSIGNED NOT NULL,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
