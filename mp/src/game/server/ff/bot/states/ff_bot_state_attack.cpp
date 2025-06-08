//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_attack.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"

// Local bot utility headers
#include "../bot_constants.h"  // For enums like DodgeStateType, BotTaskType, PriorityType, etc.
#include "../bot_profile.h"    // For GetProfile()
#include "../bot_util.h"       // For PrintIfWatched


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin attacking
 */
void AttackState::OnEnter( CFFBot *me )
{
	if (!me) return; // Null check

	CBasePlayer *enemy = me->GetBotEnemy();

	me->PushPostureContext();
	me->DestroyPath();

	// TODO_FF: Knife logic
	if (enemy && me->IsUsingKnife() && !me->IsPlayerFacingMe( enemy ))
		me->Walk();
	else
		me->Run();

	me->GetOffLadder();
	me->ResetStuckMonitor();

	m_repathTimer.Invalidate();
	m_haveSeenEnemy = me->IsEnemyVisible();
	m_nextDodgeStateTimestamp = 0.0f;
	m_firstDodge = true;
	m_isEnemyHidden = false;
	m_reacquireTimestamp = 0.0f;

	m_pinnedDownTimestamp = gpGlobals->curtime + RandomFloat( 7.0f, 10.0f );

	// TODO_FF: Shield logic
	m_shieldToggleTimestamp = gpGlobals->curtime + RandomFloat( 2.0f, 10.0f );
	m_shieldForceOpen = false;

	// TODO_FF: Bomb logic
	if (me->IsEscapingFromBomb())
		me->EquipBestWeapon();

	if (me->IsUsingKnife()) // TODO_FF: Knife logic
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else if (me->CanSeeSniper() && !me->IsSniper()) // TODO_FF: Sniper logic
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else
	{
		if (!m_crouchAndHold)
		{
			if (enemy && me->GetProfile()) // Null check GetProfile
			{
				const float crouchFarRange = 750.0f;
				float crouchChance;

				if (me->IsUsingSniperRifle()) crouchChance = 50.0f; // TODO_FF: Sniper logic
				else if ((GetCentroid( me ) - GetCentroid( enemy )).IsLengthGreaterThan( crouchFarRange )) crouchChance = 50.0f;
				else crouchChance = 20.0f * (1.0f - me->GetProfile()->GetAggression());

				if (RandomFloat( 0.0f, 100.0f ) < crouchChance)
				{
					trace_t result; Vector origin = GetCentroid( me );
					if (!me->IsCrouching()) origin.z -= 20.0f; // Approx crouch height change
					UTIL_TraceLine( origin, enemy->EyePosition(), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result );
					if (result.fraction == 1.0f) m_crouchAndHold = true;
				}
			}
		}

		if (m_crouchAndHold)
		{
			me->Crouch();
			PrintIfWatched(me, "Crouch and hold attack!\n" ); // Updated PrintIfWatched
		}
	}

	m_scopeTimestamp = 0;
	m_didAmbushCheck = false;

	if (me->GetProfile()) // Null check
	{
		float skill = me->GetProfile()->GetSkill();
		float dodgeChance = 80.0f * skill;
		if (skill > 0.5f && (me->IsOutnumbered() || me->CanSeeSniper())) dodgeChance = 100.0f; // TODO_FF: Sniper logic
		m_shouldDodge = (RandomFloat( 0, 100 ) <= dodgeChance);
		m_isCoward = (RandomFloat( 0, 100 ) > 100.0f * me->GetProfile()->GetAggression());
	} else {
		m_shouldDodge = false;
		m_isCoward = true; // Default to coward if no profile
	}
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::StopAttacking( CFFBot *me )
{
	if (!me) return;
	// TODO_FF: Sniper logic (SNIPING task)
	if (me->GetTask() == CFFBot::BOT_TASK_SNIPING)
	{
		if (me->GetLastKnownArea()) me->Hide( me->GetLastKnownArea(), -1.0f, 50.0f );
	}
	else
	{
		me->StopAttacking();
	}
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::Dodge( CFFBot *me )
{
	if (!me || !me->GetProfile()) return; // Null checks

	if (m_shouldDodge && !me->IsUsingSniperRifle() && !m_crouchAndHold) // TODO_FF: Sniper logic
	{
		CBasePlayer *enemy = me->GetBotEnemy();
		if (enemy == NULL) return;

		Vector toEnemy = enemy->GetAbsOrigin() - me->GetAbsOrigin();
		float range = toEnemy.Length();
		const float hysterisRange = 125.0f;
		float minRange = me->GetCombatRange() - hysterisRange;
		float maxRange = me->GetCombatRange() + hysterisRange;

		if (me->IsUsingKnife()) maxRange = FLT_MAX; // TODO_FF: Knife logic

		if (me->GetProfile()->GetSkill() < 0.66f || !me->IsEnemyVisible())
		{
			if (range > maxRange) me->MoveForward();
			else if (range < minRange) me->MoveBackward();
		}

		const float dodgeRange = 2000.0f;
		if (!me->CanSeeSniper() && (range > dodgeRange || !me->IsPlayerFacingMe( enemy ))) // TODO_FF: Sniper logic
		{
			m_dodgeState = STEADY_ON; // STEADY_ON from DodgeStateType enum (bot_constants.h)
			m_nextDodgeStateTimestamp = 0.0f;
		}
		else if (gpGlobals->curtime >= m_nextDodgeStateTimestamp)
		{
			int next;
			if (me->GetProfile()->GetSkill() > 0.5f && me->CanSeeSniper()) // TODO_FF: Sniper logic
			{
				if (m_firstDodge) next = (RandomInt( 0, 100 ) < 50) ? SLIDE_RIGHT : SLIDE_LEFT; // Enums
				else next = (m_dodgeState == SLIDE_LEFT) ? SLIDE_RIGHT : SLIDE_LEFT; // Enums
			}
			else
			{
				do {
					if (m_firstDodge && me->GetProfile()->GetSkill() < 0.5f && RandomFloat( 0, 100 ) < 33.3f && !me->IsNotMoving())
						next = RandomInt( 0, NUM_ATTACK_STATES-1 ); // NUM_ATTACK_STATES enum count
					else
						next = RandomInt( 0, NUM_ATTACK_STATES-2 ); // Exclude jump if not first dodge
				} while( !m_firstDodge && next == m_dodgeState );
			}
			m_dodgeState = (DodgeStateType)next; // DodgeStateType enum
			m_nextDodgeStateTimestamp = gpGlobals->curtime + RandomFloat( 0.3f, 1.0f );
			m_firstDodge = false;
		}

		Vector forward, right; me->EyeVectors( &forward, &right );
		const float lookAheadRange = 30.0f; float ground;
		switch( m_dodgeState )
		{
			case STEADY_ON: break;
			case SLIDE_LEFT: { Vector pos = me->GetAbsOrigin() - (lookAheadRange * right); if (me->GetSimpleGroundHeightWithFloor( pos, &ground ) && (me->GetAbsOrigin().z - ground < StepHeight)) me->StrafeLeft(); break; } // StepHeight
			case SLIDE_RIGHT: { Vector pos = me->GetAbsOrigin() + (lookAheadRange * right); if (me->GetSimpleGroundHeightWithFloor( pos, &ground ) && (me->GetAbsOrigin().z - ground < StepHeight)) me->StrafeRight(); break; } // StepHeight
			case JUMP: { if (me->IsEnemyVisible()) me->Jump(); break; } // JUMP enum
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetProfile() || !me->GetChatter() || !TheFFBots() || !me->GetGameState()) return; // Null checks

	me->ResetStuckMonitor();

	// TODO_FF: C4 and CS grenade checks
	// CFFWeaponBase *weapon = me->GetActiveFFWeapon();
	// if (weapon) { /* if (weapon->GetWeaponID() == FF_WEAPON_C4_EQUIVALENT || weapon->IsGrenade()) me->EquipBestWeapon(); */ }

	CBasePlayer *enemy = me->GetBotEnemy();
	if (enemy == NULL) { StopAttacking( me ); return; }

	Vector myOrigin = GetCentroid( me );
	Vector enemyOrigin = GetCentroid( enemy );
	if (!m_haveSeenEnemy) m_haveSeenEnemy = me->IsEnemyVisible();

	if (m_retreatTimer.IsElapsed())
	{
		bool isPinnedDown = (gpGlobals->curtime > m_pinnedDownTimestamp);
		if (isPinnedDown || (me->CanSeeSniper() && !me->IsSniper()) || (me->IsOutnumbered() && m_isCoward) || (me->OutnumberedCount() >= 2 && me->GetProfile()->GetAggression() < 1.0f))
		{
			if (me->IsAnyVisibleEnemyLookingAtMe( true )) // CHECK_FOV = true
			{
				if (isPinnedDown) me->GetChatter()->PinnedDown();
				else if (!me->CanSeeSniper()) me->GetChatter()->Scared();
				m_retreatTimer.Start( RandomFloat( 3.0f, 15.0f ) );
				if (me->TryToRetreat()) { if (me->IsOutnumbered()) me->GetChatter()->NeedBackup(); }
				else PrintIfWatched(me, "I want to retreat, but no safe spots nearby!\n" ); // Updated PrintIfWatched
			}
		}
	}

	// TODO_FF: Knife fighting logic
	// if (me->IsUsingKnife()) { /* ... */ return; }

	// TODO_FF: Shield logic
	// if (me->HasShield()) { /* ... */ }

	// TODO_FF: Weapon range checks for FF weapons (sniper min range, shotgun max range)
	// if (me->IsUsingSniperRifle() && (enemyOrigin - myOrigin).IsLengthLessThan(160.0f)) me->EquipPistol();
	// else if (me->IsUsingShotgun() && (enemyOrigin - myOrigin).IsLengthGreaterThan(600.0f)) me->EquipPistol();

	// TODO_FF: Sniper zoom logic for FF
	// if (me->IsUsingSniperRifle()) { /* ... (AdjustZoom, m_scopeTimestamp logic) ... */ return; }

	if (me->IsAwareOfEnemyDeath())
	{
		if (me->GetLastVictimID() == enemy->entindex() && me->GetNearbyEnemyCount() <= 1)
		{
			me->GetChatter()->KilledMyEnemy( enemy->entindex() );
			if (me->GetEnemiesRemaining()) me->Wait( RandomFloat( 1.0f, 3.0f ) );
		}
		StopAttacking( me ); return;
	}

	float notSeenEnemyTime = gpGlobals->curtime - me->GetLastSawEnemyTimestamp();
	if (!me->IsEnemyVisible())
	{
		if (notSeenEnemyTime > 0.5f && me->CanHearNearbyEnemyGunfire())
		{
			StopAttacking( me );
			const Vector *pos = me->GetNoisePosition();
			if (pos) { me->SetLookAt( "Nearby enemy gunfire", *pos, PRIORITY_HIGH, 0.0f ); PrintIfWatched(me, "Checking nearby threatening enemy gunfire!\n" ); } // Updated PrintIfWatched
			return;
		}
		if (notSeenEnemyTime > 0.25f) m_isEnemyHidden = true;
		if (notSeenEnemyTime > 0.1f)
		{
			if (me->GetDisposition() == CFFBot::ENGAGE_AND_INVESTIGATE)
			{
				if (m_haveSeenEnemy && !m_didAmbushCheck)
				{
					if (RandomFloat( 0.0, 100.0f ) < 33.3f)
					{
						const Vector *spot = FindNearbyRetreatSpot( me, 200.0f ); // FindNearbyRetreatSpot
						if (spot) { me->IgnoreEnemies( 1.0f ); me->Run(); me->StandUp(); me->Hide( *spot, RandomFloat(3.0f, 15.0f), true ); return; }
					}
					m_didAmbushCheck = true;
				}
			}
			else { StopAttacking( me ); return; }
		}
	}
	else { m_didAmbushCheck = false; if (m_isEnemyHidden) { m_reacquireTimestamp = gpGlobals->curtime + me->GetProfile()->GetReactionTime(); m_isEnemyHidden = false; } }

	float chaseTime = 2.0f + 2.0f * (1.0f - me->GetProfile()->GetAggression());
	if (me->IsUsingSniperRifle()) chaseTime += 3.0f; else if (me->IsCrouching()) chaseTime += 1.0f;

	if (!me->IsEnemyVisible() && (notSeenEnemyTime > chaseTime || !m_haveSeenEnemy))
	{
		if (me->GetTask() == CFFBot::BOT_TASK_SNIPING) { StopAttacking( me ); return; } // TODO_FF: Sniper logic
		else { me->SetTask( CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION, enemy ); me->MoveTo( me->GetLastKnownEnemyPosition() ); return; }
	}

	if (!me->IsEnemyVisible() && me->GetTimeSinceAttacked() < 3.0f && me->GetAttacker() && me->GetAttacker() != me->GetBotEnemy())
	{
		if (me->IsVisible( me->GetAttacker(), true )) { me->Attack( me->GetAttacker() ); PrintIfWatched(me, "Switching targets to retaliate against new attacker!\n" ); } // Updated PrintIfWatched
		return;
	}

	if (gpGlobals->curtime > m_reacquireTimestamp) me->FireWeaponAtEnemy();
	Dodge( me );
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnExit( CFFBot *me )
{
	if (!me) return;
	PrintIfWatched(me, "AttackState:OnExit()\n" ); // Updated PrintIfWatched
	m_crouchAndHold = false;
	me->ForgetNoise();
	me->ResetStuckMonitor();
	me->PopPostureContext();
	// TODO_FF: Shield logic
	// if (me->IsProtectedByShield()) me->SecondaryAttack();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_attack.cpp]
