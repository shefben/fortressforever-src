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
 * Begin defusing the bomb - FF_TODO_GAME_MECHANIC: This state is CS-specific and needs to be re-evaluated for FF objectives, or removed.
 */
void DefuseBombState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: DefuseBombState::OnEnter - CS-specific logic removed/stubbed.\n" );
	// me->Crouch();
	// me->SetDisposition( CFFBot::SELF_DEFENSE ); // Changed CCSBot to CFFBot
	// me->GetChatter()->Say( "DefusingBomb" ); // FF_TODO_AI_BEHAVIOR: FF Chatter equivalent
	me->Idle(); // Default to idle as defusal is not applicable
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Defuse the bomb - FF_TODO_GAME_MECHANIC: This state is CS-specific.
 */
void DefuseBombState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: DefuseBombState::OnUpdate - CS-specific logic removed/stubbed.\n" );
	// const Vector *bombPos = me->GetGameState()->GetBombPosition(); // GetBombPosition is CS-specific

	// if (bombPos == NULL)
	// {
	// 	me->PrintIfWatched( "In Defuse state, but don't know where the bomb is!\n" );
	// 	me->Idle();
	// 	return;
	// }

	// me->SetLookAt( "Defuse bomb", *bombPos, PRIORITY_HIGH );
	// me->UseEnvironment(); // This is generic, but context is CS-specific

	// if (gpGlobals->curtime - me->GetStateTimestamp() > 1.0f)
	// {
	// 	if (TheFFBots()->GetBombDefuser() == NULL) // GetBombDefuser is CS-specific
	// 	{
	// 		me->PrintIfWatched( "Failed to start defuse, giving up\n" );
	// 		me->Idle();
	// 		return;
	// 	}
	// 	else if (TheFFBots()->GetBombDefuser() != me) // GetBombDefuser is CS-specific
	// 	{
	// 		me->PrintIfWatched( "Someone else started defusing, giving up\n" );
	// 		me->Idle();
	// 		return;
	// 	}
	// }

	// if (TheFFBots() && !TheFFBots()->IsBombPlanted()) // IsBombPlanted is CS-specific
	// {
	// 	me->Idle();
	// 	return;
	// }
	me->Idle(); // Default to idle
}

//--------------------------------------------------------------------------------------------------------------
void DefuseBombState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: DefuseBombState::OnExit - CS-specific logic reviewed.\n" );
	me->StandUp();
	me->ResetStuckMonitor();
	// me->SetTask( CFFBot::SEEK_AND_DESTROY ); // SEEK_AND_DESTROY is a generic task
	// me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // ENGAGE_AND_INVESTIGATE is generic
	me->ClearLookAt();
}
