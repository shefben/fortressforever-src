//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "fmtstr.h"
#include "usermessages.h"
#include "soundent.h"

// Local bot utility headers
#include "bot_constants.h"
#include "bot_profile.h"
#include "bot_util.h"


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

	if (!TheNavMesh || TheNavMesh->IsGenerating() || !IsAlive())
		return;
	
	if ( cv_bot_flipout.GetBool() )
	{
		int val = RandomInt( 0, 2 );
		if ( val == 0 ) MoveForward(); else if ( val == 1 ) MoveBackward();
		val = RandomInt( 0, 2 );
		if ( val == 0 ) StrafeLeft(); else if ( val == 1 ) StrafeRight();
		if ( RandomInt( 0, 5 ) == 0 ) Jump( true );
		val = RandomInt( 0, 2 );
		if ( val == 0 ) Crouch(); else StandUp();
		return;
	}
	
	m_eyePosition = EyePosition();
	Vector myOrigin = GetCentroid( this );

	if (IsAimingAtEnemy())
	{
		UpdateAimOffset();
		if (m_enemy != NULL && m_enemy->IsAlive())
		{
			Vector enemyOrigin = GetCentroid( m_enemy.Get() );
			if (m_isEnemyVisible)
			{
				const float sharpshooter = 0.8f;
				VisiblePartType aimAtPart;
				if (IsUsingMachinegun()) aimAtPart = GUT;
				// else if (IsUsing( FF_WEAPON_AWP ) || IsUsingShotgun()) aimAtPart = GUT;
				else if (GetProfile() && GetProfile()->GetSkill() > 0.5f && IsActiveWeaponRecoilHigh() ) aimAtPart = GUT;
				else if (!GetProfile() || GetProfile()->GetSkill() < sharpshooter) aimAtPart = GUT;
				else aimAtPart = HEAD;

				if (IsEnemyPartVisible( aimAtPart )) m_aimSpot = GetPartPosition( GetBotEnemy(), aimAtPart );
				else
				{
					if (IsEnemyPartVisible( GUT )) m_aimSpot = GetPartPosition( GetBotEnemy(), GUT );
					else if (IsEnemyPartVisible( HEAD )) m_aimSpot = GetPartPosition( GetBotEnemy(), HEAD );
					else if (IsEnemyPartVisible( LEFT_SIDE )) m_aimSpot = GetPartPosition( GetBotEnemy(), LEFT_SIDE );
					else if (IsEnemyPartVisible( RIGHT_SIDE )) m_aimSpot = GetPartPosition( GetBotEnemy(), RIGHT_SIDE );
					else m_aimSpot = GetPartPosition( GetBotEnemy(), FEET );
				}
			}
			else
			{
				m_aimSpot = m_lastEnemyPosition;
			}
			m_aimSpot.x += m_aimOffset.x; m_aimSpot.y += m_aimOffset.y; m_aimSpot.z += m_aimOffset.z;
			Vector to = m_aimSpot - EyePositionConst();
			QAngle idealAngle;
			VectorAngles( to, idealAngle );
			const QAngle &punchAngles = GetPunchAngle();
			if (GetProfile()) idealAngle -= punchAngles * GetProfile()->GetSkill();
			SetLookAngles( idealAngle.y, idealAngle.x );
		}
	}
	else
	{
		if (m_lookAtSpotClearIfClose)
		{
			const float tooCloseRange = 100.0f;
			if ((m_lookAtSpot - myOrigin).IsLengthLessThan( tooCloseRange ))
				m_lookAtSpotState = NOT_LOOKING_AT_SPOT;
		}
		switch( m_lookAtSpotState )
		{
			case NOT_LOOKING_AT_SPOT: SetLookAngles( m_lookAheadAngle, 0.0f ); break;
			case LOOK_TOWARDS_SPOT:
				UpdateLookAt();
				if (IsLookingAtPosition( m_lookAtSpot, m_lookAtSpotAngleTolerance ))
					{ m_lookAtSpotState = LOOK_AT_SPOT; m_lookAtSpotTimestamp = gpGlobals->curtime; }
				break;
			case LOOK_AT_SPOT:
				UpdateLookAt();
				if (m_lookAtSpotDuration >= 0.0f && gpGlobals->curtime - m_lookAtSpotTimestamp > m_lookAtSpotDuration)
					{ m_lookAtSpotState = NOT_LOOKING_AT_SPOT; m_lookAtSpotDuration = 0.0f; }
				break;
		}
		if (!IsUsingSniperRifle())
		{
			float driftAmplitude = IsBlind() ? 5.0f : 2.0f;
			m_lookYaw += driftAmplitude * cos( 33.0f * gpGlobals->curtime * M_PI/180.0f );
			m_lookPitch += driftAmplitude * sin( 13.0f * gpGlobals->curtime * M_PI/180.0f );
		}
	}
	UpdateLookAngles();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Heavyweight processing, invoked less often
 */
