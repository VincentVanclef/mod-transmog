-- RTG character-owned appearance memory and discovery provenance.
-- Target database: CHARACTERS

CREATE TABLE IF NOT EXISTS `custom_unlocked_appearances` (
  `account_id` int unsigned NOT NULL DEFAULT '0' COMMENT 'Account metadata only; collection ownership is character-specific',
  `owner_guid` int unsigned NOT NULL COMMENT 'Character guidLow',
  `item_template_id` int unsigned NOT NULL DEFAULT '0',
  `discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'First known discovery timestamp; 0 for legacy imports',
  `discovery_source` varchar(24) NOT NULL DEFAULT 'legacy',
  PRIMARY KEY (`owner_guid`,`item_template_id`),
  KEY `idx_account_id` (`account_id`),
  KEY `idx_discovered_at` (`owner_guid`,`discovered_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Character-specific RTG item appearance memory';

-- Use information_schema guards and conditional ALTER statements so this
-- forward migration remains compatible with the older MySQL/MariaDB versions
-- commonly used by AzerothCore installations.
SET @rtg_has_owner_guid := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='owner_guid'
);
SET @rtg_appearance_sql := IF(
  @rtg_has_owner_guid=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `owner_guid` int unsigned NOT NULL DEFAULT ''0'' COMMENT ''Character guidLow'' AFTER `account_id`',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

SET @rtg_has_discovered_at := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='discovered_at'
);
SET @rtg_appearance_sql := IF(
  @rtg_has_discovered_at=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `discovered_at` int unsigned NOT NULL DEFAULT ''0'' COMMENT ''First known discovery timestamp; 0 for legacy imports'' AFTER `item_template_id`',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

SET @rtg_has_discovery_source := (
  SELECT COUNT(*) FROM `information_schema`.`columns`
  WHERE `table_schema`=DATABASE() AND `table_name`='custom_unlocked_appearances' AND `column_name`='discovery_source'
);
SET @rtg_appearance_sql := IF(
  @rtg_has_discovery_source=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD COLUMN `discovery_source` varchar(24) NOT NULL DEFAULT ''legacy'' AFTER `discovered_at`',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

ALTER TABLE `custom_unlocked_appearances`
  MODIFY COLUMN `account_id` int unsigned NOT NULL DEFAULT '0' COMMENT 'Account metadata only; collection ownership is character-specific',
  MODIFY COLUMN `owner_guid` int unsigned NOT NULL DEFAULT '0' COMMENT 'Character guidLow',
  MODIFY COLUMN `item_template_id` int unsigned NOT NULL DEFAULT '0',
  MODIFY COLUMN `discovered_at` int unsigned NOT NULL DEFAULT '0' COMMENT 'First known discovery timestamp; 0 for legacy imports',
  MODIFY COLUMN `discovery_source` varchar(24) NOT NULL DEFAULT 'legacy';

CREATE TABLE IF NOT EXISTS `custom_unlocked_appearances_orphan_archive` (
  `account_id` int unsigned NOT NULL DEFAULT '0',
  `owner_guid` int unsigned NOT NULL DEFAULT '0',
  `item_template_id` int unsigned NOT NULL DEFAULT '0',
  `discovered_at` int unsigned NOT NULL DEFAULT '0',
  `discovery_source` varchar(24) NOT NULL DEFAULT 'legacy',
  `archived_at` int unsigned NOT NULL DEFAULT '0',
  `archive_reason` varchar(64) NOT NULL DEFAULT '',
  PRIMARY KEY (`account_id`,`owner_guid`,`item_template_id`,`archived_at`),
  KEY `idx_account_item` (`account_id`,`item_template_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='Preserved appearance rows that cannot safely belong to a live character';

-- Preserve account-wide legacy rows and deleted-character rows before removing
-- them from the active character collection. Account-wide rows cannot be safely
-- assigned to one character without inventing ownership.
INSERT IGNORE INTO `custom_unlocked_appearances_orphan_archive`
(`account_id`,`owner_guid`,`item_template_id`,`discovered_at`,`discovery_source`,`archived_at`,`archive_reason`)
SELECT a.`account_id`,a.`owner_guid`,a.`item_template_id`,a.`discovered_at`,a.`discovery_source`,
       UNIX_TIMESTAMP(),
       CASE WHEN a.`owner_guid`=0 THEN 'legacy account-wide row' ELSE 'character no longer exists' END
FROM `custom_unlocked_appearances` a
LEFT JOIN `characters` c ON c.`guid`=a.`owner_guid`
WHERE a.`owner_guid`=0 OR c.`guid` IS NULL;

DELETE a
FROM `custom_unlocked_appearances` a
LEFT JOIN `characters` c ON c.`guid`=a.`owner_guid`
WHERE a.`owner_guid`=0 OR c.`guid` IS NULL;

-- Converge an older account-wide primary key to character ownership.
SET @rtg_appearance_pk := (
  SELECT GROUP_CONCAT(`column_name` ORDER BY `seq_in_index` SEPARATOR ',')
  FROM `information_schema`.`statistics`
  WHERE `table_schema`=DATABASE()
    AND `table_name`='custom_unlocked_appearances'
    AND `index_name`='PRIMARY'
);
SET @rtg_appearance_sql := IF(
  @rtg_appearance_pk IS NOT NULL AND @rtg_appearance_pk <> 'owner_guid,item_template_id',
  'ALTER TABLE `custom_unlocked_appearances` DROP PRIMARY KEY',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

SET @rtg_appearance_pk := (
  SELECT GROUP_CONCAT(`column_name` ORDER BY `seq_in_index` SEPARATOR ',')
  FROM `information_schema`.`statistics`
  WHERE `table_schema`=DATABASE()
    AND `table_name`='custom_unlocked_appearances'
    AND `index_name`='PRIMARY'
);
SET @rtg_appearance_sql := IF(
  @rtg_appearance_pk IS NULL,
  'ALTER TABLE `custom_unlocked_appearances` ADD PRIMARY KEY (`owner_guid`,`item_template_id`)',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

SET @rtg_has_account_index := (
  SELECT COUNT(*) FROM `information_schema`.`statistics`
  WHERE `table_schema`=DATABASE()
    AND `table_name`='custom_unlocked_appearances'
    AND `index_name`='idx_account_id'
);
SET @rtg_appearance_sql := IF(
  @rtg_has_account_index=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD KEY `idx_account_id` (`account_id`)',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

SET @rtg_has_discovered_index := (
  SELECT COUNT(*) FROM `information_schema`.`statistics`
  WHERE `table_schema`=DATABASE()
    AND `table_name`='custom_unlocked_appearances'
    AND `index_name`='idx_discovered_at'
);
SET @rtg_appearance_sql := IF(
  @rtg_has_discovered_index=0,
  'ALTER TABLE `custom_unlocked_appearances` ADD KEY `idx_discovered_at` (`owner_guid`,`discovered_at`)',
  'SELECT 1'
);
PREPARE rtg_appearance_stmt FROM @rtg_appearance_sql;
EXECUTE rtg_appearance_stmt;
DEALLOCATE PREPARE rtg_appearance_stmt;

UPDATE `custom_unlocked_appearances`
SET `discovery_source`='legacy'
WHERE `discovery_source`='';

-- Preserve appearances that are visibly active on a live character even when
-- an older account-wide collection schema failed to remember them correctly.
INSERT IGNORE INTO `custom_unlocked_appearances`
(`account_id`,`owner_guid`,`item_template_id`,`discovered_at`,`discovery_source`)
SELECT c.`account`,t.`Owner`,t.`FakeEntry`,0,'active_transmog'
FROM `custom_transmogrification` t
JOIN `characters` c ON c.`guid`=t.`Owner`
WHERE t.`FakeEntry` NOT IN (0,1);

SELECT COUNT(*) AS `active_character_appearance_rows`
FROM `custom_unlocked_appearances`;
SELECT COUNT(*) AS `archived_appearance_rows`
FROM `custom_unlocked_appearances_orphan_archive`;
