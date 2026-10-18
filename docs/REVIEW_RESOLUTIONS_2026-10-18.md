# CodeRabbit review resolution — Custom Login

This is a proposed fix for the four findings in PR #2. The working AzerothCore
module on the production server and its live character database are unchanged.

## Hunter quiver / bags
- Inspect every equipped bag before modifying inventory. Existing nonstarter
  bags and higher-level hunter quivers are never destroyed.
- Only a level-one hunter's quiver may be replaced. Preflight that each
  contained stack can be stored in the ordinary backpack, reserve enough free
  backpack slots, move every item, confirm the quiver is empty, then destroy
  that empty quiver before equipping Frostweave bags.
- If any check fails, do not destroy the quiver or its contents. The receipt
  moves to manual-review state (2), never automatic repeat grants.

## Character save and receipt
- Persist inventory, money and receipt state in one character database
  transaction. The same transaction commits either state 1 (completed)
  or state 2 (partial/manual review) after attempted grants.
- Guard teleport-delayed Player::SaveToDB to avoid updating a receipt when
  saving the character would be deferred.
- Failure to read a durable receipt after attempting an insert does not
  authorize rewards. Because the transaction API returns no success indicator,
  reread the stored receipt after committing. Report success or partial-grant
  status only if its durable state matches the expected result; otherwise
  show an unavailable-status message for administrator investigation.

## Initial migration / bot-first behavior
- Existing receipt rows are never overwritten (INSERT IGNORE).
- The one-time backfill grants pending state only to first-login-flagged,
  level-one characters with no existing receipt. This avoids duplicating
  rewards for an already human-logged level-one character.
- OnPlayerLogin writes a pending receipt *before* AzerothCore clears the
  AT_LOGIN_FIRST flag, including for headless sessions. A bot-leveled alt
  therefore retains state 0 and can be granted upon its first human login.
- For a character used by a bot **before** the new receipt mechanism was
  deployed, eligibility cannot be reliably reconstructed from the at_login
  flag alone. An administrator must decide whether to flag that character
  pending; do not automatically grant to a previously logged-in level-one
  character. The initial live migration already created 423 pending records
  and this PR does not modify them.
- **Before deployment**, a server administrator must review all legacy
  pending receipts with a cleared first-login flag and determine whether
  each represents a bot-first character, an already rewarded player, or
  another exceptional case. Read-only candidate query on the *characters* DB:

  ```sql
  SELECT s.guid, c.name, c.level, c.at_login, c.totaltime
  FROM mod_customlogin_starter AS s
  JOIN characters AS c ON c.guid = s.guid
  WHERE s.state = 0 AND (c.at_login & 0x20) = 0;
  ```

  Do not automatically change these receipts. The flag can identify cases
  needing investigation; it cannot establish whether rewards were received.
  Record the intended disposition for each affected player account, and
  distinguish random-playerbot accounts from human-owned alts.

## Configuration
- Retain Login.BoA as the single master switch for the fixed Brambletown
  class-specific item and money starter package.
- Remove the unused per-item Login.BoA.* definitions and example config
  entries. Existing runtime config may still contain them; they have no effect
  when this version is eventually deployed and can be removed separately.
- Existing package item IDs, quantities, class skills, and 25-gold award
  remain otherwise unchanged.

## Validation requirements before release
- Syntax-only C++ compile against an existing AzerothCore build configuration.
- Isolated regression tests for the SQL backfill and source safeguards.
- GitHub CI/CodeRabbit review and actioning of significant findings.
- A full rebuild against the targeted production source plus in-game tests:
  new hunter with default ammo, full backpack, custom occupied bag, human-first,
  bot-first/higher-level hunter, repeated login, disconnect/restart, GUID reuse,
  partial grant and receipt persistence.
- No live SQL writes or server restarts as part of this PR.
