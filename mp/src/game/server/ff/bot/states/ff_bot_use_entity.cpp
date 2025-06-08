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


/**
 * Face the entity and "use" it
 * NOTE: This state assumes we are standing in range of the entity to be used, with no obstructions.
 */
void UseEntityState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	// Potentially add a short timer here if needed for FF specific "use" actions
}

void UseEntityState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	// Ensure the entity we want to use still exists
	if ( m_entity.Get() == NULL )
	{
		me->Idle();
		return;
	}

	// in the very rare situation where two or more bots "used" an entity at the same time,
	// one bot might fail and needs to time out of this state
	const float useTimeout = 5.0f; // This timeout can be tuned
	// Corrected timestamp check: elapsed time should be compared to timeout
	if (gpGlobals->curtime - me->GetStateTimestamp() > useTimeout)
	{
		me->PrintIfWatched( "UseEntityState timed out.\n" );
		me->Idle();
		return;
	}

	// look at the entity
	Vector pos = m_entity->WorldSpaceCenter(); // Use WorldSpaceCenter for more reliable targeting of entities
	me->SetLookAt( "Use entity", pos, PRIORITY_HIGH );

	// if we are looking at the entity, "use" it and exit
	if (me->IsLookingAtPosition( pos ))
	{
		// FF_TODO_GAME_MECHANIC: If FF has specific logic tied to using certain entities (like info_ff_script for flags),
		// that logic might go here, or be handled by the Lua scripts themselves when the 'use' occurs.
		// The CS-specific hostage collection check has been removed.
		/*
		if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES && // This would be FF_SCENARIO_ITEM_SCRIPT or similar
			me->GetTeamNumber() == FF_TEAM_BLUE && // Example: Blue team for "rescuing" an objective item
			me->GetTask() == CFFBot::COLLECT_HOSTAGES) // This would be an FF-specific task like COLLECT_FLAG
		{
			// Example: me->PickedUpObjectiveItem(m_entity.Get());
		}
		*/

		me->UseEnvironment(); // This call presses the +USE button on the entity
		me->Idle(); // Assume using an entity is a quick action, then go idle to decide next steps
	}
}

void UseEntityState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->ClearLookAt();
	me->ResetStuckMonitor();
}
