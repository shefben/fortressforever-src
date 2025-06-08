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
#include "../ff_player.h"     // For CFFPlayer
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase (potentially used via CFFBot)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
#include "../../shared/ff/ff_gamerules.h" // For FFGameRules() (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For TheNavMesh, CNavArea
#include "nav_hiding_spot.h"// For HidingSpot
#include "nav_pathfind.h"   // For ShortestPathCost related classes (if used directly)
#include "bot_constants.h"  // For GO_NORTH etc.
#include "obstacle_pushaway.h" // For CPushAwayEnumerator (engine utility)
#include "fmtstr.h"
#include "soundent.h" // For CSingleUserRecipientFilter, EmitSound (if Bot.StuckSound is used)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const float NearBreakableCheckDist = 20.0f;
const float FarBreakableCheckDist = 300.0f;

#define DEBUG_BREAKABLES 0
#define DEBUG_DOORS 0

//--------------------------------------------------------------------------------------------------------------
#if DEBUG_BREAKABLES
static void DrawOutlinedQuad( const Vector &p1,
							const Vector &p2,
							const Vector &p3,
							const Vector &p4,
							int r, int g, int b,
							float duration )
{
	NDebugOverlay::Triangle( p1, p2, p3, r, g, b, 20, false, duration );
	NDebugOverlay::Triangle( p3, p4, p1, r, g, b, 20, false, duration );
	NDebugOverlay::Line( p1, p2, r, g, b, false, duration );
	NDebugOverlay::Line( p2, p3, r, g, b, false, duration );
	NDebugOverlay::Line( p3, p4, r, g, b, false, duration );
	NDebugOverlay::Line( p4, p1, r, g, b, false, duration );
}
ConVar bot_debug_breakable_duration( "bot_debug_breakable_duration", "30" );
#endif // DEBUG_BREAKABLES


