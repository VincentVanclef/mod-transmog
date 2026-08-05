-- RTG Transmog complete CHARACTERS database base.
-- Forward migrations remain in data/sql/updates/char for existing installations.

CREATE TABLE IF NOT EXISTS `custom_transmogrification` (
  `GUID` int unsigned NOT NULL COMMENT 'Item guidLow',
  `FakeEntry` int unsigned NOT NULL COMMENT 'Applied appearance item entry',
  `Owner` int unsigned NOT NULL COMMENT 'Character guidLow',
  PRIMARY KEY (`GUID`),
  KEY `idx_owner` (`Owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Applied item transmogrifications';

CREATE TABLE IF NOT EXISTS `custom_transmogrification_sets` (
  `Owner` int unsigned NOT NULL COMMENT 'Character guidLow',
  `PresetID` tinyint unsigned NOT NULL COMMENT 'Saved outfit identifier',
  `SetName` text COMMENT 'Player-facing saved outfit name',
  `SetData` text COMMENT 'Slot and appearance pairs',
  `CreatedAt` int unsigned NOT NULL DEFAULT '0',
  `UpdatedAt` int unsigned NOT NULL DEFAULT '0',
  `DataVersion` tinyint unsigned NOT NULL DEFAULT '1',
  PRIMARY KEY (`Owner`,`PresetID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Character-specific RTG Saved Outfits';

CREATE TABLE IF NOT EXISTS `custom_unlocked_appearances` (
  `account_id` int unsigned NOT NULL DEFAULT '0' COMMENT 'Account metadata only; collection ownership is character-specific',
  `owner_guid` int unsigned NOT NULL COMMENT 'Character guidLow',
  `item_template_id` int unsigned NOT NULL DEFAULT '0',
  `discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'Legacy first-known discovery timestamp',
  `first_discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'First confirmed discovery timestamp',
  `last_discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'Most recent confirmed acquisition timestamp',
  `discovery_source` varchar(48) NOT NULL DEFAULT 'legacy',
  `legacy_discovery` tinyint unsigned NOT NULL DEFAULT '1' COMMENT '1 when exact acquisition provenance predates Trophy tracking',
  PRIMARY KEY (`owner_guid`,`item_template_id`),
  KEY `idx_account_id` (`account_id`),
  KEY `idx_discovered_at` (`owner_guid`,`discovered_at`),
  KEY `idx_character_trophy_date` (`owner_guid`,`first_discovered_at`,`item_template_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Character-specific RTG item appearance memory';

CREATE TABLE IF NOT EXISTS `custom_unlocked_appearances_orphan_archive` (
  `account_id` int unsigned NOT NULL DEFAULT '0',
  `owner_guid` int unsigned NOT NULL DEFAULT '0',
  `item_template_id` int unsigned NOT NULL DEFAULT '0',
  `discovered_at` int unsigned NOT NULL DEFAULT '0',
  `discovery_source` varchar(48) NOT NULL DEFAULT 'legacy',
  `archived_at` int unsigned NOT NULL DEFAULT '0',
  `archive_reason` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`account_id`,`owner_guid`,`item_template_id`,`archived_at`),
  KEY `idx_account_item` (`account_id`,`item_template_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Appearance rows that cannot safely belong to a live character';

CREATE TABLE IF NOT EXISTS `custom_transmogrification_free_cooldown` (
  `Owner` int unsigned NOT NULL COMMENT 'Character guidLow',
  `last_free_transmog_ts` int unsigned NOT NULL DEFAULT '0' COMMENT 'Unix timestamp of last free paid transmog use',
  PRIMARY KEY (`Owner`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='RTG free transmog cooldown';
