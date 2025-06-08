//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h
#include "../ff_nav_pathfind.h" // For FarAwayFromPositionFunctor (assuming it will be moved/adapted here)
#include "../ff_bot_manager.h" // For TheFFBots()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb. - FF_TODO_GAME_MECHANIC: This state is CS-specific and needs to be re-evaluated for FF objectives or removed.
 */
void EscapeFromBombState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: EscapeFromBombState::OnEnter - CS-specific logic removed/stubbed.\n" );
	// me->StandUp();
	// me->Run();
	// me->DestroyPath();
	// me->EquipKnife(); // FF_TODO_WEAPON_STATS: Adapt for FF melee/movement weapon
	me->Idle(); // Default to idle as escaping bomb is not applicable
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb. - FF_TODO_GAME_MECHANIC: This state is CS-specific.
 */
void EscapeFromBombState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: EscapeFromBombState::OnUpdate - CS-specific logic removed/stubbed.\n" );
	// const Vector *bombPos = me->GetGameState()->GetBombPosition(); // GetBombPosition is CS-specific

	// if (bombPos == NULL)
	// {
	// 	me->Idle();
	// 	return;
	// }

	// me->EquipKnife(); // FF_TODO_WEAPON_STATS: Adapt for FF melee/movement weapon
	// me->UpdateLookAround();

	// if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	// {
		// FF_TODO_AI_BEHAVIOR: The concept of "escaping a bomb" is CS-specific.
		// If FF has similar "danger zone" escape logic, this could be adapted.
		// FarAwayFromPositionFunctor func( *bombPos );
		// CNavArea *goalArea = FindMinimumCostArea( me->GetLastKnownArea(), func ); // FindMinimumCostArea may need to be available/adapted

		// if (goalArea)
		// {
		// 	me->ComputePath( goalArea->GetCenter(), FASTEST_ROUTE );
		// }
	// }
	me->Idle(); // Default to idle
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb. - FF_TODO_GAME_MECHANIC: This state is CS-specific.
 */
void EscapeFromBombState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "FF_TODO_GAME_MECHANIC: EscapeFromBombState::OnExit - CS-specific logic reviewed.\n" );
	// me->EquipBestWeapon(); // EquipBestWeapon needs FF logic
}