//--------------------------------------------------------------------------------------------------------------
CBaseEntity * CheckForEntitiesAlongSegment( const Vector &start, const Vector &end, const Vector &mins, const Vector &maxs, CPushAwayEnumerator *enumerator )
{
	if (!enumerator) return NULL; // Null check

	CBaseEntity *entity = NULL;

	Ray_t ray;
	ray.Init( start, end, mins, maxs );

	if (partition) // Null check for partition
		partition->EnumerateElementsAlongRay( PARTITION_ENGINE_SOLID_EDICTS, ray, false, enumerator );

	if ( enumerator->m_nAlreadyHit > 0 )
	{
		entity = enumerator->m_AlreadyHit[0];
	}

#if DEBUG_BREAKABLES
	if ( entity )
	{
		DrawOutlinedQuad( start + mins, start + maxs, end + maxs, end + mins, 255, 0, 0, bot_debug_breakable_duration.GetFloat() );
	}
	else
	{
		DrawOutlinedQuad( start + mins, start + maxs, end + maxs, end + mins, 0, 255, 0, 0.1 );
	}
#endif // DEBUG_BREAKABLES

	return entity;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Look up to 'distance' units ahead on the bot's path for entities.  Returns the closest one.
 */
CBaseEntity * CFFBot::FindEntitiesOnPath( float distance, CPushAwayEnumerator *enumerator, bool checkStuck )
{
	Vector goal;

	int pathIndex = FindPathPoint( distance, &goal, NULL );
	bool isDegeneratePath = ( pathIndex == m_pathLength );
	if ( isDegeneratePath )
	{
		goal = m_goalPosition;
	}
	goal.z += HalfHumanHeight;

	Vector mins, maxs;
	mins = Vector( 0, 0, -HalfHumanWidth ); // Using HalfHumanWidth, ensure it's defined
	maxs = Vector( 0, 0, HalfHumanHeight ); // Using HalfHumanHeight, ensure it's defined

	if ( distance <= NearBreakableCheckDist && m_isStuck && checkStuck )
	{
		mins = Vector( -HalfHumanWidth, -HalfHumanWidth, -HalfHumanWidth );
		maxs = Vector( HalfHumanWidth, HalfHumanWidth, HalfHumanHeight );
	}

	CBaseEntity *entity = NULL;
	if ( isDegeneratePath )
	{
		entity = CheckForEntitiesAlongSegment( WorldSpaceCenter(), m_goalPosition + Vector( 0, 0, HalfHumanHeight ), mins, maxs, enumerator );
#if DEBUG_BREAKABLES
		if ( entity )
		{
			NDebugOverlay::HorzArrow( WorldSpaceCenter(), m_goalPosition, 6, 0, 0, 255, 255, true, bot_debug_breakable_duration.GetFloat() );
		}
#endif // DEBUG_BREAKABLES
	}
	else
	{
		int startIndex = MAX( 0, m_pathIndex );
		float distanceLeft = distance;
		// HACK: start with an index one lower than normal, so we can trace from the bot's location to the
		// start of the path nodes.
		for( int i=startIndex-1; i<m_pathLength-1; ++i )
		{
			if (i < 0 || i+1 >= MAX_PATH_LENGTH) break; // Bounds check for m_path

			Vector start, end;
			if ( i == startIndex - 1 )
			{
				start = GetAbsOrigin();
				end = m_path[i+1].pos;
			}
			else
			{
				start = m_path[i].pos;
				end = m_path[i+1].pos;

				if ( m_path[i+1].how == GO_LADDER_UP && m_path[i+1].ladder) // Null check ladder
				{
					// Need two checks.  First we'll check along the ladder
					start = m_path[i].pos;
					end = m_path[i+1].ladder->m_top;
				}
				else if ( m_path[i].how == GO_LADDER_UP && m_path[i].ladder) // Null check ladder
				{
					start = m_path[i].ladder->m_top;
				}
				else if ( m_path[i+1].how == GO_LADDER_DOWN && m_path[i+1].ladder) // Null check ladder
				{
					// Need two checks.  First we'll check along the ladder
					start = m_path[i].pos;
					end = m_path[i+1].ladder->m_bottom;
				}
				else if ( m_path[i].how == GO_LADDER_DOWN && m_path[i].ladder) // Null check ladder
				{
					start = m_path[i].ladder->m_bottom;
				}
			}

			float segmentLength = (start - end).Length();
			if ( distanceLeft - segmentLength < 0 )
			{
				// scale our segment back so we don't look too far
				Vector direction = end - start;
				direction.NormalizeInPlace();

				end = start + direction * distanceLeft;
			}
			entity = CheckForEntitiesAlongSegment( start + Vector( 0, 0, HalfHumanHeight ), end + Vector( 0, 0, HalfHumanHeight ), mins, maxs, enumerator );
			if ( entity )
			{
#if DEBUG_BREAKABLES
				NDebugOverlay::HorzArrow( start, end, 4, 0, 255, 0, 255, true, bot_debug_breakable_duration.GetFloat() );
#endif // DEBUG_BREAKABLES
				break;
			}

			if ( m_path[i].ladder && !IsOnLadder() && distance > NearBreakableCheckDist )	// don't try to break breakables on the other end of a ladder
				break;

			distanceLeft -= segmentLength;
			if ( distanceLeft < 0 )
				break;

			if ( i != startIndex - 1 && m_path[i+1].ladder )
			{
				// Now we'll check from the ladder out to the endpoint
				start = ( m_path[i+1].how == GO_LADDER_DOWN ) ? m_path[i+1].ladder->m_bottom : m_path[i+1].ladder->m_top;
				end = m_path[i+1].pos;

				entity = CheckForEntitiesAlongSegment( start + Vector( 0, 0, HalfHumanHeight ), end + Vector( 0, 0, HalfHumanHeight ), mins, maxs, enumerator );
				if ( entity )
				{
#if DEBUG_BREAKABLES
					NDebugOverlay::HorzArrow( start, end, 4, 0, 255, 0, 255, true, bot_debug_breakable_duration.GetFloat() );
#endif // DEBUG_BREAKABLES
					break;
				}
			}
		}
	}

	if ( entity && !IsVisible( entity->WorldSpaceCenter(), false, entity ) )
		return NULL;

	return entity;
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::PushawayTouch( CBaseEntity *pOther )
{
#if DEBUG_BREAKABLES
	NDebugOverlay::EntityBounds( pOther, 255, 0, 0, 127, 0.1f );
#endif // DEBUG_BREAKABLES

	// if we're not stuck or crouched, we don't care
	if ( !m_isStuck && !IsCrouching() )
		return;

	// See if it's breakable
	CBaseEntity *props[1];
	CBotBreakableEnumerator enumerator( props, ARRAYSIZE( props ) );
	enumerator.EnumElement( pOther );

	if ( enumerator.m_nAlreadyHit == 1 && props[0] ) // Null check props[0]
	{
		// it's breakable - try to shoot it.
		SetLookAt( "Breakable", props[0]->WorldSpaceCenter(), PRIORITY_HIGH, 0.1f, false, 5.0f, true );
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Check for breakable physics props and other breakable entities.  We do this here instead of catching them
 * in OnTouch() because players don't collide with physics props, so OnTouch() doesn't get called.  Also,
 * looking ahead like this lets us anticipate when we'll need to break something, and do it before being
 * stopped by it.
 */
void CFFBot::BreakablesCheck( void )
{
#if DEBUG_BREAKABLES
	// Debug code ...
#endif // DEBUG_BREAKABLES

	if ( IsAttacking() )
	{
		// make sure we aren't running into a breakable trying to knife an enemy
		if ( IsUsingKnife() && m_enemy != NULL )
		{
			CBaseEntity *breakables[1];
			CBotBreakableEnumerator enumerator( breakables, ARRAYSIZE( breakables ) );

			CBaseEntity *breakable = NULL;
			Vector mins = Vector( -HalfHumanWidth, -HalfHumanWidth, -HalfHumanWidth );
			Vector maxs = Vector( HalfHumanWidth, HalfHumanWidth, HalfHumanHeight );
			breakable = CheckForEntitiesAlongSegment( WorldSpaceCenter(), m_enemy->WorldSpaceCenter(), mins, maxs, &enumerator );
			if ( breakable )
			{
#if DEBUG_BREAKABLES
				NDebugOverlay::HorzArrow( WorldSpaceCenter(), m_enemy->WorldSpaceCenter(), 6, 0, 0, 255, 255, true, bot_debug_breakable_duration.GetFloat() );
#endif // DEBUG_BREAKABLES

				// look at it (chances are we'll already be looking at it, since it's between us and our enemy)
				SetLookAt( "Breakable", breakable->WorldSpaceCenter(), PRIORITY_HIGH, 0.1f, false, 5.0f, true );

				// break it (again, don't wait: we don't have ammo, since we're using the knife, and we're looking mostly at it anyway)
				PrimaryAttack();
			}
		}
		return;
	}

	if ( !HasPath() )
		return;

	bool isNear = true;

	// Check just in front of us on the path
	CBaseEntity *breakables[4];
	CBotBreakableEnumerator enumerator( breakables, ARRAYSIZE( breakables ) );
	CBaseEntity *breakable = FindEntitiesOnPath( NearBreakableCheckDist, &enumerator, true );

	// If we don't have an object right in front of us, check a ways out
	if ( !breakable )
	{
		breakable = FindEntitiesOnPath( FarBreakableCheckDist, &enumerator, false );
		isNear = false;
	}

	// Try to shoot a breakable we know about
	if ( breakable )
	{
		// look at it
		SetLookAt( "Breakable", breakable->WorldSpaceCenter(), PRIORITY_HIGH, 0.1f, false, 5.0f, true );
	}

	// break it
	if ( IsLookingAtSpot( PRIORITY_HIGH ) && m_lookAtSpotAttack )
	{
		if ( IsUsingGrenade() || ( !isNear && IsUsingKnife() ) )
		{
			EquipBestWeapon( MUST_EQUIP ); // MUST_EQUIP needs to be defined
		}
		else if ( GetActiveWeapon() && GetActiveWeapon()->m_flNextPrimaryAttack <= gpGlobals->curtime )
		{
			bool shouldShoot = IsLookingAtPosition( m_lookAtSpot, 10.0f );

			if ( !shouldShoot )
			{
				CBaseEntity *losBreakables[1]; // Renamed to avoid conflict
				CBotBreakableEnumerator LOSbreakableEnumerator( losBreakables, ARRAYSIZE( losBreakables ) ); // Renamed

				// compute the unit vector along our view
				Vector aimDir = GetViewVector();

				// trace the potential bullet's path
				trace_t result;
				UTIL_TraceLine( EyePosition(), EyePosition() + FarBreakableCheckDist * aimDir, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
				if ( result.DidHitNonWorldEntity() && result.m_pEnt ) // Added null check for m_pEnt
				{
					LOSbreakableEnumerator.EnumElement( result.m_pEnt );
					if ( LOSbreakableEnumerator.m_nAlreadyHit == 1 && LOSbreakableEnumerator.m_AlreadyHit[0] == breakable )
					{
						shouldShoot = true;
					}
				}
			}

			shouldShoot = shouldShoot && !IsFriendInLineOfFire();

			if ( shouldShoot )
			{
				PrimaryAttack();
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Check for doors that need +use to open.
 */
void CFFBot::DoorCheck( void )
{
	if ( IsAttacking() && !IsUsingKnife() )
	{
		// If we're attacking with a gun or nade, don't bother with doors.  If we're trying to
		// knife someone, we might need to open a door.
		m_isOpeningDoor = false;
		return;
	}

	if ( !HasPath() )
		return;

	// Find any doors that need a +use to open just in front of us along the path.
	CBaseEntity *doors[4];
	CBotDoorEnumerator enumerator( doors, ARRAYSIZE( doors ) ); // CBotDoorEnumerator needs to be defined
	CBaseEntity *door = FindEntitiesOnPath( NearBreakableCheckDist, &enumerator, false );

	if ( door )
	{
		if ( !IsLookingAtSpot( PRIORITY_HIGH ) )
		{
			if ( !IsOpeningDoor() )
			{
				OpenDoor( door );
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Reset the stuck-checker.
 */
void CFFBot::ResetStuckMonitor( void )
{
	if (m_isStuck)
	{
		if (IsLocalPlayerWatchingMe() && cv_bot_debug.GetBool() && UTIL_GetListenServerHost())
		{
			CBasePlayer *localPlayer = UTIL_GetListenServerHost();
			if (localPlayer) // Null check
			{
				CSingleUserRecipientFilter filter( localPlayer );
				EmitSound( filter, localPlayer->entindex(), "Bot.StuckSound" ); // TODO: Update sound name for FF
			}
		}
	}

	m_isStuck = false;
	m_stuckTimestamp = 0.0f;
	m_stuckJumpTimer.Invalidate();
	m_avgVelIndex = 0;
	m_avgVelCount = 0;

	m_areaEnteredTimestamp = gpGlobals->curtime;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Test if we have become stuck
 */
void CFFBot::StuckCheck( void )
{
	if (m_isStuck)
	{
		// we are stuck - see if we have moved far enough to be considered unstuck
		Vector delta = GetAbsOrigin() - m_stuckSpot;

		const float unstuckRange = 75.0f;
		if (delta.IsLengthGreaterThan( unstuckRange ))
		{
			// we are no longer stuck
			ResetStuckMonitor();
			PrintIfWatched( "UN-STUCK\n" );
		}
	}
	else
	{
		// check if we are stuck

		// compute average velocity over a short period (for stuck check)
		Vector vel = GetAbsOrigin() - m_lastOrigin;

		// if we are jumping, ignore Z
		if (IsJumping())
			vel.z = 0.0f;

		// cannot be Length2D, or will break ladder movement (they are only Z)
		float moveDist = vel.Length();

		float deltaT = g_BotUpdateInterval; // Ensure g_BotUpdateInterval is defined

		m_avgVel[ m_avgVelIndex++ ] = moveDist/deltaT;

		if (m_avgVelIndex == MAX_VEL_SAMPLES) // Ensure MAX_VEL_SAMPLES is defined
			m_avgVelIndex = 0;

		if (m_avgVelCount < MAX_VEL_SAMPLES)
		{
			m_avgVelCount++;
		}
		else
		{
			// we have enough samples to know if we're stuck

			float avgVel = 0.0f;
			for( int t=0; t<m_avgVelCount; ++t )
				avgVel += m_avgVel[t];

			avgVel /= m_avgVelCount;

			// cannot make this velocity too high, or bots will get "stuck" when going down ladders
			float stuckVel = (IsUsingLadder()) ? 10.0f : 20.0f;

			if (avgVel < stuckVel)
			{
				// we are stuck - note when and where we initially become stuck
				m_stuckTimestamp = gpGlobals->curtime;			
				m_stuckSpot = GetAbsOrigin();
				m_stuckJumpTimer.Start( RandomFloat( 0.3f, 0.75f ) );		// 1.0

				PrintIfWatched( "STUCK\n" );
				if (IsLocalPlayerWatchingMe() && cv_bot_debug.GetInt() > 0.0f && UTIL_GetListenServerHost())
				{
					CBasePlayer *localPlayer = UTIL_GetListenServerHost();
					if (localPlayer) // Null check
					{
						CSingleUserRecipientFilter filter( localPlayer );
						EmitSound( filter, localPlayer->entindex(), "Bot.StuckStart" ); // TODO: Update sound name for FF
					}
				}

				m_isStuck = true;
			}
		}
	}

	// always need to track this
	m_lastOrigin = GetAbsOrigin();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Check if we need to jump due to height change
 */
bool CFFBot::DiscontinuityJump( float ground, bool onlyJumpDown, bool mustJump )
{
	// Don't try to jump if in the air.
	if( !(GetFlags() & FL_ONGROUND) )
	{
		return false;
	}

	float dz = ground - GetFeetZ();

	if (dz > StepHeight && !onlyJumpDown) // StepHeight needs to be defined
	{
		// dont restrict jump time when going up
		if (Jump( MUST_JUMP )) // MUST_JUMP needs to be defined
		{
			return true;
		}
	}
	else if (!IsUsingLadder() && dz < -JumpHeight) // JumpHeight needs to be defined
	{
		if (Jump( mustJump ))
		{
			return true;
		}
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Find "simple" ground height, treating current nav area as part of the floor
 */
bool CFFBot::GetSimpleGroundHeightWithFloor( const Vector &pos, float *height, Vector *normal )
{
	if (!TheNavMesh) return false; // Null check

	if (TheNavMesh->GetSimpleGroundHeight( pos, height, normal ))
	{
		// our current nav area also serves as a ground polygon
		if (m_lastKnownArea && m_lastKnownArea->IsOverlapping( pos )) // m_lastKnownArea could be null
			*height = MAX( (*height), m_lastKnownArea->GetZ( pos ) );

		return true;
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Get our current radio chatter place
 */
Place CFFBot::GetPlace( void ) const
{
	if (m_lastKnownArea) // m_lastKnownArea could be null
		return m_lastKnownArea->GetPlace();

	return UNDEFINED_PLACE; // UNDEFINED_PLACE needs to be defined
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move towards position, independant of view angle
 */
void CFFBot::MoveTowardsPosition( const Vector &pos )
{
	Vector myOrigin = GetCentroid( this );

	//
	// Jump up on ledges
	// Because we may not be able to get to our goal position and enter the next
	// area because our extent collides with a nearby vertical ledge, make sure
	// we look far enough ahead to avoid this situation.
	// Can't look too far ahead, or bots will try to jump up slopes.
	//
	// NOTE: We need to do this frequently to catch edges at the right time
	// @todo Look ahead *along path* instead of straight line 
	//
	if ((m_lastKnownArea == NULL || !(m_lastKnownArea->GetAttributes() & NAV_MESH_NO_JUMP)) && // m_lastKnownArea could be null
		!IsOnLadder())
	{
		float ground;
		Vector aheadRay( pos.x - myOrigin.x, pos.y - myOrigin.y, 0 );
		aheadRay.NormalizeInPlace();

		// look far ahead to allow us to smoothly jump over gaps, ledges, etc
		// only jump if ground is flat at lookahead spot to avoid jumping up slopes
		bool jumped = false;
		if (IsRunning())
		{
			const float farLookAheadRange = 80.0f;	// 60
			Vector normal;
			Vector stepAhead = myOrigin + farLookAheadRange * aheadRay;
			stepAhead.z += HalfHumanHeight;

			if (GetSimpleGroundHeightWithFloor( stepAhead, &ground, &normal ))
			{
				if (normal.z > 0.9f)
					jumped = DiscontinuityJump( ground, ONLY_JUMP_DOWN ); // ONLY_JUMP_DOWN needs to be defined
			}
		}

		if (!jumped)
		{
			// close up jumping
			const float lookAheadRange = 30.0f;	// cant be less or will miss jumps over low walls
			Vector stepAhead = myOrigin + lookAheadRange * aheadRay;
			stepAhead.z += HalfHumanHeight;
			if (GetSimpleGroundHeightWithFloor( stepAhead, &ground ))
			{
				jumped = DiscontinuityJump( ground );
			}
		}

		if (!jumped)
		{
			// about to fall gap-jumping
			const float lookAheadRange = 10.0f;
			Vector stepAhead = myOrigin + lookAheadRange * aheadRay;
			stepAhead.z += HalfHumanHeight;
			if (GetSimpleGroundHeightWithFloor( stepAhead, &ground ))
			{
				jumped = DiscontinuityJump( ground, ONLY_JUMP_DOWN, MUST_JUMP ); // ONLY_JUMP_DOWN, MUST_JUMP need to be defined
			}
		}
	}


	// compute our current forward and lateral vectors
	float angle = EyeAngles().y;

	Vector2D dir( BotCOS(angle), BotSIN(angle) ); // BotCOS, BotSIN might be CS specific math funcs
	Vector2D lat( -dir.y, dir.x );

	// compute unit vector to goal position
	Vector2D to( pos.x - myOrigin.x, pos.y - myOrigin.y );
	to.NormalizeInPlace();

	// move towards the position independant of our view direction
	float toProj = to.x * dir.x + to.y * dir.y;
	float latProj = to.x * lat.x + to.y * lat.y;

	const float c = 0.25f;	// 0.5
	if (toProj > c)
		MoveForward();
	else if (toProj < -c)
		MoveBackward();

	// if we are avoiding someone via strafing, don't override
	if (m_avoid != NULL)
		return;

	if (latProj >= c)
		StrafeLeft();
	else if (latProj <= -c)
		StrafeRight();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move away from position, independant of view angle
 */
void CFFBot::MoveAwayFromPosition( const Vector &pos )
{
	// compute our current forward and lateral vectors
	float angle = EyeAngles().y;

	Vector2D dir( BotCOS(angle), BotSIN(angle) ); // BotCOS, BotSIN
	Vector2D lat( -dir.y, dir.x );

	// compute unit vector to goal position
	Vector2D to( pos.x - GetAbsOrigin().x, pos.y - GetAbsOrigin().y );
	to.NormalizeInPlace();

	// move away from the position independant of our view direction
	float toProj = to.x * dir.x + to.y * dir.y;
	float latProj = to.x * lat.x + to.y * lat.y;

	const float c = 0.5f;
	if (toProj > c)
		MoveBackward();
	else if (toProj < -c)
		MoveForward();

	if (latProj >= c)
		StrafeRight();
	else if (latProj <= -c)
		StrafeLeft();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Strafe (sidestep) away from position, independant of view angle
 */
void CFFBot::StrafeAwayFromPosition( const Vector &pos )
{
	// compute our current forward and lateral vectors
	float angle = EyeAngles().y;

	Vector2D dir( BotCOS(angle), BotSIN(angle) ); // BotCOS, BotSIN
	Vector2D lat( -dir.y, dir.x );

	// compute unit vector to goal position
	Vector2D to( pos.x - GetAbsOrigin().x, pos.y - GetAbsOrigin().y );
	to.NormalizeInPlace();

	float latProj = to.x * lat.x + to.y * lat.y;

	if (latProj >= 0.0f)
		StrafeRight();
	else
		StrafeLeft();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * For getting un-stuck
 */
void CFFBot::Wiggle( void )
{
	if (IsCrouching())
	{
		return;
	}

	// for wiggling
	if (m_wiggleTimer.IsElapsed())
	{
		m_wiggleDirection = (NavRelativeDirType)RandomInt( 0, 3 ); // NavRelativeDirType needs to be defined
		m_wiggleTimer.Start( RandomFloat( 0.3f, 0.5f ) );		// 0.3, 0.5
	}

	Vector forward, right;
	EyeVectors( &forward, &right );

	const float lookAheadRange = (m_lastKnownArea && (m_lastKnownArea->GetAttributes() & NAV_MESH_WALK)) ? 5.0f : 30.0f; // m_lastKnownArea could be null
	float ground;

	switch( m_wiggleDirection )
	{
		case LEFT: // LEFT needs to be defined (NavRelativeDirType)
		{
			// don't move left if we will fall
			Vector pos = GetAbsOrigin() - (lookAheadRange * right);

			if (GetSimpleGroundHeightWithFloor( pos, &ground ))
			{
				if (GetAbsOrigin().z - ground < StepHeight) // StepHeight
				{
					StrafeLeft();
				}
			}
			break;
		}

		case RIGHT: // RIGHT needs to be defined
		{
			// don't move right if we will fall
			Vector pos = GetAbsOrigin() + (lookAheadRange * right);

			if (GetSimpleGroundHeightWithFloor( pos, &ground ))
			{
				if (GetAbsOrigin().z - ground < StepHeight) // StepHeight
				{
					StrafeRight();
				}
			}
			break;
		}

		case FORWARD: // FORWARD needs to be defined
		{
			// don't move forward if we will fall
			Vector pos = GetAbsOrigin() + (lookAheadRange * forward);

			if (GetSimpleGroundHeightWithFloor( pos, &ground ))
			{
				if (GetAbsOrigin().z - ground < StepHeight) // StepHeight
				{
					MoveForward();
				}
			}
			break;
		}

		case BACKWARD: // BACKWARD needs to be defined
		{
			// don't move backward if we will fall
			Vector pos = GetAbsOrigin() - (lookAheadRange * forward);

			if (GetSimpleGroundHeightWithFloor( pos, &ground ))
			{
				if (GetAbsOrigin().z - ground < StepHeight) // StepHeight
				{
					MoveBackward();
				}
			}
			break;
		}
	}

	if (m_stuckJumpTimer.IsElapsed() && m_lastKnownArea && !(m_lastKnownArea->GetAttributes() & NAV_MESH_NO_JUMP)) // m_lastKnownArea could be null
	{
		if (Jump())
		{
			m_stuckJumpTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Determine approach points from eye position and approach areas of current area
 */
void CFFBot::ComputeApproachPoints( void )
{
	m_approachPointCount = 0;

	if (m_lastKnownArea == NULL) // m_lastKnownArea could be null
	{
		return;
	}

	// assume we're crouching for now
	Vector eye = GetCentroid( this ); // + pev->view_ofs;	// eye position. pev->view_ofs is GoldSrc. Use GetViewOffset().

	Vector ap;
	float halfWidth;
	for( int i=0; i<m_lastKnownArea->GetApproachInfoCount() && m_approachPointCount < MAX_APPROACH_POINTS; ++i ) // MAX_APPROACH_POINTS needs definition
	{
		const CNavArea::ApproachInfo *info = m_lastKnownArea->GetApproachInfo( i ); // CNavArea::ApproachInfo needs definition

		if (info->here.area == NULL || info->prev.area == NULL)
		{
			continue;
		}

		// compute approach point (approach area is "info->here")
		if (info->prevToHereHow <= GO_WEST) // GO_WEST needs definition
		{
			info->prev.area->ComputePortal( info->here.area, (NavDirType)info->prevToHereHow, &ap, &halfWidth ); // NavDirType needs definition
			ap.z = info->here.area->GetZ( ap );
		}
		else
		{
			// use the area's center as an approach point
			ap = info->here.area->GetCenter();
		}

		// "bend" our line of sight around corners until we can see the approach point
		Vector bendPoint;
		if (BendLineOfSight( eye, ap + Vector( 0, 0, HalfHumanHeight ), &bendPoint )) // HalfHumanHeight
		{
			// put point on the ground
			if (!TheNavMesh || TheNavMesh->GetGroundHeight( bendPoint, &bendPoint.z ) == false) // Null check for TheNavMesh
			{
				bendPoint.z = ap.z;
			}

			m_approachPoint[ m_approachPointCount ].m_pos = bendPoint;
			m_approachPoint[ m_approachPointCount ].m_area = info->here.area;
			++m_approachPointCount;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::DrawApproachPoints( void ) const
{
	if (!TheFFBots()) return; // Null check, changed TheCSBots to TheFFBots

	for( int i=0; i<m_approachPointCount; ++i )
	{
		if (!m_approachPoint[i].m_area) continue; // Null check
		// TODO: Update for FF teams
		if (TheFFBots()->GetElapsedRoundTime() >= m_approachPoint[i].m_area->GetEarliestOccupyTime( OtherTeam( GetTeamNumber() ) )) // OtherTeam needs to be FF compatible
			NDebugOverlay::Cross3D( m_approachPoint[i].m_pos, 10.0f, 255, 0, 255, true, 0.1f );
		else
			NDebugOverlay::Cross3D( m_approachPoint[i].m_pos, 10.0f, 100, 100, 100, true, 0.1f );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Find the approach point that is nearest to our current path, ahead of us
 */
bool CFFBot::FindApproachPointNearestPath( Vector *pos )
{
	if (!pos) return false; // Null check
	if (!HasPath())
		return false;

	// make sure approach points are accurate
	ComputeApproachPoints();

	if (m_approachPointCount == 0)
		return false;

	Vector target = Vector( 0, 0, 0 ), close;
	float targetRangeSq = 0.0f;
	bool found = false;

	int start = m_pathIndex;
	int end = m_pathLength;

	//
	// We dont want the strictly closest point, but the farthest approach point
	// from us that is near our path
	//
	const float nearPathSq = 10000.0f;	// (100)

	for( int i=0; i<m_approachPointCount; ++i )
	{
		if (FindClosestPointOnPath( m_approachPoint[i].m_pos, start, end, &close ) == false)
			continue;

		float rangeSq = (m_approachPoint[i].m_pos - close).LengthSqr();
		if (rangeSq > nearPathSq)
			continue;

		if (rangeSq > targetRangeSq) // This should be < targetRangeSq if we want closest, or a different logic for "farthest near path"
		{
			target = close; // This logic seems to find the point on path closest to an approach point, then picks the one where this distance is largest.
			targetRangeSq = rangeSq;
			found = true;
		}
	}

	if (found)
	{
		*pos = target + Vector( 0, 0, HalfHumanHeight ); // HalfHumanHeight
		return true;	
	}

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are at the/an enemy spawn right now
 */
bool CFFBot::IsAtEnemySpawn( void ) const
{
	CBaseEntity *spot = NULL; // Initialize to NULL
	// TODO: Update spawn point classnames and team logic for FF
	const char *spawnName = (GetTeamNumber() == TEAM_TERRORIST) ? "info_player_counterterrorist" : "info_player_terrorist"; // CS Specific

	// check if we are at any of the enemy's spawn points
	for( spot = gEntList.FindEntityByClassname( NULL, spawnName ); spot; spot = gEntList.FindEntityByClassname( spot, spawnName ) )
	{
		if (!TheNavMesh) return false; // Null check
		CNavArea *area = TheNavMesh->GetNearestNavArea( spot->WorldSpaceCenter() );
		if (area && GetLastKnownArea() == area) // GetLastKnownArea() can be null
		{
			return true;
		}
	}

	return false;
}
