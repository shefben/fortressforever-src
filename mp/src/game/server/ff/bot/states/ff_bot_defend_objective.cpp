//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the bot state for defending a Lua-defined objective.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_defend_objective.h"
#include "../ff_bot.h" // For CFFBot
#include "../ff_bot_manager.h" // For LuaObjectivePoint and TheFFBots()
#include "../nav_mesh.h" // For TheNavMesh

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
DefendObjectiveState::DefendObjectiveState(void)
{
	m_objectiveToDefend = NULL;
	m_isAtDefenseSpot = false;
	m_defenseSpot = vec3_origin;
}

//--------------------------------------------------------------------------------------------------------------
void DefendObjectiveState::SetObjectiveToDefend(const CFFBotManager::LuaObjectivePoint* pObjective)
{
	m_objectiveToDefend = pObjective;
}

//--------------------------------------------------------------------------------------------------------------
const char *DefendObjectiveState::GetName( void ) const
{
	return "DefendObjective";
}

//--------------------------------------------------------------------------------------------------------------
void DefendObjectiveState::OnEnter( CFFBot *me )
{
	if (!m_objectiveToDefend)
	{
		me->PrintIfWatched("DefendObjectiveState: Objective to defend is NULL! Idling.\n");
		me->Idle();
		return;
	}

	me->PrintIfWatched("DefendObjectiveState: Entering state to defend objective '%s' (type %d).\n",
		m_objectiveToDefend->name, m_objectiveToDefend->type);

	// FF_TODO_AI_BEHAVIOR: Find a good defense spot near m_objectiveToDefend.
	// This could involve checking nearby nav areas for good cover, visibility to approaches, etc.
	// For now, m_defenseSpot can be m_objectiveToDefend->position or a slight offset.
	// Consider a small random offset to prevent bots clumping on the exact objective center.
	m_defenseSpot = m_objectiveToDefend->position + Vector(RandomFloat(-50, 50), RandomFloat(-50, 50), 0);

	// Try to find a nav area near this spot.
	CNavArea *defenseNavArea = TheNavMesh->GetNearestNavArea(m_defenseSpot, true, 200.0f, true, me->GetTeamNumber());
	if (defenseNavArea)
	{
		m_defenseSpot = defenseNavArea->GetCenter(); // Snap to nav area center for pathing
	}
	else
	{
		me->PrintIfWatched("DefendObjectiveState: Could not find a valid nav area near defense spot for '%s'. Using objective position directly.\n", m_objectiveToDefend->name);
		m_defenseSpot = m_objectiveToDefend->position; // Fallback
	}

	me->PrintIfWatched("DefendObjectiveState: Moving to defense spot at (%.f, %.f, %.f) for objective '%s'.\n",
		m_defenseSpot.x, m_defenseSpot.y, m_defenseSpot.z, m_objectiveToDefend->name);

	me->MoveTo(m_defenseSpot, SAFEST_ROUTE); // Or a specific "defensive approach" route type if available
	me->SetTask(CFFBot::TaskType::DEFEND_LUA_OBJECTIVE, m_objectiveToDefend);

	m_isAtDefenseSpot = false;
	m_scanTimer.Start(RandomFloat(1.5f, 3.0f));
	m_repathTimer.Start(RandomFloat(2.0f, 3.0f));
}

