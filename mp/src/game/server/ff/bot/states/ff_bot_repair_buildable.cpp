//========= Fortress Forever - Bot Engineer Repair Buildable State ============//
//
// Purpose: Implements the bot state for Engineers repairing buildables.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_repair_buildable.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"
#include "nav_area.h"
#include "ff_buildableobject.h" // For CFFBuildableObject casting

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define constants for repairing
const float REPAIR_CHECK_INTERVAL = 0.5f; // How often to check if repair is complete
// Using existing BUILD_PLACEMENT_RADIUS and BUILD_REPATH_TIME from sentry/dispenser build states
extern const float BUILD_PLACEMENT_RADIUS;
extern const float BUILD_REPATH_TIME;

//--------------------------------------------------------------------------------------------------------------
RepairBuildableState::RepairBuildableState(void)
{
	m_targetBuildable = NULL;
	m_repathTimer.Invalidate();
	m_checkRepairTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *RepairBuildableState::GetName( void ) const
{
	return "RepairBuildable";
}

//--------------------------------------------------------------------------------------------------------------
void RepairBuildableState::SetTargetBuildable(CBaseEntity* buildable)
{
	m_targetBuildable = buildable;
}

//--------------------------------------------------------------------------------------------------------------
void RepairBuildableState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "RepairBuildableState: Entering state.\n" );

	if (!m_targetBuildable.Get() || !m_targetBuildable->IsAlive())
	{
		me->PrintIfWatched( "RepairBuildableState: Invalid or no target buildable set. Idling.\n" );
		me->Idle();
		return;
	}

	// Ensure it's a buildable object
	CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_targetBuildable.Get());
	if (!pBuildable)
	{
		me->PrintIfWatched( "RepairBuildableState: Target is not a CFFBuildableObject. Idling.\n" );
		me->Idle();
		return;
	}

	me->PrintIfWatched( "RepairBuildableState: Target buildable: %s at (%.1f, %.1f, %.1f).\n",
		pBuildable->GetClassname(), pBuildable->GetAbsOrigin().x, pBuildable->GetAbsOrigin().y, pBuildable->GetAbsOrigin().z );

	CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
	if (spanner) me->EquipWeapon(spanner);
	else { me->PrintIfWatched("RepairBuildableState: Engineer has no spanner!\n"); me->Idle(); return; }

	if (me->MoveTo(pBuildable->GetAbsOrigin(), SAFEST_ROUTE))
	{
		m_repathTimer.Start(BUILD_REPATH_TIME);
	}
	else
	{
		me->PrintIfWatched( "RepairBuildableState: Unable to path to buildable. Idling.\n" );
		me->Idle();
	}
	m_checkRepairTimer.Start(REPAIR_CHECK_INTERVAL);
}

