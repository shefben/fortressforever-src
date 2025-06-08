//========= Fortress Forever - Bot Spy Infiltrate State ============//
//
// Purpose: Implements the bot state for Spies infiltrating enemy lines.
//
//=============================================================================//

// FF_CRITICAL_TODO_SPY: Sapping Mechanics VERIFIED (2023-10-27):
// - Omnibot button flags (TF_BOT_BUTTON_SABOTAGE_SENTRY/DISPENSER) map to ClientCommands
//   ("sentrysabotage", "dispensersabotage") in omnibot_interface.cpp.
// - These ClientCommands in CFFPlayer directly call pBuildable->Sabotage(this), which is the *final* effect.
// - The player-initiated *timed* sapping action is via CFFPlayer::SpySabotageThink(), which is called
//   in PostThink. It checks for aiming at a buildable + other conditions (low velocity, not cloaked),
//   then calls CFFPlayer::SpyStartSabotaging(), which sets up a timer (m_flSpySabotageFinish).
//   When this timer elapses, SpySabotageThink calls pBuildable->Sabotage(this).
// - CONCLUSION: The bot's current approach (CFFBot::StartSabotaging -> CFFPlayer::SpyStartSabotaging,
//   then this InfiltrateState maintains conditions like aim and stillness) IS THE CORRECT way to simulate
//   player-like timed sapping. The Omnibot commands are for a different, direct sabotage mechanism.
// - TODO: Ensure bot robustly handles interruptions to sap (e.g., if m_hSabotaging on CFFPlayer becomes NULL).
// - TODO: Verify classnames "obj_sentrygun", "obj_dispenser" are correct for FF.

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
	// If m_actionTimer is running for a sap action, don't select new target yet, unless sap was interrupted.
	bool isSapping = (m_targetEnemyBuilding.Get() && static_cast<CFFPlayer*>(me)->m_hSabotaging.Get() == m_targetEnemyBuilding.Get() && !m_actionTimer.IsElapsed());

	if (!isSapping && (!currentTarget || !currentTarget->IsAlive() || m_actionTimer.IsElapsed()))
	{
		SelectNewSpyTarget(me);
		if (me->GetState() != this) return;
		currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
		if (!currentTarget) return;
	}
	else if (isSapping) // Bot is currently in the "sapping" period for a building
	{
		// Check if sap was interrupted (e.g., player moved, LOS broken)
		if (static_cast<CFFPlayer*>(me)->m_hSabotaging.Get() == NULL)
		{
			me->PrintIfWatched("InfiltrateState: Sap on %s seems to have been interrupted early. Re-evaluating.\n", m_targetEnemyBuilding->GetClassname());
			m_actionTimer.Invalidate(); // Force re-evaluation
			SelectNewSpyTarget(me); // Find a new target or re-target
			if (me->GetState() != this) return;
			currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
			if (!currentTarget) return;
		}
		else
		{
			// Maintain conditions for CFFPlayer::SpySabotageThink
			if (me->GetAbsVelocity().LengthSqr() > 10.0f * 10.0f) // Allow very minor movement
			{
				me->StopMovement(); // Stop if moving too much
				me->PrintIfWatched("InfiltrateState: Stopping movement to maintain sap.\n");
			}
			me->SetLookAt("Maintaining Sap", m_targetEnemyBuilding->WorldSpaceCenter(), PRIORITY_HIGH);

			if (m_actionTimer.IsElapsed()) // SAP_DURATION finished
			{
				me->PrintIfWatched("InfiltrateState: SAP_DURATION elapsed for %s. Assuming sap complete. Selecting new target.\n", m_targetEnemyBuilding->GetClassname());
				SelectNewSpyTarget(me); // Sap considered done, find new target
				if (me->GetState() != this) return;
				currentTarget = m_targetEnemyBuilding.Get() ? m_targetEnemyBuilding.Get() : m_targetEnemyPlayer.Get();
				if (!currentTarget) return;
			}
			else
			{
				return; // Continue "sapping" (waiting for timer, maintaining aim/stillness)
			}
		}
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
			if (pBuildable && !pBuildable->IsSapped() && static_cast<CFFPlayer*>(me)->m_hSabotaging.Get() != pBuildable) // Not already sapped by self
			{
				if ( (pBuildable->GetAbsOrigin() - me->GetAbsOrigin()).IsLengthLessThan(INTERACTION_RANGE * 0.9f) )
				{
					me->SetLookAt("Sapping Target", m_targetEnemyBuilding->WorldSpaceCenter(), PRIORITY_HIGH);
					me->StartSabotaging(m_targetEnemyBuilding.Get());
					me->PrintIfWatched("Spy: Called StartSabotaging on %s\n", m_targetEnemyBuilding->GetClassname());

					// SAP_DURATION will now be the time the bot stays "engaged" in the sap attempt.
					// CFFPlayer::SpyStartSabotaging sets m_flSpySabotageFinish = gpGlobals->curtime + 3.0f;
					// SAP_DURATION in this state is 3.1f, so this timer aligns well.
					m_actionTimer.Start(SAP_DURATION);

					// FF_TODO_SPY: How does the bot know SpyStartSabotaging succeeded and m_hSabotaging is set on CFFPlayer?
					// We'll add a check at the start of OnUpdate for this.
					// FF_TODO_SPY: Does the bot need to hold aim or stay near while CFFPlayer::m_hSabotaging is active? Assume yes for SAP_DURATION.
					// This is handled by the new 'isSapping' block at the start of OnUpdate.
					return;
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
				if (pBuildable && static_cast<CFFPlayer*>(me)->m_hSabotaging.Get() == pBuildable)
				{
					// Already in the process of sapping this building (timer running, conditions maintained by logic at top of OnUpdate)
					me->PrintIfWatched("InfiltrateState: Continuing to sap %s.\n", pBuildable->GetClassname());
					// Ensure timer is running to eventually break out if sap finishes or game aborts it without m_hSabotaging clearing.
					if (!m_actionTimer.HasStarted() || m_actionTimer.IsElapsed()) { m_actionTimer.Start(SAP_DURATION); }
				}
				else
				{
					me->PrintIfWatched("InfiltrateState: Target building %s already sapped by someone else or invalid. Selecting new target.\n", pBuildable ? pBuildable->GetClassname() : "NULL");
					SelectNewSpyTarget(me);
				}
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
                                m_targetEnemyPlayer = NULL; // Clear player target after backstab attempt
				// SelectNewSpyTarget(me); // Optionally find a new target immediately after backstab
				return;
			}
			else
			{
				// FF_TODO_CLASS_SPY: Verify FF_WEAPON_TRANQUILISER (or equivalent revolver/primary).
				CFFWeaponBase* spyPrimary = me->GetWeaponByID(FF_WEAPON_TRANQUILISER);
				if (spyPrimary && me->GetActiveFFWeapon() != spyPrimary)
				{
					me->EquipWeapon(spyPrimary);
					m_actionTimer.Start(0.3f); // Wait for weapon switch
					return;
				}
				// Now primary should be active (or Knife if no primary and primary doesn't exist/out of ammo)
				me->PrintIfWatched("InfiltrateState: Engaging %s with current weapon (not behind).\n", m_targetEnemyPlayer->GetPlayerName());
				me->SetLookAt("Spy Target", m_targetEnemyPlayer->GetCentroid(), PRIORITY_HIGH);
				me->PressButton(IN_ATTACK);
				// Let bot manage IN_ATTACK release via normal attack behavior or timer expiry.
				m_actionTimer.Start(1.0f); // Engage for a bit, then re-evaluate.
				return;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void InfiltrateState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "InfiltrateState: Exiting state.\n" );
	if (me->IsCloaked()) // Use player's IsCloaked() to check actual state
	{
		me->HandleCommand("cloak"); // Ensure uncloaked by issuing command again
	}
	me->ReleaseButton(IN_ATTACK); // Ensure attack button isn't stuck if state is exited abruptly
	me->ClearLookAt();
	m_targetEnemyBuilding = NULL;
	m_targetEnemyPlayer = NULL;
	m_isAtTarget = false;
	m_actionTimer.Invalidate();
	m_repathTimer.Invalidate();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_infiltrate.cpp]
