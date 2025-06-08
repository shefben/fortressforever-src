//========= Fortress Forever - Bot Demoman Lay Sticky Trap State ============//
//
// Purpose: Implements the bot state for Demomen laying stickybomb traps.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_lay_sticky_trap.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "nav_area.h"
#include "nav_mesh.h" // For TheNavMesh

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const float BUILD_REPATH_TIME; // From ff_bot_build_sentry.cpp, assuming it's a general constant

//--------------------------------------------------------------------------------------------------------------
LayStickyTrapState::LayStickyTrapState(void)
{
	m_targetTrapLocation = vec3_origin;
	m_laySpot = vec3_origin;
	m_isAtLaySpot = false;
	m_laidStickiesInTrap = 0;
	m_layIntervalTimer.Invalidate();
	m_repathTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *LayStickyTrapState::GetName( void ) const
{
	return "LayStickyTrap";
}

//--------------------------------------------------------------------------------------------------------------
void LayStickyTrapState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "LayStickyTrapState: Entering state. Target location: (%.1f, %.1f, %.1f)\n",
		m_targetTrapLocation.x, m_targetTrapLocation.y, m_targetTrapLocation.z );

	m_isAtLaySpot = false;
	m_laidStickiesInTrap = 0;
	m_layIntervalTimer.Invalidate(); // Will start when at spot
	me->Stop();

	// Find a suitable spot to stand while laying the trap.
	// This spot should have LOS to m_targetTrapLocation and be a bit away.
	// FF_TODO_AI_BEHAVIOR: More sophisticated lay spot finding (e.g., using cover, higher ground).

	Vector toTargetDir = (m_targetTrapLocation - me->GetAbsOrigin()).Normalized();
	Vector candidateLaySpot = m_targetTrapLocation - toTargetDir * POSITIONING_DISTANCE;

	CNavArea *layNavArea = TheNavMesh->GetNearestNavArea(candidateLaySpot, true, 250.0f, true, true);
	if (layNavArea && me->IsReachable(layNavArea))
	{
		m_laySpot = layNavArea->GetCenter();
		if (!me->MoveTo(m_laySpot, SAFEST_ROUTE))
		{
			me->PrintIfWatched("LayStickyTrapState: Unable to path to lay spot. Idling.\n");
			me->Idle();
			return;
		}
		me->PrintIfWatched("LayStickyTrapState: Moving to lay spot (%.1f, %.1f, %.1f).\n", m_laySpot.x, m_laySpot.y, m_laySpot.z);
		m_repathTimer.Start(BUILD_REPATH_TIME);
	}
	else
	{
		// Could not find a good spot, try closer to target or give up.
		// For now, try to lay from current position if LOS is good, otherwise abort.
		trace_t tr;
		UTIL_TraceLine(me->EyePosition(), m_targetTrapLocation, MASK_SOLID_NOT_PLAYER, me, COLLISION_GROUP_NONE, &tr);
		if (tr.fraction == 1.0f || tr.m_pEnt == me->GetWorldEntity()) // Clear LOS or only hit world
		{
			me->PrintIfWatched("LayStickyTrapState: No ideal lay spot, attempting from current position.\n");
			m_laySpot = me->GetAbsOrigin(); // Use current spot
			m_isAtLaySpot = true; // Consider already at spot
			m_layIntervalTimer.Start(0.1f); // Start laying almost immediately
		}
		else
		{
			me->PrintIfWatched("LayStickyTrapState: No suitable lay spot found with LOS. Idling.\n");
			me->Idle();
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void LayStickyTrapState::OnUpdate( CFFBot *me )
{
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("LayStickyTrapState: Enemy detected! Aborting trap and attacking.\n");
		me->Attack(me->GetBotEnemy());
		return;
	}

	if (!m_isAtLaySpot)
	{
		if (me->IsAtGoal() || (me->GetAbsOrigin() - m_laySpot).IsLengthLessThan(50.0f))
		{
			m_isAtLaySpot = true;
			me->Stop();
			m_layIntervalTimer.Start(0.1f); // Start laying stickies
			me->PrintIfWatched("LayStickyTrapState: Arrived at lay spot.\n");
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("LayStickyTrapState: Path to lay spot failed or stuck. Idling.\n");
				me->Idle();
				return;
			}
			return; // Still moving to lay spot
		}
	}

	// At lay spot, start/continue laying stickies
	if (m_isAtLaySpot)
	{
		if (m_laidStickiesInTrap >= STICKIES_PER_TRAP || me->m_deployedStickiesCount >= CFFBot::MAX_BOT_STICKIES)
		{
			me->PrintIfWatched("LayStickyTrapState: Finished laying trap (%d stickies). Idling.\n", m_laidStickiesInTrap);
			// FF_TODO_AI_BEHAVIOR: Transition to a "GuardStickyTrapState" or similar, or just Idle.
			me->Idle();
			return;
		}

		if (m_layIntervalTimer.IsElapsed())
		{
			// FF_TODO_AI_BEHAVIOR: Add slight aim adjustments for each sticky for better spread.
			Vector aimLocation = m_targetTrapLocation;
			if (m_laidStickiesInTrap > 0) // Add some variance for subsequent stickies
			{
				aimLocation.x += RandomFloat(-20.f, 20.f);
				aimLocation.y += RandomFloat(-20.f, 20.f);
			}

			me->TryLayStickyTrap(aimLocation);
			// TryLayStickyTrap increments m_deployedStickiesCount internally.
			// It also starts m_stickyArmTime.

			m_laidStickiesInTrap++;
			m_layIntervalTimer.Start(LAY_INTERVAL);

			if (m_laidStickiesInTrap >= STICKIES_PER_TRAP)
			{
				me->PrintIfWatched("LayStickyTrapState: Trap complete with %d stickies. Next action soon.\n", m_laidStickiesInTrap);
				// Don't idle immediately, let the last sticky arm time start, then next update will idle.
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void LayStickyTrapState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "LayStickyTrapState: Exiting state.\n" );
	me->ClearLookAt();
	me->Stop();
	m_isAtLaySpot = false;
	m_laidStickiesInTrap = 0;
	m_layIntervalTimer.Invalidate();
	m_repathTimer.Invalidate();
}
