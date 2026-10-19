# PR A — Custom Login starter safety corrections (2026-10-18)

This branch is based on `fix/starter-package-review-20261018`, the current
reviewed starter-package fix branch. It is **not** the deployed server version.
No changes to live worldserver files, character data, configuration, or runtime.

## A1 — Uncertain starter grant

- The character save and receipt update remain in a single synchronous
  transaction. If the observed durable receipt does not match the intended
  state, attempt a conditional pending (0) to manual-review (2) receipt update.
- Whether the receipt recovers as completed (1) or manual-review (2), or is
  still unreadable (0/unknown), schedule a **no-save logout**. A failed
  initial commit can leave granted rewards in memory. Keeping the player
  online after such a failure is unsafe.
- Never call `LogoutPlayer(false)` in the login hook; AzerothCore's character
  handler continues to use the player after that hook returns.
- The no-save logout runs during the next **real** world-session update.
  This route is only used after attempted player rewards; headless bot
  sessions never receive package items.

## A2 — First login used by a playerbot

- The existing initial login hook writes an `INSERT IGNORE` pending receipt.
  It is allowed to fail closed. AzerothCore then removes `AT_LOGIN_FIRST`
  before running `OnPlayerFirstLogin`.
- In `OnPlayerFirstLogin`, check the durable receipt. If missing, restore
  `AT_LOGIN_FIRST` so a later player save does not permanently make the
  character ineligible. This works for headless bot sessions, which are
  updated by playerbots rather than the normal WorldSession update hook.
- Existing historical receipts are untouched; this PR does not modify SQL
  eligibility, current character records, or grandfathered players.

## A3 — Session recovery lifetime

- Outstanding emergency logouts are keyed by the actual WorldSession pointer
  **and** the exact character GUID. Entries for vanished/mismatched players
  are discarded without affecting the new player.
- `OnPlayerLogout` removes an entry when a session leaves through another
  code path. `OnSessionUpdate` removes it before calling no-save logout.
- A subsequent `OnPlayerLogin` also clears a matching leftover request before
  processing rewards. This prevents an exceptional disconnect from carrying
  a stale no-save logout into a new session that reuses the same identity.
- An atomic quick check avoids taking the global mutex on every ordinary
  session tick. This is not a general task scheduling framework.

## B4 — Hunter quiver inventory movement

- Validate all equipped bags and all quiver contents first. For every item,
  reserve a **distinct empty backpack slot**; request and validate exactly
  one full-stack destination from AzerothCore's `CanStoreItem`.
- Only then remove items, store them in their predetermined backpack slots,
  and verify `StoreItem` succeeded and the destination contains that item.
- Destroy only an empty original quiver. Higher-level hunters and equipped
  nonstarter bags remain untouched; failure moves the receipt to manual
  review rather than automatically retrying partial grants.

## Verification scope

- `tests/test_starter_review.py`: static guardrails and in-memory SQLite
  receipt/migration tests. These are **not** executable C++ behavior tests.
- C++ syntax/compilation against the installed AzerothCore build's
  compilation flags, with output kept outside the live modules.
- `git diff --check` and CI checks on the new pull request.
- CodeRabbit review of this independent PR.

Before deployment: exercise failures with a disposable test character and a
controlled database-error harness, bot-first/human-later logins, hunter default
quiver with and without ammunition, full backpacks, re-logins, and session
disconnects. Record the core and module release commits and obtain approval
for production restart and deployment. Never test failed database commits on
the production character database.
