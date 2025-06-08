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
extern const float BUILD_PLACEMENT_RADIUS;
extern const float BUILD_REPATH_TIME;
extern const int DISPENSER_COST_CELLS;
// Removed Dispenser Upgrade specific constants as dispensers do not upgrade in FF.

//--------------------------------------------------------------------------------------------------------------
BuildDispenserState::BuildDispenserState(void)
{
	m_buildLocation = vec3_invalid;
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_dispenserBeingBuilt = NULL;
	m_buildProgressTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_waitForBlueprintTimer.Invalidate();

	// Upgrade logic removed
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
	me->SelectSpecificBuildable(CFFBot::BUILDABLE_DISPENSER);
	me->PrintIfWatched( "BuildDispenserState: Entering state.\n" );
	m_isBuilding = false;
	m_isAtBuildLocation = false;
	m_dispenserBeingBuilt = NULL;
	m_waitForBlueprintTimer.Invalidate();
	// Upgrade logic removed

	if (m_buildLocation == vec3_invalid)
	{
		Vector forward;
		AngleVectors(me->GetLocalAngles(), &forward);
		m_buildLocation = me->GetAbsOrigin() + forward * 100.0f;
		me->PrintIfWatched( "BuildDispenserState: No build location set, choosing default in front.\n" );
	}

	me->PrintIfWatched( "BuildDispenserState: Target build location: (%.1f, %.1f, %.1f).\n",
		m_buildLocation.x, m_buildLocation.y, m_buildLocation.z );

	if (me->MoveTo(m_buildLocation, SAFEST_ROUTE))
	{
		m_repathTimer.Start(BUILD_REPATH_TIME);
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
	// me->PrintIfWatched( "BuildDispenserState: Updating...\n" ); // Too spammy

	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("BuildDispenserState: Enemy detected! Aborting build and assessing threat.\n");
		me->Attack(me->GetBotEnemy());
		return;
	}

	if (!m_isAtBuildLocation)
	{
		if ((me->GetAbsOrigin() - m_buildLocation).IsLengthLessThan(BUILD_PLACEMENT_RADIUS))
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
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
			return;
		}
	}

	// At build location
	if (!m_isBuilding) // Corresponds to initial placement and finding blueprint
	{
		if (!m_dispenserBeingBuilt.Get()) // Try to find/confirm blueprint
		{
			if (!m_waitForBlueprintTimer.HasStarted())
			{
				m_waitForBlueprintTimer.Start(0.3f);
			}
			if (!m_waitForBlueprintTimer.IsElapsed())
			{
				return;
			}

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
				me->PrintIfWatched("BuildDispenserState: Failed to find placed dispenser blueprint nearby after waiting. Idling.\n");
				me->Idle();
				return;
			}
		}

		// Blueprint found, proceed with building logic.
		if (me->GetAmmoCount(AMMO_CELLS) < DISPENSER_COST_CELLS)
		{
			me->PrintIfWatched("BuildDispenserState: Not enough cells (%d required) for initial dispenser build. Finding resources.\n", DISPENSER_COST_CELLS);
			me->TryToFindResources();
			return;
		}
		// Note: Actual resource deduction for buildables in FF might happen automatically when CFFPlayer calls the build command.
		// For bot simulation, we deduct it here to make decisions based on it.
		// me->RemoveAmmo(DISPENSER_COST_CELLS, AMMO_CELLS);
		// me->PrintIfWatched("BuildDispenserState: Deducted %d cells for dispenser. Remaining: %d\n", DISPENSER_COST_CELLS, me->GetAmmoCount(AMMO_CELLS));

		m_isBuilding = true;
		m_buildProgressTimer.Start(ENGINEER_DISPENSER_BUILD_TIME);
		me->PrintIfWatched("BuildDispenserState: Dispenser blueprint found/placed. Starting initial build timer.\n");
	}
	else // Initial construction phase (m_isBuilding is true)
	{
		CFFBuildableObject *pDispenserObject = dynamic_cast<CFFBuildableObject*>(m_dispenserBeingBuilt.Get());
		if (!pDispenserObject)
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser being built became NULL or not a CFFBuildableObject. Idling.\n");
			me->Idle();
			return;
		}

		if (pDispenserObject->IsBuilt())
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser construction complete (IsBuilt() is true).\n");
			me->ReleaseButton(IN_ATTACK);
			// me->NotifyBuildingBuilt(pDispenserObject, CFFBot::BUILDABLE_DISPENSER); // BotManager does this on game event
			// Dispensers do not upgrade in FF. Transition to Idle or another appropriate state.
			me->PrintIfWatched("BuildDispenserState: Dispenser built. Idling.\n");
			me->Idle();
			return;
		}

		// Still in initial construction phase
		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("BuildDispenserState: Engineer has no spanner during build!\n"); me->Idle(); return; }
		}

		me->SetLookAt("Building Dispenser", pDispenserObject->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Hold primary fire to hit/build

		if (m_buildProgressTimer.IsElapsed()) // Fallback timer in case IsBuilt() doesn't become true quickly
		{
			me->PrintIfWatched("BuildDispenserState: Build timer elapsed. Assuming dispenser built.\n");
			me->ReleaseButton(IN_ATTACK);
			// me->NotifyBuildingBuilt(pDispenserObject, CFFBot::BUILDABLE_DISPENSER); // BotManager does this on game event
			me->Idle();
			return;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuildDispenserState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "BuildDispenserState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();

	if (m_dispenserBeingBuilt.Get() && m_dispenserBeingBuilt->IsAlive())
	{
		CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_dispenserBeingBuilt.Get());
		if (pBuildable && pBuildable->GetHealth() < pBuildable->GetMaxHealth())
		{
			me->PrintIfWatched("BuildDispenserState: Dispenser built, now checking for repairs.\n");
			me->TryToRepairBuildable(pBuildable);
		}
	}

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
	m_waitForBlueprintTimer.Invalidate();
	// Upgrade logic members already removed from .h
}

[end of mp/src/game/server/ff/bot/states/ff_bot_build_dispenser.cpp]
