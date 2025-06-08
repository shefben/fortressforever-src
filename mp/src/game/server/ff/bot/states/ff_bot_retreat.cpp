//========= Fortress Forever - Bot Retreat State ============//
//
// Purpose: Implements the bot state for retreating when health is low.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_retreat.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "nav_area.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Constants for retreating behavior
const float RETREAT_HEALTH_THRESHOLD_PERCENT = 0.3f; // Retreat if health is below 30%
const float RETREAT_WAIT_TIME = 5.0f;              // How long to wait at a retreat spot
const float RETREAT_REPATH_TIME = 2.0f;            // How often to repath if stuck
const float RETREAT_DISTANCE_AWAY = 750.0f;        // How far to try and move away from enemy
const float RETREAT_RANDOM_FLEE_DISTANCE = 500.0f; // How far to flee if no enemy known

//--------------------------------------------------------------------------------------------------------------
RetreatState::RetreatState(void)
{
	m_retreatSpot = vec3_origin;
	m_repathTimer.Invalidate();
	m_waitAtRetreatSpotTimer.Invalidate();
	m_isAtRetreatSpot = false;
}

//--------------------------------------------------------------------------------------------------------------
const char *RetreatState::GetName( void ) const
{
	return "Retreat";
}

//--------------------------------------------------------------------------------------------------------------
void RetreatState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "RetreatState: Entering state due to low health (%.1f%%).\n", (me->GetHealth() * 100.0f) / me->GetMaxHealth() );

	m_isAtRetreatSpot = false;
	m_waitAtRetreatSpotTimer.Invalidate();
	me->Stop(); // Stop current movement before calculating retreat path

	CFFPlayer* enemy = me->GetBotEnemy();
	Vector fleeFromPos = vec3_origin;
	bool hasFleeTarget = false;

	if (enemy && enemy->IsAlive())
	{
		fleeFromPos = enemy->GetAbsOrigin();
		hasFleeTarget = true;
		me->PrintIfWatched( "RetreatState: Fleeing from current enemy %s.\n", enemy->GetPlayerName() );
	}
	else if (me->GetLastKnownEnemyPosition() != vec3_origin) // Use last known enemy pos if current enemy is null/dead
	{
		fleeFromPos = me->GetLastKnownEnemyPosition();
		hasFleeTarget = true;
		me->PrintIfWatched( "RetreatState: Fleeing from last known enemy position.\n" );
	}

	if (hasFleeTarget)
	{
		Vector toEnemy = fleeFromPos - me->GetAbsOrigin();
		if (toEnemy.IsLengthGreaterThan(1.0f)) // Avoid division by zero if already on top
		{
			m_retreatSpot = me->GetAbsOrigin() - toEnemy.Normalized() * RETREAT_DISTANCE_AWAY;
		}
		else // Already very close or on top of the flee point, pick a random direction
		{
			m_retreatSpot = me->GetAbsOrigin() + Vector(RandomFloat(-1,1), RandomFloat(-1,1), 0).Normalized() * RETREAT_RANDOM_FLEE_DISTANCE;
		}
	}
	else // No enemy or last known position, flee randomly
	{
		me->PrintIfWatched( "RetreatState: No enemy/last known position. Fleeing randomly.\n" );
		m_retreatSpot = me->GetAbsOrigin() + Vector(RandomFloat(-1,1), RandomFloat(-1,1), 0).Normalized() * RETREAT_RANDOM_FLEE_DISTANCE;
	}

	// Find a valid nav area near the calculated retreat spot
	// FF_TODO_AI_BEHAVIOR: Improve retreat spot selection (e.g., towards health, teammates, away from multiple threats)
	CNavArea *retreatNavArea = TheNavMesh->GetNearestNavArea(m_retreatSpot, true, 1000.0f, true, true); // Increased search radius
	if (retreatNavArea)
	{
		m_retreatSpot = retreatNavArea->GetCenter();
		if (me->MoveTo(m_retreatSpot, SAFEST_ROUTE))
		{
			me->PrintIfWatched( "RetreatState: Retreating to (%.1f, %.1f, %.1f).\n", m_retreatSpot.x, m_retreatSpot.y, m_retreatSpot.z );
			m_repathTimer.Start(RETREAT_REPATH_TIME);
		}
		else
		{
			me->PrintIfWatched( "RetreatState: Unable to path to calculated retreat spot. Idling.\n" );
			me->Idle();
		}
	}
	else
	{
		me->PrintIfWatched( "RetreatState: No valid nav area found near retreat spot. Idling.\n" );
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void RetreatState::OnUpdate( CFFBot *me )
{
	// If health recovered significantly, stop retreating
	if (me->GetHealth() > (me->GetMaxHealth() * (RETREAT_HEALTH_THRESHOLD_PERCENT + 0.15f))) // e.g. > 45% if threshold is 30%
	{
		me->PrintIfWatched("RetreatState: Health recovered sufficiently. Exiting retreat.\n");
		me->Idle();
		return;
	}

	if (m_isAtRetreatSpot)
	{
		if (m_waitAtRetreatSpotTimer.IsElapsed())
		{
			me->PrintIfWatched("RetreatState: Wait time at retreat spot elapsed. Idling.\n");
			me->Idle();
			return;
		}
		// FF_TODO_AI_BEHAVIOR: Could look for health kits or medics here.
		// For now, just wait.
		me->SetLookAheadAngle(me->GetAbsAngles().y + RandomFloat(-45.f, 45.f)); // Look around a bit
		return;
	}

	// Pathing to retreat spot
	if (me->IsAtGoal()) // Check if bot reached the current path segment's goal
	{
		// More robust check for actual retreat spot, as IsAtGoal might be true for intermediate path points
		if ((me->GetAbsOrigin() - m_retreatSpot).IsLengthLessThan(100.0f)) // Close enough to final retreat spot
		{
			m_isAtRetreatSpot = true;
			m_waitAtRetreatSpotTimer.Start(RETREAT_WAIT_TIME);
			me->Stop();
			me->PrintIfWatched("RetreatState: Arrived at retreat spot. Waiting.\n");
		}
		else // Reached an intermediate point, path should handle next segment or repath
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("RetreatState: Path segment complete, but not at final spot. Retrying path.\n");
				if (!me->MoveTo(m_retreatSpot, SAFEST_ROUTE)) { me->Idle(); return; }
				m_repathTimer.Start(RETREAT_REPATH_TIME);
			}
		}
	}
	else if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed()) // Stuck or path ended prematurely
	{
		me->PrintIfWatched("RetreatState: Path failed or stuck. Retrying path to retreat spot.\n");
		if (!me->MoveTo(m_retreatSpot, SAFEST_ROUTE))
		{
			me->PrintIfWatched("RetreatState: Still unable to path to retreat spot. Idling.\n");
			me->Idle();
			return;
		}
		m_repathTimer.Start(RETREAT_REPATH_TIME);
	}

	// FF_TODO_AI_BEHAVIOR: If attacked while retreating, the CFFBot::OnTakeDamage will handle transitioning
	// to AttackState if appropriate. We might want more specific evasive maneuvers here later.
}

//--------------------------------------------------------------------------------------------------------------
void RetreatState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "RetreatState: Exiting state.\n" );
	me->Stop(); // Stop any movement
	m_retreatSpot = vec3_origin;
	m_isAtRetreatSpot = false;
	m_waitAtRetreatSpotTimer.Invalidate();
	m_repathTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_retreat.cpp]
