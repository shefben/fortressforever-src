//========= Fortress Forever - Bot Engineer Guard Sentry State ============//
//
// Purpose: Implements the bot state for Engineers guarding Sentry Guns.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_guard_sentry.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"
#include "nav_area.h"
#include "ff_buildableobject.h" // For CFFSentryGun / CFFBuildableObject

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const float GUARD_SCAN_INTERVAL = 1.0f;     // How often to scan for threats/damage
const float GUARD_SPOT_OFFSET_DIST = 150.0f; // How far from sentry to find a guard spot
extern const float BUILD_REPATH_TIME;       // Defined in ff_bot_build_sentry.cpp

//--------------------------------------------------------------------------------------------------------------
GuardSentryState::GuardSentryState(void)
{
	m_sentryToGuard = NULL;
	m_scanForThreatsTimer.Invalidate();
	m_guardSpot = vec3_origin;
	m_repathTimer.Invalidate();
	m_isAtGuardSpot = false;
	m_tendSentryTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *GuardSentryState::GetName( void ) const
{
	return "GuardSentry";
}

//--------------------------------------------------------------------------------------------------------------
void GuardSentryState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "GuardSentryState: Entering state.\n" );
	m_isAtGuardSpot = false;
	m_sentryToGuard = me->GetSentryGun();

	if (!m_sentryToGuard.Get() || !m_sentryToGuard->IsAlive())
	{
		me->PrintIfWatched( "GuardSentryState: No valid sentry to guard. Idling.\n" );
		me->Idle();
		return;
	}

	// Enhanced Guard Spot Selection
	Vector sentryOrigin = m_sentryToGuard->GetAbsOrigin();
	QAngle sentryAngles = m_sentryToGuard->GetAbsAngles();
	Vector sentryForward, sentryRight, sentryUp;
	AngleVectors(sentryAngles, &sentryForward, &sentryRight, &sentryUp);

	// FF_TODO_AI_BEHAVIOR: A more advanced check would be to find a spot that has LOS to common enemy approaches
	// but keeps the Engineer somewhat safe, perhaps checking for nearby cover nodes or using visibility analysis.
	const float candidateDist = GUARD_SPOT_OFFSET_DIST; // 150.0f
	Vector candidateSpots[] = {
		sentryOrigin - sentryForward * candidateDist + sentryRight * (candidateDist * 0.75f), // Behind and to the right
		sentryOrigin - sentryForward * candidateDist - sentryRight * (candidateDist * 0.75f), // Behind and to the left
		sentryOrigin + sentryRight * candidateDist,                                          // To the right
		sentryOrigin - sentryRight * candidateDist,                                          // To the left
		sentryOrigin + sentryForward * (candidateDist * 0.5f) - sentryRight * candidateDist, // Slightly in front, to the left (less ideal)
		sentryOrigin + sentryForward * (candidateDist * 0.5f) + sentryRight * candidateDist, // Slightly in front, to the right (less ideal)
		sentryOrigin - sentryForward * (candidateDist * 0.5f)                                // Directly behind (closer)
	};

	CNavArea *bestNavArea = NULL;
	Vector chosenGuardSpot = sentryOrigin; // Default to sentry origin if no good spot found

	for (const auto& spotAttempt : candidateSpots)
	{
		CNavArea *navArea = TheNavMesh->GetNearestNavArea(spotAttempt, true, 200.0f, true, true);
		if (navArea && me->IsReachable(navArea)) // IsReachable is a conceptual check, might need actual path test
		{
			// Basic check: is it not too close to the sentry itself (unless it's the only option)
			if ((navArea->GetCenter() - sentryOrigin).IsLengthGreaterThan(GUARD_SPOT_OFFSET_DIST * 0.5f))
			{
				// Further checks could involve LOS to sentry, cover, etc.
				// For now, first valid reachable spot is chosen.
				bestNavArea = navArea;
				chosenGuardSpot = navArea->GetCenter();
				me->PrintIfWatched("GuardSentryState: Found candidate guard spot at (%.1f, %.1f, %.1f)\n", chosenGuardSpot.x, chosenGuardSpot.y, chosenGuardSpot.z);
				break;
			}
		}
	}

	if (!bestNavArea) // If no candidate spots worked, try very close to the sentry.
	{
		me->PrintIfWatched("GuardSentryState: No ideal candidate guard spots found. Trying very close to sentry.\n");
		Vector closeSpot = sentryOrigin - sentryForward * 75.0f; // Closer behind
		bestNavArea = TheNavMesh->GetNearestNavArea(closeSpot, true, 100.0f, true, true);
		if (bestNavArea && me->IsReachable(bestNavArea))
		{
			chosenGuardSpot = bestNavArea->GetCenter();
		}
		else // Fallback to sentry's own area if even that fails
		{
			bestNavArea = TheNavMesh->GetNearestNavArea(sentryOrigin, true, 50.0f, true, true);
			if (bestNavArea && me->IsReachable(bestNavArea)) chosenGuardSpot = bestNavArea->GetCenter();
			// If still no good spot, chosenGuardSpot remains sentryOrigin, pathing might fail but it's a last resort.
		}
	}

	m_guardSpot = chosenGuardSpot;

	me->PrintIfWatched( "GuardSentryState: Guarding sentry %s. Final guard spot (%.1f, %.1f, %.1f).\n",
		m_sentryToGuard->GetClassname(), m_guardSpot.x, m_guardSpot.y, m_guardSpot.z );

	if (!me->MoveTo(m_guardSpot, SAFEST_ROUTE))
	{
		me->PrintIfWatched( "GuardSentryState: Unable to path to final guard spot. Staying very close to sentry.\n" );
		m_guardSpot = sentryOrigin; // Fallback to sentry's exact origin
		if (!me->MoveTo(m_guardSpot, SAFEST_ROUTE))
		{
		    me->PrintIfWatched( "GuardSentryState: Still unable to path even to sentry origin. Idling.\n" );
		    me->Idle();
		    return;
		}
	}
	m_repathTimer.Start(BUILD_REPATH_TIME);
	m_scanForThreatsTimer.Start(GUARD_SCAN_INTERVAL);
	m_tendSentryTimer.Start(RandomFloat(5.0f, 10.0f)); // Initialize tend timer
}

