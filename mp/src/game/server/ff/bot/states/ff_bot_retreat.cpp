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
	me->PrintIfWatched( "RetreatState: Entering state. Health: %.1f%%, Needs Ammo: %s\n",
		(me->GetHealth() * 100.0f) / me->GetMaxHealth(),
		me->NeedsAmmo() ? "Yes" : "No" );

	m_isAtRetreatSpot = false;
	m_waitAtRetreatSpotTimer.Invalidate();
	m_chosenRetreatResource = NULL;
	me->Stop();

	CBaseEntity* pBestRetreatResource = NULL;
	Vector chosenSpot = vec3_origin;
	bool foundSpot = false;

	// Priority 1: Nearby Friendly Dispenser if needed
	// Engineers might always prefer their dispenser, others if low health/ammo.
	if (me->IsEngineer() || me->GetHealth() < me->GetMaxHealth() * 0.75f || me->NeedsAmmo())
	{
		pBestRetreatResource = me->FindResourceSource(); // This finds Dispensers or Ammo
		if (pBestRetreatResource && FClassnameIs(pBestRetreatResource, "obj_dispenser")) // FF_TODO_BUILDING: Verify dispenser classname
		{
			// Ensure it's a friendly, operational dispenser
			CFFDispenser* pDispenser = dynamic_cast<CFFDispenser*>(pBestRetreatResource);
			if (pDispenser && pDispenser->IsBuilt() && !pDispenser->IsSapped() && pDispenser->GetTeamNumber() == me->GetTeamNumber())
			{
				chosenSpot = pDispenser->GetAbsOrigin();
				m_chosenRetreatResource = pDispenser; // Store the handle to the dispenser
				foundSpot = true;
				me->PrintIfWatched("RetreatState: Retreating towards friendly Dispenser: %s at (%.1f, %.1f, %.1f)\n",
					pDispenser->GetDebugName(), chosenSpot.x, chosenSpot.y, chosenSpot.z);
			}
			else
			{
				pBestRetreatResource = NULL; // Not a suitable dispenser
			}
		}
		else
		{
			pBestRetreatResource = NULL; // Didn't find a dispenser, or it was an ammo pack we don't prioritize for hiding.
		}
	}

	// Priority 2: Fallback (move away from enemy / random)
	if (!foundSpot)
	{
		m_chosenRetreatResource = NULL; // Ensure it's null if we're not going to a resource
		CFFPlayer* enemy = me->GetBotEnemy();
		Vector fleeDir;
		Vector fleeFromPos = vec3_origin;
		bool hasFleeTarget = false;

		if (enemy && enemy->IsAlive())
		{
			fleeFromPos = enemy->GetAbsOrigin();
			hasFleeTarget = true;
		}
		else if (me->GetLastAttacker() && me->GetLastAttacker()->IsAlive()) // Prefer last attacker if current enemy is gone
		{
			fleeFromPos = me->GetLastAttacker()->GetAbsOrigin();
			hasFleeTarget = true;
		}
		else if (me->GetLastKnownEnemyPosition() != vec3_origin)
		{
			fleeFromPos = me->GetLastKnownEnemyPosition();
			hasFleeTarget = true;
		}

		if (hasFleeTarget)
		{
			fleeDir = me->GetAbsOrigin() - fleeFromPos;
			if (fleeDir.IsLengthGreaterThan(1.0f))
			{
				fleeDir.NormalizeInPlace();
			}
			else // Too close, pick a random direction
			{
				fleeDir = Vector(RandomFloat(-1,1), RandomFloat(-1,1), 0);
				fleeDir.NormalizeInPlace();
			}
			me->PrintIfWatched( "RetreatState: Fleeing from threat at (%.1f, %.1f, %.1f).\n", fleeFromPos.x, fleeFromPos.y, fleeFromPos.z );
		}
		else
		{
			fleeDir = Vector(RandomFloat(-1,1), RandomFloat(-1,1), 0);
			fleeDir.NormalizeInPlace();
			me->PrintIfWatched( "RetreatState: No specific threat. Fleeing randomly.\n" );
		}
		chosenSpot = me->GetAbsOrigin() + fleeDir * RETREAT_DISTANCE_AWAY;
		foundSpot = true;
	}

	CNavArea *retreatNavArea = TheNavMesh->GetNearestNavArea(chosenSpot, true, 1000.0f, true, true);
	if (retreatNavArea)
	{
		m_retreatSpot = retreatNavArea->GetCenter(); // Use the center of the nav area as the actual spot
		if (me->MoveTo(m_retreatSpot, SAFEST_ROUTE))
		{
			if (m_chosenRetreatResource.Get())
				me->PrintIfWatched( "RetreatState: Pathing to Dispenser near (%.1f, %.1f, %.1f).\n", m_retreatSpot.x, m_retreatSpot.y, m_retreatSpot.z );
			else
				me->PrintIfWatched( "RetreatState: Pathing to general retreat spot (%.1f, %.1f, %.1f).\n", m_retreatSpot.x, m_retreatSpot.y, m_retreatSpot.z );
			m_repathTimer.Start(RETREAT_REPATH_TIME);
		}
		else
		{
			me->PrintIfWatched( "RetreatState: Unable to path to chosen retreat spot. Idling.\n" );
			me->Idle();
		}
	}
	else
	{
		me->PrintIfWatched( "RetreatState: No valid nav area found for retreat spot. Idling.\n" );
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void RetreatState::OnUpdate( CFFBot *me )
{
	bool isHealthy = me->GetHealth() >= me->GetMaxHealth() * 0.90f; // Considered healthy if >= 90%
	bool hasEnoughAmmo = !me->NeedsAmmo(); // Check if ammo is sufficient

	if (m_isAtRetreatSpot)
	{
		// If at a dispenser, stay until healthy and has ammo, or timer runs out
		if (m_chosenRetreatResource.Get() && FClassnameIs(m_chosenRetreatResource.Get(), "obj_dispenser"))
		{
			// FF_TODO_RESOURCES: How to check if actively receiving from dispenser?
			// For now, assume proximity means receiving. Game events for resupply would be better.
			if ((isHealthy && hasEnoughAmmo) || m_waitAtRetreatSpotTimer.IsElapsed())
			{
				me->PrintIfWatched("RetreatState: Finished at Dispenser (Health: %.0f, Ammo: OK, Timer: %s). Idling.\n",
					me->GetHealth(), m_waitAtRetreatSpotTimer.IsElapsed() ? "Yes" : "No");
				me->Idle();
				return;
			}
			// Still waiting at dispenser
			me->SetLookAheadAngle(me->GetAbsAngles().y + RandomFloat(-45.f, 45.f)); // Look around
			return;
		}
		else // At a generic retreat spot (not a dispenser)
		{
			if (isHealthy || m_waitAtRetreatSpotTimer.IsElapsed()) // Healthy or timer up
			{
				me->PrintIfWatched("RetreatState: Wait time at general retreat spot elapsed or health okay. Idling.\n");
				me->Idle();
				return;
			}
			me->SetLookAheadAngle(me->GetAbsAngles().y + RandomFloat(-45.f, 45.f)); // Look around
			return;
		}
	}

	// Still pathing to retreat spot
	if (me->IsAtGoal() || (me->GetAbsOrigin() - m_retreatSpot).IsLengthLessThan(60.0f)) // Close enough to final retreat spot
	{
		m_isAtRetreatSpot = true;
		m_waitAtRetreatSpotTimer.Start(RETREAT_WAIT_TIME); // Standard wait time
		me->Stop();
		if (m_chosenRetreatResource.Get())
			me->PrintIfWatched("RetreatState: Arrived at Dispenser. Waiting.\n");
		else
			me->PrintIfWatched("RetreatState: Arrived at general retreat spot. Waiting.\n");
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

	// If attacked while retreating, CFFBot::OnTakeDamage might transition state.
	// If health drops very low (e.g. <15%) while retreating, might need a "last stand" or more desperate flee.
	if (me->GetHealth() < me->GetMaxHealth() * 0.15f && !m_isAtRetreatSpot)
	{
		// FF_TODO_AI_BEHAVIOR: Consider more desperate actions if health becomes critical during retreat.
		// For now, continue trying to reach the spot.
	}
}

//--------------------------------------------------------------------------------------------------------------
void RetreatState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "RetreatState: Exiting state.\n" );
	me->Stop();
	m_retreatSpot = vec3_origin;
	m_isAtRetreatSpot = false;
	m_waitAtRetreatSpotTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_chosenRetreatResource = NULL;
}

[end of mp/src/game/server/ff/bot/states/ff_bot_retreat.cpp]