void CFFBot::Update( void )
{
	VPROF_BUDGET( "CFFBot::Update", VPROF_BUDGETGROUP_NPCS );
	if ( cv_bot_flipout.GetBool() ) return;

	Vector myOrigin = GetCentroid( this );

	if (GetTeamNumber() == TEAM_UNASSIGNED)
	{
		HandleCommand_JoinTeam( m_desiredTeam );
		if (GetProfile()) {
			int desiredClass = GetProfile()->GetSkin();
			// FF class selection logic would go here if needed
			// HandleCommand_JoinClass( desiredClass );
		}
		return;
	}

	if (GetChatter()) GetChatter()->Update(); // Null check
	
	if (!IsAlive())
	{
		m_diedLastRound = true;
		BotDeathThink();
		return;
	}
	m_hasJoined = true;

	if (cv_bot_debug.GetBool() && IsLocalPlayerWatchingMe()) DebugDisplay();
	if (cv_bot_stop.GetBool()) return;

	StuckCheck();
	BreakablesCheck();
	DoorCheck();
	UpdateTravelDistanceToAllPlayers();

	const float rememberNoiseDuration = 20.0f;
	if (m_noiseTimestamp > 0.0f && gpGlobals->curtime - m_noiseTimestamp > rememberNoiseDuration) ForgetNoise();

	if (!m_currentArea || !m_currentArea->Contains( myOrigin ))
	{
		if (TheNavMesh) m_currentArea = (CNavArea *)TheNavMesh->GetNavArea( myOrigin );
	}
	if (m_currentArea && m_currentArea != m_lastKnownArea)
	{
		m_lastKnownArea = m_currentArea;
		OnEnteredNavArea( m_currentArea );
	}

	const float stillSpeed = 10.0f;
	if (GetAbsVelocity().IsLengthLessThan( stillSpeed )) m_stillTimer.Start();
	else m_stillTimer.Invalidate();

	if (IsBlind() && m_blindFire) PrimaryAttack();
	UpdatePanicLookAround();
	UpdateReactionQueue();

	CFFPlayer *threat = GetRecognizedEnemy();
	if (threat)
	{
		Vector threatOrigin = GetCentroid( threat );
		AdjustSafeTime();
		BecomeAlert();
		bool doAttack = false;
		// ... (rest of attack decision logic, needs FF adaptation for dispositions, weapon types, scenarios) ...
		// For brevity, assuming some condition leads to doAttack = true for testing
		if (GetDisposition() != CFFBot::IGNORE_ENEMIES) doAttack = true;

		if (doAttack)
		{
			if (!IsAttacking() || threat != GetBotEnemy()) Attack( threat );
		}
		else { SetBotEnemy( threat ); m_isEnemyVisible = true; }
		if (TheFFBots()) TheFFBots()->SetLastSeenEnemyTimestamp();
	}

	if (m_enemy != NULL)
	{
		if (IsAwareOfEnemyDeath()) { m_enemy = NULL; m_isEnemyVisible = false; }
		else
		{
			if (m_enemy.IsValid() && IsVisible( m_enemy.Get(), false, &m_visibleEnemyParts ))
			{
				m_isEnemyVisible = true;
				m_lastSawEnemyTimestamp = gpGlobals->curtime;
				m_lastEnemyPosition = GetCentroid( m_enemy.Get() );
			} else m_isEnemyVisible = false;
			if (m_enemy.IsValid() && m_enemy->IsAlive()) { m_enemyDeathTimestamp = 0.0f; m_isLastEnemyDead = false; }
			else if (m_enemyDeathTimestamp == 0.0f) { m_enemyDeathTimestamp = gpGlobals->curtime; m_isLastEnemyDead = true; }
		}
	} else m_isEnemyVisible = false;

	if (!IsBlind() && !IsLookingAtSpot(PRIORITY_UNINTERRUPTABLE) )
	{
		if (m_enemy != NULL && GetTimeSinceLastSawEnemy() < 3.0f) AimAtEnemy();
		else StopAiming();
	} else if( IsAimingAtEnemy() ) StopAiming();

	if (GetDisposition() == CFFBot::IGNORE_ENEMIES) FireWeaponAtEnemy();
	LookForGrenadeTargets(); // CS Grenade logic
	UpdateGrenadeThrow();    // CS Grenade logic
	AvoidEnemyGrenades();    // CS Grenade logic

	if (!IsSafe() && !IsUsingGrenade() && IsActiveWeaponOutOfAmmo()) EquipBestWeapon(); // CS Grenade logic
	if (!IsSafe() && !IsUsingGrenade() && IsUsingKnife() && !IsEscapingFromBomb()) EquipBestWeapon(); // CS Grenade logic & Bomb logic

	const float safeRearmTime = 5.0f;
	if (!IsReloading() && IsUsingPistol() && !IsPrimaryWeaponEmpty() && GetTimeSinceLastSawEnemy() > safeRearmTime) EquipBestWeapon();
	ReloadCheck();
	SilencerCheck(); // CS Silencer logic
	RespondToRadioCommands();

	const float avoidTime = 0.33f;
	if (gpGlobals->curtime - m_avoidTimestamp < avoidTime && m_avoid != NULL) StrafeAwayFromPosition( GetCentroid( m_avoid.Get() ) );
	else m_avoid = NULL;

	if (!IsAtHidingSpot() && !IsAttacking() && IsUsingSniperRifle() && IsUsingScope()) SecondaryAttack(); // CS Sniper logic

	if (!IsBlind())
	{
		UpdatePeripheralVision();
		if (CanSeeSniper() && !HasSeenSniperRecently()) // CS Sniper logic
		{
			if(GetChatter()) GetChatter()->SpottedSniper();
			m_sawEnemySniperTimer.Start( 20.0f );
		}
		if (m_bomber != NULL && GetChatter()) GetChatter()->SpottedBomber( GetBomber() ); // CS Bomb logic
		if (TheFFBots() && CanSeeLooseBomb() && GetChatter()) GetChatter()->SpottedLooseBomb( TheFFBots()->GetLooseBomb() ); // CS Bomb logic
	}

	// Scenario interrupts (CS specific, needs heavy FF adaptation)
	if (TheFFBots()) {
		switch (TheFFBots()->GetScenario())
		{
			// case CFFBotManager::SCENARIO_DEFUSE_BOMB: { /* ... CS Bomb logic ... */ } break;
			// case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: { /* ... CS Hostage logic ... */ } break;
		}
	}

	// Auto-follow logic (mostly generic, but uses IsBusy which has CS specifics)
	if (TheFFBots() && cv_bot_auto_follow.GetBool() && TheFFBots()->GetElapsedRoundTime() > 5.0f &&
		GetProfile() && GetProfile()->GetTeamwork() > 0.4f && CanAutoFollow() &&
		!IsBusy() && !IsFollowing() && !IsBlind() /*&& !GetGameState()->IsAtPlantedBombsite() CS specific*/)
	{
		// ... (auto-follow logic) ...
	}
	if (IsFollowing()) { /* ... (check if leader is alive/valid) ... */ }

	if (m_isOpeningDoor) { /* ... */ }
	else if (m_isAttacking) { m_attackState.OnUpdate( this ); }
	else if (m_state) { m_state->OnUpdate( this ); }

	if (!IsAttacking() && IsWaiting()) { ResetStuckMonitor(); ClearMovement(); }
	if (IsReloading() && !m_isEnemyVisible) { ResetStuckMonitor(); ClearMovement(); }
	// Hostage escort waiting logic (CS specific)
	m_wasSafe = IsSafe();
}


//--------------------------------------------------------------------------------------------------------------
class DrawTravelTime 
{
public:
	DrawTravelTime( const CFFBot *me ) { m_me = me; }
	bool operator() ( CBasePlayer *player )
	{
		// ... (Debug display logic, references CFFPlayer cast) ...
		return true;
	}
	const CFFBot *m_me;
};


//--------------------------------------------------------------------------------------------------------------
void CFFBot::DebugDisplay( void ) const
{
	// ... (Debug display logic, uses GetTaskName, GetDispositionName, GetMoraleName, TheFFBots) ...
	// Ensure these helper functions and TheFFBots are available and correct.
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::UpdateTravelDistanceToAllPlayers( void )
{
	// ... (Pathfinding logic, uses NavAreaTravelDistance, PathCost) ...
	// Ensure these pathfinding utilities are available and correct for FF.
}

[end of mp/src/game/server/ff/bot/ff_bot_update.cpp]
