//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h" // For TheFFBots()
#include "../ff_player.h"     // For CFFPlayer, ToCSPlayer (becomes ToFFPlayer if such a func exists)
#include "../../shared/ff/ff_gamerules.h" // For FFGameRules()
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase, FFWeaponID, WEAPONTYPE_*
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For TheNavMesh, CNavArea
#include "nav_pathfind.h"   // For NavAreaTravelDistance, PathCost
#include "bot_constants.h"  // For PriorityType, VisiblePartType, BotTaskType, DispositionType, MoraleType, etc.
#include "bot_profile.h"    // For BotProfile
#include "fmtstr.h"
#include "usermessages.h"   // For user messages if any debug text uses them indirectly.
#include "soundent.h"       // For CSingleUserRecipientFilter, EmitSound (if Bot.FellOff is used)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------------------------------------
float CFFBot::GetMoveSpeed( void )
{
	// TODO: This should probably be based on the class or current weapon in FF
	return 250.0f;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Lightweight maintenance, invoked frequently
 */
void CFFBot::Upkeep( void )
{
	VPROF_BUDGET( "CFFBot::Upkeep", VPROF_BUDGETGROUP_NPCS );

	if (!TheNavMesh || TheNavMesh->IsGenerating() || !IsAlive()) // Added null check for TheNavMesh
		return;
	
	// If bot_flipout is on, then generate some random commands.
	if ( cv_bot_flipout.GetBool() )
	{
		int val = RandomInt( 0, 2 );
		if ( val == 0 )
			MoveForward();
		else if ( val == 1 )
			MoveBackward();
		
		val = RandomInt( 0, 2 );
		if ( val == 0 )
			StrafeLeft();
		else if ( val == 1 )
			StrafeRight();

		if ( RandomInt( 0, 5 ) == 0 )
			Jump( true );
		
		val = RandomInt( 0, 2 );
		if ( val == 0 )
			Crouch();
		else // Removed (val == 1) as it's the only other option
			StandUp();
	
		return;
	}
	
	// BOTPORT: Remove this nasty hack if possible, or ensure GetViewOffset() is correct for FF
	m_eyePosition = EyePosition();

	Vector myOrigin = GetCentroid( this );

	// aiming must be smooth - update often
	if (IsAimingAtEnemy())
	{
		UpdateAimOffset();

		// aim at enemy, if he's still alive
		if (m_enemy != NULL && m_enemy->IsAlive())
		{
			Vector enemyOrigin = GetCentroid( m_enemy.Get() ); // Use .Get() for EHANDLE

			if (m_isEnemyVisible)
			{
				//
				// Enemy is visible - determine which part of him to shoot at
				//
				const float sharpshooter = 0.8f;
				VisiblePartType aimAtPart; // Ensure VisiblePartType is defined (likely bot_constants.h)

				// TODO: Update weapon checks for FF (IsUsingMachinegun, WEAPON_AWP, IsUsingShotgun)
				if (IsUsingMachinegun())
				{
					// spray the big machinegun at the enemy's gut
					aimAtPart = GUT;
				}
				// else if (IsUsing( FF_WEAPON_AWP ) || IsUsingShotgun()) // Example FF weapon enum
				// {
				//	// these weapons are best aimed at the chest
				//	aimAtPart = GUT;
				// }
				else if (GetProfile() && GetProfile()->GetSkill() > 0.5f && IsActiveWeaponRecoilHigh() ) // Null check GetProfile
				{
					// sprayin' and prayin' - aim at the gut since we're not going to be accurate
					aimAtPart = GUT;
				}
				else if (!GetProfile() || GetProfile()->GetSkill() < sharpshooter) // Null check GetProfile
				{
					// low skill bots don't go for headshots
					aimAtPart = GUT;
				}
				else
				{
					// high skill - aim for the head
					aimAtPart = HEAD;
				}

				if (IsEnemyPartVisible( aimAtPart ))
				{
					m_aimSpot = GetPartPosition( GetBotEnemy(), aimAtPart );
				}
				else
				{
					// desired part is blocked - aim at whatever part is visible 
					if (IsEnemyPartVisible( GUT ))
					{
						m_aimSpot = GetPartPosition( GetBotEnemy(), GUT );
					}
					else if (IsEnemyPartVisible( HEAD ))
					{
						m_aimSpot = GetPartPosition( GetBotEnemy(), HEAD );
					}
					else if (IsEnemyPartVisible( LEFT_SIDE ))
					{
						m_aimSpot = GetPartPosition( GetBotEnemy(), LEFT_SIDE );
					}
					else if (IsEnemyPartVisible( RIGHT_SIDE ))
					{
						m_aimSpot = GetPartPosition( GetBotEnemy(), RIGHT_SIDE );
					}
					else // FEET
					{
						m_aimSpot = GetPartPosition( GetBotEnemy(), FEET );
					}
				}
			}
			else
			{
				m_aimSpot = m_lastEnemyPosition;
			}

			// add in aim error
			m_aimSpot.x += m_aimOffset.x;
			m_aimSpot.y += m_aimOffset.y;
			m_aimSpot.z += m_aimOffset.z;

			Vector to = m_aimSpot - EyePositionConst();

			QAngle idealAngle;
			VectorAngles( to, idealAngle );

			// adjust aim angle for recoil, based on bot skill
			const QAngle &punchAngles = GetPunchAngle(); // Ensure GetPunchAngle() is valid for FF
			if (GetProfile()) idealAngle -= punchAngles * GetProfile()->GetSkill(); // Null check GetProfile

			SetLookAngles( idealAngle.y, idealAngle.x );
		}
	}
	else
	{
		if (m_lookAtSpotClearIfClose)
		{
			// dont look at spots just in front of our face - it causes erratic view rotation
			const float tooCloseRange = 100.0f;
			if ((m_lookAtSpot - myOrigin).IsLengthLessThan( tooCloseRange ))
				m_lookAtSpotState = NOT_LOOKING_AT_SPOT; // Enum needs definition
		}

		switch( m_lookAtSpotState ) // Enum needs definition
		{
			case NOT_LOOKING_AT_SPOT:
			{
				// look ahead
				SetLookAngles( m_lookAheadAngle, 0.0f );
				break;
			}

			case LOOK_TOWARDS_SPOT:
			{
				UpdateLookAt();
				if (IsLookingAtPosition( m_lookAtSpot, m_lookAtSpotAngleTolerance ))
				{
					m_lookAtSpotState = LOOK_AT_SPOT;
					m_lookAtSpotTimestamp = gpGlobals->curtime;
				}
				break;
			}

			case LOOK_AT_SPOT:
			{
				UpdateLookAt();

				if (m_lookAtSpotDuration >= 0.0f && gpGlobals->curtime - m_lookAtSpotTimestamp > m_lookAtSpotDuration)
				{
					m_lookAtSpotState = NOT_LOOKING_AT_SPOT;
					m_lookAtSpotDuration = 0.0f;
				}
				break;
			}
		}

		// have view "drift" very slowly, so view looks "alive"
		if (!IsUsingSniperRifle()) // TODO: Update for FF sniper check
		{
			float driftAmplitude = 2.0f;
			if (IsBlind())
			{
				driftAmplitude = 5.0f;
			}
			// BotCOS and BotSIN are likely CS specific, replace with standard cos/sin or engine equivalents
			m_lookYaw += driftAmplitude * cos( 33.0f * gpGlobals->curtime * M_PI/180.0f );
			m_lookPitch += driftAmplitude * sin( 13.0f * gpGlobals->curtime * M_PI/180.0f );
		}
	}

	// view angles can change quickly
	UpdateLookAngles();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Heavyweight processing, invoked less often
 */
void CFFBot::Update( void )
{
	VPROF_BUDGET( "CFFBot::Update", VPROF_BUDGETGROUP_NPCS );

	// If bot_flipout is on, then we only do stuff in Upkeep().
	if ( cv_bot_flipout.GetBool() )
		return;

	Vector myOrigin = GetCentroid( this );

	// if we are spectating, get into the game
	// TODO: Update for FF teams (TEAM_UNASSIGNED, TEAM_CT, TEAM_TERRORIST, FIRST_CT_CLASS, FIRST_T_CLASS)
	if (GetTeamNumber() == TEAM_UNASSIGNED)
	{
		HandleCommand_JoinTeam( m_desiredTeam );
		if (GetProfile()) { // Null check
			int desiredClass = GetProfile()->GetSkin();
			// if ( m_desiredTeam == TEAM_CT && desiredClass )
			// {
			//	desiredClass = FIRST_CT_CLASS + desiredClass - 1;
			// }
			// else if ( m_desiredTeam == TEAM_TERRORIST && desiredClass )
			// {
			//	desiredClass = FIRST_T_CLASS + desiredClass - 1;
			// }
			// HandleCommand_JoinClass( desiredClass ); // This needs to be FF compatible
		}
		return;
	}


	// update our radio chatter
	// need to allow bots to finish their chatter even if they are dead
	GetChatter()->Update();
	
	// check if we are dead
	if (!IsAlive())
	{
		// remember that we died
		m_diedLastRound = true;

		BotDeathThink();
		return;
	}

	// the bot is alive and in the game at this point
	m_hasJoined = true;

	//
	// Debug beam rendering
	//

	if (cv_bot_debug.GetBool() && IsLocalPlayerWatchingMe())
	{
		DebugDisplay();
	}

	if (cv_bot_stop.GetBool())
		return;

	// check if we are stuck
	StuckCheck();

	// Check for physics props and other breakables in our way that we can break
	BreakablesCheck();

	// Check for useable doors in our way that we need to open
	DoorCheck();

	// update travel distance to all players (this is an optimization)
	UpdateTravelDistanceToAllPlayers();

	// if our current 'noise' was heard a long time ago, forget it
	const float rememberNoiseDuration = 20.0f;
	if (m_noiseTimestamp > 0.0f && gpGlobals->curtime - m_noiseTimestamp > rememberNoiseDuration)
	{
		ForgetNoise();
	}

	// where are we
	if (!m_currentArea || !m_currentArea->Contains( myOrigin ))
	{
		if (TheNavMesh) m_currentArea = (CNavArea *)TheNavMesh->GetNavArea( myOrigin ); // Cast from CNavArea, ensure TheNavMesh is not null
	}

	// track the last known area we were in
	if (m_currentArea && m_currentArea != m_lastKnownArea)
	{
		m_lastKnownArea = m_currentArea;

		OnEnteredNavArea( m_currentArea );
	}

	// keep track of how long we have been motionless
	const float stillSpeed = 10.0f;
	if (GetAbsVelocity().IsLengthLessThan( stillSpeed ))
	{
		m_stillTimer.Start();
	}
	else
	{
		m_stillTimer.Invalidate();
	}

	// if we're blind, retreat!
	if (IsBlind())
	{
		if (m_blindFire)
		{
			PrimaryAttack();
		}
	}

	UpdatePanicLookAround();

	//
	// Enemy acquisition and attack initiation
	//

	// take a snapshot and update our reaction time queue
	UpdateReactionQueue();

	// "threat" may be the same as our current enemy
	CFFPlayer *threat = GetRecognizedEnemy();
	if (threat)
	{
		Vector threatOrigin = GetCentroid( threat );

		// adjust our personal "safe" time
		AdjustSafeTime();

		BecomeAlert();

		const float selfDefenseRange = 500.0f; // 750.0f;
		const float farAwayRange = 2000.0f;

		//
		// Decide if we should attack
		//
		bool doAttack = false;
		// TODO: Update DispositionType enums for FF
		switch( GetDisposition() )
		{
			case CFFBot::IGNORE_ENEMIES:
			{
				// never attack
				doAttack = false;
				break;
			}

			case CFFBot::SELF_DEFENSE:
			{
				// attack if fired on
				doAttack = (IsPlayerLookingAtMe( threat, 0.99f ) && DidPlayerJustFireWeapon( threat ));

				// attack if enemy very close
				if (!doAttack)
				{
					doAttack = (myOrigin - threatOrigin).IsLengthLessThan( selfDefenseRange );
				}

				break;
			}

			case CFFBot::ENGAGE_AND_INVESTIGATE:
			case CFFBot::OPPORTUNITY_FIRE:
			{
				if ((myOrigin - threatOrigin).IsLengthGreaterThan( farAwayRange ))
				{
					// enemy is very far away - wait to take our shot until he is closer
					// unless we are a sniper or he is shooting at us
					if (IsSniper()) // TODO: Update for FF sniper check
					{
						// snipers love far away targets
						doAttack = true;
					}
					else
					{
						// attack if fired on
						doAttack = (IsPlayerLookingAtMe( threat, 0.99f ) && DidPlayerJustFireWeapon( threat ));					
					}
				}
				else
				{
					// normal combat range
					doAttack = true;
				}

				break;
			}
		}

		// if we aren't attacking but we are being attacked, retaliate
		if (!doAttack && !IsAttacking() && GetDisposition() != CFFBot::IGNORE_ENEMIES)
		{
			const float recentAttackDuration = 1.0f;
			if (GetTimeSinceAttacked() < recentAttackDuration)
			{
				doAttack = true;
				PrintIfWatched( "Ouch! Retaliating!\n" );
			}
		}

		if (doAttack)
		{
			if (!IsAttacking() || threat != GetBotEnemy())
			{
				// TODO: Update for FF knife
				if (IsUsingKnife() && IsHiding())
				{
					// if hiding with a knife, wait until threat is close
					const float knifeAttackRange = 250.0f;
					if ((GetAbsOrigin() - threat->GetAbsOrigin()).IsLengthLessThan( knifeAttackRange ))
					{
						Attack( threat );
					}
				}
				else
				{
					Attack( threat );
				}
			}
		}
		else
		{
			// dont attack, but keep track of nearby enemies
			SetBotEnemy( threat );
			m_isEnemyVisible = true;
		}

		if (TheFFBots()) TheFFBots()->SetLastSeenEnemyTimestamp(); // Null check
	}

	//
	// Validate existing enemy, if any
	//
	if (m_enemy != NULL)
	{
		if (IsAwareOfEnemyDeath())
		{
			// we have noticed that our enemy has died
			m_enemy = NULL;
			m_isEnemyVisible = false;
		}
		else
		{
			// check LOS to current enemy (chest & head), in case he's dead (GetNearestEnemy() only returns live players)
			// note we're not checking FOV - once we've acquired an enemy (which does check FOV), assume we know roughly where he is
			if (IsVisible( m_enemy.Get(), false, &m_visibleEnemyParts )) // Use .Get() for EHANDLE
			{
				m_isEnemyVisible = true;
				m_lastSawEnemyTimestamp = gpGlobals->curtime;
				m_lastEnemyPosition = GetCentroid( m_enemy.Get() ); // Use .Get() for EHANDLE
			}
			else
			{
				m_isEnemyVisible = false;
			}
				
			// check if enemy died
			if (m_enemy.IsValid() && m_enemy->IsAlive()) // Use .IsValid() before .Get()
			{
				m_enemyDeathTimestamp = 0.0f;
				m_isLastEnemyDead = false;
			}
			else if (m_enemyDeathTimestamp == 0.0f)
			{
				// note time of death (to allow bots to overshoot for a time)
				m_enemyDeathTimestamp = gpGlobals->curtime;
				m_isLastEnemyDead = true;
			}
		}
	}
	else
	{
		m_isEnemyVisible = false;
	}


	// if we have seen an enemy recently, keep an eye on him if we can
	if (!IsBlind() && !IsLookingAtSpot(PRIORITY_UNINTERRUPTABLE) ) // PRIORITY_UNINTERRUPTABLE
	{
		const float seenRecentTime = 3.0f;
		if (m_enemy != NULL && GetTimeSinceLastSawEnemy() < seenRecentTime)
		{
			AimAtEnemy();
		}
		else
		{
			StopAiming();
		}
	}
	else if( IsAimingAtEnemy() )
	{
		StopAiming();
	}

	//
	// Hack to fire while retreating
	/// @todo Encapsulate aiming and firing on enemies separately from current task
	//
	if (GetDisposition() == CFFBot::IGNORE_ENEMIES) // Enum access
	{
		FireWeaponAtEnemy();
	}

	// toss grenades
	LookForGrenadeTargets();

	// process grenade throw state machine
	UpdateGrenadeThrow();

	// avoid enemy grenades
	AvoidEnemyGrenades();


	// check if our weapon is totally out of ammo
	// or if we no longer feel "safe", equip our weapon
	// TODO: Update IsUsingGrenade() for FF
	if (!IsSafe() && !IsUsingGrenade() && IsActiveWeaponOutOfAmmo())
	{
		EquipBestWeapon();
	}

	/// @todo This doesn't work if we are restricted to just knives and sniper rifles because we cant use the rifle at close range
	// TODO: Update IsUsingGrenade() and IsUsingKnife() for FF
	if (!IsSafe() && !IsUsingGrenade() && IsUsingKnife() && !IsEscapingFromBomb())
	{
		EquipBestWeapon();
	}

	// if we haven't seen an enemy in awhile, and we switched to our pistol during combat, 
	// switch back to our primary weapon (if it still has ammo left)
	const float safeRearmTime = 5.0f;
	if (!IsReloading() && IsUsingPistol() && !IsPrimaryWeaponEmpty() && GetTimeSinceLastSawEnemy() > safeRearmTime) // TODO: Update IsUsingPistol for FF
	{
		EquipBestWeapon();
	}

	// reload our weapon if we must
	ReloadCheck();

	// equip silencer
	SilencerCheck(); // TODO: Silencer logic might be CS specific

	// listen to the radio
	RespondToRadioCommands();

	// make way
	const float avoidTime = 0.33f;
	if (gpGlobals->curtime - m_avoidTimestamp < avoidTime && m_avoid != NULL)
	{
		StrafeAwayFromPosition( GetCentroid( m_avoid.Get() ) ); // Use .Get() for EHANDLE
	}
	else
	{
		m_avoid = NULL;
	}

	// if we're using a sniper rifle and are no longer attacking, stop looking thru scope
	// TODO: Update IsUsingSniperRifle and IsUsingScope for FF
	if (!IsAtHidingSpot() && !IsAttacking() && IsUsingSniperRifle() && IsUsingScope())
	{
		SecondaryAttack();
	}

	if (!IsBlind())
	{
		// check encounter spots
		UpdatePeripheralVision();

		// watch for snipers
		// TODO: Update for FF sniper logic
		if (CanSeeSniper() && !HasSeenSniperRecently())
		{
			GetChatter()->SpottedSniper();

			const float sniperRecentInterval = 20.0f;
			m_sawEnemySniperTimer.Start( sniperRecentInterval );
		}

		//
		// Update gamestate
		//
		if (m_bomber != NULL)
			GetChatter()->SpottedBomber( GetBomber() ); // TODO: Update GetBomber for FF bomb logic

		// TODO: Update for FF bomb logic
		if (TheFFBots() && CanSeeLooseBomb()) // Null check
			GetChatter()->SpottedLooseBomb( TheFFBots()->GetLooseBomb() );
	}

	//
	// Scenario interrupts
	// TODO: Entire scenario logic section needs heavy FF adaptation
	if (TheFFBots()) { // Null check
		switch (TheFFBots()->GetScenario())
		{
			case CFFBotManager::SCENARIO_DEFUSE_BOMB: // CS Specific
			{
				// flee if the bomb is ready to blow and we aren't defusing it or attacking and we know where the bomb is
				// (aggressive players wait until its almost too late)
				float gonnaBlowTime = 8.0f - (2.0f * (GetProfile() ? GetProfile()->GetAggression() : 0.5f)); // Null check GetProfile

				// if we have a defuse kit, can wait longer
				// if (m_bHasDefuser) // m_bHasDefuser is CS specific
				// 	gonnaBlowTime *= 0.66f;

				if (!IsEscapingFromBomb() &&								// we aren't already escaping the bomb
					TheFFBots()->IsBombPlanted() &&							// is the bomb planted
					GetGameState()->IsPlantedBombLocationKnown() &&			// we know where the bomb is
					TheFFBots()->GetBombTimeLeft() < gonnaBlowTime &&		// is the bomb about to explode
					!IsDefusingBomb() &&									// we aren't defusing the bomb
					!IsAttacking())											// we aren't in the midst of a firefight
				{
					EscapeFromBomb();
					break;
				}
				break;
			}

			case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // CS Specific
			{
				// if (GetTeamNumber() == TEAM_CT) // CS Specific team
				// {
				//	UpdateHostageEscortCount();
				// }
				// else
				// {
				//	// Terrorists have imperfect information on status of hostages
				//	unsigned char status = GetGameState()->ValidateHostagePositions();

				//	if (status & FFGameState::HOSTAGES_ALL_GONE) // FFGameState enum
				//	{
				//		GetChatter()->HostagesTaken();
				//		Idle();
				//	}
				//	else if (status & FFGameState::HOSTAGE_GONE) // FFGameState enum
				//	{
				//		GetGameState()->HostageWasTaken();
				//		Idle();
				//	}
				// }
				break;
			}
		}
	}


	//
	// Follow nearby humans if our co-op is high and we have nothing else to do
	// If we were just following someone, don't auto-follow again for a short while to 
	// give us a chance to do something else.
	//
	const float earliestAutoFollowTime = 5.0f;
	const float minAutoFollowTeamwork = 0.4f;
	if (TheFFBots() && cv_bot_auto_follow.GetBool() && // Null check
		TheFFBots()->GetElapsedRoundTime() > earliestAutoFollowTime &&
		GetProfile() && GetProfile()->GetTeamwork() > minAutoFollowTeamwork && // Null check
		CanAutoFollow() &&
		!IsBusy() && 
		!IsFollowing() && 
		!IsBlind() && 
		!GetGameState()->IsAtPlantedBombsite()) // IsAtPlantedBombsite might be CS specific
	{

		// chance of following is proportional to teamwork attribute
		if (GetProfile()->GetTeamwork() > RandomFloat( 0.0f, 1.0f ))
		{
			CFFPlayer *leader = GetClosestVisibleHumanFriend();
			// if (leader && leader->IsAutoFollowAllowed()) // IsAutoFollowAllowed might not exist
			if (leader)
			{
				// count how many bots are already following this player
				const float maxFollowCount = 2;
				if (GetBotFollowCount( leader ) < maxFollowCount)
				{
					const float autoFollowRange = 300.0f;
					Vector leaderOrigin = GetCentroid( leader );
					if ((leaderOrigin - myOrigin).IsLengthLessThan( autoFollowRange ))
					{
						if (TheNavMesh) { // Null check
							CNavArea *leaderArea = TheNavMesh->GetNavArea( leaderOrigin );
							if (leaderArea && GetLastKnownArea()) // Null check GetLastKnownArea
							{
								PathCost cost( this ); // PathCost needs to be defined
								float travelRange = NavAreaTravelDistance( GetLastKnownArea(), leaderArea, cost ); // NavAreaTravelDistance
								if (travelRange >= 0.0f && travelRange < autoFollowRange)
								{
									// follow this human
									Follow( leader );
									PrintIfWatched( "Auto-Following %s\n", leader->GetPlayerName() );

									// TODO: Update for FF if career mode exists
									// if (FFGameRules() && FFGameRules()->IsCareer())
									// {
									//	GetChatter()->Say( "FollowingCommander", 10.0f ); // TODO: Update chatter
									// }
									// else
									// {
									//	GetChatter()->Say( "FollowingSir", 10.0f ); // TODO: Update chatter
									// }
								}
							}
						}
					}
				}
			}
		}
		else
		{
			// we decided not to follow, don't re-check for a duration
			m_allowAutoFollowTime = gpGlobals->curtime + 15.0f + (1.0f - (GetProfile() ? GetProfile()->GetTeamwork() : 0.5f)) * 30.0f; // Null check
		}
	}

	if (IsFollowing())
	{
		// if we are following someone, make sure they are still alive
		CBaseEntity *leader = m_leader.Get(); // Use .Get() for EHANDLE
		if (leader == NULL || !leader->IsAlive())
		{
			StopFollowing();
		}

		// decide whether to continue following them
		const float highTeamwork = 0.85f;
		if (GetProfile() && GetProfile()->GetTeamwork() < highTeamwork) // Null check
		{
			float minFollowDuration = 15.0f;
			if (GetFollowDuration() > minFollowDuration + 40.0f * GetProfile()->GetTeamwork())
			{
				// we are bored of following our leader
				StopFollowing();
				PrintIfWatched( "Stopping following - bored\n" );
			}
		}
	}


	//
	// Execute state machine
	//
	if (m_isOpeningDoor) // This logic implies OpenDoorState doesn't use SetState like others
	{
		m_openDoorState.OnUpdate( this );

		if (m_openDoorState.IsDone())
		{
			m_openDoorState.OnExit( this );
			m_isOpeningDoor = false;
		}
	}
	else if (m_isAttacking)
	{
		m_attackState.OnUpdate( this );
	}
	else if (m_state) // Null check m_state
	{
		m_state->OnUpdate( this );
	}

	// do wait behavior
	if (!IsAttacking() && IsWaiting())
	{
		ResetStuckMonitor();
		ClearMovement();
	}

	// don't move while reloading unless we see an enemy
	if (IsReloading() && !m_isEnemyVisible)
	{
		ResetStuckMonitor();
		ClearMovement();
	}

	// if we get too far ahead of the hostages we are escorting, wait for them
	// TODO: Hostage logic is CS specific
	// if (!IsAttacking() && m_inhibitWaitingForHostageTimer.IsElapsed())
	// {
	//	const float waitForHostageRange = 500.0f;
	//	if ((GetTask() == CFFBot::COLLECT_HOSTAGES || GetTask() == CFFBot::RESCUE_HOSTAGES) && GetRangeToFarthestEscortedHostage() > waitForHostageRange)
	//	{
	//		if (!m_isWaitingForHostage)
	//		{
	//			// just started waiting
	//			m_isWaitingForHostage = true;
	//			m_waitForHostageTimer.Start( 10.0f );
	//		}
	//		else
	//		{
	//			// we've been waiting
	//			if (m_waitForHostageTimer.IsElapsed())
	//			{
	//				// give up waiting for awhile
	//				m_isWaitingForHostage = false;
	//				m_inhibitWaitingForHostageTimer.Start( 3.0f );
	//			}
	//			else
	//			{
	//				// keep waiting
	//				ResetStuckMonitor();
	//				ClearMovement();
	//			}
	//		}
	//	}
	// }

	// remember our prior safe time status
	m_wasSafe = IsSafe();
}


//--------------------------------------------------------------------------------------------------------------
class DrawTravelTime 
{
public:
	DrawTravelTime( const CFFBot *me ) // Changed to CFFBot
	{
		m_me = me;
	}

	bool operator() ( CBasePlayer *player )
	{
		if (!player || !m_me) return true; // Null checks

		if (player->IsAlive() && !m_me->InSameTeam( player ))
		{
			CFmtStr msg;
			// player->EntityText(	0, // EntityText might not be available or might need different signature
			//					msg.sprintf( "%3.0f", m_me->GetTravelDistanceToPlayer( static_cast<CFFPlayer *>(player) ) ), // Cast to CFFPlayer
			//					0.1f );


			// if (m_me->DidPlayerJustFireWeapon( static_cast<CFFPlayer *>(player) )) // Cast to CFFPlayer
			// {
			//	player->EntityText( 1, "BANG!", 0.1f );
			// }
		}

		return true;
	}

	const CFFBot *m_me;
};


//--------------------------------------------------------------------------------------------------------------
/**
 * Render bot debug info
 */
void CFFBot::DebugDisplay( void ) const
{
	const float duration = 0.15f;
	CFmtStr msg;
	
	NDebugOverlay::ScreenText( 0.5f, 0.34f, msg.sprintf( "Skill: %d%%", (int)(100.0f * (GetProfile() ? GetProfile()->GetSkill() : 0.0f)) ), 255, 255, 255, 150, duration ); // Null check

	if ( m_pathLadder )
	{
		NDebugOverlay::ScreenText( 0.5f, 0.36f, msg.sprintf( "Ladder: %d", m_pathLadder->GetID() ), 255, 255, 255, 150, duration );
	}

	// show safe time
	float safeTime = GetSafeTimeRemaining();
	if (safeTime > 0.0f)
	{
		NDebugOverlay::ScreenText( 0.5f, 0.38f, msg.sprintf( "SafeTime: %3.2f", safeTime ), 255, 255, 255, 150, duration );
	}

	// show if blind
	if (IsBlind())
	{
		NDebugOverlay::ScreenText( 0.5f, 0.38f, msg.sprintf( "<<< BLIND >>>" ), 255, 255, 255, 255, duration );
	}

	// show if alert
	if (IsAlert())
	{
		NDebugOverlay::ScreenText( 0.5f, 0.38f, msg.sprintf( "ALERT" ), 255, 0, 0, 255, duration );
	}

	// show if panicked
	if (IsPanicking())
	{
		NDebugOverlay::ScreenText( 0.5f, 0.36f, msg.sprintf( "PANIC" ), 255, 255, 0, 255, duration );
	}

	// show behavior variables
	if (m_isAttacking)
	{
		NDebugOverlay::ScreenText( 0.5f, 0.4f, msg.sprintf( "ATTACKING: %s", GetBotEnemy() ? GetBotEnemy()->GetPlayerName() : "NULL" ), 255, 0, 0, 255, duration ); // Null check
	}
	else if (m_state) // Null check m_state
	{
		NDebugOverlay::ScreenText( 0.5f, 0.4f, msg.sprintf( "State: %s", m_state->GetName() ), 255, 255, 0, 255, duration );
		NDebugOverlay::ScreenText( 0.5f, 0.42f, msg.sprintf( "Task: %s", GetTaskName() ), 0, 255, 0, 255, duration );
		NDebugOverlay::ScreenText( 0.5f, 0.44f, msg.sprintf( "Disposition: %s", GetDispositionName() ), 100, 100, 255, 255, duration );
		NDebugOverlay::ScreenText( 0.5f, 0.46f, msg.sprintf( "Morale: %s", GetMoraleName() ), 0, 200, 200, 255, duration );
	}

	// show look at status
	if (m_lookAtSpotState != NOT_LOOKING_AT_SPOT) // Enum
	{
		const char *lookAtStateName = (m_lookAtSpotState == LOOK_TOWARDS_SPOT) ? "LOOK_TOWARDS_SPOT" : "LOOK_AT_SPOT"; // Enums
		const char *string = msg.sprintf( "LookAt: %s (%s)", m_lookAtDesc ? m_lookAtDesc : "NULL", lookAtStateName ); // Null check m_lookAtDesc

		NDebugOverlay::ScreenText( 0.5f, 0.60f, string, 255, 255, 0, 150, duration );
	}

	NDebugOverlay::ScreenText( 0.5f, 0.62f, msg.sprintf( "Steady view = %s", HasViewBeenSteady( 0.2f ) ? "YES" : "NO" ), 255, 255, 0, 150, duration );


	// show friend/foes I know of
	NDebugOverlay::ScreenText( 0.5f, 0.64f, msg.sprintf( "Nearby friends = %d", m_nearbyFriendCount ), 100, 255, 100, 150, duration );
	NDebugOverlay::ScreenText( 0.5f, 0.66f, msg.sprintf( "Nearby enemies = %d", m_nearbyEnemyCount ), 255, 100, 100, 150, duration );

	if ( m_lastKnownArea ) // Null check
	{
		NDebugOverlay::ScreenText( 0.5f, 0.68f, msg.sprintf( "Nav Area: %d (%s)", m_lastKnownArea->GetID(), m_szLastPlaceName.Get() ), 255, 255, 255, 150, duration );
	}

	// show debug message history
	float y = 0.8f;
	const float lineHeight = 0.02f;
	const float fadeAge = 7.0f;
	const float maxAge = 10.0f;
	// TODO: TheBots might need to be TheFFBots here if it refers to the manager instance. Assuming TheBots is correct global.
	if (TheBots) { // Null check
		for( int i=0; i<TheBots->GetDebugMessageCount(); ++i )
		{
			const CBotManager::DebugMessage *debugMsg = TheBots->GetDebugMessage( i ); // Renamed msg to avoid conflict

			if (debugMsg && debugMsg->m_age.GetElapsedTime() < maxAge) // Null check
			{
				int alpha = 255;

				if (debugMsg->m_age.GetElapsedTime() > fadeAge)
				{
					alpha *= (1.0f - (debugMsg->m_age.GetElapsedTime() - fadeAge) / (maxAge - fadeAge));
				}

				NDebugOverlay::ScreenText( 0.5f, y, debugMsg->m_string, 255, 255, 255, alpha, duration );
				y += lineHeight;
			}
		}
	}


	// show noises
	const Vector *noisePos = GetNoisePosition();
	if (noisePos)
	{
		const float size = 25.0f;
		NDebugOverlay::VertArrow( *noisePos + Vector( 0, 0, size ), *noisePos, size/4.0f, 255, 255, 0, 0, true, duration );
	}

	// show aim spot
	if (IsAimingAtEnemy())
	{
		NDebugOverlay::Cross3D( m_aimSpot, 5.0f, 255, 0, 0, true, duration );
	}



	if (IsHiding())
	{
		// show approach points
		DrawApproachPoints();
	}
	else
	{
		// show encounter spot data
		// TODO: SpotEncounter, SpotOrder, and related logic is CS specific.
		// if (false && m_spotEncounter)
		// {
		// }
	}

	// show aim targets
	if (false) // This debug was already disabled
	{
		// ... (original CS debug code for hitboxes) ...
	}

	DrawTravelTime drawTravelTime( this );
	ForEachPlayer( drawTravelTime ); // ForEachPlayer might need adaptation
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Periodically compute shortest path distance to each player.
 * NOTE: Travel distance is NOT symmetric between players A and B.  Each much be computed separately.
 */
void CFFBot::UpdateTravelDistanceToAllPlayers( void )
{
	const unsigned char numPhases = 3;

	if (m_updateTravelDistanceTimer.IsElapsed())
	{
		ShortestPathCost pathCost; // PathCost

		for( int i=1; i<=gpGlobals->maxClients; ++i )
		{
			CFFPlayer *player = static_cast< CFFPlayer * >( UTIL_PlayerByIndex( i ) ); // Cast to CFFPlayer

			if (player == NULL)
				continue;

			if ( FNullEnt( player->edict() ) ) // FNullEnt might be deprecated
				continue;

			if (!player->IsPlayer())
				continue;
			
			if (!player->IsAlive())
				continue;

			// skip friends for efficiency
			if (player->InSameTeam( this ))
				continue;

			int which = player->entindex() % MAX_PLAYERS; // MAX_PLAYERS needs to be defined
			if (which < 0 || which >= MAX_PLAYERS) continue; // Bounds check

			// if player is very far away, update every third time (on phase 0)
			const float veryFarAway = 4000.0f;
			if (m_playerTravelDistance[ which ] < 0.0f || m_playerTravelDistance[ which ] > veryFarAway)
			{
				if (m_travelDistancePhase != 0)
					continue;
			}
			else
			{
				// if player is far away, update two out of three times (on phases 1 and 2)
				const float farAway = 2000.0f;
				if (m_playerTravelDistance[ which ] > farAway && m_travelDistancePhase == 0)
					continue;
			}

			// if player is fairly close, update often
			// TODO: NavAreaTravelDistance needs to be defined/ported for FF
			if (EyePosition().IsValid() && player->EyePosition().IsValid() && GetLastKnownArea()) { // Ensure vectors and area are valid
				m_playerTravelDistance[ which ] = NavAreaTravelDistance( GetLastKnownArea(), TheNavMesh->GetNearestNavArea(player->EyePosition()), pathCost );
			} else {
				m_playerTravelDistance[ which ] = -1.0f; // Indicate error or invalid path
			}
		}

		// throttle the computation frequency
		const float checkInterval = 1.0f;
		m_updateTravelDistanceTimer.Start( checkInterval );

		// round-robin the phases
		++m_travelDistancePhase;
		if (m_travelDistancePhase >= numPhases)
		{
			m_travelDistancePhase = 0;
		}
	}
}
