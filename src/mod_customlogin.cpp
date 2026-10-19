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


#include "Bag.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "GuildMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

// When a starter transaction cannot be verified, keep the Player alive until
// the login hook returns; only then may the world-session update log it out.
// Match BOTH session identity and character GUID so a stale entry cannot
// affect another player if a session pointer or character GUID is reused.
namespace
{
    std::mutex pendingStarterLogoutMutex;
    std::unordered_map<WorldSession*, uint32> pendingStarterLogouts;
    std::atomic<bool> hasPendingStarterLogouts{false};

    void QueueStarterNoSaveLogout(Player* player)
    {
        WorldSession* session = player->GetSession();
        std::lock_guard<std::mutex> lock(pendingStarterLogoutMutex);
        pendingStarterLogouts[session] = player->GetGUID().GetCounter();
        hasPendingStarterLogouts.store(true, std::memory_order_release);
    }

    void ClearPendingStarterLogout(Player* player)
    {
        if (!hasPendingStarterLogouts.load(std::memory_order_acquire))
            return;

        std::lock_guard<std::mutex> lock(pendingStarterLogoutMutex);
        WorldSession* session = player->GetSession();
        auto it = pendingStarterLogouts.find(session);
        if (it != pendingStarterLogouts.end() && it->second == player->GetGUID().GetCounter())
            pendingStarterLogouts.erase(it);
        hasPendingStarterLogouts.store(!pendingStarterLogouts.empty(), std::memory_order_release);
    }
}

class CustomLoginRecoverySessionScript : public SessionScript
{
public:
    CustomLoginRecoverySessionScript() : SessionScript("CustomLoginRecoverySessionScript", {
        SESSIONHOOK_ON_UPDATE
    }) { }

    void OnSessionUpdate(WorldSession* session, uint32 /*diff*/) override
    {
        if (!session || !hasPendingStarterLogouts.load(std::memory_order_acquire))
            return;

        bool shouldLogout = false;
        {
            std::lock_guard<std::mutex> lock(pendingStarterLogoutMutex);
            auto it = pendingStarterLogouts.find(session);
            if (it == pendingStarterLogouts.end())
                return;

            Player* player = session->GetPlayer();
            shouldLogout = player && player->GetGUID().GetCounter() == it->second;
            // Always clear the entry, including a vanished or changed player.
            pendingStarterLogouts.erase(it);
            hasPendingStarterLogouts.store(!pendingStarterLogouts.empty(), std::memory_order_release);
        }

        if (shouldLogout)
        {
            LOG_ERROR("module", "CustomLogin: discarding unverified starter grants with no-save logout");
            session->LogoutPlayer(false);
        }
    }
};

