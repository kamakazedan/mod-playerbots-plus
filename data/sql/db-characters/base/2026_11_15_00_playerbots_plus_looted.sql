-- mod-playerbots-plus: the stacks bots looted or made themselves, the only
-- things a bot ever sells (the auction house, Trade chat). A stack stays its
-- own while nothing but more of its own joins it; `away` is up for auction or
-- in the post; `made` is some of it made with its professions. Rows of stacks
-- gone, or with someone else, are dropped at startup. A table made before
-- `made` was has it added by the module at startup.
CREATE TABLE IF NOT EXISTS `playerbots_plus_looted` (
  `item` INT UNSIGNED NOT NULL,
  `owner` INT UNSIGNED NOT NULL,
  `count` INT UNSIGNED NOT NULL DEFAULT 0,
  `away` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `made` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`item`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: stacks bots looted or made themselves, the only things they sell';
