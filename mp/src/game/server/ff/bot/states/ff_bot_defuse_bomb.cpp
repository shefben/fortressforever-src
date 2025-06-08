//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin defusing the bomb
 */
void DefuseBombState::OnEnter( CFFBot *me )
{
	me->Crouch();
	me->SetDisposition( CFFBot::SELF_DEFENSE );
	me->GetChatter()->Say( "DefusingBomb" );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Defuse the bomb
 */
void DefuseBombState::OnUpdate( CFFBot *me )
{
	const Vector *bombPos = me->GetGameState()->GetBombPosition();

	if (bombPos == NULL)
	{
		me->PrintIfWatched( "In Defuse state, but don't know where the bomb is!\n" );
		me->Idle();
		return;
	}

	// look at the bomb
	me->SetLookAt( "Defuse bomb", *bombPos, PRIORITY_HIGH );

	// defuse...
	me->UseEnvironment();

	if (gpGlobals->curtime - me->GetStateTimestamp() > 1.0f)
	{
		// if we missed starting the defuse, give up
		if (TheFFBots()->GetBombDefuser() == NULL)
		{
			me->PrintIfWatched( "Failed to start defuse, giving up\n" );
			me->Idle();
			return;
		}
		else if (TheFFBots()->GetBombDefuser() != me)
		{
			// if someone else got the defuse, give up
			me->PrintIfWatched( "Someone else started defusing, giving up\n" );
			me->Idle();
			return;
		}
	}

	// if bomb has been defused, give up
	if (!TheFFBots()->IsBombPlanted())
	{
		me->Idle();
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void DefuseBombState::OnExit( CFFBot *me )
{
	me->StandUp();
	me->ResetStuckMonitor();
	me->SetTask( CFFBot::SEEK_AND_DESTROY );
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE );
	me->ClearLookAt();
}
