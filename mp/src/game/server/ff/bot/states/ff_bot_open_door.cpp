//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), April 2005

#include "cbase.h"
#include "ff_bot_state_open_door.h" // Assuming this is the header for OpenDoorState
#include "../ff_bot.h"
#include "../ff_bot_manager.h" // For TheFFBots() (potentially used)
#include "../../ff_player.h" // For CFFPlayer (potentially used by CFFBot)
#include "../../../shared/ff/weapons/ff_weapon_base.h" // For FFWeaponID (used in CFFBot)
// #include "../../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../../shared/ff/ff_gamerules.h" // For CFFGameRules or FFGameRules() (potentially used)
#include "../ff_gamestate.h" // For FFGameState (used in CFFBot)
#include "BasePropDoor.h" // Specific to door interactions
#include "doors.h" // Specific to door interactions


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-------------------------------------------------------------------------------------------------
/**
 * Face the door and open it.
 * NOTE: This state assumes we are standing in range of the door to be opened, with no obstructions.
 */
void OpenDoorState::OnEnter( CFFBot *me )
{
	m_isDone = false;
	m_timeout.Start( 1.0f );
}


//-------------------------------------------------------------------------------------------------
void OpenDoorState::SetDoor( CBaseEntity *door )
{
	CBaseDoor *funcDoor = dynamic_cast< CBaseDoor * >(door);
	if ( funcDoor )
	{
		m_funcDoor = funcDoor;
		return;
	}

	CBasePropDoor *propDoor = dynamic_cast< CBasePropDoor * >(door);
	if ( propDoor )
	{
		m_propDoor = propDoor;
		return;
	}
}


//-------------------------------------------------------------------------------------------------
void OpenDoorState::OnUpdate( CFFBot *me )
{
	me->ResetStuckMonitor();

	// wait for door to swing open before leaving state
	if (m_timeout.IsElapsed())
	{
		m_isDone = true;
		return;
	}

	// look at the door
	Vector pos;
	bool isDoorMoving = false;
	if ( m_funcDoor )
	{
		pos = m_funcDoor->WorldSpaceCenter();
		isDoorMoving = m_funcDoor->m_toggle_state == TS_GOING_UP || m_funcDoor->m_toggle_state == TS_GOING_DOWN;
	}
	else if ( m_propDoor ) // Ensure m_propDoor is valid before using
	{
		pos = m_propDoor->WorldSpaceCenter();
		isDoorMoving = m_propDoor->IsDoorOpening() || m_propDoor->IsDoorClosing();
	}
	else // No valid door entity
	{
		m_isDone = true; // Can't operate on a null door
		return;
	}


	me->SetLookAt( "Open door", pos, PRIORITY_HIGH );

	// if we are looking at the door, "use" it and exit
	if (me->IsLookingAtPosition( pos ))
	{
		me->UseEnvironment();
	}
}


//-------------------------------------------------------------------------------------------------
void OpenDoorState::OnExit( CFFBot *me )
{
	me->ClearLookAt();
	me->ResetStuckMonitor();
}
