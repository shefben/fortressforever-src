//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h
#include "../ff_player.h" // Added for CFFPlayer
#include "../ff_bot_manager.h" // For TheFFBots() and team definitions

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin attacking
 */
void AttackState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	CFFPlayer *enemy = me->GetBotEnemy(); // Changed CBasePlayer to CFFPlayer, GetBotEnemy returns CFFPlayer*

	// Select best weapon for the situation first
	if (enemy)
	{
		float flDist = me->GetRangeTo(enemy);
		CFFWeaponBase* pBestWeapon = me->SelectBestWeaponForSituation(enemy, flDist);
		if (pBestWeapon && me->GetActiveFFWeapon() != pBestWeapon)
		{
			me->EquipWeapon(pBestWeapon);
		}
	}
	else
	{
		// No specific enemy, perhaps select a generally good weapon or based on no enemy
		CFFWeaponBase* pBestGeneralWeapon = me->SelectBestWeaponForSituation(NULL, -1.0f);
		if (pBestGeneralWeapon && me->GetActiveFFWeapon() != pBestGeneralWeapon)
		{
			me->EquipWeapon(pBestGeneralWeapon);
		}
	}

	me->PushPostureContext();
	me->DestroyPath();

	if (enemy && me->IsUsingKnife() && !me->IsPlayerFacingMe( enemy )) // IsPlayerFacingMe takes CFFPlayer*
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

	// m_shieldToggleTimestamp = gpGlobals->curtime + RandomFloat( 2.0f, 10.0f ); // FF No Shield
	// m_shieldForceOpen = false; // FF No Shield

	// FF_TODO_AI_BEHAVIOR: Adapt if FF has a similar concept to "escaping from bomb" that requires re-equipping
	// if (me->IsEscapingFromBomb()) // CS Specific
	//	me->EquipBestWeapon();

	if (me->IsUsingKnife()) // FF_TODO_WEAPON_STATS: Adapt for FF melee weapon
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else if (me->CanSeeSniper() && !me->IsSniper()) // Assumes CanSeeSniper and IsSniper are valid for FF
	{
		m_crouchAndHold = false;
		me->StandUp();
	}
	else
	{
		if (!m_crouchAndHold)
		{
			if (enemy)
			{
				const float crouchFarRange = 750.0f;
				float crouchChance;

				if (me->IsUsingSniperRifle()) // FF_TODO_WEAPON_STATS: Adapt for FF sniper rifle
					crouchChance = 50.0f;
				else if ((GetCentroid( me ) - GetCentroid( enemy )).IsLengthGreaterThan( crouchFarRange ))
					crouchChance = 50.0f;
				else
					crouchChance = 20.0f * (1.0f - me->GetProfile()->GetAggression());

				if (RandomFloat( 0.0f, 100.0f ) < crouchChance)
				{
					trace_t result;
					Vector origin = GetCentroid( me );
					if (!me->IsCrouching()) origin.z -= 20.0f; // Approx crouch height change

					UTIL_TraceLine( origin, enemy->EyePosition(), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result );
					if (result.fraction == 1.0f) m_crouchAndHold = true;
				}
			}
		}
		if (m_crouchAndHold) { me->Crouch(); me->PrintIfWatched( "Crouch and hold attack!\n" ); }
	}

	m_scopeTimestamp = 0;
	m_didAmbushCheck = false;
	float skill = me->GetProfile()->GetSkill();
	float dodgeChance = 80.0f * skill;
	if (skill > 0.5f && (me->IsOutnumbered() || me->CanSeeSniper())) dodgeChance = 100.0f;
	m_shouldDodge = (RandomFloat( 0, 100 ) <= dodgeChance);
	m_isCoward = (RandomFloat( 0, 100 ) > 100.0f * me->GetProfile()->GetAggression());
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::StopAttacking( CFFBot *me ) // Changed CCSBot to CFFBot
{
	if (me->GetTask() == CFFBot::SNIPING) // SNIPING is an FF_Bot task
	{
		me->Hide( me->GetLastKnownArea(), -1.0f, 50.0f );
	}
	else
	{
		me->StopAttacking();
	}
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::Dodge( CFFBot *me ) // Changed CCSBot to CFFBot
{
	if (m_shouldDodge && !me->IsUsingSniperRifle() && !m_crouchAndHold) // FF_TODO_WEAPON_STATS: Adapt IsUsingSniperRifle
	{
		CFFPlayer *enemy = me->GetBotEnemy(); // Changed CBasePlayer to CFFPlayer
		if (enemy == NULL) return;

		Vector toEnemy = enemy->GetAbsOrigin() - me->GetAbsOrigin();
		float range = toEnemy.Length();
		const float hysterisRange = 125.0f;
		float minRange = me->GetCombatRange() - hysterisRange;
		float maxRange = me->GetCombatRange() + hysterisRange;

		if (me->IsUsingKnife()) { maxRange = 999999.9f; } // FF_TODO_WEAPON_STATS: Adapt IsUsingKnife

		if (me->GetProfile()->GetSkill() < 0.66f || !me->IsEnemyVisible())
		{
			if (range > maxRange) me->MoveForward();
			else if (range < minRange) me->MoveBackward();
		}

		const float dodgeRange = 2000.0f;
		if (!me->CanSeeSniper() && (range > dodgeRange || !me->IsPlayerFacingMe( enemy ))) // IsPlayerFacingMe takes CFFPlayer
		{
			m_dodgeState = STEADY_ON;
			m_nextDodgeStateTimestamp = 0.0f;
		}
		else if (gpGlobals->curtime >= m_nextDodgeStateTimestamp)
		{
			int next;
			if (me->GetProfile()->GetSkill() > 0.5f && me->CanSeeSniper())
			{
				if (m_firstDodge) next = (RandomInt( 0, 100 ) < 50) ? SLIDE_RIGHT : SLIDE_LEFT;
				else next = (m_dodgeState == SLIDE_LEFT) ? SLIDE_RIGHT : SLIDE_LEFT;
			}
			else
			{
				do {
					const float jumpChance = 33.3f;
					if (m_firstDodge && me->GetProfile()->GetSkill() < 0.5f && RandomFloat( 0, 100 ) < jumpChance && !me->IsNotMoving())
						next = RandomInt( 0, NUM_ATTACK_STATES-1 );
					else
						next = RandomInt( 0, NUM_ATTACK_STATES-2 );
				} while( !m_firstDodge && next == m_dodgeState );
			}
			m_dodgeState = (DodgeStateType)next;
			m_nextDodgeStateTimestamp = gpGlobals->curtime + RandomFloat( 0.3f, 1.0f );
			m_firstDodge = false;
		}

		Vector forward, right; me->EyeVectors( &forward, &right );
		const float lookAheadRange = 30.0f; float ground;
		switch( m_dodgeState )
		{
			case STEADY_ON: break;
			case SLIDE_LEFT: { Vector pos = me->GetAbsOrigin() - (lookAheadRange * right); if (me->GetSimpleGroundHeightWithFloor( pos, &ground )) { if (me->GetAbsOrigin().z - ground < StepHeight) me->StrafeLeft(); } break; }
			case SLIDE_RIGHT: { Vector pos = me->GetAbsOrigin() + (lookAheadRange * right); if (me->GetSimpleGroundHeightWithFloor( pos, &ground )) { if (me->GetAbsOrigin().z - ground < StepHeight) me->StrafeRight(); } break; }
			case JUMP: { if (me->m_isEnemyVisible) me->Jump(); break; }
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->ResetStuckMonitor();

	CFFWeaponBase *weapon = me->GetActiveFFWeapon(); // FF_TODO_WEAPON_STATS: Changed from CBasePlayerWeapon
	if (weapon)
	{
		// FF_TODO_WEAPON_STATS: Replace with FF specific weapon checks if needed. This was for CS C4/grenades.
		// Example: if ( weapon->GetWeaponID() == FF_WEAPON_PIPELAUNCHER && me->IsReloading() ) me->EquipBestWeapon();
		// For now, ensure no direct use of CS Weapon IDs.
		// FFWeaponID currentID = weapon->GetWeaponID();
		// if (currentID == FF_WEAPON_DEPLOYDETPACK || // Example FF deployable
		// 	currentID == FF_WEAPON_GRENADELAUNCHER && weapon->Clip1() == 0) // Example: Don't attack if GL is empty and needs reload
		// {
		// 	me->EquipBestWeapon(); // Or appropriate FF weapon switching logic
		// }
	}

	CFFPlayer *enemy = me->GetBotEnemy(); // Changed CBasePlayer to CFFPlayer
	if (enemy == NULL) { StopAttacking( me ); return; }

	// If health is low, consider retreating
	// Use the constant defined in CFFBot for the threshold
	if (me->IsAlive() && (me->GetHealth() * 1.0f / me->GetMaxHealth()) < CFFBot::RETREAT_HEALTH_THRESHOLD_PERCENT)
	{
		me->PrintIfWatched("AttackState: Health low (%.1f%%), attempting to retreat.\n", (me->GetHealth() * 100.0f) / me->GetMaxHealth());
		me->TryToRetreat(); // Pass NULL for info, or more context if TryToRetreat is enhanced
		if (me->IsRetreating()) // Check if retreat was successful (e.g. not overridden by other conditions)
		{
			return; // Exit AttackState, RetreatState will take over
		}
	}

	Vector myOrigin = GetCentroid( me ); Vector enemyOrigin = GetCentroid( enemy );
	if (!m_haveSeenEnemy) m_haveSeenEnemy = me->IsEnemyVisible();

	if (m_retreatTimer.IsElapsed())
	{
		bool isPinnedDown = (gpGlobals->curtime > m_pinnedDownTimestamp);
		if (isPinnedDown || (me->CanSeeSniper() && !me->IsSniper()) || (me->IsOutnumbered() && m_isCoward) || (me->OutnumberedCount() >= 2 && me->GetProfile()->GetAggression() < 1.0f))
		{
			if (me->IsAnyVisibleEnemyLookingAtMe( CHECK_FOV ))
			{
				if (isPinnedDown) me->GetChatter()->PinnedDown();
				else if (!me->CanSeeSniper()) me->GetChatter()->Scared();
				m_retreatTimer.Start( RandomFloat( 3.0f, 15.0f ) );
				if (me->TryToRetreat()) { if (me->IsOutnumbered()) me->GetChatter()->NeedBackup(); }
				else { me->PrintIfWatched( "I want to retreat, but no safe spots nearby!\n" ); }
			}
		}
	}

	if (me->IsUsingKnife()) // FF_TODO_WEAPON_STATS: Adapt for FF melee
	{
		m_crouchAndHold = false; me->StandUp();
		if (me->IsPlayerFacingMe( enemy )) { me->ForceRun( 5.0f ); me->Hurry( 10.0f ); } // IsPlayerFacingMe takes CFFPlayer
		me->FireWeaponAtEnemy();
		const float slashRange = 70.0f;
		if ((enemy->GetAbsOrigin() - me->GetAbsOrigin()).IsLengthGreaterThan( slashRange ))
		{
			const float repathInterval = 0.5f; bool repath = false;
			if (me->HasPath()) { const float repathRange = 100.0f; if ((me->GetPathEndpoint() - enemy->GetAbsOrigin()).IsLengthGreaterThan( repathRange )) repath = true; }
			else repath = true;
			if (repath && m_repathTimer.IsElapsed()) { Vector enemyPos = enemy->GetAbsOrigin() + Vector( 0, 0, HalfHumanHeight ); me->ComputePath( enemyPos, FASTEST_ROUTE ); m_repathTimer.Start( repathInterval ); }
			if (me->UpdatePathMovement( NO_SPEED_CHANGE ) != CFFBot::PROGRESSING) me->DestroyPath(); // Changed CCSBot to CFFBot
		} return;
	}

	// FF_TODO_GAME_MECHANIC: Remove shield logic if not applicable
	// if (me->HasShield()) { ... }

	if (me->IsUsingSniperRifle()) // FF_TODO_WEAPON_STATS: Adapt for FF sniper
	{
		const float sniperMinRange = 160.0f;
		if ((enemyOrigin - myOrigin).IsLengthLessThan( sniperMinRange )) me->EquipPistol(); // FF_TODO_WEAPON_STATS: Adapt EquipPistol
	}
	else if (me->IsUsingShotgun()) // FF_TODO_WEAPON_STATS: Adapt for FF shotgun
	{
		const float shotgunMaxRange = 600.0f;
		if ((enemyOrigin - myOrigin).IsLengthGreaterThan( shotgunMaxRange )) me->EquipPistol(); // FF_TODO_WEAPON_STATS: Adapt EquipPistol
	}

	if (me->IsUsingSniperRifle()) // FF_TODO_WEAPON_STATS: Adapt for FF sniper
	{
		if (me->m_bResumeZoom) { m_scopeTimestamp = gpGlobals->curtime; return; } // m_bResumeZoom might be CFFPlayer member
		Vector toAimSpot3D = me->m_aimSpot - myOrigin; float targetRange = toAimSpot3D.Length();
		if (me->GetZoomLevel() == CFFBot::NO_ZOOM && me->AdjustZoom( targetRange )) m_scopeTimestamp = gpGlobals->curtime; // Changed CCSBot to CFFBot
		const float waitScopeTime = 0.3f + me->GetProfile()->GetReactionTime();
		if (gpGlobals->curtime - m_scopeTimestamp < waitScopeTime) return;
	}

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
			if (pos) { me->SetLookAt( "Nearby enemy gunfire", *pos, PRIORITY_HIGH, 0.0f ); me->PrintIfWatched( "Checking nearby threatening enemy gunfire!\n" ); return; }
		}
		if (notSeenEnemyTime > 0.25f) m_isEnemyHidden = true;
		if (notSeenEnemyTime > 0.1f)
		{
			if (me->GetDisposition() == CFFBot::ENGAGE_AND_INVESTIGATE) // Changed CCSBot to CFFBot
			{
				if (m_haveSeenEnemy && !m_didAmbushCheck)
				{
					float hideChance = 33.3f;
					if (RandomFloat( 0.0, 100.0f ) < hideChance)
					{
						float ambushTime = RandomFloat( 3.0f, 15.0f );
						const Vector *spot = FindNearbyRetreatSpot( me, 200.0f ); // FindNearbyRetreatSpot takes CFFBot
						if (spot) { me->IgnoreEnemies( 1.0f ); me->Run(); me->StandUp(); me->Hide( *spot, ambushTime, true ); return; }
					}
					m_didAmbushCheck = true;
				}
			} else { StopAttacking( me ); return; }
		}
	} else { m_didAmbushCheck = false; if (m_isEnemyHidden) { m_reacquireTimestamp = gpGlobals->curtime + me->GetProfile()->GetReactionTime(); m_isEnemyHidden = false; } }

	float chaseTime = 2.0f + 2.0f * (1.0f - me->GetProfile()->GetAggression());
	if (me->IsUsingSniperRifle()) chaseTime += 3.0f; // FF_TODO_WEAPON_STATS: Adapt IsUsingSniperRifle
	else if (me->IsCrouching()) chaseTime += 1.0f;

	if (!me->IsEnemyVisible() && (notSeenEnemyTime > chaseTime || !m_haveSeenEnemy))
	{
		if (me->GetTask() == CFFBot::SNIPING) { StopAttacking( me ); return; } // SNIPING is CFFBot task
		else { me->SetTask( CFFBot::MOVE_TO_LAST_KNOWN_ENEMY_POSITION, enemy ); me->MoveTo( me->GetLastKnownEnemyPosition() ); return; } // MOVE_TO_LAST_KNOWN_ENEMY_POSITION is CFFBot task
	}

	const float hurtRecentlyTime = 3.0f;
	if (!me->IsEnemyVisible() && me->GetTimeSinceAttacked() < hurtRecentlyTime && me->GetAttacker() && me->GetAttacker() != me->GetBotEnemy())
	{
		if (me->IsVisible( me->GetAttacker(), CHECK_FOV )) { me->Attack( me->GetAttacker() ); me->PrintIfWatched( "Switching targets to retaliate against new attacker!\n" );}
		return;
	}

	if (gpGlobals->curtime > m_reacquireTimestamp) me->FireWeaponAtEnemy();
	Dodge( me );
}

//--------------------------------------------------------------------------------------------------------------
void AttackState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->PrintIfWatched( "AttackState:OnExit()\n" );
	m_crouchAndHold = false;
	me->ForgetNoise();
	me->ResetStuckMonitor();
	me->PopPostureContext();
	// FF_TODO_GAME_MECHANIC: Remove shield logic if not applicable
	// if (me->IsProtectedByShield()) me->SecondaryAttack();
}
