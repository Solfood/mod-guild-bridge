-- World of Craft part 1 (Plan 6a): profession skills and the current craft on the roster.
-- prof1_skill / prof2_skill: the skill value of prof1 / prof2 (0 = none). craft: the item it is making on a craft trip
-- ("an enchant" for an enchant), NULL otherwise.
ALTER TABLE `bot_state`
  ADD COLUMN `prof1_skill` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'skill value of prof1 (Plan 6a)' AFTER `prof2`,
  ADD COLUMN `prof2_skill` SMALLINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'skill value of prof2 (Plan 6a)' AFTER `prof1_skill`,
  ADD COLUMN `craft` VARCHAR(48) NULL COMMENT 'the item it is making on a craft trip (Plan 6a)' AFTER `guided`;
