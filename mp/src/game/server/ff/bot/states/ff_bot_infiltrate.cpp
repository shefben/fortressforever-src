//========= Fortress Forever - Bot Spy Infiltrate State ============//
//
// Purpose: Implements the bot state for Spies infiltrating enemy lines.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_infiltrate.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"  // For FF_WEAPON_KNIFE, FF_WEAPON_SPANNER (conceptual sapper)
#include "nav_area.h"
#include "ff_buildableobject.h" // For CFFSentryGun, CFFDispenser

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const float BUILD_REPATH_TIME; // Assuming accessible from other build state .cpp files

//--------------------------------------------------------------------------------------------------------------
InfiltrateState::InfiltrateState(void)
{
	m_targetEnemyBuilding = NULL;
	m_targetEnemyPlayer = NULL;
	m_isCloaked = false;
	m_actionTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_isAtTarget = false;
}

//--------------------------------------------------------------------------------------------------------------
const char *InfiltrateState::GetName( void ) const
{
	return "Infiltrate";
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::SelectNewSpyTarget(CFFBot *me)
{
	m_targetEnemyBuilding = NULL;
	m_targetEnemyPlayer = NULL;
	m_isAtTarget = false;

	CBaseEntity *target = me->FindSpyTarget(); // This method needs to be implemented in CFFBot
	if (target)
	{
		if (target->IsPlayer())
		{
			m_targetEnemyPlayer = ToFFPlayer(target);
			me->PrintIfWatched("InfiltrateState: New player target: %s\n", m_targetEnemyPlayer->GetPlayerName());
		}
		else
		{
			m_targetEnemyBuilding = target;
			me->PrintIfWatched("InfiltrateState: New building target: %s\n", m_targetEnemyBuilding->GetClassname());
		}

		if (me->MoveTo(target->GetAbsOrigin(), SAFEST_ROUTE)) // Spies should probably use SAFEST until near target
		{
			m_repathTimer.Start(BUILD_REPATH_TIME); // Use existing constant for now
		}
		else
		{
			me->PrintIfWatched("InfiltrateState: Unable to path to new target. Idling.\n");
			me->Idle(); // Can't path, give up for now
		}
	}
	else
	{
		me->PrintIfWatched("InfiltrateState: No spy targets found. Idling.\n");
		me->Idle();
	}
	m_actionTimer.Start(TARGET_REACQUIRE_TIME); // Time before trying to find a new target if this one fails/completes
}


//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "InfiltrateState: Entering state.\n" );
	m_isCloaked = false; // Assume starts uncloaked or cloak status is unknown
	m_isAtTarget = false;

	SelectNewSpyTarget(me);
	if (me->GetState() != this) return; // Target selection might have immediately changed state to Idle

	// Attempt to cloak immediately if has a target and not already cloaked
	// CFFPlayer::IsCloaked() should be used if available. For now, rely on m_isCloaked.
	if ((m_targetEnemyBuilding.Get() || m_targetEnemyPlayer.Get()) && !m_isCloaked)
	{
		me->PrintIfWatched("InfiltrateState: Attempting initial cloak.\n");
		me->PressButton(IN_ATTACK2); // Assumes IN_ATTACK2 is cloak for Spy
		// me->ReleaseButton(IN_ATTACK2); // Some cloaks might be toggle, others hold? Assume toggle for now.
		m_isCloaked = true;
		// Add a short delay for cloak to activate before moving, if necessary
		// m_actionTimer.Start(CLOAK_UNCLOAK_TIME);
	}
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnUpdate( CFFBot *me )
{
	CBaseEntity* currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();

	// Target validation or re-acquisition
	if (!currentTarget || !currentTarget->IsAlive() || m_actionTimer.IsElapsed())
	{
		SelectNewSpyTarget(me);
		if (me->GetState() != this) return; // Target selection might have changed state
		currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
		if (!currentTarget) return; // Still no target, Idle handled in SelectNewSpyTarget
	}

	// Enemy handling - Spies are generally stealthy but might need to fight if discovered
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		if (m_isCloaked) // Uncloak if enemy is too close or attacking
		{
			me->PrintIfWatched("InfiltrateState: Enemy visible! Uncloaking to defend.\n");
			me->PressButton(IN_ATTACK2); // Uncloak
			m_isCloaked = false;
		}
		// FF_TODO_SPY: More sophisticated Spy combat logic (e.g., use Revolver, or try to flee and re-cloak)
		me->Attack(me->GetBotEnemy());
		return;
	}

	float distToTargetSq = (currentTarget->GetAbsOrigin() - me->GetAbsOrigin()).LengthSqr();

	// Cloak management while moving
	if (!m_isAtTarget && distToTargetSq > Square(MOVEMENT_CLOAK_DIST_THRESHOLD) && !m_isCloaked)
	{
		me->PrintIfWatched("InfiltrateState: Moving to target, cloaking.\n");
		me->PressButton(IN_ATTACK2); // Cloak
		m_isCloaked = true;
	}

	// Movement and Target Interaction
	if (!m_isAtTarget)
	{
		if (distToTargetSq < Square(INTERACTION_RANGE))
		{
			m_isAtTarget = true;
			me->Stop();
			me->PrintIfWatched("InfiltrateState: Arrived at target %s.\n", currentTarget->GetClassname()); // or GetPlayerName
			m_actionTimer.Start(0.5f); // Short delay before "acting"
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				if (!me->MoveTo(currentTarget->GetAbsOrigin(), SAFEST_ROUTE))
				{
					SelectNewSpyTarget(me); // Give up on this target
					return;
				}
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
			return; // Still moving
		}
	}

	// At target, perform action
	if (m_isAtTarget && m_actionTimer.IsElapsed()) // Ensure action timer has elapsed for any previous action like cloaking
	{
		if (m_isCloaked) // Need to uncloak to sap or backstab
		{
			me->PrintIfWatched("InfiltrateState: Uncloaking to act on target.\n");
			me->PressButton(IN_ATTACK2); // Uncloak
			m_isCloaked = false;
			m_actionTimer.Start(CLOAK_UNCLOAK_TIME); // Give time to uncloak
			return; // Wait for uncloak to complete
		}

		// Target is a Building
		if (m_targetEnemyBuilding.Get())
		{
			CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_targetEnemyBuilding.Get());
			if (pBuildable && !pBuildable->IsSapped()) // Don't sap an already sapped building (or handle differently)
			{
				// FF_TODO_SPY: Equip Sapper if it's a distinct weapon, otherwise Spanner might be used conceptually.
				// For now, assume Spanner is used or sapping is a special action.
				me->EquipWeapon(me->GetWeaponByID(FF_WEAPON_SPANNER)); // Placeholder for sapper action
				me->PrintIfWatched("InfiltrateState: Conceptually sapping %s.\n", pBuildable->GetClassname());
				// me->PressButton(IN_USE); // Or some other button for sapping
				m_actionTimer.Start(SAP_DURATION); // Time it takes to sap / time before next action
				// After sapping, logic should make the bot flee or find new target. For now, SelectNewSpyTarget will handle.
				// To ensure it picks a new target next time:
                m_targetEnemyBuilding = NULL; // Mark as "handled"
				return;
			}
			else { SelectNewSpyTarget(me); return; } // Sapped already or invalid, find new target
		}
		// Target is a Player
		else if (m_targetEnemyPlayer.Get())
		{
			if (me->IsBehind(m_targetEnemyPlayer.Get()))
			{
				me->EquipWeapon(me->GetWeaponByID(FF_WEAPON_KNIFE));
				me->PrintIfWatched("InfiltrateState: Conceptually backstabbing %s.\n", m_targetEnemyPlayer->GetPlayerName());
				// me->PressButton(IN_ATTACK); // Primary fire for backstab
				m_actionTimer.Start(BACKSTAB_DURATION);
                m_targetEnemyPlayer = NULL; // Mark as "handled"
				return;
			}
			else // Not behind, or regular attack
			{
				// FF_TODO_SPY: Equip primary weapon (Revolver/Tranq?)
				CFFWeaponBase* spyPrimary = me->GetWeaponByID(FF_WEAPON_TRANQUILISER); // Placeholder, might be a revolver
				if (spyPrimary) me->EquipWeapon(spyPrimary);

				me->PrintIfWatched("InfiltrateState: Engaging %s with primary weapon.\n", m_targetEnemyPlayer->GetPlayerName());
				me->SetLookAt("Spy Target", m_targetEnemyPlayer->GetCentroid(), PRIORITY_HIGH);
				me->PressButton(IN_ATTACK); // Fire primary
				// Let normal combat logic take over if needed, or make Spy flee after a shot
				m_actionTimer.Start(1.0f); // Brief pause before re-evaluating
                // Don't nullify target here, might still be a threat. Re-evaluation will happen.
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "InfiltrateState: Exiting state.\n" );
	if (m_isCloaked)
	{
		me->PressButton(IN_ATTACK2); // Ensure uncloaked
		m_isCloaked = false;
	}
	me->ReleaseButton(IN_ATTACK); // Ensure attack buttons aren't stuck
	me->ClearLookAt();
	m_targetEnemyBuilding = NULL;
	m_targetEnemyPlayer = NULL;
	m_isAtTarget = false;
	m_actionTimer.Invalidate();
	m_repathTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_infiltrate.cpp]
