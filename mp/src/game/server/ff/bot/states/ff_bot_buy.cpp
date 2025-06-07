//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../../ff_gamerules.h" // Changed from cs_gamerules.h
#include "../ff_bot.h"       // Changed from cs_bot.h
#include "../ff_bot_manager.h" // For TheFFBots() and team definitions
#include "ff_weapon_base.h" // FF_WEAPONS: Ensure this is included for FFWeaponID potentially

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
ConVar bot_loadout( "bot_loadout", "", FCVAR_CHEAT, "bots are given these items at round start" );
ConVar bot_randombuy( "bot_randombuy", "0", FCVAR_CHEAT, "should bots ignore their prefered weapons and just buy weapons at random?" ); // FF_TODO: Review if this makes sense for FF

//--------------------------------------------------------------------------------------------------------------
/**
 *  Debug command to give a named weapon - This is CFFBot::GiveWeapon, which is already in ff_bot.cpp (ported from cs_bot.cpp)
 *  The version here was likely an older or specific version for buy state context.
 *  Keeping the ff_bot.cpp version as the primary one. This can be removed or fully stubbed.
 */
/*
void CFFBot::GiveWeapon( const char *weaponAlias )
{
	// FF_TODO_WEAPONS: This entire function is highly CS-specific due to CCSWeaponInfo and weapon slot logic.
	// It needs to be re-written for FF's weapon system. The main version is in ff_bot.cpp.
	Warning("CFFBot::GiveWeapon (in ff_bot_buy.cpp) for '%s' not implemented for FF. Using version from ff_bot.cpp.\n", weaponAlias);
}
*/

//--------------------------------------------------------------------------------------------------------------
static bool HasDefaultPistol( CFFBot *me )
{
	// FF_TODO_WEAPONS: This function is CS-specific (Glock, USP). Adapt for FF default secondaries if concept exists.
	// For example, Scout's default secondary might be Nailgun, Engy's Railgun, Spy's Tranq.
	// This would require checking bot's class and then the specific FFWeaponID for that class's default secondary.
	/*
	CFFWeaponBase *pistol = static_cast<CFFWeaponBase *>(me->Weapon_GetSlot( FF_SECONDARY_WEAPON_SLOT ));

	if (pistol == NULL)
		return false;

	FFWeaponID pistolID = pistol->GetWeaponID();
	PlayerClass_t pClass = me->GetPlayerClass();

	switch(pClass)
	{
		case CLASS_SCOUT: return pistolID == FF_WEAPON_NAILGUN; // Example
		case CLASS_ENGINEER: return pistolID == FF_WEAPON_RAILGUN; // Example
		case CLASS_SPY: return pistolID == FF_WEAPON_TRANQUILISER; // Example
		// Add cases for other classes if they have specific default secondaries
	}
	*/
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Buy weapons, armor, etc.
 */
void BuyState::OnEnter( CFFBot *me )
{
	m_retries = 0;
	m_prefRetries = 0;
	m_prefIndex = 0;

	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		m_doneBuying = false;
	}
	else
	{
		// FF_TODO_ECONOMY: cv_bot_eco_limit may or may not apply to FF's economy (if any).
		// Bots in FF might not "buy" items with money.
		// if (me->m_iAccount < cv_bot_eco_limit.GetFloat()) // m_iAccount is CS-specific
		// {
		//	me->PrintIfWatched( "Saving money for next round (or FF equivalent).\n" );
		//	m_doneBuying = true;
		// }
		// else
		// {
			m_doneBuying = false; // Assuming for now that bots will try to get loadout unless bot_loadout is set
		// }
	}

	m_isInitialDelay = true;
	me->EquipBestWeapon( MUST_EQUIP ); // EquipBestWeapon needs FF logic

	// m_buyDefuseKit = false; // FF no bomb
	// m_buyShield = false; // FF no shield

	// FF_TODO_WEAPONS: Grenade buying logic needs to use FF grenade names/types and FF_CVARs
	// This depends on how bots acquire grenades in FF (buy, class default, profile).
	m_buyGrenade = false;
	// if (TheFFBots()->AllowGrenades()) // Assuming a generic AllowGrenades cvar for now
	// {
	// 	m_buyGrenade = (RandomFloat( 0.0f, 100.0f ) < 33.3f) ? true : false;
	// }


	m_buyPistol = false;
	// FF_TODO_WEAPONS: Pistol buying logic needs FF weapon names/types and FF_CVARs for weapon allowances.
	// This depends on how bots acquire secondary weapons in FF.
	// if (TheFFBots()->AllowPistols()) // Assuming a generic AllowPistols cvar
	// {
	//		CFFWeaponBase* secondary = static_cast<CFFWeaponBase*>(me->Weapon_GetSlot(FF_SECONDARY_WEAPON_SLOT));
	//		if (secondary)
	//		{
	//			if (HasDefaultPistol(me)) // HasDefaultPistol needs FF adaptation
	//			{
	//				// if (all primary weapon types disallowed by cvars) m_buyPistol = (RandomFloat(0,100) < 75.0f);
	//				// else if (me->GetMoney() < 1000) m_buyPistol = (RandomFloat(0,100) < 75.0f); // GetMoney() for FF
	//				// else m_buyPistol = (RandomFloat(0,100) < 33.3f);
	//			}
	//		}
	//		else { m_buyPistol = true; } // If no secondary, try to get one
	// }
}

