/*

# Custom Login Modifications #

### Description ###
------------------------------------------------------------------------------------------------------------------
- All options can be enabled or disabled in config
- Player ([ Faction ] - Name - Logon/Logoff message) notification can be announced to the world
- New characters can receive items, bags, and class-specific heirlooms on first login
- New characters can receive class-specific skills on first login
- New characters can receive specialized skills on first login
- New characters can receive exalted rep with capital cities (Ambassador) on first login


### Data ###
------------------------------------------------------------------------------------------------------------------
- Type: Player/Server
- Script: CustomLogin
- Config: Yes
- SQL: No


### Version ###
------------------------------------------------------------------------------------------------------------------
- v2018.12.19 - Add BOA specific config options
- v2018.12.01 - Fix PlayerAnnounce when exiting game
- v2017.08.25 - Clean up code, add rep gain, add config options
- v2017.08.01 - Release


### Credits ###
------------------------------------------------------------------------------------------------------------------
#### A module for AzerothCore by StygianTheBest ([stygianthebest.github.io](http://stygianthebest.github.io)) ####

###### Additional Credits include:
- [Blizzard Entertainment](http://blizzard.com)
- [TrinityCore](https://github.com/TrinityCore/TrinityCore/blob/3.3.5/THANKS)
- [SunwellCore](http://www.azerothcore.org/pages/sunwell.pl/)
- [AzerothCore](https://github.com/AzerothCore/azerothcore-wotlk/graphs/contributors)
- [AzerothCore Discord](https://discord.gg/gkt4y2x)
- [EMUDevs](https://youtube.com/user/EmuDevs)
- [AC-Web](http://ac-web.org/)
- [ModCraft.io](http://modcraft.io/)
- [OwnedCore](http://ownedcore.com/)
- [OregonCore](https://wiki.oregon-core.net/)
- [Wowhead.com](http://wowhead.com)
- [AoWoW](https://wotlk.evowow.com/)


### License ###
------------------------------------------------------------------------------------------------------------------
- This code and content is released under the [GNU AGPL v3](https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3).

*/


#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "GuildMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

bool LoginEnable = 1;
bool LoginAnnounceModule = 1;
bool LoginPlayerAnnounce = 1;
bool LoginBOA = 1;
bool LoginBOARing = 1;
bool LoginBOABags = 1;
bool LoginBOAShoulders = 1;
bool LoginBOAShoulders2 = 1;
bool LoginBOAChest = 1;
bool LoginBOAChest2 = 1;
bool LoginBOATrinket = 1;
bool LoginBOATrinket2 = 1;
bool LoginBOAWeapon = 1;
bool LoginBOAWeapon2 = 1;
bool LoginBOAWeapon3 = 1;
bool LoginSkills = 1;
bool LoginSpecialAbility = 0;
bool LoginReputation = 1;

class LoginConfig : public WorldScript
{
public:
    LoginConfig() : WorldScript("LoginConfig", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD
    }) { }

    void OnBeforeConfigLoad(bool reload) override
    {
        if (!reload)
        {
            // Load Configuration Settings
            SetInitialWorldSettings();
        }
    }

    // Load Configuration Settings
    void SetInitialWorldSettings()
    {
        LoginEnable = sConfigMgr->GetOption<bool>("Login.Enable", 1);
        LoginAnnounceModule = sConfigMgr->GetOption<bool>("Login.Announce", 1);
        LoginPlayerAnnounce = sConfigMgr->GetOption<bool>("Login.PlayerAnnounce", 1);
        LoginBOA = sConfigMgr->GetOption<bool>("Login.BoA", 1);
        LoginBOARing = sConfigMgr->GetOption<bool>("Login.BoA.Ring", 1);
        LoginBOABags = sConfigMgr->GetOption<bool>("Login.BoA.Bags",1);
        LoginBOAShoulders = sConfigMgr->GetOption<bool>("Login.BoA.Shoulders", 1);
        LoginBOAShoulders2 = sConfigMgr->GetOption<bool>("Login.BoA.Shoulders2",1);
        LoginBOAChest = sConfigMgr->GetOption<bool>("Login.BoA.Chest",1);
        LoginBOAChest2 = sConfigMgr->GetOption<bool>("Login.BoA.Chest2",1);
        LoginBOATrinket = sConfigMgr->GetOption<bool>("Login.BoA.Trinket",1);
        LoginBOATrinket2 = sConfigMgr->GetOption<bool>("Login.BoA.Trinket2",1);
        LoginBOAWeapon = sConfigMgr->GetOption<bool>("Login.BoA.Weapon",1);
        LoginBOAWeapon2 = sConfigMgr->GetOption<bool>("Login.BoA.Weapon2",1);
        LoginBOAWeapon3 = sConfigMgr->GetOption<bool>("Login.BoA.Weapon3",1);
        LoginSkills = sConfigMgr->GetOption<bool>("Login.Skills", 1);
        LoginSpecialAbility = sConfigMgr->GetOption<bool>("Login.SpecialAbility", 1);
        LoginReputation = sConfigMgr->GetOption<bool>("Login.Reputation", 1);
    }
};

