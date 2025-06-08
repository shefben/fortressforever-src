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

	// Cover behavior initialization
	m_assessCoverTimer.Start(RandomFloat(1.0f, 2.0f)); // Initial delay before first cover check
	m_isMovingToCover = false;
	m_coverSpot = vec3_invalid;

	// Evasive action (sidestep) initialization
	m_evasiveActionTimer.Start(RandomFloat(0.5f, 1.0f));
	m_isEvading = false;
	m_evadeToSpot = vec3_invalid;
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

	CFFPlayer *enemy = me->GetBotEnemy(); // Changed CBasePlayer to CFFPlayer
	if (enemy == NULL) { StopAttacking( me ); return; }

	// Handle Movement to Cover (High Priority if active)
	if (m_isMovingToCover)
	{
		me->UpdatePathMovement();
		// Using m_repathTimer from AttackState base, ensure it's used appropriately or a new one is made for cover pathing.
		// For now, assume m_repathTimer is okay, or path failure will be caught by IsAtGoal/IsMoving.
		if (me->IsAtGoal() || !me->IsMovingAlongPath() || m_repathTimer.IsYetElapsed())
		{
			m_isMovingToCover = false;
			me->Stop();
			m_assessCoverTimer.Start(RandomFloat(3.0f, 5.0f)); // Cooldown after reaching/failing cover
			me->PrintIfWatched("AttackState: Reached cover spot or path failed/stuck, reassessing.\n");
			// Fall through to regular attack logic for this frame after stopping cover move
		}
		else
		{
			// FF_TODO_AI_BEHAVIOR: Should the bot shoot while moving to cover? For now, prioritize movement.
			me->SetLookAt("Enemy While Moving To Cover", enemy->EyePosition(), PRIORITY_HIGH); // Keep looking at enemy
			return; // Prioritize moving to cover over attacking this frame
		}
	}

	// --- Start Evasive Sidestep Logic ---
	// This is considered if not already committed to a longer cover move.
	if (m_isEvading)
	{
		me->UpdatePathMovement(); // Use existing path follow
		if (me->IsAtGoal() || !me->IsMovingAlongPath() || m_repathTimer.IsYetElapsed()) // Check m_repathTimer from AttackState
		{
			m_isEvading = false;
			me->StopMovement();
			m_evasiveActionTimer.Start(RandomFloat(2.0f, 4.0f)); // Cooldown after completing/failing evasion
			me->PrintIfWatched("AttackState: Evasion sidestep complete/failed. Resuming attack pattern.");
			// Fall through to other attack logic
		}
		else
		{
			// If evading, keep looking at enemy but prioritize movement.
			me->SetLookAt("EvadeLookAtEnemy", enemy->EyePosition(), PRIORITY_HIGH);
			return; // Skip other attack actions this frame
		}
	}
	else if (me->IsEnemyAimingAtMe(enemy) && m_evasiveActionTimer.IsElapsed() && me->IsOnGround() && !m_isMovingToCover) // Don't try to sidestep if already moving to cover
	{
		m_evasiveActionTimer.Start(RandomFloat(2.5f, 4.5f)); // Cooldown for next evasion attempt

		Vector myRight;
		AngleVectors(me->GetAbsAngles(), NULL, &myRight, NULL);
		float evadeDist = RandomFloat(80.0f, 120.0f);
		Vector evadeDir = (RandomFloat(-1.0f, 1.0f) > 0 ? myRight : -myRight);
		Vector evadeAttemptPos = me->GetAbsOrigin() + evadeDir * evadeDist;

		// Find nearest nav area for the sidestep
		CNavArea *pEvadeNavArea = TheNavMesh->GetNearestNavArea(evadeAttemptPos, me->GetCurrentNavArea(), 200.0f, true, true);
		if (pEvadeNavArea && pEvadeNavArea->IsReachable(me))
		{
			m_evadeToSpot = pEvadeNavArea->GetCenter();
			// Check if the evade spot is too close to the enemy or would move into them
			if ((m_evadeToSpot - enemy->GetAbsOrigin()).IsLengthLessThan(50.0f))
			{
				me->PrintIfWatched("AttackState: Sidestep evade spot too close to enemy. Aborting sidestep.");
			}
			else if (me->MoveTo(m_evadeToSpot, FASTEST_ROUTE))
			{
				m_isEvading = true;
				me->PrintIfWatched("AttackState: Enemy aiming! Evading with sidestep to %f %f %f", VEC_T_ARGS(m_evadeToSpot));
				return;
			}
			else
			{
				me->PrintIfWatched("AttackState: Wanted to sidestep evade, but MoveTo failed.");
			}
		}
		else
		{
			me->PrintIfWatched("AttackState: Wanted to sidestep evade, but no suitable nearby nav spot found.");
		}
	}
	// --- End Evasive Sidestep Logic ---

	// Assess Need for Cover (Periodically) - only if not currently evading with a sidestep
	if (m_assessCoverTimer.IsElapsed() && !m_isMovingToCover && !m_isEvading)
	{
		m_assessCoverTimer.Start(RandomFloat(2.0f, 4.0f)); // Time until next assessment. Shorter if under fire.
		bool shouldTakeCover = false;

		if (me->IsEnemyAimingAtMe(enemy))
		{
			if (me->GetHealth() < me->GetMaxHealth() * 0.75f) // More likely to take cover if already hurt
			{
				shouldTakeCover = true;
				m_assessCoverTimer.Start(RandomFloat(1.0f, 2.0f)); // Quicker reassessment if being aimed at and hurt
			}
			else if (me->GetHealth() < me->GetMaxHealth() * 0.9f && RandomFloat(0,1) < 0.3f) // Chance even if less hurt
			{
                 shouldTakeCover = true;
			}
		}
		// FF_TODO_AI_BEHAVIOR: Add other conditions (e.g., enemy is Sniper and has LOS, bot is low on ammo, bot is outgunned by multiple enemies).
		// Example: if (enemy->IsSniper() && me->IsEnemyVisible() && me->GetHealth() < me->GetMaxHealth()) shouldTakeCover = true;

		if (shouldTakeCover)
		{
			me->PrintIfWatched("AttackState: Assessing cover due to enemy aiming or other threat.\n");
			Vector enemyPos = enemy->EyePosition();
			// Vector myPos = me->EyePosition(); // Not used in current simplified LOS check from spot
			CNavArea *currentArea = me->GetCurrentNavArea();
			bool foundGoodCover = false;

			if (currentArea)
			{
				// Check adjacent areas first
				for (int i = 0; i < currentArea->GetConnectionCount(); ++i)
				{
					CNavConnection *conn = currentArea->GetConnection(i);
					if (!conn) continue;
					CNavArea *adjArea = conn->GetConnectedArea();

					if (adjArea && adjArea->IsReachable(me)) // Conceptual: IsReachable might need path test
					{
						Vector spot = adjArea->GetCenter();
						// Check LOS from 'spot' (approximated from nav area center) to 'enemyPos'
						trace_t tr;
						UTIL_TraceLine(spot + Vector(0,0,HumanEyeHeight), enemyPos, MASK_SOLID_NOT_PLAYER, me, COLLISION_GROUP_NONE, &tr);

						if (tr.fraction < 1.0f && tr.m_pEnt != enemy) // LOS is blocked by something other than the enemy itself
						{
							m_coverSpot = spot;
							m_isMovingToCover = true;
							if (me->MoveTo(m_coverSpot, FASTEST_ROUTE)) // Use FASTEST_ROUTE to get to cover
							{
								me->PrintIfWatched("AttackState: Enemy aiming, moving to cover at (%.1f, %.1f, %.1f) in area %d!\n", spot.x, spot.y, spot.z, adjArea->GetID());
								m_repathTimer.Start(1.0f); // Short repath for cover, as it's urgent
								return; // Action taken for this frame
							}
							else
							{
								m_isMovingToCover = false; // Pathing failed
							}
						}
					}
				}
				// FF_TODO_AI_BEHAVIOR: If no adjacent area offers cover, try moving short distance within current area,
				// or a bit further back along current path if retreating from enemy was the last move.
				// Could also try random points around the bot.
			}
			if (!m_isMovingToCover)
			{
				me->PrintIfWatched("AttackState: Wanted cover, but found no suitable spot nearby.\n");
				// If no cover found, might increase aggression of dodging or just fight.
				// For now, just means bot won't move to cover this assessment cycle.
				m_assessCoverTimer.Start(RandomFloat(4.0f, 7.0f)); // Longer cooldown if no spot found
			}
		}
	}


	// If not moving to cover, proceed with normal attack logic:

	// Demoman: Check if should detonate stickies
	if (me->IsDemoman() && me->m_deployedStickiesCount > 0 && me->m_stickyArmTime.IsElapsed())
	{
		// FF_TODO_CLASS_DEMOMAN: More sophisticated check for detonating stickies.
		// e.g., if enemy is near a known trap location (needs bot to remember trap spots).
		// For now, a simple check: if enemy is generally in front and within a certain range.
		if (enemy && me->IsEnemyVisible() && me->IsPlayerFacingMe(enemy, 0.7f) ) // Check if enemy is somewhat in front
		{
			float distToEnemySqr = (enemy->GetAbsOrigin() - me->GetAbsOrigin()).LengthSqr();
			// Detonate if enemy is moderately close, assuming stickies are placed defensively/proximately.
			// This threshold should ideally relate to where traps are typically laid relative to engagement.
			const float detonateRangeCheck = Square(600.0f); // Example: detonate if enemy is within 600 units.
			if (distToEnemySqr < detonateRangeCheck)
			{
				CFFWeaponBase *pipeLauncher = me->GetWeaponByID(FF_WEAPON_PIPELAUNCHER);
				if (me->GetActiveFFWeapon() == pipeLauncher) // Only detonate if pipelauncher is active
				{
					me->PrintIfWatched("Demoman %s: Enemy %s is near potential sticky trap area. Detonating!\n", me->GetPlayerName(), enemy->GetPlayerName());
					me->TryDetonateStickies(enemy);
					// Detonation might take a frame or have a delay, bot might continue other actions or should pause.
					// For now, TryDetonateStickies starts a cooldown.
					// Depending on game mechanics, might want to 'return' here or let other attack logic proceed.
				}
			}
		}
	}

	CFFWeaponBase *weapon = me->GetActiveFFWeapon();
	if (weapon)
	{
		// FF_TODO_WEAPON_STATS: Add any FF specific logic if certain weapons prevent attacking (e.g. while deploying)
		// or if bot needs to switch off a weapon that's empty in AttackState.
		// SelectBestWeaponForSituation in OnEnter and BotThink should mostly handle this.
	}

	// Health-based retreat check (already present, ensure it's not clashing with cover)
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

	// FF_TODO_GAME_MECHANIC: Remove shield logic if not applicable (already done for FF)

	// Heavy Minigun Spin-up/down logic (conceptual, needs CFFBot members like IsMinigunSpunUp)
	if (me->IsHeavy() && weapon && weapon->GetWeaponID() == FF_WEAPON_ASSAULTCANNON) // FF_TODO_WEAPON_STATS: Verify ID
	{
		bool shouldSpin = (me->IsEnemyVisible() && me->GetRangeTo(enemy) < CFFBot::MINIGUN_EFFECTIVE_RANGE);
		// FF_TODO_CLASS_HEAVY: Access actual CFFBot::IsMinigunSpunUp() and CFFBot::SetMinigunSpunUp()
		// For now, direct IN_ATTACK2. This might conflict if IN_ATTACK2 is also used for other things.
		// This logic is simplified and better handled in BotThink or a dedicated weapon handling layer.
		// if (shouldSpin && !me->IsMinigunSpunUpConcept()) // Conceptual check
		// {
		//	me->PressButton(IN_ATTACK2);
		// }
		// else if (!shouldSpin && me->IsMinigunSpunUpConcept())
		// {
		//	me->ReleaseButton(IN_ATTACK2);
		// }
	}


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
	// if (me->IsProtectedByShield()) me->SecondaryAttack(); // FF No Shield
	m_isMovingToCover = false;
	m_isEvading = false;
	if (me->IsMovingAlongPath())
	{
		me->Stop();
	}
}