// FF_TODO_WEAPONS: The entire CS weapon classification (WeaponType enum, BuyInfo struct, primary/secondary weapon tables, GetWeaponType func)
// is CS-specific and not applicable to FF in this form. FF bots will likely rely on class defaults,
// bot_loadout cvar, or a new system based on FFWeaponID and BotProfile preferences.
// This entire block is commented out.
/*
enum WeaponType { PISTOL, SHOTGUN, SUB_MACHINE_GUN, RIFLE, MACHINE_GUN, SNIPER_RIFLE, GRENADE, NUM_WEAPON_TYPES };
struct BuyInfo { WeaponType type; bool preferred; const char *buyAlias; };
#define PRIMARY_WEAPON_BUY_COUNT 13
#define SECONDARY_WEAPON_BUY_COUNT 3
// ... CS specific tables ...
inline WeaponType GetWeaponType( const char *alias ) { ... }
*/

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnUpdate( CFFBot *me )
{
	if (!TheNavMesh->IsLoaded()) return;

	if (m_isInitialDelay)
	{
		const float waitToBuyTime = 0.25f; // Allow some time for server to settle
		if (gpGlobals->curtime - me->GetStateTimestamp() < waitToBuyTime) return;
		m_isInitialDelay = false;
	}

	if (m_doneBuying)
	{
		if (FFGameRules() && FFGameRules()->IsFreezePeriod())
		{
			me->EquipBestWeapon( MUST_EQUIP );
			me->Reload(); // CFFBot::Reload
			me->ResetStuckMonitor();
			return;
		}
		me->Idle();
		return;
	}

	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		CUtlVector<char*, CUtlMemory<char*> > loadout;
		Q_SplitString( cheatWeaponString, " ", loadout );
		for ( int i=0; i<loadout.Count(); ++i )
		{
			const char *item = loadout[i];
			// FF_TODO_ITEMS: Adapt item names for FF (armor types, ammo types, weapon names using FFWeaponID aliases)
			// The CFFBot::GiveWeapon is now based on FFWeaponID aliases.
			// Other items like armor need FF specific names.
			// Example: "item_armor_light", "item_armor_heavy" if those are the entity names.
			// Ammo giving needs FF ammo type names.
			if ( FStrEq( item, "vest" ) ) me->GiveNamedItem( "item_armor_red_light" ); // Example, needs actual FF entity name
			else if ( FStrEq( item, "vesthelm" ) ) me->GiveNamedItem( "item_armor_red_heavy" ); // Example
			// else if ( FStrEq( item, "nvgs" ) ) { /* me->GiveNamedItem("item_nvgs_ff"); */ } // If FF has NVGs
			else if ( FStrEq( item, "primammo" ) ) me->GiveAmmo( 100, AMMO_ROCKETS ); // Example FF ammo type from ff_weapon_base.h
			else if ( FStrEq( item, "secammo" ) ) me->GiveAmmo( 100, AMMO_SHELLS );  // Example
			else me->GiveWeapon( item ); // Calls CFFBot::GiveWeapon which uses FF aliases
		}
		m_doneBuying = true;
		return;
	}

	// FF_TODO_ECONOMY: Buying in FF is not like CS. Bots typically spawn with class loadouts.
	// This section needs to be entirely rethought if bots are to acquire items beyond defaults or bot_loadout.
	// For now, if not using bot_loadout, assume class defaults are sufficient.

	// Check if in buy zone (concept might not exist or be different in FF)
	// if (FFGameRules() && !FFGameRules()->IsInBuyZone(me))
	// {
	//		m_doneBuying = true;
	//		CONSOLE_ECHO( "%s bot spawned outside of a buy zone (%d, %d, %d)\n",
	//						FFGameRules()->GetTeamName(me->GetTeamNumber()),
	//						(int)me->GetAbsOrigin().x, (int)me->GetAbsOrigin().y, (int)me->GetAbsOrigin().z );
	//		return;
	// }

	me->PrintIfWatched( "FF_TODO: Implement Fortress Forever bot item acquisition logic beyond bot_loadout.\n" );
	m_doneBuying = true; // Mark as done, as detailed CS buy logic is removed.

	/* --- CS BUY LOGIC START (Commented Out) ---
	const float buyInterval = 0.02f; // CS: time between buy commands
	if (gpGlobals->curtime - me->GetStateTimestamp() > buyInterval)
	{
		me->m_stateTimestamp = gpGlobals->curtime; // Update timestamp for next potential buy action

		// ... Entire complex CS buying logic based on money, weapon tables, preferences, etc. is omitted ...
		// This involved:
		// - Buying armor (vest, vesthelm)
		// - Buying a primary weapon based on preference and allowance cvars
		// - Buying a pistol if needed/allowed
		// - Buying grenades based on allowance and chance
		// - Buying defuse kit (CS specific)
		// - Buying shield (CS specific)
		// - Managing retries and iterating through preferred weapon lists

		m_doneBuying = true;
	}
	--- CS BUY LOGIC END --- */
}

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnExit( CFFBot *me )
{
	me->ResetStuckMonitor();
	me->EquipBestWeapon(); // EquipBestWeapon should use FF logic now
}
