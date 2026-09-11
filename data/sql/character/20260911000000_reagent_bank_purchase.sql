-- Persist the one-time per-character purchase of Material Storage.
-- Existing characters with stored balances already own the feature.

CREATE TABLE IF NOT EXISTS `custom_reagent_bank_access` (
  `character_id` INT UNSIGNED NOT NULL,
  `purchased_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`character_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_general_ci;

INSERT IGNORE INTO `custom_reagent_bank_access` (`character_id`)
SELECT DISTINCT `character_id`
FROM `custom_reagent_bank`
WHERE `amount` > 0;
