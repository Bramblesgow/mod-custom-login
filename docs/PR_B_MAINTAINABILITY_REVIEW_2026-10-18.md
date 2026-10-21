# PR B — Custom Login readability and maintainability (2026-10-18)

This is a **review-only** refactor stacked on PR A
(`fix/starter-safety-pr-a-20261018`). It is not a production release.

## Why the code was reorganized

The original `GrantStarterPackage` function combined inventory movement,
ten class-specific equipment lists, skill training, optional cross-class
abilities, faction reputation, and money; `OnPlayerLogin` also performed
receipt eligibility, transactions, recovery and announcements.

New focused helpers keep these operations separate without introducing a
generic reward framework:

- `PrepareStarterBags`: preflight and preserve hunter ammunition, equip bags
- `GrantClassEquipment`: fixed per-class item lists and the Dread Pirate Ring
- `GrantWeaponSkills`: fixed per-class proficiency spells
- `GrantSpecialAbilities`: optional abilities, runes and teleports
- `GrantCapitalReputation`: optional faction reputations
- `GrantStarterPackage`: retains the original gate and grant order
- `ProcessStarterLogin`: pending receipt, bot-first behavior, eligibility
- `CompleteStarterGrant`: character/receipt transaction and failure recovery
- `SendPlayerAnnouncement`: replaces repeated faction join/leave templates

The existing emergency session callback/atomic fast-path comes from PR A
and was not reimplemented here.

## Preserved behavior

- All ten class lists retain their original item IDs, quantities and order.
- All ten class proficiency lists retain their original spell IDs and order.
- Four bags, Dread Pirate Ring, 25 gold and class items remain under `Login.BoA`.
- `Login.Skills`, `Login.SpecialAbility`, `Login.Reputation` and
  `Login.PlayerAnnounce` remain independent configuration gates.
- Human-first, bot-first/human-later, grandfathered, partial-grant and
  failed-transaction receipt outcomes are handled in the same sequence.
- Horde and Alliance join/leave announcements retain their original
  punctuation and text. The obsolete `Login.Announce` setting, which sent
  only an empty chat line, has been removed from code and sample config.
  Existing runtime configurations containing the unused key are tolerated.

## Documentation and test cleanup

- Correct historical statements that the module has no SQL dependency.
- Describe the first-*human*-login policy, receipt states and bag safeguards.
- Remove abandoned commented skill-reference code and outdated 2017
  reputation implementation rationale while preserving author attribution.
- Capture an immutable JSON manifest of the PR A reward item/skill lists;
  source regression tests now assert the refactored lists match that baseline.
- Extend CI to run the complete Python unittest suite on PR changes.
- Add executable in-memory SQLite tests for atomic commit/rollback of
  modeled inventory, money, and receipt data. **These do not execute C++**.

## Validation and release conditions

- Git diff whitespace verification.
- C++ source syntax check and independently compiled object using the
  installed AzerothCore revision and its real compile flags; no production
  worldserver binary was rebuilt or replaced.
- Python unittest discovery of source contracts, reward snapshots and
  in-memory SQLite receipt behavior.
- GitHub core-build and regression checks plus independent CodeRabbit review.
- Before deployment, test on an isolated worldserver or test character for
  first human login, bot-first then human login, all ten class packages,
  hunter quiver/ammo preservation, database failure, manual-review receipts,
  announcements, disabled configuration toggles and logout behavior.

No database migrations, real user character data, live module source,
runtime configuration or production server service have been modified.
