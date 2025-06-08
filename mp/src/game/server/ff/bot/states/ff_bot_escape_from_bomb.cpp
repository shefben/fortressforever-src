//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_escape_from_bomb.h" // Assuming this is the header for EscapeFromBombState
#include "../ff_bot.h"
#include "../ff_bot_manager.h" // For TheFFBots() (potentially used)
#include "../../ff_player.h" // For CFFPlayer (potentially used by CFFBot)
#include "../../../shared/ff/weapons/ff_weapon_base.h" // For FFWeaponID (used in CFFBot)
// #include "../../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../../shared/ff/ff_gamerules.h" // For CFFGameRules or FFGameRules() (potentially used)
#include "../ff_gamestate.h" // For FFGameState (used in CFFBot)
#include "nav_area.h" // For CNavArea, FindMinimumCostArea, FarAwayFromPositionFunctor

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb.
 */
void EscapeFromBombState::OnEnter( CFFBot *me )
{
	me->StandUp();
	me->Run();
	me->DestroyPath();
	me->EquipKnife();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb.
 */
void EscapeFromBombState::OnUpdate( CFFBot *me )
{
	const Vector *bombPos = me->GetGameState()->GetBombPosition();

	// if we don't know where the bomb is, we shouldn't be in this state
	if (bombPos == NULL)
	{
		me->Idle();
		return;
	}

	// grab our knife to move quickly
	me->EquipKnife();

	// look around
	me->UpdateLookAround();

	if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
	{
		// we have no path, or reached the end of one - create a new path far away from the bomb
		FarAwayFromPositionFunctor func( *bombPos );
		CNavArea *goalArea = FindMinimumCostArea( me->GetLastKnownArea(), func );

		// if this fails, we'll try again next time
		if (goalArea) // Ensure goalArea is not null before using
		{
			me->ComputePath( goalArea->GetCenter(), FASTEST_ROUTE );
		}
		else
		{
			// Fallback if no area found (should ideally not happen if nav mesh is connected)
			me->Idle();
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Escape from the bomb.
 */
void EscapeFromBombState::OnExit( CFFBot *me )
{
	me->EquipBestWeapon();
}
