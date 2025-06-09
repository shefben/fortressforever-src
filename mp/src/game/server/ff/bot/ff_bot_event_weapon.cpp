//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
// #include "../../shared/ff/weapons/ff_weapon_parse.h"
#include "ff_gamestate.h"
#include "bot_constants.h"

#include "KeyValues.h"      // Already included, seems fine


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponFire( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	/// @todo Propagate events into active state
	if (GetEnemy() == player && IsUsingKnife())
	{
		ForceRun( 5.0f );
	}

	const float ShortRange = 1000.0f;
	const float NormalRange = 2000.0f;

	float range;

	/// @todo Check weapon type (knives are pretty quiet)
	/// @todo Use actual volume, account for silencers, etc.
	CFFWeaponBase *weapon = dynamic_cast<CFFWeaponBase *>((player)?player->GetActiveWeapon():NULL); // Use dynamic_cast

	if (weapon == NULL)
		return;

	// TODO_FF: Update all WEAPON_ enums to FFWeaponID equivalents for Fortress Forever
	// The GetWeaponID() method should return an FFWeaponID enum value.
	// The case statements here are CS-specific and will need to be updated for FF weapons.
	// For this refactoring, the main goal is ensuring GetWeaponID() is called on CFFWeaponBase.
	// Actual weapon IDs and their properties will be handled in FF-specific logic later.
	switch( weapon->GetWeaponID() ) // Assuming GetWeaponID() returns FFWeaponID
	{
		// silent "firing"
		// case FF_WEAPON_GRENADE_CONC: // Example for FF
		// case FF_WEAPON_GRENADE_SMOKE: // Example for FF
		// case FF_WEAPON_GRENADE_FLASH: // Example for FF
		// case FF_WEAPON_SHIELDGUN: // If FF has a shield gun
		// case FF_WEAPON_C4: // If FF has C4 or equivalent
		//	return;

		// quiet
		// case FF_WEAPON_KNIFE: // Example for FF
		// case FF_WEAPON_TMP_SILENCED: // Example for FF
		//	range = ShortRange;
		//	break;

		// M4A1 - check for silencer (Example, FF will have different weapons)
		// case FF_WEAPON_ASSAULTRIFLE: // Example for FF
		// {
		//	if (weapon->IsSilenced()) // May not apply or need FF specific check
		//	{
		//		range = ShortRange;
		//	}
		//	else
		//	{
		//		range = NormalRange;
		//	}
		//	break;
		// }

		// loud
		// case FF_WEAPON_SNIPERRIFLE_AWP: // Example for FF
		//	range = 99999.0f;
		//	break;

		// normal
		default:
			range = NormalRange; // Default for unknown/unhandled weapons
			break;
	}

	OnAudibleEvent( event, player, range, PRIORITY_HIGH, true ); // weapon_fire // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponFireOnEmpty( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	/// @todo Propagate events into active state
	if (GetEnemy() == player && IsUsingKnife())
	{
		ForceRun( 5.0f );
	}

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_fire_on_empty // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponReload( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// for knife fighting - if our victim is attacking or reloading, rush him
	/// @todo Propagate events into active state
	if (GetEnemy() == player && IsUsingKnife())
	{
		ForceRun( 5.0f );
	}

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_reload // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnWeaponZoom( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // weapon_zoom // TODO: Update event name for FF
}
