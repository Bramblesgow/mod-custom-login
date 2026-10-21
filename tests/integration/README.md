# Isolated DB-worker reproduction: first-login transaction crash

This test exercises the **real AzerothCore `DatabaseWorkerPool<CharacterDatabaseConnection>`**, its prepared statements and MySQL transaction handling. It does **not** instantiate a `Player`, simulate a WorldSession, or test actual client login.

Tested server core: `2a2211cd8f3d157da432ec0175ddd4b8a191931f`.

## Safety

**Never target production databases or execute this test through the live worldserver.** Use a disposable schema with a `clfx_` prefix, created separately from production and populated with schema only from an existing *nonproduction* characters database. The harness refuses database names not beginning `clfx_` and rejects connection strings selecting a different database. Its hardcoded test GUIDs are reserved for disposable databases. It creates one synthetic rewards table and two receipt rows only in that schema.

The `legacy` mode intentionally asserts/segfaults **its own standalone test process**. Disable core dumps. Never invoke `legacy` in production.

## Build

From a separate test directory:

```sh
python3 tests/integration/build_harness.py \
    --build /path/to/nonproduction-or-existing-core/build \
    --output /path/to/separate-test-workspace
```

This compiles with the original core build's actual `compile_commands.json` and links its existing static libraries. It writes no objects, executables, or configurations into the original build or production paths.

## Prepare and execute

Create a **new empty scratch database** with the complete character table structures (including `mod_customlogin_starter`) from a nonproduction schema. Do not copy production data. Confirm the scratch database name and set its connection info in the environment; the connection string must have exactly the `host;port;user;password;database` structure used by AzerothCore.

```sh
export ACORE_TEST_CHARACTER_DB='localhost;3306;TEST_USER;TEST_PASSWORD;clfx_disposable_characters'
ulimit -c 0
/path/to/separate-test-workspace/isolated_db_worker legacy clfx_disposable_characters
# Expected: isolated process exits 139 on this AzerothCore revision.
/path/to/separate-test-workspace/isolated_db_worker success clfx_disposable_characters
# Expected: exit 0, changes to prepared statement, synthetic items/gold, and receipt commit.
/path/to/separate-test-workspace/isolated_db_worker failure clfx_disposable_characters
# Expected: exit 0, duplicate-key fault rolls back all changes; state-2 recovery.
```

Run the modes **once against a fresh disposable schema**. The test intentionally does not reset receipt states or delete records. On completion, inspect the two synthetic receipt rows and rewards and preserve results with test logs. Do not run SQL cleanup against the production database.

The legacy-mode reproduction uses `CHAR_UPD_ADD_AT_LOGIN_FLAG`, an actual async-only character prepared statement. The real `Player::SaveToDB` path includes other async-only statements, notably `CHAR_UPD_CHARACTER`. This reproduces their connection-type failure mechanism, not a full Player save. `success` and `failure` exercise the worker's actual transaction execution, but gold and item counts are synthetic; they do not substitute for isolated client testing of inventory, logout/reconnect, bots, or Hunter quivers.

## Regression coverage

`python3 -m unittest discover -s tests -p 'test_*.py'` includes a guard rejecting `DirectCommitTransaction(transaction)` and requiring the async worker's result to be observed before the receipt is inspected. Prior tests incorrectly **asserted the unsafe synchronous commit was present**, so they missed the crash.

The corrective implementation synchronously waits for the async worker `future` only on the one-time first-human-login path; it does not schedule a callback that could outlive the player's session. Core DB worker errors remain subject to AzerothCore's own fatal handling for unrecoverable schema/connectivity errors.
