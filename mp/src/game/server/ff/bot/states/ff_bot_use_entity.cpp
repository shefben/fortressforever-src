//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_use_entity.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"

// Local bot utility headers
#include "../bot_constants.h"  // For PriorityType, BotTaskType, TEAM_CT, etc.
#include "../bot_profile.h"    // For GetProfile() (potentially used by CFFBot methods)
#include "../bot_util.h"       // For PrintIfWatched (if used in future)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/**
 * Face the entity and "use" it
 * NOTE: This state assumes we are standing in range of the entity to be used, with no obstructions.
 */
void UseEntityState::OnEnter( CFFBot *me )
{
	// Intentionally_empty
}

void UseEntityState::OnUpdate( CFFBot *me )
{
	if (!me || !m_entity.Get() || !TheFFBots()) return; // Null checks

	// in the very rare situation where two or more bots "used" a hostage at the same time,
	// one bot will fail and needs to time out of this state
	const float useTimeout = 5.0f; // TODO_FF: This timeout was likely for CS hostage interaction, might not apply.
	if (gpGlobals->curtime - me->GetStateTimestamp() > useTimeout) // Corrected timestamp check direction
	{
		me->Idle();
		return;
	}

	// look at the entity
	Vector pos = m_entity->EyePosition(); // If EyePosition doesn't exist, WorldSpaceCenter() or similar
	me->SetLookAt( "Use entity", pos, PRIORITY_HIGH );

	// if we are looking at the entity, "use" it and exit
	if (me->IsLookingAtPosition( pos ))
	{
		// TODO_FF: This is CS-specific hostage logic. Adapt for FF objectives or generic entity use.
		// if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES &&
		//	me->GetTeamNumber() == TEAM_CT &&
		//	me->GetTask() == CFFBot::BOT_TASK_COLLECT_HOSTAGES)
		// {
		//	me->IncreaseHostageEscortCount();
		// }

		me->UseEnvironment(); // This is a generic +USE command
		me->Idle();
	}
}

void UseEntityState::OnExit( CFFBot *me )
{
	if (!me) return; // Null check
	me->ClearLookAt();
	me->ResetStuckMonitor();
}
