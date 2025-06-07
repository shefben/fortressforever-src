//========= Fortress Forever - Bot Engineer Build Dispenser State ============//
//
// Purpose: Implements the bot state for Engineers building Dispensers.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_build_dispenser.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"
#include "nav_area.h"
#include "GameRules.h" // For g_pGameRules
#include "ff_buildableobject.h" // For CFFBuildableObject casting

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define constants for building
const float ENGINEER_DISPENSER_BUILD_TIME = 4.0f; // Placeholder: Time in seconds to initially build a dispenser
// BUILD_PLACEMENT_RADIUS and BUILD_REPATH_TIME can be shared with sentry or defined separately if needed.
// Using existing ones for now.
extern const float BUILD_PLACEMENT_RADIUS; // Defined in ff_bot_build_sentry.cpp, assuming accessible
extern const float BUILD_REPATH_TIME;      // Defined in ff_bot_build_sentry.cpp, assuming accessible
const float ENGINEER_DISPENSER_UPGRADE_TIME_PER_LEVEL = 4.0f; // Placeholder, dispensers might upgrade faster
const int DEFAULT_TARGET_DISPENSER_UPGRADE_LEVEL = 2;    // Target level 2 by default
extern const int DISPENSER_COST_CELLS; // Defined in ff_bot.cpp (or should be moved to a shared consts file)
const int DISPENSER_UPGRADE_COST_CELLS = 40; // FF_TODO_BUILDING: Tune this value


