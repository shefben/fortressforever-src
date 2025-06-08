//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../../ff_gamerules.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "ff_weapon_base.h"
#include "ff_playerclass_parse.h" // For CFFPlayerClassInfo access

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
ConVar bot_loadout( "bot_loadout", "", FCVAR_CHEAT, "bots are given these items at round start" );
ConVar bot_randombuy( "bot_randombuy", "0", FCVAR_CHEAT, "should bots ignore their prefered weapons and just buy weapons at random?" ); // FF_TODO_AI_BEHAVIOR: Review if this makes sense for FF

//--------------------------------------------------------------------------------------------------------------
// CFFBot::GiveWeapon is defined in ff_bot.cpp
//--------------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------------
/**
 * Buy weapons, armor, etc. - For FF, this is more "EnsureLoadout"
 */
void BuyState::OnEnter( CFFBot *me )
{
	m_isInitialDelay = true;
	m_doneBuying = false;

	// FF_TODO_GAME_MECHANIC: cv_bot_eco_limit is CS-specific. Removed direct usage.
	// If FF had an economy, logic to check funds would go here.

	// EquipBestWeapon will be called in OnExit or when m_doneBuying is true.
	// Grenade and pistol buying flags are not really used in FF context as items are granted by class or loadout.
	m_buyGrenade = false;
	m_buyPistol = false;
}

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnUpdate( CFFBot *me )
{
	if (!TheNavMesh->IsLoaded()) // Should not happen if bots are running
	{
		me->Idle();
		return;
	}

	if (m_isInitialDelay)
	{
		const float waitToBuyTime = 0.25f; // Short delay
		if (gpGlobals->curtime - me->GetStateTimestamp() < waitToBuyTime) return;
		m_isInitialDelay = false;
	}

	if (m_doneBuying)
	{
		// If in freeze period, bots might repeatedly call Buy if not immediately idling.
		if (FFGameRules() && FFGameRules()->IsFreezePeriod())
		{
			me->EquipBestWeapon( MUST_EQUIP );
			me->Reload(); // CFFBot::Reload
			me->ResetStuckMonitor();
			// Bots should ideally wait or do some other freeze period action.
			// Forcing Idle might cause them to re-enter Buy if conditions still met.
			// However, for now, this matches previous structure.
			me->Idle();
		}
		else
		{
			me->Idle();
		}
		return;
	}

	const char *loadoutString = bot_loadout.GetString();
	if ( loadoutString && *loadoutString )
	{
		CUtlVector<char*, CUtlMemory<char*> > loadoutItems;
		Q_SplitString( loadoutString, " ", loadoutItems );
		for ( int i=0; i<loadoutItems.Count(); ++i )
		{
			const char *item = loadoutItems[i];
			// FF_TODO_GAME_MECHANIC: More robust item type detection (armor, specific ammo packs beyond prim/sec)
			if ( FStrEq( item, "primammo" ) )
			{
				// Give a large amount, CFFPlayer::GiveAmmo will cap it based on weapon's max carry
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_SHELLS) ); // Example, needs proper primary ammo type for class
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_NAILS) );
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_ROCKETS) );
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_CELLS) ); // For relevant classes
			}
			else if ( FStrEq( item, "secammo" ) )
			{
				// Give a large amount for typical secondary types
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_SHELLS) ); // If shotgun is secondary
				me->GiveAmmo( 999, GetAmmoDef()->Index(AMMO_NAILS) );  // If nailgun is secondary
			}
			else if ( FStrEq( item, "cells" ) ) me->GiveAmmo(999, AMMO_CELLS); // Generic cell top-up
			else if ( FStrEq( item, "shells" ) ) me->GiveAmmo(999, AMMO_SHELLS);
			else if ( FStrEq( item, "nails" ) ) me->GiveAmmo(999, AMMO_NAILS);
			else if ( FStrEq( item, "rockets" ) ) me->GiveAmmo(999, AMMO_ROCKETS);
			// FF_TODO_GAME_MECHANIC: Add actual armor entity names for FF if they exist
			// else if ( FStrEq( item, "vest" ) ) me->GiveNamedItem( "item_armor_ff_light" );
			// else if ( FStrEq( item, "vesthelm" ) ) me->GiveNamedItem( "item_armor_ff_heavy" );
			else
			{
				me->GiveWeapon( item ); // Calls CFFBot::GiveWeapon
			}
		}
		loadoutItems.PurgeAndDeleteElements();
		m_doneBuying = true;
	}
	else
	{
		// No bot_loadout defined, ensure default ammo and grenades are topped up.
		// CFFPlayer::Spawn() and SetupClassVariables() should have given default weapons and initial ammo.
		const CFFPlayerClassInfo &classInfo = me->GetFFClassData();

		// Top up ammo for all weapons the bot currently has.
		for (int i = 0; i < MAX_WEAPONS; ++i)
		{
			CFFWeaponBase *weapon = dynamic_cast<CFFWeaponBase*>(me->GetWeapon(i));
			if (weapon)
			{
				int ammoTypeIndex = weapon->GetPrimaryAmmoType(); // From CBaseCombatWeapon
				if (ammoTypeIndex != -1 && ammoTypeIndex < MAX_AMMO_TYPES) // Ensure valid index
				{
					// GetMaxAmmo is CBasePlayer, uses m_iMaxAmmo array filled by CFFPlayer::SetupClassVariables
					int maxAmmoForType = me->GetMaxCarryAmmo(ammoTypeIndex);
					int currentAmmo = me->GetAmmoCount(ammoTypeIndex);
					if (currentAmmo < maxAmmoForType)
					{
						me->GiveAmmo(maxAmmoForType - currentAmmo, ammoTypeIndex, true); // Suppress sound
					}
				}
			}
		}

		// Top up grenades (primary)
		if (classInfo.m_iPrimaryMax > 0 && me->GetPrimaryGrenades() < classInfo.m_iPrimaryMax)
		{
			me->AddPrimaryGrenades(classInfo.m_iPrimaryMax - me->GetPrimaryGrenades());
		}
		// Top up grenades (secondary)
		if (classInfo.m_iSecondaryMax > 0 && me->GetSecondaryGrenades() < classInfo.m_iSecondaryMax)
		{
			me->AddSecondaryGrenades(classInfo.m_iSecondaryMax - me->GetSecondaryGrenades());
		}

		// Specific for Engineer: Ensure they have cells for building
		if (me->GetPlayerClass() == CLASS_ENGINEER)
		{
			int cellsIndex = GetAmmoDef()->Index(AMMO_CELLS);
			if (cellsIndex != -1)
			{
				int currentCells = me->GetAmmoCount(cellsIndex);
				int maxCells = classInfo.m_iMaxCells;
				if (currentCells < maxCells)
				{
					me->GiveAmmo(maxCells - currentCells, cellsIndex, true);
				}
			}
			// Also ensure detpack ammo if Engineer uses detpacks (usually Demo in FF)
			// int detpackIndex = GetAmmoDef()->Index(AMMO_DETPACK);
			// if (detpackIndex != -1 && classInfo.m_iMaxDetpack > 0) { ... }
		}
		m_doneBuying = true;
	}

	// If buying is finished this tick:
	if (m_doneBuying)
	{
		me->EquipBestWeapon( MUST_EQUIP );
		me->Reload();
		me->ResetStuckMonitor();
		// Do not call me->Idle() here if in freeze period, let the main OnUpdate logic handle that.
		// If not in freeze, then Idle is fine.
		if (FFGameRules() && !FFGameRules()->IsFreezePeriod())
		{
			me->Idle();
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnExit( CFFBot *me )
{
	me->ResetStuckMonitor();
	// Ensure a sensible weapon is equipped when exiting buy state, especially if it was forced early.
	if (!me->GetActiveFFWeapon() || !me->GetActiveFFWeapon()->HasAnyAmmo())
	{
		me->EquipBestWeapon(MUST_EQUIP);
	}
}
