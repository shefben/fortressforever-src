//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h
#include "../ff_bot_manager.h" // For TheFFBots()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
/**
 * Move to the bomb on the floor and pick it up - FF_TODO_GAME_MECHANIC: This state is CS-specific and needs to be re-evaluated or removed.
 * Could be adapted for "fetch flag" or other FF item pickup scenarios.
 */
void FetchBombState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: FetchBombState::OnEnter - CS-specific logic removed/stubbed.\n" );
	// me->DestroyPath();
	// FF_TODO_AI_BEHAVIOR: If adapting for a flag, bot might need to equip a certain weapon or change speed.
	me->Idle(); // Default to idle as fetching bomb is not applicable
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to the bomb on the floor and pick it up - FF_TODO_GAME_MECHANIC: This state is CS-specific.
 */
void FetchBombState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: FetchBombState::OnUpdate - CS-specific logic removed/stubbed.\n" );
	// if (me->HasC4()) // HasC4 is CS-specific
	// {
	// 	me->PrintIfWatched( "I picked up the bomb\n" ); // FF_TODO_AI_BEHAVIOR: Adapt for FF item
	// 	me->Idle();
	// 	return;
	// }

	// CBaseEntity *bomb = TheFFBots()->GetLooseBomb(); // GetLooseBomb is CS-specific
	// if (bomb)
	// {
	// 	if (!me->HasPath())
	// 	{
	// 		if (me->ComputePath( bomb->GetAbsOrigin() ) == false)
	// 		{
	// 			me->PrintIfWatched( "Fetch bomb pathfind failed\n" ); // FF_TODO_AI_BEHAVIOR: Adapt for FF item
	// 			me->Hunt(); // Fallback to Hunt
	// 			return;
	// 		}
	// 	}
	// }
	// else
	// {
	// 	me->PrintIfWatched( "Someone else picked up the bomb.\n" ); // FF_TODO_AI_BEHAVIOR: Adapt for FF item
	// 	me->Idle();
	// 	return;
	// }

	// me->UpdateLookAround();

	// if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	// 	me->Idle();
	me->Idle(); // Default to idle
}

// No OnExit for FetchBombState in original cs_bot_fetch_bomb.cpp