//--------------------------------------------------------------------------------------------------------------
void DefendObjectiveState::OnUpdate( CFFBot *me )
{
	if (!m_objectiveToDefend)
	{
		me->PrintIfWatched("DefendObjectiveState: Objective became NULL! Idling.\n");
		me->Idle();
		return;
	}

	// Check if objective is still valid for defense by this bot
	if (m_objectiveToDefend->m_state != LUA_OBJECTIVE_ACTIVE || // Should be active to be defended
		m_objectiveToDefend->currentOwnerTeam != me->GetTeamNumber()) // And owned by our team
	{
		me->PrintIfWatched("DefendObjectiveState: Objective '%s' no longer needs defense (state: %d, owner: %d). Idling.\n",
			m_objectiveToDefend->name, m_objectiveToDefend->m_state, m_objectiveToDefend->currentOwnerTeam);
		me->Idle();
		return;
	}

	// Handle enemy encounters
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible() && me->IsAbleToAttack())
	{
		me->Attack(me->GetBotEnemy()); // Transition to AttackState
		return;
	}

	if (!m_isAtDefenseSpot)
	{
		if (me->IsStuck())
		{
			me->PrintIfWatched("DefendObjectiveState: Stuck while moving to defense spot for '%s'. Idling.\n", m_objectiveToDefend->name);
			me->Idle(); // Simple stuck handling
			return;
		}

		if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
		{
			// Path failed or completed but not at goal (IsAtGoal will catch exact arrival)
			if (m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("DefendObjectiveState: Path failed or ended prematurely for defense spot for '%s'. Retrying path.\n", m_objectiveToDefend->name);
				me->MoveTo(m_defenseSpot, SAFEST_ROUTE);
				m_repathTimer.Start(RandomFloat(3.0f, 5.0f));
			}
		}

		// Check if we've reached the defense spot
		if ((me->GetAbsOrigin() - m_defenseSpot).IsLengthLessThan(50.0f)) // Use a small radius for "at spot"
		{
			m_isAtDefenseSpot = true;
			me->StopMovement(); // Explicitly stop, MoveTo might have completed.
			me->PrintIfWatched("DefendObjectiveState: Arrived at defense spot for '%s'.\n", m_objectiveToDefend->name);
			m_scanTimer.Start(0.1f); // Scan immediately upon arrival
		}
	}

	if (m_isAtDefenseSpot)
	{
		// At defense spot, look around and perform class-specific actions
		if (m_scanTimer.IsElapsed())
		{
			// Simple look around logic
			QAngle currentAngles = me->GetAbsAngles();
			currentAngles.y += RandomFloat(-90.0f, 90.0f);
			me->SetLookAngles(currentAngles.y, currentAngles.p); // SetIdealYaw might be better if it exists
			me->PrintIfWatched("DefendObjectiveState: Scanning area around '%s'.\n", m_objectiveToDefend->name);
			m_scanTimer.Start(RandomFloat(2.0f, 4.0f));
		}

		// FF_TODO_CLASS_ENGINEER: If Engineer, could check/maintain nearby buildings.
		// if (me->IsEngineer()) { me->TryToRepairBuildable(m_objectiveToDefend->m_entity); /* Or find nearby sentry */ }

		// FF_TODO_CLASS_SNIPER: If Sniper, could find a good sniping spot overlooking objective.
		// if (me->IsSniper()) { /* find sniping spot logic */ }

		// FF_TODO_CLASS_DEMOMAN: If Demoman, could TryLayStickyTrap near approaches to m_objectiveToDefend.
		// if (me->IsDemoman() && me->m_deployedStickiesCount < CFFBot::MAX_BOT_STICKIES)
		// {
		//    Vector approachDir = (TheFFBots()->GetClosestEnemySpawn(me->GetAbsOrigin()) - m_objectiveToDefend->position).Normalized(); // very rough
		//    Vector trapPos = m_objectiveToDefend->position - approachDir * 200.0f;
		//    me->StartLayingStickyTrap(trapPos); // This will change state
		//    return;
		// }
	}
}

//--------------------------------------------------------------------------------------------------------------
void DefendObjectiveState::OnExit( CFFBot *me )
{
	me->PrintIfWatched("DefendObjectiveState: Exiting state for objective '%s'.\n",
		m_objectiveToDefend ? m_objectiveToDefend->name : "UNKNOWN");

	m_objectiveToDefend = NULL;
	m_isAtDefenseSpot = false;
	me->StopMovement(); // Ensure bot stops moving if it was
	me->ClearLookAt();
	if (me->GetTask() == CFFBot::TaskType::DEFEND_LUA_OBJECTIVE)
	{
		me->SetTask(CFFBot::TaskType::SEEK_AND_DESTROY); // Clear specific defend task
	}
}
