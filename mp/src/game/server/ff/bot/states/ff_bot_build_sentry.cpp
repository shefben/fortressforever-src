//========= Fortress Forever - Bot Engineer Build Sentry State ============//
//
// Purpose: Implements the bot state for Engineers building Sentry Guns.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_build_sentry.h"
#include "../ff_bot_manager.h" // For TheFFBots() etc.
#include "../ff_player.h"       // For CFFPlayer
#include "../ff_weapon_base.h"  // For FF_WEAPON_SPANNER etc.
#include "nav_area.h"         // For CNavArea
#include "ff_buildable_sentrygun.h" // For CFFSentryGun

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define constants for building
const float ENGINEER_SENTRY_BUILD_TIME = 5.0f;
const float BUILD_PLACEMENT_RADIUS = 75.0f;
const float BUILD_REPATH_TIME = 2.5f;
const float ENGINEER_SENTRY_UPGRADE_HIT_CYCLE_TIME = 1.0f; // Time for one "session" of hits for an upgrade attempt
const int DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL = 2;
extern const int SENTRY_COST_CELLS;
const int SENTRY_UPGRADE_COST_CELLS = 50; // FF_TODO_BUILDING: Tune this value
const int SENTRY_MAX_LEVEL = 3; // Assuming max level is 3, like TF2. Should be from CFFSentryGun::GetMaxUpgradeLevel()

//--------------------------------------------------------------------------------------------------------------
BuildSentryState::BuildSentryState(void)
{
	m_buildLocation = vec3_invalid;
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_sentryBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate(); // Used for initial build and for upgrade hit cycles
	m_repathTimer.Invalidate();
	m_waitForBlueprintTimer.Invalidate();

	m_isUpgrading = false;
	m_targetUpgradeLevel = DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL;
	// m_currentUpgradeLevel removed
	// m_upgradeProgressTimer removed (using m_buildProgressTimer)
}

//--------------------------------------------------------------------------------------------------------------
const char *BuildSentryState::GetName( void ) const
{
	return "BuildSentry";
}

//--------------------------------------------------------------------------------------------------------------
void BuildSentryState::SetBuildLocation(const Vector &location)
{
	m_buildLocation = location;
}

