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

	// Check if objective became inactive
	if (!m_targetObjective->isActive)
	{
		me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is no longer active. Idling.\n", m_targetObjective->name);
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
		// Primary action when at the objective: check status and react.
		if (m_checkObjectiveStatusTimer.IsElapsed())
		{
			// Re-check isActive, as it might change while on point
			if (!m_targetObjective->isActive)
			{
				me->PrintIfWatched("CaptureObjectiveState: Objective '%s' became inactive while at point. Idling.\n", m_targetObjective->name);
				me->Idle();
				return;
			}

			int currentOwner = m_targetObjective->currentOwnerTeam;
			if (currentOwner == me->GetTeamNumber())
			{
				me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is now controlled by our team. Holding briefly.\n", m_targetObjective->name);
				me->SetTask(CFFBot::TaskType::DEFEND_LUA_OBJECTIVE, m_targetObjective);
				// FF_TODO_AI_BEHAVIOR: Transition to a dedicated DefendObjectiveState or implement more robust holding behavior.
				// For now, just wait a bit then Idle. This makes m_isDefending member less critical.
				m_checkObjectiveStatusTimer.Start(RandomFloat(3.0f, 5.0f)); // Hold for a few seconds
				if (m_checkObjectiveStatusTimer.GetElapsedTime() > 0.1f) { // Ensure it's not the first frame of holding
					me->Idle(); // After holding, go idle to re-evaluate overall situation.
					return;
				}
			}
			else // Objective is neutral or enemy controlled
			{
				me->PrintIfWatched("CaptureObjectiveState: Attempting to capture/contest '%s' (type %d) currently owned by team %d.\n",
					m_targetObjective->name, m_targetObjective->type, currentOwner);
				me->SetTask(CFFBot::TaskType::CAPTURE_LUA_OBJECTIVE, m_targetObjective);

				// Bot remains on point, game logic handles actual capture.
				// FF_TODO_LUA: Ensure Lua side updates currentOwnerTeam based on game events/triggers.
				// FF_TODO_GAME_MECHANIC: Define how capturing actually works (e.g. simple timer on point, or needs 'use' key).
				// Bot should look around for threats.
				me->SetLookAt("Objective Area", m_targetObjective->position, PRIORITY_MEDIUM, 0.5f, true);
			}
			m_checkObjectiveStatusTimer.Start(1.0f); // Re-check status in 1 second
		}

		// Engineer behavior: If defending (already confirmed it's ours), consider building.
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
