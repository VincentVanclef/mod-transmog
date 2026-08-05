-- RTG Transmog cleanup for existing CHARACTERS databases.
-- Hidden equipment appearances are no longer offered. Existing hidden rows are
-- restored to the equipped item's original appearance by removing FakeEntry=1.
DELETE FROM `custom_transmogrification`
WHERE `FakeEntry` = 1;

-- Earlier migrations and manual schemas could leave two identical account_id
-- indexes. Keep the canonical idx_account_id and drop only the duplicate.
SET @rtg_has_idx_account := (
  SELECT COUNT(*) FROM `information_schema`.`statistics`
  WHERE `table_schema` = DATABASE()
    AND `table_name` = 'custom_unlocked_appearances'
    AND `index_name` = 'idx_account'
);
SET @rtg_has_idx_account_id := (
  SELECT COUNT(*) FROM `information_schema`.`statistics`
  WHERE `table_schema` = DATABASE()
    AND `table_name` = 'custom_unlocked_appearances'
    AND `index_name` = 'idx_account_id'
);
SET @rtg_drop_duplicate_account_index := IF(
  @rtg_has_idx_account > 0 AND @rtg_has_idx_account_id > 0,
  'ALTER TABLE `custom_unlocked_appearances` DROP INDEX `idx_account`',
  'SELECT 1'
);
PREPARE rtg_stmt FROM @rtg_drop_duplicate_account_index;
EXECUTE rtg_stmt;
DEALLOCATE PREPARE rtg_stmt;