//--------------------------------------------------------------------------------------------------------------
void BuildSentryState::OnEnter( CFFBot *me )
{
	me->SelectSpecificBuildable(CFFBot::BUILDABLE_SENTRY);
	me->PrintIfWatched( "BuildSentryState: Entering state.\n" );
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_sentryBeingBuilt = NULL;
	m_waitForBlueprintTimer.Invalidate();
	m_isUpgrading = false;
	m_targetUpgradeLevel = DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL;

	if (m_buildLocation == vec3_invalid)
	{
		Vector forward;
		AngleVectors(me->GetLocalAngles(), &forward);
		m_buildLocation = me->GetAbsOrigin() + forward * 100.0f;
		me->PrintIfWatched( "BuildSentryState: No build location set, choosing default in front.\n" );
	}

	me->PrintIfWatched( "BuildSentryState: Target build location: (%.1f, %.1f, %.1f).\n",
		m_buildLocation.x, m_buildLocation.y, m_buildLocation.z );

	if (me->MoveTo(m_buildLocation, SAFEST_ROUTE))
	{
		m_repathTimer.Start(BUILD_REPATH_TIME);
	}
	else
	{
		me->PrintIfWatched( "BuildSentryState: Unable to path to build location. Idling.\n" );
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildSentryState::OnUpdate( CFFBot *me )
{
	// me->PrintIfWatched( "BuildSentryState: Updating...\n" ); // Too spammy

	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("BuildSentryState: Enemy detected! Aborting build and attacking.\n");
		me->Attack(me->GetBotEnemy());
		return;
	}

	if (!m_isAtBuildLocation)
	{
		if ((me->GetAbsOrigin() - m_buildLocation).IsLengthLessThan(BUILD_PLACEMENT_RADIUS))
		{
			m_isAtBuildLocation = true;
			me->Stop();
			me->PrintIfWatched("BuildSentryState: Arrived at build location.\n");
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("BuildSentryState: Path failed or stuck. Retrying path to build location.\n");
				if (!me->MoveTo(m_buildLocation, SAFEST_ROUTE))
				{
					me->PrintIfWatched("BuildSentryState: Still unable to path. Idling.\n");
					me->Idle();
					return;
				}
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
			return;
		}
	}

	// At build location
	if (!m_isBuilding && !m_isUpgrading) // Phase 1: Place blueprint and wait for initial build
	{
		if (!m_sentryBeingBuilt.Get()) // Try to find/confirm blueprint
		{
			// SelectSpecificBuildable in OnEnter issues the command. Now wait for blueprint.
			if (!m_waitForBlueprintTimer.HasStarted())
			{
				m_waitForBlueprintTimer.Start(0.3f); // Wait a bit for entity to spawn
			}
			if (!m_waitForBlueprintTimer.IsElapsed())
			{
				return;
			}

			float searchRadius = 150.0f;
			CBaseEntity *pEntity = NULL;
			while ((pEntity = gEntList.FindEntityInSphere(pEntity, m_buildLocation, searchRadius)) != NULL)
			{
				if (FClassnameIs(pEntity, "obj_sentrygun"))
				{
					CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(pEntity);
					if (pBuildable && pBuildable->GetBuilder() == me && pBuildable->IsBuilding())
					{
						m_sentryBeingBuilt = pBuildable;
						me->PrintIfWatched("BuildSentryState: Found placed sentry blueprint: %s\n", pBuildable->GetClassname());
						break;
					}
				}
			}

			if (!m_sentryBeingBuilt.Get())
			{
				me->PrintIfWatched("BuildSentryState: Failed to find placed sentry blueprint nearby after waiting. Idling.\n");
				me->Idle();
				return;
			}
		}

		// Blueprint found, now "hit" it for initial construction
		CFFBuildableObject *pSentryObject = dynamic_cast<CFFBuildableObject*>(m_sentryBeingBuilt.Get());
		if (!pSentryObject) { me->Idle(); return; }


		if (pSentryObject->IsBuilt()) // Initial build completed (likely by NotifyBuildingBuilt setting level > 0)
		{
			me->PrintIfWatched("BuildSentryState: Initial sentry construction complete (IsBuilt() is true).\n");
			me->ReleaseButton(IN_ATTACK);

			CFFSentryGun *pSentryGun = dynamic_cast<CFFSentryGun*>(pSentryObject);
			if (!pSentryGun) { me->PrintIfWatched("Sentry is not CFFSentryGun after build? Idling.\n"); me->Idle(); return; }

			int currentActualLevel = pSentryGun->GetLevel();
			me->SetBuildingLevel(pSentryGun, currentActualLevel); // Sync bot's internal belief

			if (currentActualLevel < m_targetUpgradeLevel && currentActualLevel < pSentryGun->GetMaxUpgradeLevel())
			{
				if (me->GetAmmoCount(AMMO_CELLS) < SENTRY_UPGRADE_COST_CELLS)
				{
					me->PrintIfWatched("BuildSentryState: Not enough cells (%d) for upgrade to L%d. Finding resources.\n", SENTRY_UPGRADE_COST_CELLS, currentActualLevel + 1);
					me->TryToFindResources();
					return;
				}
				me->RemoveAmmo(SENTRY_UPGRADE_COST_CELLS, AMMO_CELLS);
				me->PrintIfWatched("BuildSentryState: Deducted %d cells for L%d upgrade. Remaining: %d\n", SENTRY_UPGRADE_COST_CELLS, currentActualLevel + 1, me->GetAmmoCount(AMMO_CELLS));

				m_isUpgrading = true;
				me->PrintIfWatched("BuildSentryState: Starting upgrade to level %d.\n", currentActualLevel + 1);
				m_buildProgressTimer.Start(ENGINEER_SENTRY_UPGRADE_HIT_CYCLE_TIME);
			}
			else
			{
				me->PrintIfWatched("BuildSentryState: Sentry at target/max level %d. Guarding.\n", currentActualLevel);
				me->TryToGuardSentry();
			}
			return;
		}
		else // Still in initial construction phase (IsBuilding() was true, IsBuilt() is false)
		{
			CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
			if (me->GetActiveFFWeapon() != spanner) {
				if (spanner) me->EquipWeapon(spanner);
				else { me->PrintIfWatched("BuildSentryState: Engineer has no spanner!\n"); me->Idle(); return; }
			}
			me->SetLookAt("Building Sentry", pSentryObject->WorldSpaceCenter(), PRIORITY_HIGH);
			me->PressButton(IN_ATTACK); // Hold primary fire to hit/build
		}
	}
	else if (m_isUpgrading) // Phase 2: Upgrading
	{
		CFFSentryGun *pSentryGun = dynamic_cast<CFFSentryGun*>(m_sentryBeingBuilt.Get());
		if (!pSentryGun || !pSentryGun->IsAlive())
		{
			me->PrintIfWatched("BuildSentryState: Sentry being upgraded became NULL/dead or not a CFFSentryGun. Idling.\n");
			me->Idle();
			return;
		}

		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildSentryState: Engineer has no spanner during upgrade!\n"); m_isUpgrading = false; me->Idle(); return; }
		}

		me->SetLookAt("Upgrading Sentry", pSentryGun->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Keep hitting

		if (m_buildProgressTimer.IsElapsed()) // A cycle of hits for upgrade attempt is done
		{
			me->ReleaseButton(IN_ATTACK); // Stop hitting to check level

			int currentActualLevel = pSentryGun->GetLevel();
			me->SetBuildingLevel(pSentryGun, currentActualLevel);
			me->PrintIfWatched("BuildSentryState: Sentry upgrade hit cycle complete. Current actual level: %d. Target: %d\n", currentActualLevel, m_targetUpgradeLevel);

			if (currentActualLevel < m_targetUpgradeLevel && currentActualLevel < pSentryGun->GetMaxUpgradeLevel())
			{
				if (me->GetAmmoCount(AMMO_CELLS) < SENTRY_UPGRADE_COST_CELLS)
				{
					me->PrintIfWatched("BuildSentryState: Not enough cells (%d) for upgrade to L%d. Finding resources.\n", SENTRY_UPGRADE_COST_CELLS, currentActualLevel + 1);
					m_isUpgrading = false;
					me->TryToFindResources();
					return;
				}
				me->RemoveAmmo(SENTRY_UPGRADE_COST_CELLS, AMMO_CELLS);
				me->PrintIfWatched("BuildSentryState: Deducted %d cells for next sentry upgrade attempt (to L%d). Remaining: %d\n", SENTRY_UPGRADE_COST_CELLS, currentActualLevel + 1, me->GetAmmoCount(AMMO_CELLS));
				m_buildProgressTimer.Start(ENGINEER_SENTRY_UPGRADE_HIT_CYCLE_TIME);
			}
			else
			{
				me->PrintIfWatched("BuildSentryState: Sentry fully upgraded to target/max level %d. Current actual: %d. Guarding.\n", m_targetUpgradeLevel, currentActualLevel);
				m_isUpgrading = false;
				me->TryToGuardSentry();
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildSentryState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "BuildSentryState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();

	if (m_sentryBeingBuilt.Get() && m_sentryBeingBuilt->IsAlive())
	{
		CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_sentryBeingBuilt.Get());
		if (pBuildable && pBuildable->GetHealth() < pBuildable->GetMaxHealth())
		{
			me->PrintIfWatched("BuildSentryState: Sentry built/upgraded, now checking for repairs.\n");
			me->TryToRepairBuildable(pBuildable);
		}
	}

	if (me->GetState() == this && me->GetAmmoCount(AMMO_CELLS) < CFFBot::ENGINEER_LOW_CELL_THRESHOLD)
	{
		me->PrintIfWatched("BuildSentryState: Low on cells after building/repair. Finding resources.\n");
		me->TryToFindResources();
	}

	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_sentryBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_waitForBlueprintTimer.Invalidate();
	m_isUpgrading = false;
}

[end of mp/src/game/server/ff/bot/states/ff_bot_build_sentry.cpp]
