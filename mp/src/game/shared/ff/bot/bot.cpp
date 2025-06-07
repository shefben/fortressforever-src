//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), Leon Hartwig, 2003

#include "cbase.h"
#include "basegrenade_shared.h" // Engine header, should be fine

#include "bot.h" // Should now refer to the FF version of bot.h
#include "bot_util.h"
#include "ff_weapon_base.h" // FF_WEAPONS: Added for CFFWeaponBase and FFWeaponID
// Add FF specific manager if needed for constants, though ff_bot.h should bring in team defs
// #include "../../server/ff/bot/ff_bot_manager.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/// @todo Remove this nasty hack - CreateFakeClient() calls CBot::Spawn, which needs the profile and team
const BotProfile *g_botInitProfile = NULL;
int g_botInitTeam = 0; // This will be FF_TEAM_*

//
// NOTE: Because CBot had to be templatized, the code was moved into bot.h
//


//--------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------

ActiveGrenade::ActiveGrenade( CBaseGrenade *grenadeEntity )
{
	m_entity = grenadeEntity;
	m_detonationPosition = grenadeEntity->GetAbsOrigin();
	m_dieTimestamp = 0.0f;

	// FF_TODO_WEAPONS: Adapt grenade specific radius logic for FF.
	// Using a generic radius for now, or this needs to be tied to FF weapon data.
	// HEGrenadeRadius might be a CS define, or generic. If CS-specific, replace with a default float.
	// For now, assume HEGrenadeRadius (from bot_manager.h shared) is a generic placeholder or will be available.
	// If not, use a default like DEFAULT_HEGRENADE_RADIUS.
	m_radius = DEFAULT_HEGRENADE_RADIUS; // Using the define from shared bot_manager.h (was HEGrenadeRadius)

	// FF_TODO_WEAPONS: CS specific classname checks for grenade types.
	// This needs to be replaced with FF specific grenade identification, possibly using FFWeaponID
	// if the grenade entity or its thrower's weapon can provide it.
	m_isSmoke = false;
	// if ( grenadeEntity->GetWeaponID() == FF_WEAPON_GRENADE_SMOKE_EXAMPLE ) // Example using a hypothetical FFWeaponID
	// {
	//		m_isSmoke = true;
	// 		m_radius = DEFAULT_SMOKEGRENADE_RADIUS; // Using define from shared bot_manager.h (was SmokeGrenadeRadius)
	// }

	m_isFlashbang = false;
	// if ( grenadeEntity->GetWeaponID() == FF_WEAPON_GRENADE_FLASH_EXAMPLE ) // Example
	// {
	//		m_isFlashbang = true;
	// 		m_radius = DEFAULT_FLASHBANG_RADIUS; // Using define from shared bot_manager.h (was FlashbangGrenadeRadius)
	// }
	// For now, all non-typed grenades are considered HE-like for radius.
	// Actual effects (smoke, flash) are not determined here yet.
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Called when the grenade in the world goes away
 */
void ActiveGrenade::OnEntityGone( void )
{
	// FF_TODO: Adapt smoke grenade linger time if FF has smoke grenades with similar behavior.
	if (m_isSmoke) // This relies on m_isSmoke being set by FF-specific logic if needed
	{
		// const float smokeLingerTime = 4.0f;
		// m_dieTimestamp = gpGlobals->curtime + smokeLingerTime;
	}

	m_entity = NULL;
}

//--------------------------------------------------------------------------------------------------------------
void ActiveGrenade::Update( void )
{
	if (m_entity != NULL)
	{
		m_detonationPosition = m_entity->GetAbsOrigin();
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this grenade is valid
 */
bool ActiveGrenade::IsValid( void ) const
{
	// FF_TODO: Adapt smoke grenade linger logic if FF has smoke grenades with similar behavior.
	if ( m_isSmoke )
	{
		if ( m_entity == NULL && gpGlobals->curtime > m_dieTimestamp )
		{
			return false;
		}
	}
	else
	{
		if ( m_entity == NULL )
		{
			return false;
		}
	}

	return true;
}

//--------------------------------------------------------------------------------------------------------------
const Vector &ActiveGrenade::GetPosition( void ) const
{
	// FF_TODO: Adapt smoke grenade logic if needed.
	if (m_entity == NULL && m_isSmoke) // Check m_isSmoke as well
		return GetDetonationPosition();

	if (m_entity != NULL)
		return m_entity->GetAbsOrigin();

	// Should not happen if IsValid() is checked first, but return something
	static Vector vecZero(0,0,0);
	return vecZero;
}
