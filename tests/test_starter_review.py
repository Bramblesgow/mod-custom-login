"""Static guardrails and isolated SQL-model checks for starter receipt fixes.

Run: python3 tests/test_starter_review.py
These tests do not execute game server logic or touch live character data.
"""
from pathlib import Path
import sqlite3
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = (ROOT / "src/mod_customlogin.cpp").read_text()
CONFIG = (ROOT / "conf/mod_customlogin.conf.dist").read_text()
MIGRATION = (ROOT / "data/sql/db-characters/updates/2026_10_16_00_customlogin_starter.sql").read_text()


class StarterReview(unittest.TestCase):
    def test_quiver_move_and_destroy_order(self):
        section = CODE.split("bool const removableQuiver", 1)[1].split("for (uint8 slot = INVENTORY_SLOT_BAG_START;", 1)[0]
        self.assertIn("player->GetLevel() == 1", section)
        full = CODE.split("bool const removableQuiver", 1)[1].split("// One Dread Pirate Ring", 1)[0]
        self.assertIn("freeBackpackSlots < itemsToMove", full)
        self.assertIn("player->CanStoreItem(INVENTORY_SLOT_BAG_0, NULL_SLOT", full)
        self.assertLess(full.index("nonstarter bag in slot"), full.index("player->DestroyItem("))
        self.assertLess(full.index("player->RemoveItem(slot, inside, true)"), full.index("player->DestroyItem("))
        self.assertIn("if (!quiver->IsEmpty())", full)

    def test_one_atomic_character_receipt_transaction(self):
        section = CODE.split("if (state == 0)", 1)[1].split("else if (state == 2)", 1)[0]
        self.assertLess(section.index("player->IsBeingTeleportedFar()"), section.index("GrantStarterPackage(player)"))
        self.assertLess(section.index("GrantStarterPackage(player)"), section.index("CharacterDatabase.BeginTransaction()"))
        self.assertLess(section.index("player->SaveToDB(transaction, false, false)"), section.index('transaction->Append('))
        self.assertLess(section.index("transaction->Append("), section.index("CharacterDatabase.DirectCommitTransaction(transaction)"))
        self.assertIn("granted ? 1u : 2u", section)
        self.assertNotIn("CharacterDatabase.DirectExecute", section)
        self.assertLess(section.index("DirectCommitTransaction(transaction)"),
                        section.index("QueryResult committedReceipt"))
        self.assertLess(section.index("QueryResult committedReceipt"),
                        section.index("if (!committed)"))
        self.assertLess(section.index("if (!committed)"),
                        section.index("else if (granted)"))
        self.assertIn('committedReceipt->Fetch()[0].Get<uint8>() == (granted ? 1u : 2u)', section)

    def test_unconfirmed_commit_recovery_cannot_leave_regrant_open(self):
        recovery = CODE.split("if (!committed)", 1)[1].split("else if (granted)", 1)[0]
        self.assertIn("SET state = 2 WHERE guid = {} AND state = 0", recovery)
        self.assertLess(recovery.index("DirectCommitTransaction(recovery)"),
                        recovery.index("QueryResult recovered"))
        self.assertIn("recoveredState != 1 && recoveredState != 2", recovery)
        self.assertIn("QueueStarterNoSaveLogout(guid)", recovery)

    def test_no_save_logout_is_deferred_until_session_update(self):
        session = CODE.split("class CustomLoginRecoverySessionScript", 1)[1].split("bool LoginEnable", 1)[0]
        self.assertIn("SESSIONHOOK_ON_UPDATE", session)
        self.assertIn("pendingStarterLogoutGuids.erase(guid)", session)
        self.assertIn("session->LogoutPlayer(false)", session)
        self.assertIn("new CustomLoginRecoverySessionScript()", CODE)
        # PlayerLogin is called before the core finishes using its Player*.
        # Never delete the player there or rely on a regular save-on-disconnect.
        on_login = CODE.split("void OnPlayerLogin(Player* player) override")[-1]
        self.assertNotIn("LogoutPlayer(false)", on_login)

    def test_missing_receipt_is_not_level_based(self):
        section = CODE.split("void OnPlayerLogin(Player* player) override", 2)[-1]
        self.assertIn("uint8 initialState = isFirstLogin ? 0 : 1;", section)
        self.assertNotIn("player->GetLevel() == 1 || isFirstLogin", section)
        self.assertIn('if (!receipt)', section)
        self.assertIn("missing starter receipt", section)
        self.assertIn("player->GetSession()->IsHeadless()", section)
        self.assertIn("INSERT IGNORE INTO mod_customlogin_starter (guid, state) VALUES ({}, 0)", section)

    def test_deprecated_per_item_settings(self):
        self.assertIn("Login.BoA = 1", CONFIG)
        for key in ("Login.BoA.Ring =", "Login.BoA.Bags =", "Login.BoA.Weapon ="):
            self.assertNotIn(key, CONFIG)
        for old_var in ("LoginBOARing", "LoginBOABags", "LoginBOAWeapon3"):
            self.assertNotIn(old_var, CODE)

    def test_migration_does_not_modify_existing_receipts(self):
        self.assertIn("INSERT IGNORE", MIGRATION)
        self.assertIn("(at_login & 0x20)", MIGRATION)
        conn = sqlite3.connect(":memory:")
        conn.executescript("""
            CREATE TABLE characters (guid INTEGER PRIMARY KEY, level INTEGER, at_login INTEGER);
            CREATE TABLE mod_customlogin_starter (guid INTEGER PRIMARY KEY, state INTEGER);
            INSERT INTO characters VALUES (1,1,32),(2,1,0),(3,9,0),(4,1,0),(5,1,32);
            INSERT INTO mod_customlogin_starter VALUES (3,0),(4,1);
        """)
        insertion = MIGRATION[MIGRATION.index("INSERT IGNORE INTO"):].strip()
        insertion = insertion.replace("INSERT IGNORE", "INSERT OR IGNORE", 1)
        conn.executescript(insertion)
        rows = dict(conn.execute("SELECT guid,state FROM mod_customlogin_starter"))
        self.assertEqual(rows, {1:0, 2:1, 3:0, 4:1, 5:0})
        conn.executescript(insertion)
        self.assertEqual(dict(conn.execute("SELECT guid,state FROM mod_customlogin_starter")), rows)
        conn.close()


if __name__ == "__main__":
    unittest.main()
