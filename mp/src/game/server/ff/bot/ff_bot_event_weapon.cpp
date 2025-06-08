//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
// #include "cs_gamerules.h" // FF_CHANGE: Not directly needed, game rules are usually accessed via singleton
#include "KeyValues.h" // Keep for IGameEvent

#include "ff_bot.h" // FF_CHANGE: Changed from cs_bot.h
#include "ff_player.h" // FF_CHANGE: Added for CFFPlayer
#include "ff_weapon_base.h" // FF_CHANGE: Added for CFFWeaponBase and FFWeaponID

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponFire( IGameEvent *event ) // FF_CHANGE: CCSBot to CFFBot
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CFFPlayer *player = ToFFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ); // FF_CHANGE: Cast to CFFPlayer
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	/// @todo Propagate events into active state
	if (GetEnemy() == player && IsUsingKnife()) // IsUsingKnife is CFFBot method
	{
		ForceRun( 5.0f );
	}

	const float ShortRange = 1000.0f;
	const float NormalRange = 2000.0f;
	float range = NormalRange; // Default range

	// FF_TODO_WEAPONS: Check weapon type (knives are pretty quiet)
	// FF_TODO_WEAPONS: Use actual volume, account for FF specific weapon sounds (eg. silenced spy pistol vs rocket launcher).
	CFFWeaponBase *weapon = NULL; // FF_CHANGE: CWeaponCSBase to CFFWeaponBase
	if (player)
	{
		weapon = player->GetActiveFFWeapon(); // FF_CHANGE: GetActiveCSWeapon to GetActiveFFWeapon (returns CFFWeaponBase*)
	}

	if (weapon == NULL)
		return;

	// FF_TODO_WEAPONS: The following switch is CS-specific. Adapt for FFWeaponID and FF weapon characteristics.
	// For now, most will default to NormalRange. Specific loud/quiet FF weapons should be added.
	/*
	switch( weapon->GetWeaponID() ) // GetWeaponID() should be from CFFWeaponBase, returning FFWeaponID
	{
		// silent "firing" examples for FF (these might not even generate weapon_fire events)
		// case FF_WEAPON_MEDKIT_FIRE: // If medkit has a "fire" mode that's silent
		// case FF_WEAPON_BUILDTOOL_FIRE: // If build tools have a silent "fire"
		//	return;

		// quiet examples for FF
		case FF_WEAPON_KNIFE:
		case FF_WEAPON_SPANNER:
		case FF_WEAPON_TRANQUILISER: // Example Spy silenced pistol
			range = ShortRange;
			break;

		// M4A1 - check for silencer (Example of CS logic to be removed/adapted)
		// case WEAPON_M4A1:
		// {
		// 	if (weapon->IsSilenced()) // IsSilenced() would need to be on CFFWeaponBase if FF has silencers
		// 	{
		// 		range = ShortRange;
		// 	}
		// 	else
		// 	{
		// 		range = NormalRange;
		// 	}
		// 	break;
		// }

		// loud examples for FF
		case FF_WEAPON_RPG:
		case FF_WEAPON_ASSAULTCANNON:
		case FF_WEAPON_SNIPERRIFLE: // Sniper rifles are typically loud
			range = 99999.0f; // Effectively map-wide
			break;

		// normal
		default:
			range = NormalRange;
			break;
	}
	*/
	// Simplified default for now:
	FFWeaponID firedWeaponID = weapon->GetWeaponID();
	if (firedWeaponID == FF_WEAPON_KNIFE || firedWeaponID == FF_WEAPON_SPANNER || firedWeaponID == FF_WEAPON_MEDKIT || firedWeaponID == FF_WEAPON_TRANQUILISER)
	{
		range = ShortRange;
	}
	else if (firedWeaponID == FF_WEAPON_RPG || firedWeaponID == FF_WEAPON_ASSAULTCANNON || firedWeaponID == FF_WEAPON_SNIPERRIFLE || firedWeaponID == FF_WEAPON_GRENADELAUNCHER || firedWeaponID == FF_WEAPON_PIPELAUNCHER || firedWeaponID == FF_WEAPON_IC)
	{
		range = 99999.0f; // Very loud
	}


	OnAudibleEvent( event, player, range, PRIORITY_HIGH, true ); // weapon_fire
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponFireOnEmpty( IGameEvent *event ) // FF_CHANGE: CCSBot to CFFBot
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CFFPlayer *player = ToFFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ); // FF_CHANGE
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	if (GetEnemy() == player && IsUsingKnife())
	{
		ForceRun( 5.0f );
	}

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_fire_on_empty
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponReload( IGameEvent *event ) // FF_CHANGE: CCSBot to CFFBot
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CFFPlayer *player = ToFFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ); // FF_CHANGE
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	if (GetEnemy() == player && IsUsingKnife())
	{
		ForceRun( 5.0f );
	}

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_reload
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponZoom( IGameEvent *event ) // FF_CHANGE: CCSBot to CFFBot
{
	// FF_TODO_WEAPONS: This event might not be relevant for all FF weapons or might need different handling.
	// For now, treat it as a generic low-priority audible event.
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CFFPlayer *player = ToFFPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ); // FF_CHANGE
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_zoom
}
