CREATE TABLE IF NOT EXISTS `custom_transmogrification_free_cooldown` (
  `Owner` int(10) unsigned NOT NULL COMMENT 'Player guidLow',
  `last_free_transmog_ts` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'Unix timestamp of last free paid transmog use',
  PRIMARY KEY (`Owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='RTG free transmog cooldown';
