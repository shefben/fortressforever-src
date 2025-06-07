//========= Fortress Forever - Bot Smarter Medic State ============//
//
// Purpose: Implements the bot state for Medics healing teammates.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_heal_teammate.h"
#include "../ff_bot_manager.h" // For ThePlayers() etc.
#include "../ff_player.h"       // For CFFPlayer
#include "../ff_weapon_base.h"  // For FF_WEAPON_MEDIKIT
#include "player_pickup.h"      // For IteratePlayers


//--------------------------------------------------------------------------------------------------------------
HealTeammateState::HealTeammateState(void)
{
	m_healTarget = NULL;
	m_findPlayerTimer.Invalidate();
	m_repathTimer.Invalidate();
}

//--------------------------------------------------------------------------------------------------------------
const char *HealTeammateState::GetName( void ) const
{
	return "HealTeammate";
}

//--------------------------------------------------------------------------------------------------------------
void HealTeammateState::RequestHealTarget( CFFPlayer *target )
{
	m_healTarget = target;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Private helper to validate the current heal target.
 * Returns true if target is valid, false otherwise (and clears m_healTarget).
 */
bool HealTeammateState::ValidateHealTarget( CFFBot *me )
{
	if ( !m_healTarget.IsValid() || !m_healTarget->IsAlive() || m_healTarget->GetTeamNumber() != me->GetTeamNumber() )
	{
		m_healTarget = NULL;
		return false;
	}
	// Consider fully healed if > 98% health to avoid chasing tiny amounts
	if ( m_healTarget->GetHealth() >= (m_healTarget->GetMaxHealth() * 0.98f) )
	{
		me->PrintIfWatched( "HealTeammateState: Target '%s' is fully healed.\n", m_healTarget->GetPlayerName() );
		m_healTarget = NULL;
		return false;
	}
	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Private helper to find and set a new heal target.
 * Returns true if a target was found and set.
 */
bool HealTeammateState::FindAndSetHealTarget( CFFBot *me )
{
	CFFPlayer *pBestTarget = NULL;
	float flBestTargetHealth = 1.0f; // Health percentage (0.0 to 1.0)
	float flBestTargetDistSq = FLT_MAX;

	CPlayerPickupPlayerEnumerator playerEnum; // From player_pickup.h
	playerpickup_IteratePlayers( &playerEnum, me, me->GetTeamNumber() ); // Only teammates

	for ( int i = 0; i < playerEnum.GetPlayerCount(); ++i )
	{
		CBasePlayer *pPlayer = playerEnum.GetPlayer(i);
		if ( !pPlayer || pPlayer == me || !pPlayer->IsAlive() )
			continue;

		CFFPlayer *pFFPlayer = ToFFPlayer(pPlayer);
		if (!pFFPlayer)
			continue;

		float healthPercent = (float)pFFPlayer->GetHealth() / (float)pFFPlayer->GetMaxHealth();
		if ( healthPercent < 0.98f ) // Needs healing (allow slight overheal to ensure target is not immediately dropped)
		{
			// Prioritize lower health percentage, then distance.
			float distSq = me->GetAbsOrigin().DistToSqr(pFFPlayer->GetAbsOrigin());
			// Simple heuristic: if health is much lower, prefer it even if further.
			// Otherwise, prefer closer target if health is comparable.
			if (healthPercent < (flBestTargetHealth - 0.2f) || (healthPercent < flBestTargetHealth && distSq < flBestTargetDistSq) )
			{
				// Basic visibility/path check (can be expanded)
				if (me->IsVisible(pFFPlayer, false) || me->ComputePath(pFFPlayer->GetAbsOrigin()) ) // Check LoS or if pathable
				{
					pBestTarget = pFFPlayer;
					flBestTargetHealth = healthPercent;
					flBestTargetDistSq = distSq;
				}
			}
		}
	}

	if (pBestTarget)
	{
		me->PrintIfWatched( "HealTeammateState: Found new heal target '%s' (%.0f%% health).\n", pBestTarget->GetPlayerName(), flBestTargetHealth * 100.0f );
		m_healTarget = pBestTarget;
		return true;
	}
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is entering the 'HealTeammate' state.
 */
void HealTeammateState::OnEnter( CFFBot *me )
{
	me->PrintIfWatched( "HealTeammateState: Entering state.\n" );

	if ( !ValidateHealTarget(me) ) // If an initial target wasn't requested or is invalid
	{
		FindAndSetHealTarget(me);
	}

	if ( !m_healTarget.IsValid() )
	{
		me->PrintIfWatched( "HealTeammateState: No valid heal target found on entry. Idling.\n" );
		me->Idle();
		return;
	}

	me->PrintIfWatched( "HealTeammateState: Current target: '%s'.\n", m_healTarget->GetPlayerName() );
	m_findPlayerTimer.Start( 1.0f ); // Check for new targets periodically
	m_repathTimer.Invalidate(); // Will start if pathing
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is healing a teammate.
 */
void HealTeammateState::OnUpdate( CFFBot *me )
{
	// Validate current target first
	if ( !ValidateHealTarget(me) )
	{
		// Target became invalid (dead, fully healed, etc.)
		me->ReleaseButton(IN_ATTACK); // Stop healing beam
		if ( m_findPlayerTimer.IsElapsed() )
		{
			if ( FindAndSetHealTarget(me) )
			{
				m_findPlayerTimer.Start(1.0f); // Reset timer
			}
			else
			{
				me->PrintIfWatched( "HealTeammateState: No new heal target found. Idling.\n" );
				me->Idle();
				return;
			}
		}
		// If no target and timer not elapsed, just wait for next find cycle or Idle if appropriate
		if (!m_healTarget.IsValid()) {
			// Potentially transition to idle sooner if no target for a while
			return;
		}
	}

	// Ensure we have a target after validation/finding
	if ( !m_healTarget.IsValid() )
	{
		me->Idle(); // Should have been caught by above, but as a safeguard
		return;
	}

	// Handle enemies
	// FF_TODO_MEDIC_COMBAT: More sophisticated Medic combat/flee logic
	if ( me->GetBotEnemy() != NULL && me->IsEnemyVisible() )
	{
		me->PrintIfWatched( "HealTeammateState: Enemy sighted while trying to heal. Switching to Attack.\n" );
		me->Attack( me->GetBotEnemy() );
		return;
	}

	// Path to target if not in range
	// FF_TODO_WEAPONS: Get actual Medigun range
	const float MEDIGUN_RANGE = 400.0f; // Estimate
	float distToTargetSq = me->GetAbsOrigin().DistToSqr( m_healTarget->GetAbsOrigin() );

	if ( distToTargetSq > Square(MEDIGUN_RANGE) )
	{
		me->ReleaseButton(IN_ATTACK); // Stop healing if target out of range
		if ( m_repathTimer.IsElapsed() || !me->HasPath() )
		{
			me->PrintIfWatched( "HealTeammateState: Target '%s' out of range. Moving closer.\n", m_healTarget->GetPlayerName() );
			me->MoveTo( m_healTarget->GetAbsOrigin(), FASTEST_ROUTE ); // Medics should probably hurry
			m_repathTimer.Start( RandomFloat(1.5f, 2.5f) );
		}
		if (me->UpdatePathMovement() == CFFBot::PATH_FAILURE)
		{
			me->PrintIfWatched("HealTeammateState: Path to heal target '%s' failed.\n", m_healTarget->GetPlayerName());
			m_healTarget = NULL; // Give up on this target for now
			me->Idle();
			return;
		}
	}
	else // In range
	{
		me->Stop(); // Stop moving if close enough

		// Equip Medigun (FF_WEAPON_MEDIKIT is often the ID for Medigun like weapons)
		CFFWeaponBase *pMedigun = me->GetWeaponByID(FF_WEAPON_MEDIKIT); // Assumes GetWeaponByID exists
		if (pMedigun)
		{
			me->EquipWeapon(pMedigun);
		}
		else
		{
			// This should not happen if bot is a medic and has spawned correctly
			me->PrintIfWatched("HealTeammateState: Medic has no Medigun (FF_WEAPON_MEDIKIT)!\n");
			me->Idle();
			return;
		}

		me->SetLookAt( "Heal Target", m_healTarget->GetCentroid(), PRIORITY_HIGH );
		me->PressButton( IN_ATTACK ); // Hold primary fire to heal
		me->PrintIfWatched( "HealTeammateState: Healing target '%s'.\n", m_healTarget->GetPlayerName() );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Bot is exiting the 'HealTeammate' state.
 */
void HealTeammateState::OnExit( CFFBot *me )
{
	me->PrintIfWatched( "HealTeammateState: Exiting state.\n" );
	me->ReleaseButton(IN_ATTACK);
	me->ClearLookAt();
	m_healTarget = NULL;
}

[end of mp/src/game/server/ff/bot/states/ff_bot_heal_teammate.cpp]
