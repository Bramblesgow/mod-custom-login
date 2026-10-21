-- Brambletown starter-package receipt; one row per character.
-- state: 0=pending human login, 1=completed or grandfathered, 2=partial grant / manual review.
CREATE TABLE IF NOT EXISTS `mod_customlogin_starter` (
  `guid` INT UNSIGNED NOT NULL,
  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,
  `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB;

-- Initial rollout only. INSERT IGNORE preserves all existing receipts.
-- 0x20 = AT_LOGIN_FIRST: never-logged level-1 characters remain eligible.
-- Bot-first accounts are supported by the pending receipt written by
-- OnPlayerLogin before the core consumes AT_LOGIN_FIRST. For older bot-first
-- characters without a receipt, an administrator must verify eligibility.
INSERT IGNORE INTO mod_customlogin_starter (guid, state)
SELECT guid, CASE WHEN level = 1 AND (at_login & 0x20) <> 0 THEN 0 ELSE 1 END
FROM characters;
