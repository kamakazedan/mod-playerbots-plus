-- mod-playerbots-plus: the profession skill each random bot has earned
-- (Skills.*). Playerbots sets every random bot's professions to five times its
-- level whenever it looks it over; the module puts each back to this, which
-- rises only by the game's own skill-ups from gathering and crafting. Rows of
-- characters gone are dropped at startup.
CREATE TABLE IF NOT EXISTS `playerbots_plus_skills` (
  `guid` INT UNSIGNED NOT NULL,
  `skill` SMALLINT UNSIGNED NOT NULL,
  `earned` SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`, `skill`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: the profession skill random bots have earned';
