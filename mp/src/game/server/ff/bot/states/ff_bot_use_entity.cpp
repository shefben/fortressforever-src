//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_use_entity.h" // Assuming this is the header for UseEntityState
#include "../ff_bot.h"
#include "../ff_bot_manager.h" // For TheFFBots() and scenario enums
#include "../../ff_player.h" // For CFFPlayer (potentially used by CFFBot)
#include "../../../shared/ff/weapons/ff_weapon_base.h" // For FFWeaponID (used in CFFBot)
// #include "../../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../../shared/ff/ff_gamerules.h" // For CFFGameRules or FFGameRules() (potentially used)
#include "../ff_gamestate.h" // For FFGameState (used in CFFBot)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


/**
 * Face the entity and "use" it
 * NOTE: This state assumes we are standing in range of the entity to be used, with no obstructions.
 */
void UseEntityState::OnEnter( CFFBot *me )
{
}

void UseEntityState::OnUpdate( CFFBot *me )
{
	// in the very rare situation where two or more bots "used" a hostage at the same time,
	// one bot will fail and needs to time out of this state
	const float useTimeout = 5.0f;
	if (me->GetStateTimestamp() - gpGlobals->curtime > useTimeout)
	{
		me->Idle();
		return;
	}

	// look at the entity
	Vector pos = m_entity->EyePosition();
	me->SetLookAt( "Use entity", pos, PRIORITY_HIGH );

	// if we are looking at the entity, "use" it and exit
	if (me->IsLookingAtPosition( pos ))
	{
		// TODO: Update for FF teams and scenarios if necessary
		if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES &&
			me->GetTeamNumber() == TEAM_CT &&
			me->GetTask() == CFFBot::COLLECT_HOSTAGES)
		{
			// we are collecting a hostage, assume we were successful - the update check will correct us if we weren't
			me->IncreaseHostageEscortCount();
		}

		me->UseEnvironment();
		me->Idle();
	}
}

void UseEntityState::OnExit( CFFBot *me )
{
	me->ClearLookAt();
	me->ResetStuckMonitor();
}
