# Custom Login - October 23, 2026 test results and live release

Tested PR #10 head: `6bb3c3d7992a3fcad4ee88e14295325257958852`; merge/release commit `c7f84a1337e0d9c7ddff9b69ffd297fb7e188e2f`.

## Owner-reported in-game results: 6/6 Pass

1. New Hunter: four Frostweave Bags, 25 gold, Dread Pirate Ring, class heirlooms/ammunition and safe starting-quiver/ammo handling.
2. New non-Hunter: class-specific equipment, weapon skills, bags/ring/gold.
3. All-Race/All-Class: rewards selected by class rather than race.
4. Repeat login: no duplicate equipment, bags or gold.
5. Playerbot-first: no bot package; first human login grants exactly once.
6. Existing character and disconnect/recovery: no inappropriate extra package, missing or duplicated rewards.

These are the owner's observations, supported by the retained completed checklist screenshot and test receipt rows. Receipt presence alone is not proof of reward correctness. No first-login worldserver crash recurred during the combined session.

Both hosted `core-build` and `Brambletown starter review regressions` passed on the tested head, and the actionable CodeRabbit thread was resolved. The exact source was compiled/linked locally and tested in the combined runtime before rollout.

## Separate test-environment finding: UAC Hunter pet grants

The test clone lacked the five UAC early-Hunter pet spell grants and level-1 spell overlays even though production had all five and PlayerStart.CustomSpells=1. The Hunter's missing pet skills were not evidence that PR #10 was absent or that this module grants those spells. The owner retained the working production UAC feature and requested manual spell/pet setup in the disposable test environment.

Future combined-test preparation should verify production-equivalent required UAC data before gameplay so it does not misdiagnose Custom Login. This session does not count as an automatic pet-grant pass in the clone.

## Deployment safeguards

Preserved the existing live starter receipt table (1545 rows at rollout verification), production characters and all current module options. Did not reseed rewards or import test receipts/accounts/characters. Login.Reputation=0 and Login.SpecialAbility=0 remain as configured. The asynchronous transaction/session-lifetime correction was deployed; the historical crash was not reproduced after the fix in isolated testing.

No new Custom Login gameplay defect was established during this session. Deferred tests in the other modules remain documented rather than being presented as full-suite passes.

## Shared session and deployment record

Owner-completed sheet: `Brambletown_Combined_Test_Sheet_2026-10-23(1).pdf`. Corrected user marks: **65 Pass, 7 Deferred, 2 Blocked, 23 Not run** across 97 cases. This is the user-form tally, not a claim that all operator/restoration cases were unexecuted: operator evidence and subsequent deployment are reconciled separately. S03 and E06 were explicitly corrected to Pass after submission.

Combined isolated test binary SHA-256: `8d2fa8f0c0593d02b455b34a471450fff4fdb4b6b7c1af53f8393fcc6964bcdf`.
Production build from the same tested gameplay source, rebuilt with production configuration paths: `ac4d29bd37f436f495f8211ae4d3eb291ef181a8c43926a3e995497eb2f3f483`.

Released October 23, 2026 after explicit owner approval. Production controller recorded success at **11:48 AM America/Chicago (CDT)**. Auth/world ports responded, world console responded, and the inspected error log was empty. All **35 production configuration files remained byte-for-byte unchanged**; original authserver retained. Production characters/starter receipts and normal loot rates preserved; no cloned test data, guaranteed-drop fixtures or test configuration were imported.

Production rollback binaries, database dumps, configuration backup, migration output and deployment manifest are retained on the authorized server under `rollout-20261023/`. Test evidence is retained under `combined-runtime-20261023/evidence/`. Live startup checks are operational checks, not a substitute for later human gameplay confirmation.
