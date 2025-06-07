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

	me->PrintIfWatched("CaptureObjectiveState: Moving to capture/evaluate objective '%s' at (%.f, %.f, %.f)\n",
		m_targetObjective->name, m_targetObjective->position.x, m_targetObjective->position.y, m_targetObjective->position.z);

	me->MoveTo(m_targetObjective->position, SAFEST_ROUTE); // Or FASTEST_ROUTE depending on bot personality/situation
	// Task will be set based on objective status once reached (CAPTURE_LUA_OBJECTIVE or DEFEND_LUA_OBJECTIVE)
	// me->SetTask(CFFBot::TaskType::CAPTURE_FLAG); // Commented out: FF_TODO_TASKS: Create a more generic CAPTURE_OBJECTIVE_LUA task

	m_isAtObjective = false;
	m_isDefending = false;
	// m_captureTimer.Invalidate(); // REMOVED: Will start when at objective
	m_checkObjectiveStatusTimer.Start(0.1f); // Check status quickly upon arrival or first update
	m_repathTimer.Start(RandomFloat(2.0f, 3.0f)); // Check path periodically
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is attempting to capture an objective.
 */
void CaptureObjectiveState::OnUpdate( CFFBot *me )
{
	if (!m_targetObjective)
	{
		me->PrintIfWatched("CaptureObjectiveState: Target objective became NULL!\n");
		me->Idle();
		return;
	}

	// FF_TODO_GAME_LOGIC: Check if objective is still valid/active/not captured by own team already
	// if (TheFFBots()->IsObjectiveCaptured(m_targetObjective, me->GetTeamNumber())) { me->Idle(); return; }

	if (me->IsStuck())
	{
		me->PrintIfWatched("CaptureObjectiveState: Stuck while trying to reach objective '%s'. Re-evaluating.\n", m_targetObjective->name);
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

			// FF_TODO_GAME_LOGIC: Actual capture time should come from objective data or game rules // REMOVED CAPTURE TIMER START
			// const float captureDuration = 5.0f; // Placeholder capture time // REMOVED
			// m_captureTimer.Start(captureDuration); // REMOVED
			m_checkObjectiveStatusTimer.Start(0.1f); // Force immediate status check

			// FF_TODO_ANIMATION: Play capturing animation or gesture
			me->SetLookAt("Objective Area", m_targetObjective->position, PRIORITY_MEDIUM); // Changed from "Capturing Objective" and PRIORITY_HIGH
		}
		else
		{
			// Not at objective yet, update path movement
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
			{
				// Path failed or completed but not at objective (should be caught by radius check)
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

	// Check for enemies first, even if at objective. This was moved up from the old else block.
	// It's already present before the `if (!m_isAtObjective)` block, so this is effectively a no-op here,
	// but the old else block is being entirely replaced. The new logic for m_isAtObjective true follows.

	if( m_isAtObjective ) // This 'if' is effectively the 'else' from before, ensuring we are at the point.
	{
		me->SetLookAt("Objective Area", m_targetObjective->position, PRIORITY_MEDIUM); // Look at the objective area

		if (m_checkObjectiveStatusTimer.IsElapsed())
		{
			if (!m_targetObjective->isActive)
			{
				me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is no longer active. Returning to Idle.\n", m_targetObjective->name);
				me->Idle();
				return;
			}

			int currentOwner = m_targetObjective->currentOwnerTeam;
			if (currentOwner == me->GetTeamNumber())
			{
				if (!m_isDefending)
				{
					me->PrintIfWatched("CaptureObjectiveState: Objective '%s' is held by our team. Defending.\n", m_targetObjective->name);
					m_isDefending = true;
					me->SetTask(CFFBot::TaskType::DEFEND_LUA_OBJECTIVE, m_targetObjective);

					// Engineer behavior: If defending, consider building a sentry
					if (me->IsEngineer() && !me->HasSentry())
					{
						// Build near the objective. Find a slightly offset position.
						// TODO: More sophisticated placement logic needed (e.g., check LoS, valid nav, etc.)
						Vector buildPos = m_targetObjective->position + Vector(RandomFloat(-150, 150), RandomFloat(-150,150), 0);
						me->PrintIfWatched("Engineer in CaptureObjectiveState: Decided to build sentry near friendly objective '%s'.\n", m_targetObjective->name);
						me->TryToBuildSentry(&buildPos);
						return; // Transitioned to BuildSentryState
					}
				}
				// Bot will stay in this state to defend. Could add logic to roam around point, look for enemies etc.
				// For now, it just stays and re-checks status.
			}
			else // Objective is neutral or enemy controlled
			{
				if (m_isDefending) // It was ours, but now it's not
				{
					me->PrintIfWatched("CaptureObjectiveState: Objective '%s' lost to team %d!\n", m_targetObjective->name, currentOwner);
					m_isDefending = false;
				}

				if (currentOwner == FF_TEAM_NEUTRAL)
				{
					me->PrintIfWatched("CaptureObjectiveState: Attempting to capture neutral objective '%s'.\n", m_targetObjective->name);
				}
				else
				{
					me->PrintIfWatched("CaptureObjectiveState: Attempting to capture objective '%s' from enemy team %d.\n", m_targetObjective->name, currentOwner);
				}
				me->SetTask(CFFBot::TaskType::CAPTURE_LUA_OBJECTIVE, m_targetObjective);
				// FF_TODO_GAME_LOGIC: Here, the bot would need to "interact" or "stand on point"
				// for a duration to cause currentOwnerTeam to change in the Lua environment.
				// Since we are just reading placeholder data, this state will persist until
				// the Lua data is externally changed, an enemy appears, or objective becomes inactive.
			}
			m_checkObjectiveStatusTimer.Start(1.0f); // Check again in 1 second
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
	// FF_TODO_ANIMATION: Stop capturing animation
}

[end of mp/src/game/server/ff/bot/states/ff_bot_capture_objective.cpp]