//--------------------------------------------------------------------------------------------------------------
void GuardSentryState::OnUpdate( CFFBot *me )
{
	CFFSentryGun *sentry = dynamic_cast<CFFSentryGun *>(m_sentryToGuard.Get());

	if (!sentry || !sentry->IsAlive())
	{
		me->PrintIfWatched("GuardSentryState: Sentry destroyed or invalid. Idling (will try to rebuild).\n");
		me->Idle();
		return;
	}

	// Priority 1: Handle enemy threats
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("GuardSentryState: Enemy detected! Engaging.\n");
		me->Attack(me->GetBotEnemy());
		return;
	}

	// Priority 2: Check Sentry Status (Damage, Sappers) - if not currently tending
	if (!m_tendSentryTimer.HasStarted() || m_tendSentryTimer.IsGreaterThan(1.0f)) // Don't interrupt very recent tending action
	{
		if (sentry->IsSapped() || sentry->GetHealth() < sentry->GetMaxHealth())
		{
			me->PrintIfWatched("GuardSentryState: Sentry %s needs repair (Sapped: %s, Health: %d/%d). Switching to RepairBuildableState.\n",
				sentry->GetClassname(), sentry->IsSapped() ? "YES" : "NO", sentry->GetHealth(), sentry->GetMaxHealth());
			me->TryToRepairBuildable(sentry);
			return;
		}
	}

	// Priority 3: Check Resources
	if (me->GetAmmoCount(AMMO_CELLS) < CFFBot::ENGINEER_LOW_CELL_THRESHOLD) // FF_TODO_CLASS_ENGINEER: AMMO_CELLS needs to be the correct enum for metal/resources
	{
		me->PrintIfWatched("GuardSentryState: Low on cells/metal. Switching to FindResourcesState.\n");
		me->TryToFindResources();
		return;
	}

	// Pathing to guard spot
	if (!m_isAtGuardSpot)
	{
		if ((me->GetAbsOrigin() - m_guardSpot).IsLengthLessThan(50.0f)) // Reduced radius for being "at" spot
		{
			m_isAtGuardSpot = true;
			me->Stop();
			me->PrintIfWatched("GuardSentryState: Arrived at guard spot.\n");
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("GuardSentryState: Path failed or stuck. Retrying path to guard spot.\n");
				if (!me->MoveTo(m_guardSpot, SAFEST_ROUTE))
				{
					me->PrintIfWatched("GuardSentryState: Still unable to path to guard spot. Idling.\n");
					me->Idle();
					return;
				}
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
			return; // Still moving
		}
	}

	// At guard spot, perform periodic actions
	if (m_isAtGuardSpot)
	{
		// "Tending" to Sentry
		if (m_tendSentryTimer.IsElapsed())
		{
			me->PrintIfWatched("Engineer: Tending to sentry %s", sentry->GetDebugName());

			CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER); // FF_TODO_WEAPON_STATS: Verify Spanner ID
			if (spanner && me->GetActiveFFWeapon() != spanner)
			{
				me->EquipWeapon(spanner);
			}

			// Briefly move to sentry, hit it, then can decide to move back to guard spot or stay
			// For simplicity, just simulate the hit. More complex logic could path to sentry then back.
			if ((me->GetAbsOrigin() - sentry->GetAbsOrigin()).IsLengthGreaterThan(80.0f)) // If not right next to it
			{
				// Optional: me->MoveTo(sentry->GetAbsOrigin(), FASTEST_ROUTE); // and handle pathing
				// For now, assume spanner has some range or bot is close enough from guard spot for a symbolic hit.
			}

			me->SetLookAt("Tending Sentry", sentry->WorldSpaceCenter(), PRIORITY_HIGH, 0.5f);
			me->PressButton(IN_ATTACK);
			// FF_TODO_GAME_MECHANIC: Does hitting a healthy, non-sapped sentry provide ammo or any benefit in FF?
			// If it's a continuous action, bot might need to hold IN_ATTACK or do it in RepairState.
			// For GuardState, a symbolic single hit to "check/boost" is fine.
			// Schedule a release if IN_ATTACK is not automatically released
			me->ReleaseButton(IN_ATTACK); // Assuming a quick tap

			m_tendSentryTimer.Start(RandomFloat(7.0f, 12.0f)); // Reset tend timer
			m_scanForThreatsTimer.Start(GUARD_SCAN_INTERVAL); // Reset scan timer too after tending
			return; // Tending action taken for this update cycle
		}

		// Sentry Firing Reaction (Conceptual)
		// FF_TODO_ENGINEER: How to detect if m_sentryToGuard is actively firing?
		// Needs a CFFSentryGun::IsFiring() method or game event.
		bool isSentryConceptuallyFiring = false; // Placeholder
		if (isSentryConceptuallyFiring)
		{
			Vector vSentryFacing;
			AngleVectors(sentry->GetAbsAngles(), &vSentryFacing);
			me->SetLookAt("Sentry Target Direction", sentry->GetAbsOrigin() + vSentryFacing * 750.0f, PRIORITY_MEDIUM_HIGH, 1.0f);
			// Consider becoming more alert or moving to a position to support the sentry.
		}
		else if (m_scanForThreatsTimer.IsElapsed()) // General scan if sentry not firing and not tending
		{
			// Look around, prioritize directions of likely enemy approach relative to sentry
			// FF_TODO_ENGINEER: Implement more intelligent "look around" logic for guarding.
			if (me->HasNotSeenEnemyForLongTime())
			{
				// Occasionally look at the sentry then sweep common approach paths
				if (RandomFloat(0,1) < 0.3f)
				{
					me->SetLookAt("Checking Sentry", sentry->WorldSpaceCenter() + Vector(0,0,30), PRIORITY_LOW, 1.5f, true);
				}
				else
				{
					// Conceptual: Look towards a known enemy approach vector if available from map data or learned behavior
					Vector approachDir = Vector(RandomFloat(-1,1), RandomFloat(-1,1), 0); // Placeholder
					approachDir.NormalizeInPlace();
					me->SetLookAt("Sweeping Approaches", me->EyePosition() + approachDir * 500.0f, PRIORITY_LOW, 2.0f);
				}
			}
			else
			{
				me->UpdateLookAround(); // Default look around behavior if saw enemy recently
			}
			m_scanForThreatsTimer.Start(GUARD_SCAN_INTERVAL);
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void GuardSentryState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "GuardSentryState: Exiting state.\n" );
	me->ClearLookAt();
	m_sentryToGuard = NULL;
	m_isAtGuardSpot = false;
	m_repathTimer.Invalidate();
	m_scanForThreatsTimer.Invalidate();
	m_tendSentryTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_guard_sentry.cpp]
