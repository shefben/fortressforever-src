//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_fetch_bomb.h" // Assuming this is the header for FetchBombState
#include "../ff_bot.h"
#include "../ff_bot_manager.h" // For TheFFBots()
#include "../../ff_player.h" // For CFFPlayer (potentially used by CFFBot)
#include "../../../shared/ff/weapons/ff_weapon_base.h" // For FFWeaponID (used in CFFBot)
// #include "../../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../../shared/ff/ff_gamerules.h" // For CFFGameRules or FFGameRules() (potentially used)
#include "../ff_gamestate.h" // For FFGameState (used in CFFBot)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
/**
 * Move to the bomb on the floor and pick it up
 */
void FetchBombState::OnEnter( CFFBot *me )
{
	me->DestroyPath();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to the bomb on the floor and pick it up
 */
void FetchBombState::OnUpdate( CFFBot *me )
{
	if (me->HasC4()) // TODO: Update HasC4 for FF if it's different (e.g. different item name)
	{
		me->PrintIfWatched( "I picked up the bomb\n" );
		me->Idle();
		return;
	}


	CBaseEntity *bomb = TheFFBots()->GetLooseBomb();
	if (bomb)
	{
		if (!me->HasPath())
		{
			// build a path to the bomb
			if (me->ComputePath( bomb->GetAbsOrigin() ) == false)
			{
				me->PrintIfWatched( "Fetch bomb pathfind failed\n" );

				// go Hunt instead of Idle to prevent continuous re-pathing to inaccessible bomb
				me->Hunt();
				return;
			}
		}
	}
	else
	{
		// someone picked up the bomb
		me->PrintIfWatched( "Someone else picked up the bomb.\n" );
		me->Idle();
		return;
	}

	// look around
	me->UpdateLookAround();

	if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
		me->Idle();
}
