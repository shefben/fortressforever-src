//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the bot state for carrying a flag.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_carry_flag.h"
#include "../ff_bot.h" // For CFFBot
#include "../ff_bot_manager.h" // For LuaObjectivePoint and TheFFBots()
#include "../ff_gamerules.h"   // For FFGameRules() if needed

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
CarryFlagState::CarryFlagState(void)
{
	m_captureTargetPoint = NULL;
	m_isAtCapturePoint = false;
}

//--------------------------------------------------------------------------------------------------------------
void CarryFlagState::SetCaptureTarget(const CFFBotManager::LuaObjectivePoint* capturePoint)
{
	m_captureTargetPoint = capturePoint;
}

//--------------------------------------------------------------------------------------------------------------
void CarryFlagState::OnEnter( CFFBot *me )
{
	if (!me->HasEnemyFlag())
	{
		me->PrintIfWatched("CarryFlagState: Entered state but not carrying enemy flag! Idling.\n");
		me->Idle();
		return;
	}

	if (!m_captureTargetPoint)
	{
		me->PrintIfWatched("CarryFlagState: No capture target point set! Idling.\n");
		me->Idle();
		return;
	}

	me->PrintIfWatched("CarryFlagState: Carrying enemy flag, moving to capture point '%s' at (%.f, %.f, %.f).\n",
		m_captureTargetPoint->name, m_captureTargetPoint->position.x, m_captureTargetPoint->position.y, m_captureTargetPoint->position.z);

	me->MoveTo(m_captureTargetPoint->position, FASTEST_ROUTE); // Move quickly with the flag
	me->SetTask(CFFBot::TaskType::CAPTURE_FLAG, m_captureTargetPoint->m_entity); // Conceptual task

	m_isAtCapturePoint = false;
	m_repathTimer.Start(RandomFloat(2.0f, 3.0f));
	// m_captureInteractionTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
void CarryFlagState::OnUpdate( CFFBot *me )
{
	// Ensure bot is still carrying the enemy flag
	if (!me->HasEnemyFlag())
	{
		me->PrintIfWatched("CarryFlagState: No longer carrying enemy flag (dropped or captured by other means). Idling.\n");
		me->Idle();
		return;
	}

	if (!m_captureTargetPoint)
	{
		me->PrintIfWatched("CarryFlagState: Capture target point became NULL! Idling.\n");
		me->Idle();
		return;
	}

	// Ensure capture point is still active
	if (m_captureTargetPoint->m_state != LUA_OBJECTIVE_ACTIVE)
	{
		me->PrintIfWatched("CarryFlagState: Capture point '%s' is no longer active. Idling to re-evaluate.\n", m_captureTargetPoint->name);
		me->Idle(); // Re-evaluate in IdleState, might find another capture point or logic.
		return;
	}

	if (me->IsStuck())
	{
		me->PrintIfWatched("CarryFlagState: Stuck while trying to reach capture point '%s'. Idling.\n", m_captureTargetPoint->name);
		me->Idle(); // Simple handling
		return;
	}

	// Handle enemy encounters
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible() && me->IsAbleToAttack())
	{
		// FF_TODO_AI_BEHAVIOR: Flag carrier might prefer to run/evade rather than fight unless cornered.
		// For now, standard attack logic.
		me->Attack(me->GetBotEnemy()); // This will transition to AttackState
		return;
	}


	if (!m_isAtCapturePoint)
	{
		float captureRadius = m_captureTargetPoint->radius > 0 ? m_captureTargetPoint->radius : 100.0f; // Radius for cap zone
		if ((me->GetAbsOrigin() - m_captureTargetPoint->position).IsLengthLessThan(captureRadius))
		{
			m_isAtCapturePoint = true;
			me->Stop();
			me->PrintIfWatched("CarryFlagState: Arrived at capture point '%s'. Attempting capture.\n", m_captureTargetPoint->name);
			// FF_TODO_GAME_MECHANIC: Simulate capture. This might involve a timer, a "use" action, or just presence.
			// For now, assume presence is enough and it's instant for testing.
			// In a real game, a "flag_captured" event would come from Lua/game, which then updates objective states.

			CFFInfoScript* pCarriedFlag = me->GetCarriedFlag(); // Get the flag entity we are carrying
			if (pCarriedFlag)
			{
				// Simulate successful capture:
				me->PrintIfWatched("CarryFlagState: Successfully captured flag '%s' at '%s'!\n",
					pCarriedFlag->GetEntityNameAsCStr() ? pCarriedFlag->GetEntityNameAsCStr() : "flag",
					m_captureTargetPoint->name);

				// Notify self that flag is "dropped" (because it's captured, no longer carried)
				me->NotifyDroppedFlag(pCarriedFlag);

				// Manually update the LuaObjectivePoint for the *flag* to reflect it's now neutral/returned (if game doesn't auto-event this for bots)
				// This is a bit of a hack; ideally, a game event "flag_captured" would trigger this in CFFBotManager::OnLuaEvent
				// const CFFBotManager::LuaObjectivePoint* flagObjective = TheFFBots()->GetLuaObjectivePointByEntity(pCarriedFlag); // Need GetLuaObjectivePointByEntity
				// if (flagObjective) {
					// This direct modification is not ideal. Prefer game events.
					// TheFFBots()->UpdateObjectiveState(flagObjective->m_id, LUA_OBJECTIVE_INACTIVE or LUA_OBJECTIVE_COMPLETED, FF_TEAM_NEUTRAL);
				// }
			}

			me->Idle(); // Flag captured, go idle.
			return;
		}
		else
		{
			// Not at capture point yet
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
			{
				if (m_repathTimer.IsElapsed())
				{
					me->PrintIfWatched("CarryFlagState: Path failed or ended prematurely for capture point '%s'. Retrying path.\n", m_captureTargetPoint->name);
					me->MoveTo(m_captureTargetPoint->position, FASTEST_ROUTE);
					m_repathTimer.Start(RandomFloat(3.0f, 5.0f));
				}
			}
		}
	}
	else // Is at capture point, waiting for capture or defending
	{
		// Logic from previous block should handle instant capture for now.
		// If capture took time: me->SetLookAt("Capture Point", m_captureTargetPoint->position, PRIORITY_HIGH);
		// Check m_captureInteractionTimer.IsElapsed() then do stuff.
		// For now, since capture is instant, this block might not be reached if logic flows correctly.
		me->PrintIfWatched("CarryFlagState: At capture point, but logic flaw? Should have captured or idled. Idling.\n");
		me->Idle();
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void CarryFlagState::OnExit( CFFBot *me )
{
	me->PrintIfWatched("CarryFlagState: Exiting state. Target was '%s'.\n", m_captureTargetPoint ? m_captureTargetPoint->name : "UNKNOWN");
	// If bot still thinks it's carrying the flag but is exiting this state prematurely (e.g. killed),
	// the game's flag drop event should handle calling NotifyDroppedFlag on the bot.
	// If bot successfully captured, NotifyDroppedFlag was already called from OnUpdate.
	m_captureTargetPoint = NULL;
	m_isAtCapturePoint = false;
	me->Stop();
	me->ClearLookAt();
}
