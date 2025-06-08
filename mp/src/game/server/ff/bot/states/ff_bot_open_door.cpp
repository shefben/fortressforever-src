//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), April 2005

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h
#include "BasePropDoor.h"
#include "doors.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-------------------------------------------------------------------------------------------------
/**
 * Face the door and open it.
 * NOTE: This state assumes we are standing in range of the door to be opened, with no obstructions.
 */
void OpenDoorState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
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
void OpenDoorState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
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
	// bool isDoorMoving = false; // This variable is not used
	if ( m_funcDoor )
	{
		pos = m_funcDoor->WorldSpaceCenter();
		// isDoorMoving = m_funcDoor->m_toggle_state == TS_GOING_UP || m_funcDoor->m_toggle_state == TS_GOING_DOWN;
	}
	else if ( m_propDoor ) // Added else if to ensure one path is taken if m_funcDoor is null
	{
		pos = m_propDoor->WorldSpaceCenter();
		// isDoorMoving = m_propDoor->IsDoorOpening() || m_propDoor->IsDoorClosing();
	}
	else // No valid door entity
	{
		m_isDone = true; // Can't operate on a NULL door handle
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
void OpenDoorState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->ClearLookAt();
	me->ResetStuckMonitor();
}
