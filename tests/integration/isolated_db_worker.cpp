#include "DatabaseEnv.h"
#include "MySQLThreading.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <chrono>

static constexpr uint32 kSuccess = 391000001;
static constexpr uint32 kFailure = 391000002;
static constexpr uint32 kPending = 391000003;

static uint32 scalar(std::string const& sql)
{
    QueryResult r = CharacterDatabase.Query(sql);
    if (!r) throw std::runtime_error("missing test result");
    return r->Fetch()[0].Get<uint32>();
}
static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
static CharacterDatabaseTransaction transactionWithPrepared(uint32 guid)
{
    auto trans = CharacterDatabase.BeginTransaction();
    // Registered CONNECTION_ASYNC in the exact installed AzerothCore revision.
    auto* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);
    stmt->SetData(0, uint16(32));
    stmt->SetData(1, uint32(guid));
    trans->Append(stmt);
    return trans;
}
int main(int argc,char** argv)
{
    if (argc != 3) {std::cerr<<"mode scratch_db\n";return 2;}
    try
    {
        const std::string mode = argv[1], db=argv[2];
        check(db.rfind("clfx_", 0) == 0, "refusing database without clfx_ scratch prefix");
        char const* credentials = std::getenv("ACORE_TEST_CHARACTER_DB");
        check(credentials != nullptr, "ACORE_TEST_CHARACTER_DB must specify a disposable database");
        std::string const connectionInfo(credentials);
        check(connectionInfo.substr(connectionInfo.find_last_of(';') + 1) == db,
            "connection string must target the disposable database");
        MySQL::Library_Init();
        CharacterDatabase.SetConnectionInfo(connectionInfo, 1, 1);
        check(CharacterDatabase.Open() == 0, "database open failed");
        check(CharacterDatabase.PrepareStatements(), "prepared statements failed");
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS clfx_rewards (guid INT UNSIGNED PRIMARY KEY, gold INT UNSIGNED NOT NULL DEFAULT 0, item_count INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB");
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO clfx_rewards(guid) VALUES ({})", kSuccess);
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO clfx_rewards(guid) VALUES ({})", kFailure);
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO clfx_rewards(guid) VALUES ({})", kPending);
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_customlogin_starter(guid,state) VALUES ({},0)",kSuccess);
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_customlogin_starter(guid,state) VALUES ({},0)",kFailure);
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_customlogin_starter(guid,state) VALUES ({},0)",kPending);

        if (mode == "legacy")
        {
            auto tx=transactionWithPrepared(kFailure);
            tx->Append("UPDATE clfx_rewards SET gold=gold+250000 WHERE guid = 391000002");
            // Expected failure: async-only statement executed on synchronous pool.
            std::cout << "legacy synchronous connection attempt (isolated subprocess)\n" << std::flush;
            CharacterDatabase.DirectCommitTransaction(tx);
            std::cout << "legacy unexpectedly returned\n";
            return 3;
        }
        if (mode == "success")
        {
            check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000001")==0,"success receipt not pending");
            auto tx=transactionWithPrepared(kSuccess);
            tx->Append("UPDATE clfx_rewards SET gold=gold+250000,item_count=item_count+1 WHERE guid=391000001");
            tx->Append("UPDATE mod_customlogin_starter SET state=1 WHERE guid=391000001 AND state=0");
            auto complete=CharacterDatabase.AsyncCommitTransaction(tx);
            check(complete.m_future.get(),"worker transaction failed");
            check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000001")==1,"receipt not complete");
            check(scalar("SELECT gold FROM clfx_rewards WHERE guid=391000001")==250000,"gold incorrect");
            check(scalar("SELECT item_count FROM clfx_rewards WHERE guid=391000001")==1,"item receipt incorrect");
            // Mimic ProcessStarterLogin gate: complete receipt prevents repeated grant.
            for (int i=0;i<3;++i)
                check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000001")==1,"reconnect/relogin gate failed");
            check(scalar("SELECT gold FROM clfx_rewards WHERE guid=391000001")==250000,"duplicate gold");
            std::cout << "PASS actual async worker prepared statement + atomic gold/items/receipt and repeat receipt gate\n";
        }
        else if (mode == "pending")
        {
            // Hold a row lock on the synchronous connection to back up the
            // async worker and show that polling its future never blocks.
            CharacterDatabase.DirectExecute("START TRANSACTION");
            CharacterDatabase.DirectExecute("UPDATE clfx_rewards SET gold=gold WHERE guid=391000003");
            auto tx = transactionWithPrepared(kPending);
            tx->Append("UPDATE clfx_rewards SET gold=gold+250000 WHERE guid=391000003");
            tx->Append("UPDATE mod_customlogin_starter SET state=1 WHERE guid=391000003 AND state=0");
            auto future = CharacterDatabase.AsyncCommitTransaction(tx);
            auto const start = std::chrono::steady_clock::now();
            auto const status = future.m_future.wait_for(std::chrono::seconds(0));
            auto const elapsed = std::chrono::steady_clock::now() - start;
            check(status == std::future_status::timeout, "blocked worker unexpectedly completed");
            check(elapsed < std::chrono::milliseconds(100), "readiness poll blocked");
            CharacterDatabase.DirectExecute("ROLLBACK");
            check(future.m_future.get(), "pending transaction did not recover after row unlock");
            check(scalar("SELECT gold FROM clfx_rewards WHERE guid=391000003")==250000,
                "pending transaction lost gold");
            check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000003")==1,
                "pending transaction lost receipt");
            std::cout << "PASS pending async worker polled without blocking and committed after unlock\n";
        }
        else if (mode=="failure")
        {
            auto tx=transactionWithPrepared(kFailure);
            tx->Append("UPDATE clfx_rewards SET gold=gold+250000,item_count=item_count+1 WHERE guid=391000002");
            tx->Append("INSERT INTO clfx_rewards(guid) VALUES (391000002)"); // Duplicate key: recoverable SQL error 1062.
            tx->Append("UPDATE mod_customlogin_starter SET state=1 WHERE guid=391000002 AND state=0");
            auto complete=CharacterDatabase.AsyncCommitTransaction(tx);
            check(!complete.m_future.get(),"expected failed commit");
            check(scalar("SELECT gold FROM clfx_rewards WHERE guid=391000002")==0,"gold not rolled back");
            check(scalar("SELECT item_count FROM clfx_rewards WHERE guid=391000002")==0,"inventory not rolled back");
            check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000002")==0,"receipt unexpectedly changed");
            auto recovery=CharacterDatabase.BeginTransaction();
            recovery->Append("UPDATE mod_customlogin_starter SET state=2 WHERE guid=391000002 AND state=0");
            CharacterDatabase.DirectCommitTransaction(recovery); // raw statement only, synchronous is valid.
            check(scalar("SELECT state FROM mod_customlogin_starter WHERE guid=391000002")==2,"recovery failed");
            std::cout << "PASS failed async worker transaction rolls back all and conservative recovery blocks replay\n";
        }
        else {std::cerr << "Unknown mode\n";return 2;}
        CharacterDatabase.Close();
        MySQL::Library_End();
        return 0;
    }
    catch (std::exception const& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
