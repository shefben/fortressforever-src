//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the bot state for capturing Lua-defined objectives.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_capture_objective.h"
#include "../ff_bot_manager.h" // For TheFFBots() to potentially check objective status (future)
#include "../ff_gamerules.h"   // For FFGameRules() if needed for game state checks

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
CaptureObjectiveState::CaptureObjectiveState(void)
{
	m_targetObjective = NULL;
	m_isAtObjective = false;
}

//--------------------------------------------------------------------------------------------------------------
void CaptureObjectiveState::SetObjective(const CFFBotManager::LuaObjectivePoint* objective)
{
	m_targetObjective = objective;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is entering the 'CaptureObjective' state.
 */
void CaptureObjectiveState::OnEnter( CFFBot *me )
{
	if (!m_targetObjective)
	{
		me->PrintIfWatched("CaptureObjectiveState: No target objective set!\n");
		me->Idle(); // No objective, so go back to idle
		return;
	}

	me->PrintIfWatched("CaptureObjectiveState: Moving to capture/evaluate objective '%s' (type %d) at (%.f, %.f, %.f)\n",
		m_targetObjective->name, m_targetObjective->type, m_targetObjective->position.x, m_targetObjective->position.y, m_targetObjective->position.z);

	me->MoveTo(m_targetObjective->position, SAFEST_ROUTE); // Or FASTEST_ROUTE depending on bot personality/situation

	m_isAtObjective = false;
	// m_isDefending = false; // This will be determined by currentOwnerTeam in OnUpdate
	m_checkObjectiveStatusTimer.Start(0.1f);
	m_repathTimer.Start(RandomFloat(2.0f, 3.0f));
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is attempting to capture an objective.
 */
void CaptureObjectiveState::OnUpdate( CFFBot *me )
{
	if (!m_targetObjective)
	{
		me->PrintIfWatched("CaptureObjectiveState: Target objective became NULL! Idling.\n");
		me->Idle();
		return;
	}

	// Check if objective became inactive or its state is no longer suitable for capture
	if (m_targetObjective->m_state != LUA_OBJECTIVE_ACTIVE)
	{
		if (m_targetObjective->m_state == LUA_OBJECTIVE_COMPLETED && m_targetObjective->currentOwnerTeam == me->GetTeamNumber())
		{
			me->PrintIfWatched("CaptureObjectiveState: Objective '%s' was already completed by our team. Idling.\n", m_targetObjective->name);
		}
		else if (m_targetObjective->m_state == LUA_OBJECTIVE_FAILED)
		{
			me->PrintIfWatched("CaptureObjectiveState: Objective '%s' has failed. Idling.\n", m_targetObjective->name);
		}
		else
		{
			me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is no longer active (state: %d). Idling.\n", m_targetObjective->name, m_targetObjective->m_state);
		}
		me->Idle();
		return;
	}

	// FF_TODO_LUA: Potentially add a direct check here if TheFFBots() could quickly verify currentOwnerTeam
	// if it's faster than waiting for m_checkObjectiveStatusTimer for critical changes.

	if (me->IsStuck())
	{
		me->PrintIfWatched("CaptureObjectiveState: Stuck while trying to reach objective '%s'. Idling.\n", m_targetObjective->name);
		me->Idle(); // Simple handling for now
		return;
	}

	if (!m_isAtObjective)
	{
		// Check if we've reached the objective
		// Using a radius check based on the objective's radius (if available) or a default.
		float captureRadius = m_targetObjective->radius > 0 ? m_targetObjective->radius : 75.0f; // Default 75 units
		if ((me->GetAbsOrigin() - m_targetObjective->position).IsLengthLessThan(captureRadius))
		{
			m_isAtObjective = true;
			me->Stop(); // Stop moving
			me->PrintIfWatched("CaptureObjectiveState: Arrived at objective '%s'. Evaluating status.\n", m_targetObjective->name);

			// FF_TODO_GAME_MECHANIC: Actual capture time should come from objective data or game rules // REMOVED CAPTURE TIMER START
			// const float captureDuration = 5.0f; // Placeholder capture time // REMOVED
			// m_captureTimer.Start(captureDuration); // REMOVED
			m_checkObjectiveStatusTimer.Start(0.1f); // Force immediate status check

			// FF_TODO_AI_BEHAVIOR: Play capturing animation or gesture
			me->SetLookAt("Objective Area", m_targetObjective->position, PRIORITY_MEDIUM); // Changed from "Capturing Objective" and PRIORITY_HIGH
		}
		else
		{
			// Not at objective yet, update path movement
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
			{
				// Path failed or completed but not at objective (should be caught by radius check)
				// FF_TODO_SCOUT: If pathing to m_targetObjective and IsStuck() or path fails, a Scout could TryDoubleJump() to see if it overcomes a small obstacle or gap towards the objective.
				if (m_repathTimer.IsElapsed())
				{
					me->PrintIfWatched("CaptureObjectiveState: Path failed or ended prematurely for '%s'. Retrying path.\n", m_targetObjective->name);
					me->MoveTo(m_targetObjective->position, SAFEST_ROUTE);
					m_repathTimer.Start(RandomFloat(3.0f, 5.0f));
				}
			}
		}
	}
	// m_isAtObjective is true if we've reached this point in OnUpdate due to the return statement in the if block above.

	if( m_isAtObjective )
	{
		// FF_LUA_FLAGS: Handle flag pickup if this objective is a flag
		if (m_targetObjective && m_targetObjective->type == 2) // Type 2 is Item_Flag
		{
			// Ensure the flag is still available for pickup (ACTIVE or DROPPED)
			// and not already carried by someone else (currentOwnerTeam should be NEUTRAL for uncarried flags)
			if ((m_targetObjective->m_state == LUA_OBJECTIVE_ACTIVE || m_targetObjective->m_state == LUA_OBJECTIVE_DROPPED) &&
				m_targetObjective->currentOwnerTeam == FF_TEAM_NEUTRAL)
			{
				me->PrintIfWatched("CaptureObjectiveState: Attempting to pick up flag: %s\n", m_targetObjective->name);

				// Simulate pickup action. In a real scenario, game events would confirm this.
				// For now, we directly notify the bot.
				CFFInfoScript* pFlagEnt = dynamic_cast<CFFInfoScript*>(m_targetObjective->m_entity.Get());
				if (pFlagEnt)
				{
					// Determine if it's an enemy flag or own flag based on teamAffiliation
					int flagType = (me->GetTeamNumber() == m_targetObjective->teamAffiliation) ? 2 /*own_flag*/ : 1 /*enemy_flag*/;
					me->NotifyPickedUpFlag(pFlagEnt, flagType);
				}

				// After picking up, go Idle. IdleState will then decide to carry it to a capture point if it's an enemy flag.
				me->Idle();
				return;
			}
			else if (m_targetObjective->currentOwnerTeam != FF_TEAM_NEUTRAL && m_targetObjective->currentOwnerTeam != me->GetTeamNumber())
			{
				me->PrintIfWatched("CaptureObjectiveState: Flag '%s' is already carried by team %d. Idling.\n", m_targetObjective->name, m_targetObjective->currentOwnerTeam);
				me->Idle(); // Flag already taken by someone else (not us)
				return;
			}
			// If it's our team carrying it, or state is not ACTIVE/DROPPED, the general logic below might apply or lead to idling.
		}

		// Primary action when at the objective (for non-flag types, or flags that couldn't be picked up): check status and react.
		if (m_checkObjectiveStatusTimer.IsElapsed())
		{
			// Re-check objective state, as it might change while on point
			if (m_targetObjective->m_state != LUA_OBJECTIVE_ACTIVE)
			{
				if (m_targetObjective->m_state == LUA_OBJECTIVE_COMPLETED && m_targetObjective->currentOwnerTeam == me->GetTeamNumber())
				{
					me->PrintIfWatched("CaptureObjectiveState: Objective '%s' successfully completed by our team while at point. Idling.\n", m_targetObjective->name);
				}
				else if (m_targetObjective->m_state == LUA_OBJECTIVE_FAILED)
				{
					me->PrintIfWatched("CaptureObjectiveState: Objective '%s' failed while at point. Idling.\n", m_targetObjective->name);
				}
				else
				{
					me->PrintIfWatched("CaptureObjectiveState: Objective '%s' became non-active (state: %d) while at point. Idling.\n", m_targetObjective->name, m_targetObjective->m_state);
				}
				me->Idle();
				return;
			}

			int currentOwner = m_targetObjective->currentOwnerTeam;
			if (currentOwner == me->GetTeamNumber())
			{
				me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is NOW controlled by our team (or confirmed held).\n", m_targetObjective->name);
				me->SetTask(CFFBot::TaskType::DEFEND_LUA_OBJECTIVE, m_targetObjective);
				me->DefendObjective(m_targetObjective); // Transition to DefendObjectiveState
				return;
			}
			else // Objective is neutral or enemy controlled
			{
				me->PrintIfWatched("CaptureObjectiveState: Still attempting to capture/contest objective '%s' (Owner: %d, My Team: %d).\n",
					m_targetObjective->name, currentOwner, me->GetTeamNumber());
				me->SetTask(CFFBot::TaskType::CAPTURE_LUA_OBJECTIVE, m_targetObjective);

				// Bot remains on point, game logic handles actual capture.
				// FF_TODO_LUA: Ensure Lua side updates currentOwnerTeam based on game events/triggers.
				// FF_TODO_GAME_MECHANIC: Define how capturing actually works (e.g. simple timer on point, or needs 'use' key).
				// Bot should look around for threats.
				me->SetLookAt("Objective Area", m_targetObjective->position, PRIORITY_MEDIUM, 0.5f, true);
			}
			m_checkObjectiveStatusTimer.Start(1.0f); // Re-check status in 1 second
		}

		// Engineer behavior: If AT an objective that is NEUTRAL or ENEMY (so still in capture logic), consider building.
		// If it becomes friendly, the above logic will transition to Idle/Defend.
		// This is a secondary action after status check.
		if (m_targetObjective->currentOwnerTeam == me->GetTeamNumber() && me->IsEngineer() && !me->HasSentry())
		{
			// Check if enough time has passed since last build attempt or if conditions are right
			// This is a simplified check; more advanced logic would be in Idle or a dedicated Engineer state.
			if (me->GetStateTime() > 5.0f) // Example: Been in this state trying to cap/defend for 5s
			{
				Vector buildPos = m_targetObjective->position + Vector(RandomFloat(-150, 150), RandomFloat(-150,150), 0);
				me->PrintIfWatched("Engineer in CaptureObjectiveState: Decided to build sentry near friendly objective '%s'.\n", m_targetObjective->name);
				me->TryToBuildSentry(&buildPos);
				// TryToBuildSentry will change state if successful
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is exiting the 'CaptureObjective' state.
 */
void CaptureObjectiveState::OnExit( CFFBot *me )
{
	me->PrintIfWatched("CaptureObjectiveState: Exiting state for objective '%s'.\n", m_targetObjective ? m_targetObjective->name : "UNKNOWN");
	// m_targetObjective = NULL; // Don't nullify here, IdleState might want to know what we were doing. CFFBot::SetTask clears task entity.
	m_isAtObjective = false;
	m_isDefending = false;
	me->Stop(); // Ensure bot stops moving if it was
	me->ClearLookAt();
	// FF_TODO_AI_BEHAVIOR: Stop capturing animation
}

[end of mp/src/game/server/ff/bot/states/ff_bot_capture_objective.cpp]