//--------------------------------------------------------------------------------------------------------------
void RepairBuildableState::OnUpdate( CFFBot *me )
{
	CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_targetBuildable.Get());

	// Initial validation: Is the buildable pointer valid and the entity alive?
	if (!pBuildable || !pBuildable->IsAlive())
	{
		me->PrintIfWatched("RepairBuildableState: Target buildable invalid or destroyed. Idling.\n");
		me->Idle();
		return;
	}

	// Sapper Check & Removal has top priority if at the buildable
	if (pBuildable->IsSapped()) // Assumes IsSapped() exists
	{
		me->PrintIfWatched("RepairBuildableState: Target %s is sapped! Removing sapper.\n", pBuildable->GetClassname());

		// Ensure at buildable to hit sapper
		if ((me->GetAbsOrigin() - pBuildable->GetAbsOrigin()).IsLengthLessThan(BUILD_PLACEMENT_RADIUS))
		{
			me->Stop();
			CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
			if (me->GetActiveFFWeapon() != spanner) {
				if (spanner) me->EquipWeapon(spanner);
				else { me->PrintIfWatched("RepairBuildableState: Engineer has no spanner to remove sapper!\n"); me->Idle(); return; }
			}
			me->SetLookAt("Removing Sapper", pBuildable->WorldSpaceCenter(), PRIORITY_HIGH);
			me->PressButton(IN_ATTACK); // Hit to remove sapper

			// Bot stays in this state, continuously hitting, until IsSapped() is false or other conditions change.
			// The next OnUpdate tick will re-evaluate IsSapped().
			// If it becomes unsapped, the normal repair logic below will kick in if it's also damaged.
			// If it's unsapped and not damaged, the first check in OnUpdate will handle it.
		}
		else // Not at buildable yet, move closer
		{
			me->ReleaseButton(IN_ATTACK); // Ensure not swinging spanner while moving
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				if (!me->MoveTo(pBuildable->GetAbsOrigin(), SAFEST_ROUTE)) { me->Idle(); return; }
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
		}
		return; // Dedicate this update cycle to sapper interaction or moving to it.
	}

	// If not sapped, check if it needs normal repair (health < maxHealth)
	if (pBuildable->GetHealth() >= pBuildable->GetMaxHealth())
	{
		me->PrintIfWatched("RepairBuildableState: Target buildable %s is no longer sapped and is fully repaired. Idling.\n", pBuildable->GetClassname());
		me->Idle();
		return;
	}

	// Handle enemy threats first (if not dealing with a sapper on a present buildable)
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		me->PrintIfWatched("RepairBuildableState: Enemy detected! Aborting repair and assessing threat.\n");
		me->Attack(me->GetBotEnemy()); // Or a more cautious response for an Engineer
		return;
	}

	// Check if at target location for normal repair
	if ((me->GetAbsOrigin() - pBuildable->GetAbsOrigin()).IsLengthLessThan(BUILD_PLACEMENT_RADIUS))
	{
		me->Stop(); // Stop moving if close enough

		CFFWeaponBase *spanner = me->GetWeaponByID(FF_WEAPON_SPANNER);
		if (me->GetActiveFFWeapon() != spanner) {
			if (spanner) me->EquipWeapon(spanner);
			else { me->PrintIfWatched("RepairBuildableState: Engineer has no spanner during repair!\n"); me->Idle(); return; }
		}

		me->SetLookAt("Repairing Buildable", pBuildable->WorldSpaceCenter(), PRIORITY_HIGH);
		me->PressButton(IN_ATTACK); // Hold primary fire to hit/repair

		// FF_TODO_BUILDING: Check resource cost for repairing if applicable in FF.
		// (e.g., if (me->GetAmmoCount(AMMO_CELLS) < REPAIR_TICK_COST) { me->ReleaseButton(IN_ATTACK); me->Idle(); return; })

		if (m_checkRepairTimer.IsElapsed())
		{
			// Health check is now at the top of OnUpdate after sapper check
			// If we are here, it means it was damaged and not sapped.
			// The main health check at the top will transition to Idle if fully repaired.
			m_checkRepairTimer.Start(REPAIR_CHECK_INTERVAL); // Just restart timer to continue repairing
		}
	}
	else // Not at target location yet for normal repair
	{
		me->ReleaseButton(IN_ATTACK); // Ensure not trying to repair while moving
		if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
		{
			me->PrintIfWatched("RepairBuildableState: Path failed or stuck. Retrying path to buildable.\n");
			if (!me->MoveTo(pBuildable->GetAbsOrigin(), SAFEST_ROUTE))
			{
				me->PrintIfWatched("RepairBuildableState: Still unable to path. Idling.\n");
				me->Idle();
				return;
			}
			m_repathTimer.Start(BUILD_REPATH_TIME);
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void RepairBuildableState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "RepairBuildableState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();

	// If the buildable we just repaired was our sentry and it's now fine, try to guard it.
	if (m_targetBuildable.Get() && me->GetSentryGun() == m_targetBuildable.Get())
	{
		CFFSentryGun *sentry = dynamic_cast<CFFSentryGun *>(m_targetBuildable.Get());
		if (sentry && sentry->IsAlive() && !sentry->IsSapped() && sentry->GetHealth() >= sentry->GetMaxHealth())
		{
			me->PrintIfWatched("RepairBuildableState: Repaired own sentry. Considering guarding it.\n");
			me->TryToGuardSentry();
		}
	}

	m_targetBuildable = NULL;
	m_repathTimer.Invalidate();
	m_checkRepairTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_repair_buildable.cpp]
