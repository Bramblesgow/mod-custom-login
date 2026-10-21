"""SQLite model tests for the expected atomic character/receipt save contract.

This does not execute AzerothCore C++ or connect to the live character DB.
"""
import sqlite3
import unittest


class ReceiptTransactionModel(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.db.executescript("""
            CREATE TABLE receipts (guid INTEGER PRIMARY KEY, state INTEGER NOT NULL);
            CREATE TABLE characters (guid INTEGER PRIMARY KEY, gold INTEGER NOT NULL);
            CREATE TABLE inventory (guid INTEGER, item_id INTEGER, quantity INTEGER);
            INSERT INTO receipts VALUES (10, 0);
            INSERT INTO characters VALUES (10, 0);
        """)

    def tearDown(self):
        self.db.close()

    def grant(self, receipt_state, fail=False):
        self.db.execute("BEGIN")
        self.db.execute("UPDATE characters SET gold = gold + 250000 WHERE guid=10")
        self.db.execute("INSERT INTO inventory VALUES (10, 50255, 1)")
        self.db.execute("UPDATE receipts SET state=? WHERE guid=10 AND state=0", (receipt_state,))
        if fail:
            self.db.rollback()
        else:
            self.db.commit()

    def snapshot(self):
        return (
            self.db.execute("SELECT state FROM receipts WHERE guid=10").fetchone()[0],
            self.db.execute("SELECT gold FROM characters WHERE guid=10").fetchone()[0],
            self.db.execute("SELECT COUNT(*) FROM inventory WHERE guid=10").fetchone()[0],
        )

    def test_failed_transaction_rolls_back_rewards_and_receipt(self):
        self.grant(1, fail=True)
        self.assertEqual(self.snapshot(), (0, 0, 0))

    def test_completed_transaction_persists_both(self):
        self.grant(1)
        self.assertEqual(self.snapshot(), (1, 250000, 1))

    def test_partial_grant_status_blocks_regrant(self):
        self.grant(2)
        self.db.execute("UPDATE receipts SET state=2 WHERE guid=10 AND state=0")
        self.db.commit()
        self.assertEqual(self.snapshot(), (2, 250000, 1))

    def test_recovery_never_downgrades_completed(self):
        self.grant(1)
        self.db.execute("UPDATE receipts SET state=2 WHERE guid=10 AND state=0")
        self.db.commit()
        self.assertEqual(self.snapshot(), (1, 250000, 1))


if __name__ == "__main__":
    unittest.main()
