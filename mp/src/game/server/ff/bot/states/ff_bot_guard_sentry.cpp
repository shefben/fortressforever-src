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
	m_sentryToGuard = me->GetSentryGun(); // Assumes CFFBot::GetSentryGun() returns their CFFSentryGun*

	if (!m_sentryToGuard.Get() || !m_sentryToGuard->IsAlive())
	{
		me->PrintIfWatched( "GuardSentryState: No valid sentry to guard. Idling.\n" );
		me->Idle(); // Should probably try to build one
		return;
	}

	// Determine a guard spot. Placeholder: a spot near the sentry.
	// TODO: More sophisticated logic to find a covered spot with LoS to sentry/approaches.
	Vector forward, right;
	AngleVectors(m_sentryToGuard->GetAbsAngles(), &forward, &right, NULL);
	m_guardSpot = m_sentryToGuard->GetAbsOrigin() - forward * GUARD_SPOT_OFFSET_DIST + right * (GUARD_SPOT_OFFSET_DIST / 2.0f) ;

	// Attempt to find a valid nav area near the calculated spot
	CNavArea *sentryArea = TheNavMesh->GetNearestNavArea(m_sentryToGuard->GetAbsOrigin());
	if (sentryArea)
	{
		m_guardSpot = sentryArea->GetClosestPointOnArea(m_guardSpot); // Snap to navmesh
	}
	// Else, m_guardSpot might be off-mesh, pathing will likely fail.

	me->PrintIfWatched( "GuardSentryState: Guarding sentry %s at (%.1f, %.1f, %.1f). Moving to guard spot (%.1f, %.1f, %.1f).\n",
		m_sentryToGuard->GetClassname(),
		m_sentryToGuard->GetAbsOrigin().x, m_sentryToGuard->GetAbsOrigin().y, m_sentryToGuard->GetAbsOrigin().z,
		m_guardSpot.x, m_guardSpot.y, m_guardSpot.z );

	if (!me->MoveTo(m_guardSpot, SAFEST_ROUTE))
	{
		me->PrintIfWatched( "GuardSentryState: Unable to path to guard spot. Staying near sentry.\n" );
		// Fallback: If path to ideal guard spot fails, just move to the sentry's origin.
		m_guardSpot = m_sentryToGuard->GetAbsOrigin(); // Update guard spot to sentry itself
		if (!me->MoveTo(m_guardSpot, SAFEST_ROUTE))
		{
		    me->PrintIfWatched( "GuardSentryState: Still unable to path even to sentry origin. Idling.\n" );
		    me->Idle(); // Give up for now
		    return;
		}
	}
	m_repathTimer.Start(BUILD_REPATH_TIME);
	m_scanForThreatsTimer.Start(GUARD_SCAN_INTERVAL);
}

//--------------------------------------------------------------------------------------------------------------
void GuardSentryState::OnUpdate( CFFBot *me )
{
	CFFSentryGun *sentry = dynamic_cast<CFFSentryGun *>(m_sentryToGuard.Get());

	if (!sentry || !sentry->IsAlive())
	{
		me->PrintIfWatched("GuardSentryState: Sentry destroyed or invalid. Idling (will try to rebuild).\n");
		me->Idle(); // Idle state will trigger TryToBuildSentry if appropriate
		return;
	}

	// Handle enemy threats first - this is the primary purpose of guarding
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("GuardSentryState: Enemy detected! Engaging.\n");
		me->Attack(me->GetBotEnemy());
		return;
	}

	if (!m_isAtGuardSpot)
	{
		if ((me->GetAbsOrigin() - m_guardSpot).IsLengthLessThan(BUILD_PLACEMENT_RADIUS * 0.5f)) // Tighter radius for being "at" spot
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

	// At guard spot, perform periodic checks
	if (m_isAtGuardSpot && m_scanForThreatsTimer.IsElapsed())
	{
		// 1. Scan for nearby enemies (even if not direct LoS to bot, but maybe to sentry)
		// This is implicitly handled by CFFBot::Update() which sets m_enemy if one is found.
		// If an enemy is found by bot's main update, the check at the start of this OnUpdate will trigger AttackState.

		// 2. Check Sentry Status (Damage, Sappers)
		if (sentry->IsSapped() || sentry->GetHealth() < sentry->GetMaxHealth())
		{
			me->PrintIfWatched("GuardSentryState: Sentry %s needs repair (Sapped: %s, Health: %d/%d). Switching to RepairBuildableState.\n",
				sentry->GetClassname(), sentry->IsSapped() ? "YES" : "NO", sentry->GetHealth(), sentry->GetMaxHealth());
			me->TryToRepairBuildable(sentry); // This will change state
			return;
		}

		// 3. Check Resources
		if (me->GetAmmoCount(AMMO_CELLS) < CFFBot::ENGINEER_LOW_CELL_THRESHOLD)
		{
			me->PrintIfWatched("GuardSentryState: Low on cells. Switching to FindResourcesState.\n");
			me->TryToFindResources(); // This will change state
			return;
		}

		m_scanForThreatsTimer.Start(GUARD_SCAN_INTERVAL);
	}

	// Look around, prioritize directions of likely enemy approach relative to sentry
	if (m_isAtGuardSpot)
	{
		// FF_TODO_ENGINEER: Implement more intelligent "look around" logic for guarding.
		// For now, just look towards the sentry occasionally, or generally look around.
		if (me->HasNotSeenEnemyForLongTime())
		{
			me->SetLookAt("Guarding Sentry Area", sentry->WorldSpaceCenter() + Vector(0,0,30), PRIORITY_LOW, 2.0f, true);
		}
		else
		{
			me->UpdateLookAround(); // Default look around behavior
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
}

[end of mp/src/game/server/ff/bot/states/ff_bot_guard_sentry.cpp]
