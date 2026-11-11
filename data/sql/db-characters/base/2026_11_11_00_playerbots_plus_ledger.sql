-- mod-playerbots-plus: the random bots' economy, a row per day and figure
-- (PlayerbotsPlus.Ledger.*). Figures named now.* are the latest look at the
-- bots that day; the rest are summed over the day. .pbp economy reads it.
CREATE TABLE IF NOT EXISTS `playerbots_plus_ledger` (
  `day` DATE NOT NULL,
  `name` VARCHAR(64) NOT NULL,
  `value` BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (`day`, `name`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: the random bots economy, by day';