bool LoginEnable = 1;
bool LoginAnnounceModule = 1;
bool LoginPlayerAnnounce = 1;
bool LoginBOA = 1;
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
        PLAYERHOOK_ON_FIRST_LOGIN,
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_DELETE
    }) { }

    void OnPlayerFirstLogin(Player* player) override
    {
        // AzerothCore clears AT_LOGIN_FIRST immediately before this hook.
        // If the durable receipt could not be written, restore the flag so
        // a bot-first character remains eligible even if it later saves.
        if (!LoginEnable || !player)
            return;

        QueryResult receipt = CharacterDatabase.Query(
            "SELECT state FROM mod_customlogin_starter WHERE guid = {}", player->GetGUID().GetCounter());
        if (!receipt)
        {
            player->SetAtLoginFlag(AT_LOGIN_FIRST);
            LOG_ERROR("module", "CustomLogin: retaining first-login eligibility for {} (missing receipt)", player->GetName());
        }
    }

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
            // Only replace a level-one hunter's default quiver. Map each
            // contained stack to a distinct empty backpack slot so the core's
            // StoreItem() cannot split or merge it (and invalidate its pointer).
            struct AmmoMove
            {
                uint8 bagSlot;
                uint8 itemSlot;
                uint8 backpackSlot;
            };
            std::vector<AmmoMove> ammoMoves;
            std::vector<uint8> emptyBackpackSlots;
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    emptyBackpackSlots.push_back(slot);

            bool const removableQuiver = player->getClass() == CLASS_HUNTER && player->GetLevel() == 1;
            for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            {
                Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (!equipped || equipped->GetEntry() == 41599)
                    continue;

                if (!removableQuiver || equipped->GetTemplate()->Class != ITEM_CLASS_QUIVER || !equipped->ToBag())
                {
                    LOG_ERROR("module", "CustomLogin: nonstarter bag in slot {} for character {}", slot, player->GetName());
                    return false;
                }

                Bag* quiver = equipped->ToBag();
                for (uint8 inside = 0; inside < quiver->GetBagSize(); ++inside)
                {
                    Item* item = quiver->GetItemByPos(inside);
                    if (!item)
                        continue;

                    if (ammoMoves.size() >= emptyBackpackSlots.size())
                    {
                        LOG_ERROR("module", "CustomLogin: no safe backpack space for quiver items on {}", player->GetName());
                        return false;
                    }

                    uint8 destinationSlot = emptyBackpackSlots[ammoMoves.size()];
                    ItemPosCountVec destinations;
                    uint16 destination = (INVENTORY_SLOT_BAG_0 << 8) | destinationSlot;
                    if (player->CanStoreItem(INVENTORY_SLOT_BAG_0, destinationSlot, destinations, item, false) != EQUIP_ERR_OK ||
                        destinations.size() != 1 || destinations[0].pos != destination ||
                        destinations[0].count != item->GetCount())
                    {
                        LOG_ERROR("module", "CustomLogin: cannot preserve quiver item {} for character {}",
                            item->GetEntry(), player->GetName());
                        return false;
                    }
                    ammoMoves.push_back({slot, inside, destinationSlot});
                }
            }

            // The entire plan was checked before the first inventory mutation.
            for (AmmoMove const& move : ammoMoves)
            {
                Bag* quiver = player->GetItemByPos(INVENTORY_SLOT_BAG_0, move.bagSlot)->ToBag();
                Item* item = quiver->GetItemByPos(move.itemSlot);
                if (!item)
                    return false;

                uint16 destination = (INVENTORY_SLOT_BAG_0 << 8) | move.backpackSlot;
                ItemPosCountVec destinations;
                if (player->CanStoreItem(INVENTORY_SLOT_BAG_0, move.backpackSlot, destinations, item, false) != EQUIP_ERR_OK ||
                    destinations.size() != 1 || destinations[0].pos != destination ||
                    destinations[0].count != item->GetCount())
                    return false;

                player->RemoveItem(move.bagSlot, move.itemSlot, true);
                Item* stored = player->StoreItem(destinations, item, true);
                if (!stored || player->GetItemByPos(INVENTORY_SLOT_BAG_0, move.backpackSlot) != stored)
                {
                    // For a single empty destination, AzerothCore's StoreItem
                    // reuses the original item; attempt to restore it on failure.
                    if (!stored && !quiver->GetItemByPos(move.itemSlot) &&
                        !player->GetItemByPos(INVENTORY_SLOT_BAG_0, move.backpackSlot))
                        quiver->StoreItem(move.itemSlot, item, true);
                    LOG_ERROR("module", "CustomLogin: quiver move failed for {}", player->GetName());
                    return false;
                }
            }

            if (removableQuiver)
            {
                for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
                {
                    Item* equipped = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                    if (!equipped || equipped->GetEntry() == 41599)
                        continue;

                    Bag* quiver = equipped->ToBag();
                    if (!quiver || !quiver->IsEmpty())
                        return false;
                    player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
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
                // A bot-first login creates a pending receipt before AzerothCore
                // consumes AT_LOGIN_FIRST. Never infer eligibility from level 1.
                QueryResult receipt = CharacterDatabase.Query(
                    "SELECT state FROM mod_customlogin_starter WHERE guid = {}", guid);
                if (!receipt)
                {
                    uint8 initialState = isFirstLogin ? 0 : 1;
                    CharacterDatabase.DirectExecute(
                        "INSERT IGNORE INTO mod_customlogin_starter (guid, state) VALUES ({}, {})", guid, initialState);
                    receipt = CharacterDatabase.Query(
                        "SELECT state FROM mod_customlogin_starter WHERE guid = {}", guid);
                }

                if (!receipt)
                {
                    // Database errors must never authorize a duplicate grant.
                    LOG_ERROR("module", "CustomLogin: missing starter receipt for {}", player->GetName());
                    ChatHandler(player->GetSession()).SendSysMessage(
                        "Your starter package status is unavailable. Please contact a server administrator.");
                }
                else
                {
                    uint8 state = receipt->Fetch()[0].Get<uint8>();
                    if (state == 0)
                    {
                        // SaveToDB postpones a save during a far teleport.
                        if (player->IsBeingTeleportedFar())
                            return;

                        bool granted = GrantStarterPackage(player);
                        // Character inventory, money, and the receipt must commit
                        // together, including partial grants needing GM review.
                        CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
                        player->SaveToDB(transaction, false, false);
                        transaction->Append(
                            "UPDATE mod_customlogin_starter SET state = {} WHERE guid = {} AND state = 0",
                            granted ? 1u : 2u, guid);
                        CharacterDatabase.DirectCommitTransaction(transaction);

                        // DirectCommitTransaction is synchronous but does not report
                        // failure to its caller. Confirm the durable receipt before
                        // telling the player that their starter package completed.
                        QueryResult committedReceipt = CharacterDatabase.Query(
                            "SELECT state FROM mod_customlogin_starter WHERE guid = {}", guid);
                        bool committed = committedReceipt &&
                            committedReceipt->Fetch()[0].Get<uint8>() == (granted ? 1u : 2u);
                        if (!committed)
                        {
                            // A failed commit may leave rewards in memory while
                            // the durable receipt still permits another grant.
                            // Mark the receipt for GM review without replacing
                            // an already-completed receipt.
                            CharacterDatabaseTransaction recovery = CharacterDatabase.BeginTransaction();
                            recovery->Append(
                                "UPDATE mod_customlogin_starter SET state = 2 WHERE guid = {} AND state = 0", guid);
                            CharacterDatabase.DirectCommitTransaction(recovery);

                            QueryResult recovered = CharacterDatabase.Query(
                                "SELECT state FROM mod_customlogin_starter WHERE guid = {}", guid);
                            uint8 recoveredState = recovered ? recovered->Fetch()[0].Get<uint8>() : 0;
                            if (recoveredState != 1 && recoveredState != 2)
                            {
                                LOG_ERROR("module", "CustomLogin: cannot secure receipt for {}; scheduling no-save logout",
                                    player->GetName());
                            }
                            else
                                LOG_ERROR("module", "CustomLogin: starter receipt for {} recovered to state {}",
                                    player->GetName(), recoveredState);

                            // Even when the receipt is secured, a failed initial
                            // character save leaves rewards in an uncertain state.
                            // Never allow them to be used or saved again this login.
                            QueueStarterNoSaveLogout(player);

                            ChatHandler(player->GetSession()).SendSysMessage(
                                "Your starter package status is unavailable. Please contact a server administrator.");
                        }
                        else if (granted)
                        {
                            if (LoginBOA)
                                ChatHandler(player->GetSession()).SendSysMessage(
                                    "Welcome to Brambletown! Your starter heirlooms, four Frostweave Bags, and 25 gold have been added.");
                        }
                        else
                            ChatHandler(player->GetSession()).SendSysMessage(
                                "Your starter package could not be completed. Please contact a server administrator.");
                    }
                    else if (state == 2)
                        ChatHandler(player->GetSession()).SendSysMessage(
                            "Your starter package needs administrator review.");
                }
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
        if (player)
            ClearPendingStarterLogout(player);

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
    new CustomLoginRecoverySessionScript();
}
