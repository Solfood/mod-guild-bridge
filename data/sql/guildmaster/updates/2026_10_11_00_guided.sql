-- Plan 5c (guide-driven bots): 1 while the bot follows a levelling guide. The route columns then hold the guide:
-- route_hub = its short name ("Human (1-13)"), route_done = steps passed, route_total = steps in the guide.
ALTER TABLE `bot_state`
  ADD COLUMN `guided` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '1 while it follows a levelling guide (Plan 5c)' AFTER `route_style`;
