-- Material Storage balances. The table name matches upstream mod-reagent-Bank.
--
-- Upstream used signed INT columns and accepted a broader set of trade goods
-- and gems. Before converting that schema, invalid signed rows are retained in
-- a quarantine table (never silently cast to enormous unsigned balances). All
-- valid pre-port balances are marked `legacy = 1`: they can be withdrawn once
-- using the upstream class policy, but new deposits always use strict rules.
--
-- `revision` makes every balance mutation compare-and-change. The server
-- deliberately fails its transaction if amount/revision is stale; do not drop
-- the mutation-guard foreign key.

-- `mutation_guard` is a deliberately tiny parent domain. Conditional runtime
-- updates set it to 0 when their compare-and-change precondition is stale;
-- because 0 has no parent, InnoDB rejects the statement independent of the
-- server's sql_mode. A missing row is not an error in MySQL (0-row UPDATE),
-- so mutations also INSERT the parent value 1 when the expected post-state is
-- absent; that duplicate-key failure is likewise sql_mode-independent.
CREATE TABLE IF NOT EXISTS `custom_reagent_bank_mutation_guard` (
  `guard` TINYINT UNSIGNED NOT NULL,
  PRIMARY KEY (`guard`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci;

INSERT IGNORE INTO `custom_reagent_bank_mutation_guard` (`guard`) VALUES (1);

CREATE TABLE IF NOT EXISTS `custom_reagent_bank` (
  `character_id` INT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL,
  `item_subclass` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `amount` INT UNSIGNED NOT NULL,
  `revision` INT UNSIGNED NOT NULL DEFAULT 0,
  `legacy` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `mutation_guard` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  PRIMARY KEY (`character_id`, `item_entry`),
  KEY `idx_item_entry` (`item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci;

CREATE TABLE IF NOT EXISTS `custom_reagent_bank_legacy_quarantine` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `character_id` BIGINT SIGNED NOT NULL,
  `item_entry` BIGINT SIGNED NOT NULL,
  `item_subclass` BIGINT SIGNED NOT NULL,
  `amount` BIGINT SIGNED NOT NULL,
  `reason` VARCHAR(64) NOT NULL,
  `quarantined_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_character_item` (`character_id`, `item_entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci;

-- A few hand-maintained upstream copies omitted the display metadata column.
-- Add it before reading it below so this remains a repair migration as well
-- as the normal upstream-schema conversion.
SET @schema := DATABASE();
SET @sql := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
      WHERE TABLE_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND COLUMN_NAME = 'item_subclass') = 0,
    'ALTER TABLE `custom_reagent_bank` ADD COLUMN `item_subclass` INT SIGNED NOT NULL DEFAULT 0 AFTER `item_entry`',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- The upstream schema is signed. Preserve non-positive or otherwise impossible
-- keys before changing types; such rows are not redeemable as material counts.
INSERT INTO `custom_reagent_bank_legacy_quarantine`
  (`character_id`, `item_entry`, `item_subclass`, `amount`, `reason`)
SELECT `character_id`, `item_entry`, `item_subclass`, `amount`, 'INVALID_SIGNED_ROW'
FROM `custom_reagent_bank`
WHERE `character_id` <= 0 OR `item_entry` <= 0 OR `amount` <= 0;

DELETE FROM `custom_reagent_bank`
WHERE `character_id` <= 0 OR `item_entry` <= 0 OR `amount` <= 0;

-- item_subclass is display/import metadata only and is re-derived by the
-- server. Clamp invalid legacy values before narrowing it to TINYINT.
UPDATE `custom_reagent_bank`
SET `item_subclass` = 0
WHERE `item_subclass` < 0 OR `item_subclass` > 255;

ALTER TABLE `custom_reagent_bank`
  MODIFY COLUMN `character_id` INT UNSIGNED NOT NULL,
  MODIFY COLUMN `item_entry` INT UNSIGNED NOT NULL,
  MODIFY COLUMN `item_subclass` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  MODIFY COLUMN `amount` INT UNSIGNED NOT NULL;

-- Add fields idempotently for installations with a pre-existing upstream
-- table. Dynamic SQL keeps compatibility with the core's supported MariaDB
-- versions, which may not support ADD COLUMN IF NOT EXISTS.
SET @schema := DATABASE();

SET @sql := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
      WHERE TABLE_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND COLUMN_NAME = 'revision') = 0,
    'ALTER TABLE `custom_reagent_bank` ADD COLUMN `revision` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `amount`',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @legacy_column_missing := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
      WHERE TABLE_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND COLUMN_NAME = 'legacy') = 0,
    1,
    0
  )
);

SET @sql := (
  SELECT IF(
    @legacy_column_missing = 1,
    'ALTER TABLE `custom_reagent_bank` ADD COLUMN `legacy` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `revision`',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := (
  SELECT IF(
    @legacy_column_missing = 1,
    'UPDATE `custom_reagent_bank` SET `legacy` = 1 WHERE `legacy` <> 1',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Existing installs need the guard column before its constraint is added.
SET @sql := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
      WHERE TABLE_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND COLUMN_NAME = 'mutation_guard') = 0,
    'ALTER TABLE `custom_reagent_bank` ADD COLUMN `mutation_guard` TINYINT UNSIGNED NOT NULL DEFAULT 1 AFTER `legacy`',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- Keep legacy default 0 for fresh/current rows. Existing upstream balances are
-- stamped once when upgrading a table that previously lacked the legacy field.
ALTER TABLE `custom_reagent_bank`
  MODIFY COLUMN `legacy` TINYINT UNSIGNED NOT NULL DEFAULT 0;
UPDATE `custom_reagent_bank` SET `mutation_guard` = 1
WHERE `mutation_guard` <> 1 OR `mutation_guard` IS NULL;
ALTER TABLE `custom_reagent_bank`
  MODIFY COLUMN `mutation_guard` TINYINT UNSIGNED NOT NULL DEFAULT 1;

-- Upstream did not require InnoDB. The guard needs transactional FK checks;
-- without InnoDB a failed debit could still leave an inventory mutation saved.
ALTER TABLE `custom_reagent_bank` ENGINE=InnoDB;

SET @sql := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.REFERENTIAL_CONSTRAINTS
      WHERE CONSTRAINT_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND CONSTRAINT_NAME = 'fk_reagent_bank_mutation_guard') = 0,
    'ALTER TABLE `custom_reagent_bank` ADD CONSTRAINT `fk_reagent_bank_mutation_guard` FOREIGN KEY (`mutation_guard`) REFERENCES `custom_reagent_bank_mutation_guard` (`guard`)',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := (
  SELECT IF(
    (SELECT COUNT(*) FROM information_schema.STATISTICS
      WHERE TABLE_SCHEMA = @schema
        AND TABLE_NAME = 'custom_reagent_bank'
        AND INDEX_NAME = 'idx_item_entry') = 0,
    'ALTER TABLE `custom_reagent_bank` ADD KEY `idx_item_entry` (`item_entry`)',
    'SELECT 1'
  )
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;
