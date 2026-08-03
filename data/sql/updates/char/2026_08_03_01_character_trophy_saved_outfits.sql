-- RTG character Trophy Collection and Saved Outfit metadata.
-- Target database: CHARACTERS
-- Apply after 2026_08_03_00_character_appearance_memory_integrity.sql.

SET @rtg_has_first_discovered := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='first_discovered_at'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_first_discovered=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `first_discovered_at` int unsigned NOT NULL DEFAULT ''0'' COMMENT ''First confirmed discovery timestamp'' AFTER `discovered_at`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

SET @rtg_has_last_discovered := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='last_discovered_at'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_last_discovered=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `last_discovered_at` int unsigned NOT NULL DEFAULT ''0'' COMMENT ''Most recent confirmed acquisition timestamp'' AFTER `first_discovered_at`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

SET @rtg_has_legacy_discovery := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='legacy_discovery'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_legacy_discovery=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `legacy_discovery` tinyint unsigned NOT NULL DEFAULT ''1'' COMMENT ''1 when exact acquisition provenance predates Trophy tracking'' AFTER `discovery_source`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

ALTER TABLE `custom_unlocked_appearances`
  MODIFY COLUMN `discovery_source` varchar(48) NOT NULL DEFAULT 'legacy',
  MODIFY COLUMN `first_discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'First confirmed discovery timestamp',
  MODIFY COLUMN `last_discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'Most recent confirmed acquisition timestamp',
  MODIFY COLUMN `legacy_discovery` tinyint unsigned NOT NULL DEFAULT '1' COMMENT '1 when exact acquisition provenance predates Trophy tracking';

UPDATE `custom_unlocked_appearances`
SET `first_discovered_at`=`discovered_at`,
    `last_discovered_at`=`discovered_at`,
    `legacy_discovery`=1
WHERE `first_discovered_at`=0 AND `last_discovered_at`=0;

SET @rtg_has_trophy_index := (
  SELECT COUNT(*) FROM `information_schema`.`statistics`
  WHERE `table_schema`=DATABASE()
    AND `table_name`='custom_unlocked_appearances'
    AND `index_name`='idx_character_trophy_date'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_trophy_index=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD KEY `idx_character_trophy_date` (`owner_guid`,`first_discovered_at`,`item_template_id`)',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

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

SET @rtg_has_outfit_created := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_transmogrification_sets' AND `column_name`='CreatedAt'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_outfit_created=0,
  'ALTER TABLE `custom_transmogrification_sets` ADD COLUMN `CreatedAt` int unsigned NOT NULL DEFAULT ''0'' AFTER `SetData`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

SET @rtg_has_outfit_updated := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_transmogrification_sets' AND `column_name`='UpdatedAt'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_outfit_updated=0,
  'ALTER TABLE `custom_transmogrification_sets` ADD COLUMN `UpdatedAt` int unsigned NOT NULL DEFAULT ''0'' AFTER `CreatedAt`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

SET @rtg_has_outfit_version := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_transmogrification_sets' AND `column_name`='DataVersion'
);
SET @rtg_transmog_sql := IF(
  @rtg_has_outfit_version=0,
  'ALTER TABLE `custom_transmogrification_sets` ADD COLUMN `DataVersion` tinyint unsigned NOT NULL DEFAULT ''1'' AFTER `UpdatedAt`',
  'SELECT 1'
);
PREPARE rtg_transmog_stmt FROM @rtg_transmog_sql;
EXECUTE rtg_transmog_stmt;
DEALLOCATE PREPARE rtg_transmog_stmt;

ALTER TABLE `custom_transmogrification_sets`
  MODIFY COLUMN `Owner` int unsigned NOT NULL COMMENT 'Character guidLow',
  MODIFY COLUMN `CreatedAt` int unsigned NOT NULL DEFAULT '0',
  MODIFY COLUMN `UpdatedAt` int unsigned NOT NULL DEFAULT '0',
  MODIFY COLUMN `DataVersion` tinyint unsigned NOT NULL DEFAULT '1';

UPDATE `custom_transmogrification_sets`
SET `CreatedAt`=IF(`CreatedAt`=0,UNIX_TIMESTAMP(),`CreatedAt`),
    `UpdatedAt`=IF(`UpdatedAt`=0,UNIX_TIMESTAMP(),`UpdatedAt`),
    `DataVersion`=1;

DELETE s
FROM `custom_transmogrification_sets` s
LEFT JOIN `characters` c ON c.`guid`=s.`Owner`
WHERE c.`guid` IS NULL;

SELECT COUNT(*) AS `character_trophy_rows`
FROM `custom_unlocked_appearances`;
SELECT COUNT(*) AS `saved_outfits`
FROM `custom_transmogrification_sets`;
SELECT COUNT(*) AS `legacy_trophies_without_exact_date`
FROM `custom_unlocked_appearances`
WHERE `legacy_discovery`=1 AND `first_discovered_at`=0;