//--------------------------------------------------------------------------------------------------------------
BuildDispenserState::BuildDispenserState(void)
{
	m_buildLocation = vec3_invalid;
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_dispenserBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate();
	m_repathTimer.Invalidate();

	m_isUpgrading = false;
	m_targetUpgradeLevel = DEFAULT_TARGET_DISPENSER_UPGRADE_LEVEL;
	m_currentUpgradeLevel = 1; // Assumes level 1 after initial build
	m_upgradeProgressTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *BuildDispenserState::GetName( void ) const
{
	return "BuildDispenser";
}

//--------------------------------------------------------------------------------------------------------------
void BuildDispenserState::SetBuildLocation(const Vector &location)
{
	m_buildLocation = location;
}

//--------------------------------------------------------------------------------------------------------------
void BuildDispenserState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "BuildDispenserState: Entering state.\n" );
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_dispenserBeingBuilt = NULL;
	m_isUpgrading = false;
	m_currentUpgradeLevel = 1; // Assume starts at level 1 after blueprint
	m_targetUpgradeLevel = DEFAULT_TARGET_DISPENSER_UPGRADE_LEVEL; // Default target

	if (m_buildLocation == vec3_invalid)
	{
		// Placeholder: build 100 units in front.
		Vector forward;
		AngleVectors(me->GetLocalAngles(), &forward);
		m_buildLocation = me->GetAbsOrigin() + forward * 100.0f;
		// TODO: Add a check to ensure this location is on the nav mesh.
		me->PrintIfWatched( "BuildDispenserState: No build location set, choosing default in front.\n" );
	}

	me->PrintIfWatched( "BuildDispenserState: Target build location: (%.1f, %.1f, %.1f).\n",
		m_buildLocation.x, m_buildLocation.y, m_buildLocation.z );

	if (me->MoveTo(m_buildLocation, SAFEST_ROUTE))
	{
		m_repathTimer.Start(BUILD_REPATH_TIME); // Using shared constant
	}
	else
	{
		me->PrintIfWatched( "BuildDispenserState: Unable to path to build location. Idling.\n" );
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildDispenserState::OnUpdate( CFFBot *me )
{
	me->PrintIfWatched( "BuildDispenserState: Updating...\n" );

	// Handle enemy threats first
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("BuildDispenserState: Enemy detected! Aborting build and assessing threat.\n");
		// FF_TODO_ENGINEER: Decide whether to attack or flee. For now, attack.
		me->Attack(me->GetBotEnemy());
		return;
	}

	if (!m_isAtBuildLocation)
	{
		if ((me->GetAbsOrigin() - m_buildLocation).IsLengthLessThan(BUILD_PLACEMENT_RADIUS)) // Using shared constant
		{
			m_isAtBuildLocation = true;
			me->Stop();
			me->PrintIfWatched("BuildDispenserState: Arrived at build location.\n");
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				me->PrintIfWatched("BuildDispenserState: Path failed or stuck. Retrying path to build location.\n");
				if (!me->MoveTo(m_buildLocation, SAFEST_ROUTE))
				{
					me->PrintIfWatched("BuildDispenserState: Still unable to path. Idling.\n");
					me->Idle();
					return;
				}
				m_repathTimer.Start(BUILD_REPATH_TIME); // Using shared constant
			}
			return; // Still moving
		}
	}

	// At build location
	if (!m_isBuilding)
	{
		me->PrintIfWatched("BuildDispenserState: Attempting to place dispenser blueprint.\n");
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (spanner) me->EquipWeapon(spanner);
		else { me->PrintIfWatched("BuildDispenserState: Engineer has no spanner!\n"); me->Idle(); return; }

		// FF_TODO_BUILDING: Simulate selecting/placing Dispenser blueprint.
		// This would involve the bot executing a game command like "build 1" (if 1 is dispenser).
		// me->HandleCommand("builddispenser"); // Or "build 1" etc.
		// The game should then spawn an "obj_dispenser" entity in its initial building state.

		// After conceptual placement, search for the dispenser.
		// FF_TODO_BUILDING: Verify actual dispenser classname "obj_dispenser_blueprint" or "obj_dispenser".
		float searchRadius = 150.0f;
		CBaseEntity *pEntity = NULL;
		while ((pEntity = gEntList.FindEntityInSphere(pEntity, m_buildLocation, searchRadius)) != NULL)
		{
			if (FClassnameIs(pEntity, "obj_dispenser"))
			{
				CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(pEntity);
				if (pBuildable && pBuildable->GetBuilder() == me && pBuildable->IsBuilding())
				{
					m_dispenserBeingBuilt = pBuildable;
					me->PrintIfWatched("BuildDispenserState: Found placed dispenser blueprint: %s\n", pBuildable->GetClassname());
					break;
				}
			}
		}

		if (!m_dispenserBeingBuilt.Get())
		{
			me->PrintIfWatched("BuildDispenserState: Failed to find placed dispenser blueprint nearby. Idling.\n");
			me->Idle();
			return;
		}

		// Resource Check for initial build
		if (me->GetAmmoCount(AMMO_CELLS) < DISPENSER_COST_CELLS)
		{
			me->PrintIfWatched("BuildDispenserState: Not enough cells (%d required) for initial dispenser build. Finding resources.\n", DISPENSER_COST_CELLS);
			me->TryToFindResources(); // Assumes this method exists and transitions state
			return;
		}
		me->RemoveAmmo(DISPENSER_COST_CELLS, AMMO_CELLS); // Deduct resources
		me->PrintIfWatched("BuildDispenserState: Deducted %d cells for dispenser. Remaining: %d\n", DISPENSER_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

		m_isBuilding = true;
		m_buildProgressTimer.Start(ENGINEER_DISPENSER_BUILD_TIME);
		me->PrintIfWatched("BuildDispenserState: Dispenser blueprint found/placed. Starting initial build timer.\n");
	}
	else if (m_isBuilding && !m_isUpgrading) // Initial construction phase
	{
		if (!m_dispenserBeingBuilt.Get())
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser being built became NULL during initial build. Idling.\n");
			me->Idle();
			return;
		}

		// Ensure spanner is equipped
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildDispenserState: Engineer has no spanner during build!\n"); me->Idle(); return; }
		}

		me->SetLookAt("Building Dispenser", m_dispenserBeingBuilt->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Hold primary fire to hit/build

		if (m_buildProgressTimer.IsElapsed())
		{
			me->PrintIfWatched("BuildDispenserState: Initial dispenser construction complete.\n");
			me->ReleaseButton(IN_ATTACK); // Stop hitting for a moment
			m_isBuilding = false; // Initial construction is done

			if (m_dispenserBeingBuilt.Get())
			{
				// FF_TODO_BUILDING: Check actual dispenser level from m_dispenserBeingBuilt->GetLevel() if available.
				m_currentUpgradeLevel = 1;
				if (m_currentUpgradeLevel < m_targetUpgradeLevel)
				{
					// Resource Check for upgrade
					if (me->GetAmmoCount(AMMO_CELLS) < DISPENSER_UPGRADE_COST_CELLS)
					{
						me->PrintIfWatched("BuildDispenserState: Not enough cells (%d required) for dispenser upgrade to level %d. Finding resources.\n", DISPENSER_UPGRADE_COST_CELLS, m_currentUpgradeLevel + 1);
						m_isUpgrading = false; // Stop trying to upgrade this sequence
						me->TryToFindResources();
						return;
					}
					me->RemoveAmmo(DISPENSER_UPGRADE_COST_CELLS, AMMO_CELLS); // Deduct resources
					me->PrintIfWatched("BuildDispenserState: Deducted %d cells for dispenser upgrade. Remaining: %d\n", DISPENSER_UPGRADE_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

					m_isUpgrading = true;
					me->PrintIfWatched("BuildDispenserState: Starting upgrade to level %d.\n", m_currentUpgradeLevel + 1);
					m_upgradeProgressTimer.Start(ENGINEER_DISPENSER_UPGRADE_TIME_PER_LEVEL);
				}
				else
				{
					me->PrintIfWatched("BuildDispenserState: Dispenser already at target level %d. No upgrade needed.\n", m_targetUpgradeLevel);
					me->Idle();
					return;
				}
			}
			else
			{
				me->PrintIfWatched("BuildDispenserState: Dispenser became invalid after initial build. Idling.\n");
				me->Idle();
				return;
			}
		}
	}
	else if (m_isUpgrading) // Upgrading phase
	{
		if (!m_dispenserBeingBuilt.Get())
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser being upgraded became NULL. Idling.\n");
			me->Idle();
			return;
		}

		// Ensure spanner is equipped
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildDispenserState: Engineer has no spanner during upgrade!\n"); me->Idle(); return; }
		}

		me->SetLookAt("Upgrading Dispenser", m_dispenserBeingBuilt->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Keep hitting to upgrade

		// FF_TODO_BUILDING: Ideally, check m_dispenserBeingBuilt->GetLevel() to see if it actually leveled up.
		if (m_upgradeProgressTimer.IsElapsed())
		{
			m_currentUpgradeLevel++;
			me->PrintIfWatched("BuildDispenserState: Dispenser upgraded to level %d.\n", m_currentUpgradeLevel);

			if (m_currentUpgradeLevel < m_targetUpgradeLevel)
			{
				// Resource Check for next upgrade level
				if (me->GetAmmoCount(AMMO_CELLS) < DISPENSER_UPGRADE_COST_CELLS)
				{
					me->PrintIfWatched("BuildDispenserState: Not enough cells (%d required) for dispenser upgrade to level %d. Stopping upgrade.\n", DISPENSER_UPGRADE_COST_CELLS, m_currentUpgradeLevel + 1);
					m_isUpgrading = false; // Stop trying to upgrade
					me->Idle(); // Or TryToFindResources() if preferred
					return;
				}
				me->RemoveAmmo(DISPENSER_UPGRADE_COST_CELLS, AMMO_CELLS); // Deduct resources
				me->PrintIfWatched("BuildDispenserState: Deducted %d cells for dispenser upgrade. Remaining: %d\n", DISPENSER_UPGRADE_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

				me->PrintIfWatched("BuildDispenserState: Starting upgrade to level %d.\n", m_currentUpgradeLevel + 1);
				m_upgradeProgressTimer.Start(ENGINEER_DISPENSER_UPGRADE_TIME_PER_LEVEL);
			}
			else
			{
				me->PrintIfWatched("BuildDispenserState: Dispenser fully upgraded to target level %d.\n", m_targetUpgradeLevel);
				me->ReleaseButton(IN_ATTACK);
				m_isUpgrading = false;
				me->Idle();
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildDispenserState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "BuildDispenserState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();

	// If a dispenser was being built/upgraded, check if it needs immediate repair
	if (m_dispenserBeingBuilt.Get() && m_dispenserBeingBuilt->IsAlive())
	{
		CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_dispenserBeingBuilt.Get());
		if (pBuildable && pBuildable->GetHealth() < pBuildable->GetMaxHealth())
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser built/upgraded, now checking for repairs.\n");
			me->TryToRepairBuildable(pBuildable);
		}
	}

	// After attempting repairs (or if no repair was needed), check if we need resources
	if (me->GetState() == this && me->GetAmmoCount(AMMO_CELLS) < CFFBot::ENGINEER_LOW_CELL_THRESHOLD)
	{
		me->PrintIfWatched("BuildDispenserState: Low on cells after building/repair. Finding resources.\n");
		me->TryToFindResources();
	}

	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_dispenserBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate();
	m_repathTimer.Invalidate();

	m_isUpgrading = false;
	m_upgradeProgressTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_build_dispenser.cpp]