class LoginAnnounce : public PlayerScript
{

public:

    LoginAnnounce() : PlayerScript("LoginAnnounce", {
        PLAYERHOOK_ON_LOGIN
    }) {}

    void OnPlayerLogin(Player* player) override
    {
        // Announce Module
        if (LoginEnable)
            if (LoginAnnounceModule)
                ChatHandler(player->GetSession()).SendSysMessage("");
    }
};

// AzerothCore's AddItem may return true after storing only part of a stack.
// Check capacity first and verify the entire requested quantity was added.
static bool GrantStarterItem(Player* player, uint32 itemId, uint32 quantity)
{
    ItemPosCountVec destinations;
    uint32 unavailable = 0;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destinations, itemId, quantity, &unavailable) != EQUIP_ERR_OK || unavailable != 0)
    {
        LOG_ERROR("module", "CustomLogin: insufficient space for character {} item {} x{}", player->GetName(), itemId, quantity);
        return false;
    }

    uint32 before = player->GetItemCount(itemId);
    if (!player->AddItem(itemId, quantity) || player->GetItemCount(itemId) - before != quantity)
    {
        LOG_ERROR("module", "CustomLogin: incomplete grant for character {} item {} x{}", player->GetName(), itemId, quantity);
        return false;
    }
    return true;
}

