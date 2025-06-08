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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define constants for building
const float ENGINEER_SENTRY_BUILD_TIME = 5.0f; // Placeholder: Time in seconds to initially build a sentry
const float BUILD_PLACEMENT_RADIUS = 75.0f;   // How close the bot needs to be to the build location
const float BUILD_REPATH_TIME = 2.5f;         // How often to repath if stuck
const float ENGINEER_SENTRY_UPGRADE_TIME_PER_LEVEL = 5.0f; // Placeholder
const int DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL = 2;     // Target level 2 by default (can be changed)
extern const int SENTRY_COST_CELLS; // Defined in ff_bot.cpp (or should be moved to a shared consts file)
const int SENTRY_UPGRADE_COST_CELLS = 50; // FF_TODO_BUILDING: Tune this value

//--------------------------------------------------------------------------------------------------------------
BuildSentryState::BuildSentryState(void)
{
	m_buildLocation = vec3_invalid;
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_sentryBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_waitForBlueprintTimer.Invalidate();

	m_isUpgrading = false;
	m_targetUpgradeLevel = DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL;
	m_currentUpgradeLevel = 1; // Assumes level 1 after initial build
	m_upgradeProgressTimer.Invalidate();
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
/**
 * Bot is entering the 'BuildSentry' state.
 */
void BuildSentryState::OnEnter( CFFBot *me )
{
	me->SelectSpecificBuildable(CFFBot::BUILDABLE_SENTRY); // Ensure correct blueprint is selected
	me->PrintIfWatched( "BuildSentryState: Entering state.\n" );
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_sentryBeingBuilt = NULL;
	m_waitForBlueprintTimer.Invalidate();
	m_isUpgrading = false;
	m_currentUpgradeLevel = 1; // Assume starts at level 1 after blueprint
	m_targetUpgradeLevel = DEFAULT_TARGET_SENTRY_UPGRADE_LEVEL; // Default target

	if (m_buildLocation == vec3_invalid)
	{
		// If no specific location given, try to find a sensible default.
		// Placeholder: build 100 units in front.
		// A real implementation would use strategic point finding (e.g. near guarded objective).
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
/**
 * Bot is building a sentry.
 */
void BuildSentryState::OnUpdate( CFFBot *me )
{
	// Minimal placeholder logic for now. Will be fleshed out in Phase 2.
	me->PrintIfWatched( "BuildSentryState: Updating...\n" );

	// Handle enemy threats first
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
			return; // Still moving
		}
	}

	// At build location
	if (!m_isBuilding)
	{
		me->PrintIfWatched("BuildSentryState: Attempting to place sentry blueprint.\n");
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (spanner) me->EquipWeapon(spanner);
		else { me->PrintIfWatched("BuildSentryState: Engineer has no spanner!\n"); me->Idle(); return; }

		// Simulate selecting Sentry from build menu (e.g., press Attack2)
		// This is highly game-dependent. For now, a placeholder.
		// FF_TODO_BUILDING: If me->GetSelectedBuildable() is not what this state specifically wants (e.g., in BuildSentryState but BUILDABLE_DISPENSER is selected), call me->CycleSelectedBuildable() N times until the correct one is selected or log an error if it cannot be selected.
		// For this subtask, assume SelectSpecificBuildable in OnEnter correctly sets it and no cycling is needed here yet.
		// me->PrimaryAttack(); // This was to "place" the blueprint, now handled by SelectSpecificBuildable (via command) in OnEnter.

		// Start a short timer to wait for the blueprint entity to spawn after the command.
		if (!m_waitForBlueprintTimer.HasStarted())
		{
			m_waitForBlueprintTimer.Start(0.2f); // Short delay (e.g., 200ms)
		}

		// Only try to find the blueprint after the timer has elapsed
		if (!m_waitForBlueprintTimer.IsElapsed())
		{
			return; // Wait for blueprint to spawn
		}

		// After conceptual placement via command (in OnEnter), try to find the placed sentry.
		// This assumes the game has spawned an "obj_sentrygun" (or similar) entity nearby.
		// The actual classname for FF sentries needs to be verified.
		float searchRadius = 150.0f;
		CBaseEntity *pEntity = NULL;
		while ((pEntity = gEntList.FindEntityInSphere(pEntity, m_buildLocation, searchRadius)) != NULL)
		{
			// FF_TODO_BUILDING: Verify actual sentry classname "obj_sentrygun_blueprint" or "obj_sentrygun" in initial state.
			if (FClassnameIs(pEntity, "obj_sentrygun"))
			{
				CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(pEntity);
				// Check if it's ours and if it's in a building phase
				if (pBuildable && pBuildable->GetBuilder() == me && pBuildable->IsBuilding())
				{
					m_sentryBeingBuilt = pBuildable;
					me->PrintIfWatched("BuildSentryState: Found placed sentry blueprint: %s\n", pBuildable->GetClassname());
					break;
				}
			}
		}

		// This search is now inside the IsElapsed() check for m_waitForBlueprintTimer
		if (!m_sentryBeingBuilt.Get()) // Only search if we haven't found it yet
		{
			float searchRadius = 150.0f;
			CBaseEntity *pEntity = NULL;
			while ((pEntity = gEntList.FindEntityInSphere(pEntity, m_buildLocation, searchRadius)) != NULL)
			{
				// FF_TODO_BUILDING: Verify actual sentry classname "obj_sentrygun_blueprint" or "obj_sentrygun" in initial state.
				if (FClassnameIs(pEntity, "obj_sentrygun"))
				{
					CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(pEntity);
					// Check if it's ours and if it's in a building phase
					if (pBuildable && pBuildable->GetBuilder() == me && pBuildable->IsBuilding())
					{
						m_sentryBeingBuilt = pBuildable;
						me->PrintIfWatched("BuildSentryState: Found placed sentry blueprint: %s\n", pBuildable->GetClassname());
						break;
					}
				}
			}
		}

		if (!m_sentryBeingBuilt.Get()) // Check again after attempting to find it
		{
			me->PrintIfWatched("BuildSentryState: Failed to find placed sentry blueprint nearby after waiting. Idling.\n");
			// Potentially try placing again after a delay, or give up.
			me->Idle();
			return;
		}

		// Blueprint found, proceed with building logic.
		// Simulate starting to build
		// Resource Check for initial build
		if (me->GetAmmoCount(AMMO_CELLS) < SENTRY_COST_CELLS)
		{
			me->PrintIfWatched("BuildSentryState: Not enough cells (%d required) for initial sentry build. Finding resources.\n", SENTRY_COST_CELLS);
			me->TryToFindResources(); // Assumes this method exists and transitions state
			return;
		}
		me->RemoveAmmo(SENTRY_COST_CELLS, AMMO_CELLS); // Deduct resources
		me->PrintIfWatched("BuildSentryState: Deducted %d cells for sentry. Remaining: %d\n", SENTRY_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

		m_isBuilding = true;
		m_buildProgressTimer.Start(ENGINEER_SENTRY_BUILD_TIME);
		me->PrintIfWatched("BuildSentryState: Sentry blueprint found/placed. Starting initial build timer.\n");
	}
	else if (m_isBuilding && !m_isUpgrading) // Initial construction phase
	{
		// Ensure spanner is equipped
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildSentryState: Engineer has no spanner during build!\n"); me->Idle(); return; }
		}

		// Aim at the sentry and "hit" it
		if (m_sentryBeingBuilt.Get())
		{
			me->SetLookAt("Building Sentry", m_sentryBeingBuilt->WorldSpaceCenter(), PRIORITY_HIGH);
		}
		else
		{
			// Fallback if sentry handle lost, though should ideally not happen if found once.
			me->SetLookAt("Building Sentry", m_buildLocation, PRIORITY_HIGH);
		}
		me->PressButton(IN_ATTACK); // Hold primary fire to hit/build

		if (m_buildProgressTimer.IsElapsed())
		{
			me->PrintIfWatched("BuildSentryState: Initial sentry construction complete.\n");
			me->ReleaseButton(IN_ATTACK); // Stop hitting for a moment
			m_isBuilding = false; // Initial construction is done

			if (m_sentryBeingBuilt.Get()) // Check if we still have a valid sentry
			{
				// Sync currentUpgradeLevel with the bot's knowledge, which should be set by NotifyBuildingBuilt
				m_currentUpgradeLevel = me->m_sentryLevel; // Should be 1 after initial build
				// FF_TODO_BUILDING: Ideally, CFFBuildableObject::GetLevel() should be used if available and reliable here.

				if (m_currentUpgradeLevel < m_targetUpgradeLevel && m_currentUpgradeLevel > 0) // Ensure level is valid before upgrading
				{
					// Resource Check for upgrade
					if (me->GetAmmoCount(AMMO_CELLS) < SENTRY_UPGRADE_COST_CELLS)
					{
						me->PrintIfWatched("BuildSentryState: Not enough cells (%d required) for sentry upgrade to level %d. Finding resources.\n", SENTRY_UPGRADE_COST_CELLS, m_currentUpgradeLevel + 1);
						m_isUpgrading = false; // Stop trying to upgrade this sequence
						me->TryToFindResources();
						return;
					}
					me->RemoveAmmo(SENTRY_UPGRADE_COST_CELLS, AMMO_CELLS); // Deduct resources
					me->PrintIfWatched("BuildSentryState: Deducted %d cells for sentry upgrade. Remaining: %d\n", SENTRY_UPGRADE_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

					m_isUpgrading = true;
					me->PrintIfWatched("BuildSentryState: Starting upgrade to level %d.\n", m_currentUpgradeLevel + 1);
					m_upgradeProgressTimer.Start(ENGINEER_SENTRY_UPGRADE_TIME_PER_LEVEL);
				}
				else
				{
					me->PrintIfWatched("BuildSentryState: Sentry already at target level %d. No upgrade needed.\n", m_targetUpgradeLevel);
					// me->Idle(); // Already at target level - Change to TryToGuardSentry
					me->TryToGuardSentry();
					return;
				}
			}
			else
			{
				me->PrintIfWatched("BuildSentryState: Sentry became invalid after initial build. Idling.\n");
				me->Idle();
				return;
			}
		}
	}
	else if (m_isUpgrading) // Upgrading phase
	{
		if (!m_sentryBeingBuilt.Get())
		{
			me->PrintIfWatched("BuildSentryState: Sentry being upgraded became NULL. Idling.\n");
			me->Idle();
			return;
		}

		// Ensure spanner is equipped
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildSentryState: Engineer has no spanner during upgrade!\n"); me->Idle(); return; }
		}

		me->SetLookAt("Upgrading Sentry", m_sentryBeingBuilt->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Keep hitting to upgrade

		// FF_TODO_BUILDING: Ideally, check m_sentryBeingBuilt->GetLevel() to see if it actually leveled up.
		// For simulation, we rely on the timer.
		// The actual level increment should be handled by CFFBot::NotifyBuildingUpgraded
		if (m_upgradeProgressTimer.IsElapsed())
		{
			// Sync with the bot's knowledge of the sentry level, which should have been updated by an event
			m_currentUpgradeLevel = me->m_sentryLevel;
			me->PrintIfWatched("BuildSentryState: Sentry upgrade timer elapsed. Current known level: %d.\n", m_currentUpgradeLevel);

			if (m_currentUpgradeLevel < m_targetUpgradeLevel && m_currentUpgradeLevel > 0) // Ensure valid level
			{
				// Resource Check for next upgrade level
				if (me->GetAmmoCount(AMMO_CELLS) < SENTRY_UPGRADE_COST_CELLS)
				{
					me->PrintIfWatched("BuildSentryState: Not enough cells (%d required) for sentry upgrade to level %d. Stopping upgrade.\n", SENTRY_UPGRADE_COST_CELLS, m_currentUpgradeLevel + 1);
					m_isUpgrading = false; // Stop trying to upgrade
					me->Idle(); // Or TryToFindResources() if preferred
					return;
				}
				me->RemoveAmmo(SENTRY_UPGRADE_COST_CELLS, AMMO_CELLS); // Deduct resources
				me->PrintIfWatched("BuildSentryState: Deducted %d cells for sentry upgrade. Remaining: %d\n", SENTRY_UPGRADE_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

				me->PrintIfWatched("BuildSentryState: Starting upgrade to level %d.\n", m_currentUpgradeLevel + 1);
				m_upgradeProgressTimer.Start(ENGINEER_SENTRY_UPGRADE_TIME_PER_LEVEL);
			}
			else
			{
				me->PrintIfWatched("BuildSentryState: Sentry fully upgraded to target level %d.\n", m_targetUpgradeLevel);
				me->ReleaseButton(IN_ATTACK);
				m_isUpgrading = false;
				// FF_TODO_ENGINEER: Transition to a "DefendSentryState" or similar - now TryToGuardSentry
				me->TryToGuardSentry();
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is exiting the 'BuildSentry' state.
 */
void BuildSentryState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "BuildSentryState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK); // Ensure attack button is released
	me->ClearLookAt();

	// If a sentry was being built/upgraded, check if it needs immediate repair (e.g., took damage during process)
	if (m_sentryBeingBuilt.Get() && m_sentryBeingBuilt->IsAlive())
	{
		// Cast to CFFBuildableObject to check health if needed, or just attempt repair
		CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_sentryBeingBuilt.Get());
		if (pBuildable && pBuildable->GetHealth() < pBuildable->GetMaxHealth())
		{
			me->PrintIfWatched("BuildSentryState: Sentry built/upgraded, now checking for repairs.\n");
			me->TryToRepairBuildable(pBuildable);
		}
	}

	// After attempting repairs (or if no repair was needed), check if we need resources
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

	m_isUpgrading = false;
	m_upgradeProgressTimer.Invalidate();
	// m_currentUpgradeLevel and m_targetUpgradeLevel retain values for next entry if needed, or reset in OnEnter.
}

[end of mp/src/game/server/ff/bot/states/ff_bot_build_sentry.cpp]
