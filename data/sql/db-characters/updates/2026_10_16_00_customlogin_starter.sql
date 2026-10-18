-- Brambletown starter-package receipt; one row per character.
-- state: 0=pending human login, 1=completed or grandfathered, 2=partial grant / manual review.
CREATE TABLE IF NOT EXISTS `mod_customlogin_starter` (
  `guid` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB;

-- Historical rollout: level-2+ characters are exempt; level-1 characters are eligible.
-- Existing rows are never changed, preserving receipts for new characters leveled by bots.
INSERT IGNORE INTO `mod_customlogin_starter` (`guid`, `state`)
SELECT `guid`, CASE WHEN `level` >= 2 THEN 1 ELSE 0 END FROM `characters`;