class CustomLogin : public PlayerScript
{

public:
    CustomLogin() : PlayerScript("CustomLogin", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_DELETE
    }) { }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        // Prevent stale receipts if a deleted character's GUID is reused.
        CharacterDatabase.DirectExecute("DELETE FROM mod_customlogin_starter WHERE guid = {}", guid.GetCounter());
    }

    bool GrantStarterPackage(Player* player)
    {
        if (!player || !player->GetSession() || player->GetSession()->IsHeadless())
            return false;

        // If enabled..
        if (LoginEnable)
        {
            // If enabled, give heirloom and other items
	        if (LoginBOA)
		{
		    // Hunters can start with a quiver or ammo pouch occupying a bag slot.
		    // Remove it before equipping the four starter bags. Original ammunition
		    // is intentionally not retained; fresh arrows are granted below.
		    if (player->getClass() == CLASS_HUNTER)
		    {
		        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
		        {
		            if (Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
		            {
		                if (equipped->GetTemplate()->Class == ITEM_CLASS_QUIVER)
		                    player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
		            }
		        }
		    }

		    // Do not replace existing bags; require the four configured bags.
		    // A level-one hunter's starting quiver has already been removed above.
		    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
		    {
		        if (Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
		        {
		            if (equipped->GetEntry() != 41599)
		            {
		                LOG_ERROR("module", "CustomLogin: nonstarter bag in slot {} for character {}", slot, player->GetName());
		                return false;
		            }
		}
		    }

		    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
		    {
		        if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
		        {
		            uint16 pos = (INVENTORY_SLOT_BAG_0 << 8) | slot;
		            if (!player->EquipNewItem(pos, 41599, true))
		            {
		                LOG_ERROR("module", "CustomLogin: could not equip starter bag in slot {} for character {}", slot, player->GetName());
		                return false;
		            }
		        }
		    }

		    // One Dread Pirate Ring for every character
		    if (!GrantStarterItem(player, 50255, 1)) return false;

		    switch (player->getClass())
		    {
		        case CLASS_WARRIOR:
		            if (!GrantStarterItem(player, 42946, 1)) return false; // Heirloom bow
		            if (!GrantStarterItem(player, 2512, 1000)) return false; // Level-one arrows
		            if (!GrantStarterItem(player, 48685, 1)) return false;
		            if (!GrantStarterItem(player, 42949, 1)) return false;
		            if (!GrantStarterItem(player, 42943, 1)) return false;
		            if (!GrantStarterItem(player, 42945, 1)) return false;
		            if (!GrantStarterItem(player, 42991, 2)) return false;
		            break;

		        case CLASS_PALADIN:
		            if (!GrantStarterItem(player, 48685, 1)) return false;
		            if (!GrantStarterItem(player, 42949, 1)) return false;
		            if (!GrantStarterItem(player, 42943, 1)) return false;
		            if (!GrantStarterItem(player, 48683, 1)) return false;
		            if (!GrantStarterItem(player, 42951, 1)) return false;
		            if (!GrantStarterItem(player, 42948, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        case CLASS_HUNTER:
		            if (!GrantStarterItem(player, 48677, 1)) return false;
		            if (!GrantStarterItem(player, 42950, 1)) return false;
		            if (!GrantStarterItem(player, 42946, 1)) return false;
		            if (!GrantStarterItem(player, 2512, 3000)) return false; // Rough Arrows for level-one heirloom bow
		            if (!GrantStarterItem(player, 42944, 2)) return false; // Agility daggers for melee stat sticks
		            if (!GrantStarterItem(player, 42991, 2)) return false;
		            break;

		        case CLASS_ROGUE:
		            if (!GrantStarterItem(player, 42946, 1)) return false; // Heirloom bow
		            if (!GrantStarterItem(player, 2512, 1000)) return false; // Level-one arrows
		            if (!GrantStarterItem(player, 48689, 1)) return false;
		            if (!GrantStarterItem(player, 42952, 1)) return false;
		            if (!GrantStarterItem(player, 42944, 2)) return false;
		            if (!GrantStarterItem(player, 42945, 1)) return false;
		            if (!GrantStarterItem(player, 42991, 2)) return false;
		            break;

		        case CLASS_PRIEST:
		            if (!GrantStarterItem(player, 48691, 1)) return false;
		            if (!GrantStarterItem(player, 42985, 1)) return false;
		            if (!GrantStarterItem(player, 42947, 1)) return false;
		            if (!GrantStarterItem(player, 42948, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        case CLASS_DEATH_KNIGHT:
		            if (!GrantStarterItem(player, 48685, 1)) return false;
		            if (!GrantStarterItem(player, 42949, 1)) return false;
		            if (!GrantStarterItem(player, 42943, 1)) return false;
		            if (!GrantStarterItem(player, 42945, 1)) return false;
		            if (!GrantStarterItem(player, 42991, 2)) return false;
		            break;

		        case CLASS_SHAMAN:
		            if (!GrantStarterItem(player, 48677, 1)) return false;
		            if (!GrantStarterItem(player, 42950, 1)) return false;
		            if (!GrantStarterItem(player, 48716, 2)) return false;
		            if (!GrantStarterItem(player, 42991, 2)) return false;

		            if (!GrantStarterItem(player, 48683, 1)) return false;
		            if (!GrantStarterItem(player, 42951, 1)) return false;
		            if (!GrantStarterItem(player, 42948, 1)) return false;
		            if (!GrantStarterItem(player, 42947, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        case CLASS_MAGE:
		            if (!GrantStarterItem(player, 48691, 1)) return false;
		            if (!GrantStarterItem(player, 42985, 1)) return false;
		            if (!GrantStarterItem(player, 42947, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        case CLASS_WARLOCK:
		            if (!GrantStarterItem(player, 48691, 1)) return false;
		            if (!GrantStarterItem(player, 42985, 1)) return false;
		            if (!GrantStarterItem(player, 42947, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        case CLASS_DRUID:
		            if (!GrantStarterItem(player, 48689, 1)) return false;
		            if (!GrantStarterItem(player, 42952, 1)) return false;
		            if (!GrantStarterItem(player, 48718, 1)) return false;
		            if (!GrantStarterItem(player, 42991, 2)) return false;

		            if (!GrantStarterItem(player, 48687, 1)) return false;
		            if (!GrantStarterItem(player, 42984, 1)) return false;
		            if (!GrantStarterItem(player, 42947, 1)) return false;
		            if (!GrantStarterItem(player, 42948, 1)) return false;
		            if (!GrantStarterItem(player, 42992, 2)) return false;
		            break;

		        default:
		            break;
		    }

		}
            // If enabled, learn additional skills
            if (LoginSkills)
            {
                switch (player->getClass())
                {

                    /*
                        // Skill Reference

                        player->learnSpell(204);	// Defense
                        player->learnSpell(264);	// Bows
                        player->learnSpell(5011);	// Crossbow
                        player->learnSpell(674);	// Dual Wield
                        player->learnSpell(15590);	// Fists
                        player->learnSpell(266);	// Guns
                        player->learnSpell(196);	// Axes
                        player->learnSpell(198);	// Maces
                        player->learnSpell(201);	// Swords
                        player->learnSpell(750);	// Plate Mail
                        player->learnSpell(200);	// PoleArms
                        player->learnSpell(9116);	// Shields
                        player->learnSpell(197);	// 2H Axe
                        player->learnSpell(199);	// 2H Mace
                        player->learnSpell(202);	// 2H Sword
                        player->learnSpell(227);	// Staves
                        player->learnSpell(2567);	// Thrown
                    */

                case CLASS_PALADIN:
                    player->learnSpell(196);	// Axes
                    player->learnSpell(750);	// Plate Mail
                    player->learnSpell(200);	// PoleArms
                    player->learnSpell(197);	// 2H Axe
                    player->learnSpell(199);	// 2H Mace
                    break;

                case CLASS_SHAMAN:
                    player->learnSpell(674); // Dual Wield from first login
                    player->learnSpell(15590);	// Fists
                    player->learnSpell(8737);	// Mail
                    player->learnSpell(196);	// Axes
                    player->learnSpell(197);	// 2H Axe
                    player->learnSpell(199);	// 2H Mace
                    break;

                case CLASS_WARRIOR:
                    player->learnSpell(264);	// Bows
                    player->learnSpell(5011);	// Crossbow
                    player->learnSpell(674);	// Dual Wield
                    player->learnSpell(15590);	// Fists
                    player->learnSpell(266);	// Guns
                    player->learnSpell(750);	// Plate Mail
                    player->learnSpell(200);	// PoleArms
                    player->learnSpell(199);	// 2H Mace
                    player->learnSpell(227);	// Staves
                    break;

                case CLASS_HUNTER:
                    player->learnSpell(1180); // Daggers for paired Heartseekers
                    player->learnSpell(5011); // Crossbows
                    player->learnSpell(674);	// Dual Wield
                    player->learnSpell(15590);	// Fists
                    player->learnSpell(264);	// Bows
                    player->learnSpell(266);	// Guns
                    player->learnSpell(8737);	// Mail
                    player->learnSpell(200);	// PoleArms
                    player->learnSpell(227);	// Staves
                    player->learnSpell(202);	// 2H Sword
                    break;

                case CLASS_ROGUE:
                    player->learnSpell(674); // Dual Wield
                    player->learnSpell(264);	// Bows
                    player->learnSpell(5011);	// Crossbow
                    player->learnSpell(15590);	// Fists
                    player->learnSpell(266);	// Guns
                    player->learnSpell(196);	// Axes
                    player->learnSpell(198);	// Maces
                    player->learnSpell(201);	// Swords
                    break;

                case CLASS_DRUID:
                    player->learnSpell(1180);	// Daggers
                    player->learnSpell(15590);	// Fists
                    player->learnSpell(198);	// Maces
                    player->learnSpell(200);	// PoleArms
                    player->learnSpell(227);	// Staves
                    player->learnSpell(199);	// 2H Mace
                    break;

                case CLASS_MAGE:
                    player->learnSpell(201);	// Swords
                    break;

                case CLASS_WARLOCK:
                    player->learnSpell(201);	// Swords
                    break;

                case CLASS_PRIEST:
                    player->learnSpell(1180);	// Daggers
                    break;

                case CLASS_DEATH_KNIGHT:
                    player->learnSpell(674); // Dual Wield
                    player->learnSpell(198);	// Maces
                    player->learnSpell(199);	// 2H Mace
                    break;

                default:
                    break;
                }

                // Inform the player they have new skills
                std::ostringstream ss;
                ss << "|cffFF8000[|cFFBDB76BCL|cffFF8000] You have been granted |cFFBDB76Badditional weapon skills|cffFF8000.";
                ChatHandler(player->GetSession()).SendSysMessage(ss.str().c_str());
            }

            // If enabled.. learn special skills abilities
            if (LoginSpecialAbility)
            {
                // Learn Specialized Skills
                player->learnSpell(1784);	// Stealth
                player->learnSpell(921);	// Pick Pocket
                player->learnSpell(1804);	// Lockpicking
                player->learnSpell(11305);	// Sprint (3)
                player->learnSpell(5384);	// Feign Death
                player->learnSpell(475);	// Remove Curse

                // Add a few teleportation runes
                if (!GrantStarterItem(player, 17031, 5)) return false;	// Rune of Teleportation

                // Learn Teleports
                switch (player->GetTeamId())
                {

                case TEAM_ALLIANCE:

                    // Alliance Teleports
                    player->learnSpell(3565);	// Darnassus
                    player->learnSpell(32271);	// Exodar
                    player->learnSpell(3562);	// Ironforge
                    player->learnSpell(33690);	// Shattrath
                    player->learnSpell(3561);	// Stormwind
                    break;

                case TEAM_HORDE:

                    // Horde Teleports
                    player->learnSpell(3567);	// Orgrimmar
                    player->learnSpell(35715);	// Shattrath
                    player->learnSpell(32272);	// Silvermoon
                    player->learnSpell(3566);	// Thunder Bluff
                    player->learnSpell(3563);	// Undercity
                    break;

                default:
                    break;
                }

                // Inform the player they have new skills
                std::ostringstream ss;
                ss << "|cffFF8000[|cFFBDB76BCL|cffFF8000] Your spellbook has been scribed with |cFFBDB76Bspecial abilities|cffFF8000.";
                ChatHandler(player->GetSession()).SendSysMessage(ss.str().c_str());
            }

            // If enabled.. set exalted factions (AzerothCore config for rep not working as of 2017-08-25)
            if (LoginReputation)
            {
                switch (player->GetTeamId())
                {

                    // Alliance Capital Cities
                case TEAM_ALLIANCE:
                    player->SetReputation(47, 999999);	// IronForge
                    player->SetReputation(72, 999999);	// Stormwind 
                    player->SetReputation(69, 999999);	// Darnassus
                    player->SetReputation(389, 999999);	// Gnomeregan
                    player->SetReputation(930, 999999);	// Exodar
                    break;

                    // Horde Capital Cities
                case TEAM_HORDE:
                    player->SetReputation(68, 999999);	// Undercity
                    player->SetReputation(76, 999999);	// Orgrimmar
                    player->SetReputation(81, 999999);	// Thunder Bluff
                    player->SetReputation(530, 999999);	// DarkSpear
                    player->SetReputation(911, 999999);	// Silvermoon
                    break;

                default:
                    break;
                }

                // Inform the player they have exalted reputations
                std::ostringstream ss;
                ss << "|cffFF8000[|cFFBDB76BCL|cffFF8000] Your are now |cFFBDB76BExalted|cffFF8000 with your faction's capital cities.";
                ChatHandler(player->GetSession()).SendSysMessage(ss.str().c_str());
            }
        }
        if (LoginBOA)
        {
            // Money and the success message are granted only after all items succeed.
            player->ModifyMoney(250000);
            ChatHandler(player->GetSession()).SendSysMessage(
                "Welcome to Brambletown! Your starter heirlooms, four Frostweave Bags, and 25 gold have been added.");
        }
        return true;
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!player || !player->GetSession())
            return;

        if (LoginEnable)
        {
            uint32 guid = player->GetGUID().GetCounter();
            bool isFirstLogin = player->HasAtLoginFlag(AT_LOGIN_FIRST);
            // This hook runs before the core consumes AT_LOGIN_FIRST, including for bots.
            // Record pending rewards now so a character first used as a bot can claim later.
            if (isFirstLogin)
                CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_customlogin_starter (guid, state) VALUES ({}, 0)", guid);

            if (!player->GetSession()->IsHeadless())
            {
                uint8 state = 1;
                if (QueryResult result = CharacterDatabase.Query("SELECT state FROM mod_customlogin_starter WHERE guid = {}", guid))
                    state = result->Fetch()[0].Get<uint8>();
                else
                {
                    // Migration fallback: missing old higher-level characters are exempt.
                    state = player->GetLevel() == 1 || isFirstLogin ? 0 : 1;
                    CharacterDatabase.DirectExecute("INSERT IGNORE INTO mod_customlogin_starter (guid, state) VALUES ({}, {})", guid, state);
                }

                if (state == 0)
                {
                    if (GrantStarterPackage(player))
                        CharacterDatabase.DirectExecute("UPDATE mod_customlogin_starter SET state = 1 WHERE guid = {} AND state = 0", guid);
                    else
                    {
                        // Never automatically retry a potentially partial grant: doing so duplicates items.
                        CharacterDatabase.DirectExecute("UPDATE mod_customlogin_starter SET state = 2 WHERE guid = {} AND state = 0", guid);
                        ChatHandler(player->GetSession()).SendSysMessage(
                            "Your starter package could not be completed. Please contact a server administrator.");
                    }
                }
                else if (state == 2)
                    ChatHandler(player->GetSession()).SendSysMessage(
                        "Your starter package needs administrator review.");
            }
            // If enabled..
            if (LoginPlayerAnnounce && player->GetSession() && !player->GetSession()->IsHeadless())
            {
                // Announce Player Login
                if (player->GetTeamId() == TEAM_ALLIANCE)
                {
                    std::ostringstream ss;
                    ss << "|cffFFFFFF[|cff2897FF Alliance |cffFFFFFF]:|cff4CFF00 " << player->GetName() << "|cffFFFFFF has come online.";
                    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str().c_str());
                }
                else
                {
                    std::ostringstream ss;
                    ss << "|cffFFFFFF[|cffFF0000 Horde |cffFFFFFF]:|cff4CFF00 " << player->GetName() << "|cffFFFFFF has come online.";
                    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str().c_str());
                }
            }
        }
    }

    void OnPlayerLogout(Player *player) override
    {
        if (LoginEnable)
        {
            // If enabled..
            if (LoginPlayerAnnounce && player->GetSession() && !player->GetSession()->IsHeadless())
            {
                // Announce Player Login
                if (player->GetTeamId() == TEAM_ALLIANCE)
                {
                    std::ostringstream ss;
                    ss << "|cffFFFFFF[|cff2897FF Alliance |cffFFFFFF]|cff4CFF00 " << player->GetName() << "|cffFFFFFF has left the game.";
                    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str().c_str());
                }
                else
                {
                    std::ostringstream ss;
                    ss << "|cffFFFFFF[|cffFF0000 Horde |cffFFFFFF]|cff4CFF00 " << player->GetName() << "|cffFFFFFF has left the game.";
                    sWorldSessionMgr->SendServerMessage(SERVER_MSG_STRING, ss.str().c_str());
                }
            }
        }
    }
};

void AddCustomLoginScripts()
{
    new LoginConfig();
    new LoginAnnounce();
    new CustomLogin();
}
