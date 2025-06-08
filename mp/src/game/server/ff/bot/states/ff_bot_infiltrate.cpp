//========= Fortress Forever - Bot Spy Infiltrate State ============//
//
// Purpose: Implements the bot state for Spies infiltrating enemy lines.
//
//=============================================================================//

// FF_CRITICAL_TODO_SPY: Sapping Mechanics Clarification Needed:
// 1. Is FF_WEAPON_SAPPER a dedicated equippable weapon? What is its actual FFWeaponID enum value?
// 2. If FF_WEAPON_SAPPER exists, is IN_ATTACK the correct input to apply it?
// 3. If no dedicated Sapper weapon, how is sapping performed?
//    - Contextual action (IN_USE) when near a buildable with a specific weapon (e.g., Knife) active?
//    - Alt-fire (IN_ATTACK2) of a specific weapon (e.g., Knife) when near a buildable?
// 4. Should bots ever directly call a C++ function like SpyStartSabotaging() (less likely for input-driven AI but possible)?
// This implementation attempts a dedicated Sapper first, then a contextual Knife alt-fire as a fallback.

#include "cbase.h"
#include "ff_bot_infiltrate.h"
#include "../ff_bot_manager.h"
#include "../ff_player.h"
#include "../ff_weapon_base.h"  // For FF_WEAPON_KNIFE, and assumed FF_WEAPON_SAPPER
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

	CBaseEntity *target = me->FindSpyTarget();
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

		if (me->MoveTo(target->GetAbsOrigin(), SAFEST_ROUTE))
		{
			m_repathTimer.Start(BUILD_REPATH_TIME);
		}
		else
		{
			me->PrintIfWatched("InfiltrateState: Unable to path to new target. Idling.\n");
			me->Idle();
		}
	}
	else
	{
		me->PrintIfWatched("InfiltrateState: No spy targets found. Idling.\n");
		me->Idle();
	}
	m_actionTimer.Start(TARGET_REACQUIRE_TIME);
}


