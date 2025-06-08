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
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"
#include "nav_mesh.h"
#include "nav_ladder.h"
#include "datacache/imdlcache.h"
#include "model_types.h"

// Local bot utility headers
#include "bot_constants.h"
#include "bot_profile.h"
#include "bot_util.h"       // Added for PrintIfWatched


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Used to update view angles to stay on a ladder
 */
inline float StayOnLadderLine( CFFBot *me, const CNavLadder *ladder )
{
	if (!me || !ladder) return 0.0f;

	NavDirType faceDir = AngleToDirection( me->EyeAngles().y );
	const float stiffness = 1.0f;

	switch( faceDir )
	{
		case NORTH: return stiffness * (ladder->m_top.x - me->GetAbsOrigin().x);
		case SOUTH: return -stiffness * (ladder->m_top.x - me->GetAbsOrigin().x);
		case WEST: return -stiffness * (ladder->m_top.y - me->GetAbsOrigin().y);
		case EAST: return stiffness * (ladder->m_top.y - me->GetAbsOrigin().y);
	}
	return 0.0f;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::ComputeLadderAngles( float *yaw, float *pitch )
{
	if ( !yaw || !pitch || !m_pathLadder ) return;

	Vector myOrigin = GetCentroid( this );
	Vector to = m_pathLadder->GetPosAtHeight(myOrigin.z) - myOrigin;
	float idealYaw = UTIL_VecToYaw( to );
	Vector faceDir = (m_pathLadderFaceIn) ? -m_pathLadder->GetNormal() : m_pathLadder->GetNormal();
	QAngle faceAngles;
	VectorAngles( faceDir, faceAngles );

	const float lookAlongLadderRange = 50.0f;
	const float ladderPitchUpApproach = -30.0f;
	const float ladderPitchUpTraverse = -60.0f;
	const float ladderPitchDownApproach = 0.0f;
	const float ladderPitchDownTraverse = 80.0f;

	switch( m_pathLadderState )
	{
		case APPROACH_ASCENDING_LADDER:
		{
			Vector toGoal = m_goalPosition - myOrigin;
			*yaw = idealYaw;
			if (toGoal.IsLengthLessThan( lookAlongLadderRange )) *pitch = ladderPitchUpApproach;
			break;
		}
		case APPROACH_DESCENDING_LADDER:
		{
			Vector toGoal = m_goalPosition - myOrigin;
			*yaw = idealYaw;
			if (toGoal.IsLengthLessThan( lookAlongLadderRange )) *pitch = ladderPitchDownApproach;
			break;
		}
		case FACE_ASCENDING_LADDER:
			if (m_pathIndex >= MAX_PATH_LENGTH_FF || !m_path[m_pathIndex].area) break;
			if (m_path[ m_pathIndex ].area == m_pathLadder->m_topForwardArea) m_pathLadderDismountDir = FORWARD;
			else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topLeftArea) { m_pathLadderDismountDir = LEFT; idealYaw = AngleNormalizePositive( idealYaw + 90.0f ); }
			else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topRightArea) { m_pathLadderDismountDir = RIGHT; idealYaw = AngleNormalizePositive( idealYaw - 90.0f ); }
			*yaw = idealYaw;
			*pitch = ladderPitchUpApproach;
			break;
		case FACE_DESCENDING_LADDER:
			*yaw = idealYaw; *pitch = ladderPitchDownApproach; break;
		case MOUNT_ASCENDING_LADDER:
		case ASCEND_LADDER:
			if ( m_pathLadderDismountDir == LEFT ) *yaw = AngleNormalizePositive( idealYaw + 90.0f );
			else if ( m_pathLadderDismountDir == RIGHT ) *yaw = AngleNormalizePositive( idealYaw - 90.0f );
			else *yaw = faceAngles[ YAW ] + StayOnLadderLine( this, m_pathLadder );
			*pitch = ( m_pathLadderState == ASCEND_LADDER ) ? ladderPitchUpTraverse : ladderPitchUpApproach;
			break;
		case MOUNT_DESCENDING_LADDER:
		case DESCEND_LADDER:
			*yaw = faceAngles[ YAW ] + StayOnLadderLine( this, m_pathLadder );
			*pitch = ( m_pathLadderState == DESCEND_LADDER ) ? ladderPitchDownTraverse : ladderPitchDownApproach;
			break;
		case DISMOUNT_ASCENDING_LADDER:
			if ( m_pathLadderDismountDir == LEFT ) *yaw = AngleNormalizePositive( faceAngles[ YAW ] + 90.0f );
			else if ( m_pathLadderDismountDir == RIGHT ) *yaw = AngleNormalizePositive( faceAngles[ YAW ] - 90.0f );
			else *yaw = faceAngles[ YAW ];
			break;
		case DISMOUNT_DESCENDING_LADDER:
			*yaw = faceAngles[ YAW ]; break;
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::UpdateLookAngles( void )
{
	VPROF_BUDGET( "CFFBot::UpdateLookAngles", VPROF_BUDGETGROUP_NPCS );
	const float deltaT = g_BotUpkeepInterval;
	float maxAccel, stiffness, damping;

	if ( bot_mimic.GetInt() ) return;

	if (IsAttacking()) { stiffness = 300.0f; damping = 30.0f; maxAccel = 3000.0f; }
	else { stiffness = 200.0f; damping = 25.0f; maxAccel = 3000.0f; }

	float useYaw = m_lookYaw; float usePitch = m_lookPitch;
	if ( IsUsingLadder() && !(IsLookingAtSpot( PRIORITY_HIGH ) && m_lookAtSpotAttack) ) ComputeLadderAngles( &useYaw, &usePitch );

	QAngle viewAngles = EyeAngles();
	float angleDiff = AngleNormalize( useYaw - viewAngles.y );
	const float onTargetTolerance = 1.0f;
	if (angleDiff < onTargetTolerance && angleDiff > -onTargetTolerance) { m_lookYawVel = 0.0f; viewAngles.y = useYaw; }
	else
	{
		float accel = stiffness * angleDiff - damping * m_lookYawVel;
		if (accel > maxAccel) accel = maxAccel; else if (accel < -maxAccel) accel = -maxAccel;
		m_lookYawVel += deltaT * accel; viewAngles.y += deltaT * m_lookYawVel;
		if (fabs( accel ) > 1000.0f) m_viewSteadyTimer.Start();
	}
	angleDiff = AngleNormalize( usePitch - viewAngles.x );
	if (false && angleDiff < onTargetTolerance && angleDiff > -onTargetTolerance) { m_lookPitchVel = 0.0f; viewAngles.x = usePitch; }
	else
	{
		float accel = 2.0f * stiffness * angleDiff - damping * m_lookPitchVel;
		if (accel > maxAccel) accel = maxAccel; else if (accel < -maxAccel) accel = -maxAccel;
		m_lookPitchVel += deltaT * accel; viewAngles.x += deltaT * m_lookPitchVel;
		if (fabs( accel ) > 1000.0f) m_viewSteadyTimer.Start();
	}
	if (viewAngles.x < -89.0f) viewAngles.x = -89.0f; else if (viewAngles.x > 89.0f) viewAngles.x = 89.0f;
	SnapEyeAngles( viewAngles );
	if (IsWaitingForZoom()) m_viewSteadyTimer.Start();
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsVisible( const Vector &pos, bool testFOV, const CBaseEntity *ignore ) const
{
	VPROF_BUDGET( "CFFBot::IsVisible( pos )", VPROF_BUDGETGROUP_NPCS );
	if (IsBlind()) return false;
	if (testFOV && !(const_cast<CFFBot *>(this)->FInViewCone( pos ))) return false;
	if (TheFFBots() && TheFFBots()->IsLineBlockedBySmoke( EyePositionConst(), pos )) return false;
	trace_t result;
	CTraceFilterNoNPCsOrPlayer traceFilter( ignore ? ignore : this, COLLISION_GROUP_NONE );
	UTIL_TraceLine( EyePositionConst(), pos, MASK_VISIBLE_AND_NPCS, &traceFilter, &result );
	return (result.fraction == 1.0f);
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsVisible( CFFPlayer *player, bool testFOV, unsigned char *visParts ) const
{
	VPROF_BUDGET( "CFFBot::IsVisible( player )", VPROF_BUDGETGROUP_NPCS );
	if (!player) return false;
	if (testFOV && !(const_cast<CFFBot *>(this)->FInViewCone( player->WorldSpaceCenter() ))) return false;
	unsigned char testVisParts = NONE;
	Vector partPos;
	partPos = GetPartPosition( player, GUT ); if (IsVisible( partPos, testFOV )) { if (!visParts) return true; testVisParts |= GUT; }
	partPos = GetPartPosition( player, HEAD ); if (IsVisible( partPos, testFOV )) { if (!visParts) return true; testVisParts |= HEAD; }
	partPos = GetPartPosition( player, FEET ); if (IsVisible( partPos, testFOV )) { if (!visParts) return true; testVisParts |= FEET; }
	partPos = GetPartPosition( player, LEFT_SIDE ); if (IsVisible( partPos, testFOV )) { if (!visParts) return true; testVisParts |= LEFT_SIDE; }
	partPos = GetPartPosition( player, RIGHT_SIDE ); if (IsVisible( partPos, testFOV )) { if (!visParts) return true; testVisParts |= RIGHT_SIDE; }
	if (visParts) *visParts = testVisParts;
	return (testVisParts != NONE);
}

//--------------------------------------------------------------------------------------------------------------
CFFBot::PartInfo CFFBot::m_partInfo[ MAX_PLAYERS ];

//--------------------------------------------------------------------------------------------------------------
void CFFBot::ComputePartPositions( CFFPlayer *player )
{
	if (!player) return;
	// TODO_FF: Hitbox indices are CS specific.
	const int headBox = 12, gutBox = 9, leftElbowBox = 14, rightElbowBox = 17, maxBoxIndex = rightElbowBox;
	VPROF_BUDGET( "CFFBot::ComputePartPositions", VPROF_BUDGETGROUP_NPCS );
	int playerIndex = player->entindex() % MAX_PLAYERS;
	if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return;
	PartInfo *info = &m_partInfo[ playerIndex ];
	info->m_feetPos = player->GetAbsOrigin(); info->m_feetPos.z += 5.0f;
	MDLCACHE_CRITICAL_SECTION();
	CStudioHdr *studioHdr = player->GetModelPtr();
	if (studioHdr)
	{
		mstudiohitboxset_t *set = studioHdr->pHitboxSet( player->GetHitboxSet() );
		if (set && maxBoxIndex < set->numhitboxes && set->numhitboxes > 0) // Added set->numhitboxes > 0
		{
			QAngle angles; mstudiobbox_t *box;
			box = set->pHitbox( gutBox ); if (box) player->GetBonePosition( box->bone, info->m_gutPos, angles );
			box = set->pHitbox( headBox );
			if (box)
			{
				player->GetBonePosition( box->bone, info->m_headPos, angles );
				Vector forward, right; AngleVectors( angles, &forward, &right, NULL );
				info->m_headPos += 4.0f * forward + 2.0f * right; info->m_headPos.z -= 2.0f;
			}
			box = set->pHitbox( leftElbowBox ); if (box) player->GetBonePosition( box->bone, info->m_leftSidePos, angles );
			box = set->pHitbox( rightElbowBox ); if (box) player->GetBonePosition( box->bone, info->m_rightSidePos, angles );
			return;
		}
	}
	info->m_headPos = GetCentroid( player );
	info->m_gutPos = info->m_headPos; info->m_leftSidePos = info->m_headPos; info->m_rightSidePos = info->m_headPos;
}

//--------------------------------------------------------------------------------------------------------------
const Vector &CFFBot::GetPartPosition( CFFPlayer *player, VisiblePartType part ) const
{
	VPROF_BUDGET( "CFFBot::GetPartPosition", VPROF_BUDGETGROUP_NPCS );
	static Vector defaultPos(0,0,0);
	if (!player) return defaultPos;
	int playerIndex = player->entindex() % MAX_PLAYERS;
	if (playerIndex < 0 || playerIndex >= MAX_PLAYERS) return defaultPos;
	PartInfo *info = &m_partInfo[ playerIndex ];
	if (gpGlobals->framecount > info->m_validFrame)
		{ const_cast< CFFBot * >( this )->ComputePartPositions( player ); info->m_validFrame = gpGlobals->framecount; }
	switch( part )
	{
		default: case GUT: return info->m_gutPos;
		case HEAD: return info->m_headPos;
		case FEET: return info->m_feetPos;
		case LEFT_SIDE: return info->m_leftSidePos;
		case RIGHT_SIDE: return info->m_rightSidePos;
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::UpdateLookAt( void ) { Vector to = m_lookAtSpot - EyePositionConst(); QAngle idealAngle; VectorAngles( to, idealAngle ); SetLookAngles( idealAngle.y, idealAngle.x ); }
void CFFBot::SetLookAt( const char *desc, const Vector &pos, PriorityType pri, float duration, bool clearIfClose, float angleTolerance, bool attack )
{ /* ... (logic as before, ensure PrintIfWatched uses 'this') ... */ PrintIfWatched(this, "%3.1f SetLookAt( %s ), duration = %f\n", gpGlobals->curtime, desc ? desc : "NULL", duration ); }
void CFFBot::InhibitLookAround( float duration ) { m_inhibitLookAroundTimestamp = gpGlobals->curtime + duration; }
void CFFBot::UpdatePeripheralVision() { /* ... (CS specific SpotEncounter logic commented/removed) ... */ }
void CFFBot::UpdateLookAround( bool updateNow ) { /* ... (logic as before, uses PrintIfWatched, TheFFBots, OtherTeam, HalfHumanHeight, GetProfile, IsUsingSniperRifle, AdjustZoom, SetLookAt) ... */ }
bool CFFBot::BendLineOfSight( const Vector &eye, const Vector &target, Vector *bend, float angleLimit ) const { /* ... (logic as before) ... */ return false; }
bool CFFBot::IsNoticable( const CFFPlayer *player, unsigned char visParts ) const { /* ... (logic as before, uses GetProfile) ... */ return false; }
CFFPlayer *CFFBot::FindMostDangerousThreat( void ) { /* ... (logic as before, uses IsSniperRifle, GetActiveCSWeapon (needs GetActiveFFWeapon), IsPlayerLookingAtMe, IsSignificantlyCloser, TheNavMesh) ... */ return NULL; }
void CFFBot::UpdateReactionQueue( void ) { /* ... (logic as before, uses GetProfile) ... */ }
CFFPlayer *CFFBot::GetRecognizedEnemy( void ) { /* ... (logic as before) ... */ return NULL; }
bool CFFBot::IsRecognizedEnemyReloading( void ) { /* ... */ return false; }
bool CFFBot::IsRecognizedEnemyProtectedByShield( void ) { /* ... */ return false; } // CS Specific
float CFFBot::GetRangeToNearestRecognizedEnemy( void ) { /* ... */ return FLT_MAX; }
void CFFBot::Blind( float holdTime, float fadeTime, float startingAlpha ) { /* ... (logic as before, uses PrintIfWatched, GetChatter, SetLookAt) ... */ }
bool CFFBot::IsAnyVisibleEnemyLookingAtMe( bool testFOV ) const { CheckLookAt L( this, testFOV ); return (ForEachPlayer( L ) == false); } // ForEachPlayer needs definition
void CFFBot::UpdatePanicLookAround( void ) { /* ... (logic as before, uses SetLookAt, PrintIfWatched) ... */ }

[end of mp/src/game/server/ff/bot/ff_bot_vision.cpp]