//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "InfiltrateState: Entering state.\n" );
	m_isCloaked = me->IsCloaked(); // Sync with actual player cloak status
	m_isAtTarget = false;

	SelectNewSpyTarget(me);
	if (me->GetState() != this) return;

	if ((m_targetEnemyBuilding.Get() || m_targetEnemyPlayer.Get()) && !m_isCloaked)
	{
		me->PrintIfWatched("InfiltrateState: Attempting initial cloak.\n");
		// Using CFFPlayer's Command_SpyCloak or Command_SpySmartCloak might be more robust
		// For now, direct button press assuming it's a toggle.
		me->HandleCommand("cloak"); // Assumes "cloak" is the command for spy cloaking
		m_isCloaked = true;
		// m_actionTimer.Start(CLOAK_UNCLOAK_TIME); // Optional: if cloak takes time and blocks other actions
	}
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnUpdate( CFFBot *me )
{
	CBaseEntity* currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
	m_isCloaked = me->IsCloaked(); // Continuously update cloak status from player

	// Target validation or re-acquisition
	if (!currentTarget || !currentTarget->IsAlive() || m_actionTimer.IsElapsed())
	{
		SelectNewSpyTarget(me);
		if (me->GetState() != this) return;
		currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
		if (!currentTarget) return;
	}

	// Enemy handling
	if (me->GetBotEnemy() != NULL && me->IsEnemyVisible())
	{
		if (m_isCloaked)
		{
			me->PrintIfWatched("InfiltrateState: Enemy visible! Uncloaking to defend.\n");
			me->HandleCommand("cloak"); // Uncloak
			m_isCloaked = false; // Assumes command toggles it
		}
		me->Attack(me->GetBotEnemy());
		return;
	}

	float distToTargetSq = (currentTarget->GetAbsOrigin() - me->GetAbsOrigin()).LengthSqr();

	// Cloak management while moving
	if (!m_isAtTarget && distToTargetSq > Square(MOVEMENT_CLOAK_DIST_THRESHOLD) && !m_isCloaked)
	{
		me->PrintIfWatched("InfiltrateState: Moving to target, cloaking.\n");
		me->HandleCommand("cloak"); // Cloak
		m_isCloaked = true;
	}

	// Movement and Target Interaction
	if (!m_isAtTarget)
	{
		if (distToTargetSq < Square(INTERACTION_RANGE))
		{
			m_isAtTarget = true;
			me->Stop();
			me->PrintIfWatched("InfiltrateState: Arrived at target %s.\n", currentTarget->IsPlayer() ? ToFFPlayer(currentTarget)->GetPlayerName() : currentTarget->GetClassname());
			m_actionTimer.Start(0.5f);
		}
		else
		{
			if (me->UpdatePathMovement() != CFFBot::PROGRESSING && m_repathTimer.IsElapsed())
			{
				if (!me->MoveTo(currentTarget->GetAbsOrigin(), SAFEST_ROUTE))
				{
					SelectNewSpyTarget(me);
					return;
				}
				m_repathTimer.Start(BUILD_REPATH_TIME);
			}
			return;
		}
	}

	// At target, perform action
	if (m_isAtTarget && m_actionTimer.IsElapsed())
	{
		if (m_isCloaked)
		{
			me->PrintIfWatched("InfiltrateState: Uncloaking to act on target.\n");
			me->HandleCommand("cloak"); // Uncloak
			m_isCloaked = false;
			m_actionTimer.Start(CLOAK_UNCLOAK_TIME);
			return;
		}

		// Target is a Building
		if (m_targetEnemyBuilding.Get())
		{
			CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(m_targetEnemyBuilding.Get());
			if (pBuildable && !pBuildable->IsSapped())
			{
				if ( (pBuildable->GetAbsOrigin() - me->GetAbsOrigin()).IsLengthLessThan(INTERACTION_RANGE * 0.9f) )
				{
					// Attempt 1: Dedicated Sapper Weapon
					CFFWeaponBase *sapperWeapon = me->GetWeaponByID(FF_WEAPON_SAPPER); // FF_CRITICAL_TODO_SPY: Verify this ID
					if (sapperWeapon)
					{
						if (me->GetActiveFFWeapon() != sapperWeapon)
						{
							me->EquipWeapon(sapperWeapon);
							me->PrintIfWatched("InfiltrateState: Spy equipping dedicated Sapper for %s.\n", pBuildable->GetClassname());
							m_actionTimer.Start(0.35f); // Slightly longer for weapon switch + aim
							return;
						}

						// Sapper is active
						me->PrintIfWatched("InfiltrateState: Spy attempting to apply dedicated Sapper to %s with IN_ATTACK.\n", pBuildable->GetClassname());
						me->SetLookAt("Sapping Target", m_targetEnemyBuilding->WorldSpaceCenter(), PRIORITY_HIGH);
						me->PressButton(IN_ATTACK); // FF_CRITICAL_TODO: Verify IN_ATTACK is correct for Sapper
						me->ReleaseButton(IN_ATTACK);
						m_actionTimer.Start(SAP_DURATION);
						// SelectNewSpyTarget(me); // Optionally pick new target immediately
						return; // Sapping action taken
					}
					else
					{
						me->PrintIfWatched("InfiltrateState: Spy: FF_WEAPON_SAPPER not found in inventory.\n");
					}

					// Attempt 2: Contextual Action (e.g., Knife Alt-Fire) - This block executes if dedicated sapper logic didn't 'return'
					me->PrintIfWatched("InfiltrateState: Spy attempting contextual sap with Knife for %s.\n", pBuildable->GetClassname());
					CFFWeaponBase *knifeWeapon = me->GetWeaponByID(FF_WEAPON_KNIFE); // FF_CRITICAL_TODO_SPY: Verify this ID
					if (knifeWeapon)
					{
						if (me->GetActiveFFWeapon() != knifeWeapon)
						{
							me->EquipWeapon(knifeWeapon);
							me->PrintIfWatched("InfiltrateState: Spy equipping Knife for contextual sap attempt on %s.\n", pBuildable->GetClassname());
							m_actionTimer.Start(0.35f); // Wait for weapon switch + aim
							return;
						}

						// Knife is active for contextual sap
						me->PrintIfWatched("InfiltrateState: Spy attempting contextual sap (IN_ATTACK2 with Knife) on %s.\n", pBuildable->GetClassname());
						me->SetLookAt("Sapping Target (Contextual)", m_targetEnemyBuilding->WorldSpaceCenter(), PRIORITY_HIGH);
						// FF_CRITICAL_TODO: Verify IN_ATTACK2 is the correct contextual sap input. Could also be IN_USE.
						me->PressButton(IN_ATTACK2);
						me->ReleaseButton(IN_ATTACK2);
						m_actionTimer.Start(SAP_DURATION);
						// SelectNewSpyTarget(me); // Optionally pick new target immediately
						return; // Sapping action taken
					}
					else
					{
						me->PrintIfWatched("InfiltrateState: Spy: FF_WEAPON_KNIFE not found for contextual sap attempt.\n");
						SelectNewSpyTarget(me); // No way to sap, find new target
						return;
					}
				}
				else
				{
					me->PrintIfWatched("InfiltrateState: Too far to start sapping %s. Re-evaluating.\n", pBuildable->GetClassname());
					SelectNewSpyTarget(me);
				}
				return;
			}
			else
			{
				me->PrintIfWatched("InfiltrateState: Target building %s already sapped or invalid. Selecting new target.\n", pBuildable ? pBuildable->GetClassname() : "NULL");
				SelectNewSpyTarget(me);
				return;
			}
		}
		// Target is a Player
		else if (m_targetEnemyPlayer.Get())
		{
			CFFWeaponBase *knife = me->GetWeaponByID(FF_WEAPON_KNIFE);
			if (!knife)
			{
				me->PrintIfWatched("InfiltrateState: Spy has no Knife to backstab/attack! Idling.\n");
				me->Idle();
				return;
			}

			if (me->GetActiveFFWeapon() != knife)
			{
				me->EquipWeapon(knife);
				me->PrintIfWatched("InfiltrateState: Switching to Knife for %s...\n", m_targetEnemyPlayer->GetPlayerName());
				m_actionTimer.Start(0.3f); // Wait for weapon switch
				return;
			}
			// Now Knife is active

			if (me->IsBehind(m_targetEnemyPlayer.Get()))
			{
				me->PrintIfWatched("InfiltrateState: Attempting backstab on %s.\n", m_targetEnemyPlayer->GetPlayerName());
				me->PressButton(IN_ATTACK);
				me->ReleaseButton(IN_ATTACK);
				// FF_TODO_CLASS_SPY: Confirm if a single attack is enough or if it needs to be held or timed for backstab. Assuming single IN_ATTACK for now.
				// The actual backstab (crit damage) is handled by game logic.
				m_actionTimer.Start(BACKSTAB_DURATION);
                m_targetEnemyPlayer = NULL;
				return;
			}
			else
			{
				CFFWeaponBase* spyPrimary = me->GetWeaponByID(FF_WEAPON_TRANQUILISER);
				if (spyPrimary && me->GetActiveFFWeapon() != spyPrimary)
				{
					me->EquipWeapon(spyPrimary);
					m_actionTimer.Start(0.3f); // Wait for weapon switch
					return;
				}
				// Now primary should be active (or Knife if no primary)
				me->PrintIfWatched("InfiltrateState: Engaging %s with current weapon.\n", m_targetEnemyPlayer->GetPlayerName());
				me->SetLookAt("Spy Target", m_targetEnemyPlayer->GetCentroid(), PRIORITY_HIGH);
				me->PressButton(IN_ATTACK);
				m_actionTimer.Start(1.0f);
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "InfiltrateState: Exiting state.\n" );
	if (me->IsCloaked()) // Use player's IsCloaked()
	{
		me->HandleCommand("cloak"); // Ensure uncloaked
	}
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();
	m_targetEnemyBuilding = NULL;
	m_targetEnemyPlayer = NULL;
	m_isAtTarget = false;
	m_actionTimer.Invalidate();
	m_repathTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_infiltrate.cpp]
