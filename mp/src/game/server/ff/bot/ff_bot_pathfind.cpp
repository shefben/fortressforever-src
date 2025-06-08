//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h" // For TheFFBots() (potentially used)
#include "../ff_player.h"     // For CFFPlayer (potentially used by CFFBot)
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase (potentially used via CFFBot)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
// #include "../../shared/ff/ff_gamerules.h" // For FFGameRules() (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For TheNavMesh, CNavArea
#include "nav_ladder.h"     // For CNavLadder, NavLadderConnectVector
#include "nav_pathfind.h"   // For NavAreaBuildPath, PathCost
#include "bot_constants.h"  // For HalfHumanWidth, HalfHumanHeight, JumpCrouchHeight, StepHeight, JumpHeight, GenerationStepSize, MAX_PATH_LENGTH, NavDirType, RouteType, NavTraverseType (GO_LADDER_UP etc.), NUM_TRAVERSE_TYPES

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)				// disable warning that variable *may* not be initialized 
#endif


//--------------------------------------------------------------------------------------------------------------
/**
 * Finds a point from which we can approach a descending ladder.  First it tries behind the ladder,
 * then in front of ladder, based on LOS.  Once we know the direction, we snap to the aproaching nav
 * area.  Returns true if we're approaching from behind the ladder.
 */
// TODO: HalfHumanWidth needs to be defined (likely in bot_constants.h or similar)
static bool FindDescendingLadderApproachPoint( const CNavLadder *ladder, const CNavArea *area, Vector *pos )
{
	if (!ladder || !area || !pos) return false; // Null checks

	*pos = ladder->m_top - ladder->GetNormal() * 2.0f * HalfHumanWidth;

	trace_t result;
	UTIL_TraceLine( ladder->m_top, *pos, MASK_PLAYERSOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &result );
	if (result.fraction < 1.0f)
	{
		*pos = ladder->m_top + ladder->GetNormal() * 2.0f * HalfHumanWidth;

		area->GetClosestPointOnArea( *pos, pos );
	}

	// Use a cross product to determine which side of the ladder 'pos' is on
	Vector posToLadder = *pos - ladder->m_top;
	float dot = posToLadder.Dot( ladder->GetNormal() );
	return ( dot < 0.0f );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Determine actual path positions bot will move between along the path
 */
bool CFFBot::ComputePathPositions( void )
{
	if (m_pathLength == 0)
		return false;

	// start in first area's center
	if (!m_path[0].area) return false; // Null check
	m_path[0].pos = m_path[0].area->GetCenter();
	m_path[0].ladder = NULL;
	m_path[0].how = NUM_TRAVERSE_TYPES; // Ensure NUM_TRAVERSE_TYPES is defined

	for( int i=1; i<m_pathLength; ++i )
	{
		const ConnectInfo *from = &m_path[ i-1 ];
		ConnectInfo *to = &m_path[ i ];

		if (!from->area || !to->area) return false; // Null checks

		if (to->how <= GO_WEST)		// walk along the floor to the next area (GO_WEST needs to be defined)
		{
			to->ladder = NULL;

			// compute next point, keeping path as straight as possible
			from->area->ComputeClosestPointInPortal( to->area, (NavDirType)to->how, from->pos, &to->pos ); // NavDirType needs to be defined

			// move goal position into the goal area a bit
			const float stepInDist = 5.0f;		// how far to "step into" an area - must be less than min area size
			AddDirectionVector( &to->pos, (NavDirType)to->how, stepInDist ); // AddDirectionVector needs to be defined

			// we need to walk out of "from" area, so keep Z where we can reach it
			to->pos.z = from->area->GetZ( to->pos );

			// if this is a "jump down" connection, we must insert an additional point on the path
			if (to->area->IsConnected( from->area, NUM_DIRECTIONS ) == false) // NUM_DIRECTIONS needs to be defined
			{
				// this is a "jump down" link

				// compute direction of path just prior to "jump down"
				Vector2D dir;
				DirectionToVector2D( (NavDirType)to->how, &dir ); // DirectionToVector2D needs to be defined

				// shift top of "jump down" out a bit to "get over the ledge"
				const float pushDist = 75.0f; // 25.0f;
				to->pos.x += pushDist * dir.x;
				to->pos.y += pushDist * dir.y;

				// insert a duplicate node to represent the bottom of the fall
				if (m_pathLength < MAX_PATH_LENGTH-1) // MAX_PATH_LENGTH needs to be defined
				{
					// copy nodes down
					for( int j=m_pathLength; j>i; --j )
						m_path[j] = m_path[j-1];

					// path is one node longer
					++m_pathLength;

					// move index ahead into the new node we just duplicated
					++i;
					if (i >= MAX_PATH_LENGTH) return false; // Bounds check

					m_path[i].pos.x = to->pos.x;
					m_path[i].pos.y = to->pos.y;

					// put this one at the bottom of the fall
					m_path[i].pos.z = to->area->GetZ( m_path[i].pos );
				}
			}
		}
		else if (to->how == GO_LADDER_UP)		// to get to next area, must go up a ladder (GO_LADDER_UP needs to be defined)
		{
			// find our ladder
			const NavLadderConnectVector *pLadders = from->area->GetLadders( CNavLadder::LADDER_UP );
			if (!pLadders) return false; // Null check

			int it;
			for ( it = 0; it < pLadders->Count(); ++it)
			{
				CNavLadder *ladder = (*pLadders)[ it ].ladder;
				if (!ladder) continue; // Null check

				// can't use "behind" area when ascending...
				if (ladder->m_topForwardArea == to->area ||
					ladder->m_topLeftArea == to->area ||
					ladder->m_topRightArea == to->area)
				{
					to->ladder = ladder;
					to->pos = ladder->m_bottom + ladder->GetNormal() * 2.0f * HalfHumanWidth; // HalfHumanWidth
					break;
				}
			}

			if (it == pLadders->Count())
			{
				PrintIfWatched( "ERROR: Can't find ladder in path\n" );
				return false;
			}
		}
		else if (to->how == GO_LADDER_DOWN)		// to get to next area, must go down a ladder (GO_LADDER_DOWN needs to be defined)
		{
			// find our ladder
			const NavLadderConnectVector *pLadders = from->area->GetLadders( CNavLadder::LADDER_DOWN );
			if (!pLadders) return false; // Null check

			int it;
			for ( it = 0; it < pLadders->Count(); ++it)
			{
				CNavLadder *ladder = (*pLadders)[ it ].ladder;
				if (!ladder) continue; // Null check

				if (ladder->m_bottomArea == to->area)
				{
					to->ladder = ladder;

					FindDescendingLadderApproachPoint( to->ladder, from->area, &to->pos );
					break;
				}
			}

			if (it == pLadders->Count())
			{
				PrintIfWatched( "ERROR: Can't find ladder in path\n" );
				return false;
			}
		}
	}

	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * If next step of path uses a ladder, prepare to traverse it
 */
void CFFBot::SetupLadderMovement( void )
{
	if (m_pathIndex < 1 || m_pathLength == 0 || m_pathIndex >= MAX_PATH_LENGTH || m_pathIndex-1 < 0) // Bounds checks
		return;

	const ConnectInfo *to = &m_path[ m_pathIndex ];
	const ConnectInfo *from = &m_path[ m_pathIndex - 1 ];

	if (to->ladder)
	{
		m_spotEncounter = NULL;
		m_areaEnteredTimestamp = gpGlobals->curtime;

		m_pathLadder = to->ladder;
		m_pathLadderTimestamp = gpGlobals->curtime;

		QAngle ladderAngles;
		VectorAngles( m_pathLadder->GetNormal(), ladderAngles );

		// to get to next area, we must traverse a ladder
		if (to->how == GO_LADDER_UP) // GO_LADDER_UP
		{
			m_pathLadderState = APPROACH_ASCENDING_LADDER; // Enum needs definition
			m_pathLadderFaceIn = true;
			PrintIfWatched( "APPROACH_ASCENDING_LADDER\n" );
			m_goalPosition = m_pathLadder->m_bottom + m_pathLadder->GetNormal() * 2.0f * HalfHumanWidth; // HalfHumanWidth
			m_lookAheadAngle = AngleNormalizePositive( ladderAngles[ YAW ] + 180.0f );
		}
		else // Assuming GO_LADDER_DOWN
		{
			// try to mount ladder "face out" first
			bool behind = FindDescendingLadderApproachPoint( m_pathLadder, from->area, &m_goalPosition );

			if ( behind )
			{
				PrintIfWatched( "APPROACH_DESCENDING_LADDER (face out)\n" );
				m_pathLadderState = APPROACH_DESCENDING_LADDER; // Enum needs definition
				m_pathLadderFaceIn = false;
				m_lookAheadAngle = ladderAngles[ YAW ];
			}
			else
			{
				PrintIfWatched( "APPROACH_DESCENDING_LADDER (face in)\n" );
				m_pathLadderState = APPROACH_DESCENDING_LADDER; // Enum needs definition
				m_pathLadderFaceIn = true;
				m_lookAheadAngle = AngleNormalizePositive( ladderAngles[ YAW ] + 180.0f );
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/// @todo What about ladders whose top AND bottom are messed up?
void CFFBot::ComputeLadderEndpoint( bool isAscending )
{
	if (!m_pathLadder) return; // Null check

	trace_t result;
	Vector from, to;

	if (isAscending)
	{
		// find actual top in case m_pathLadder penetrates the ceiling
		// trace from our chest height at m_pathLadder base
		from = m_pathLadder->m_bottom + m_pathLadder->GetNormal() * HalfHumanWidth; // HalfHumanWidth
		from.z = GetAbsOrigin().z + HalfHumanHeight; // HalfHumanHeight
		to = m_pathLadder->m_top;
	}
	else
	{
		// find actual bottom in case m_pathLadder penetrates the floor
		// trace from our chest height at m_pathLadder top
		from = m_pathLadder->m_top + m_pathLadder->GetNormal() * HalfHumanWidth; // HalfHumanWidth
		from.z = GetAbsOrigin().z + HalfHumanHeight; // HalfHumanHeight
		to = m_pathLadder->m_bottom;
	}

	UTIL_TraceLine( from, to, MASK_PLAYERSOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &result ); // Corrected: to instead of m_pathLadder->m_bottom

	if (result.fraction == 1.0f)
		m_pathLadderEnd = to.z;
	else
		m_pathLadderEnd = from.z + result.fraction * (to.z - from.z);
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Navigate our current ladder. Return true if we are doing ladder navigation.
 * @todo Need Push() and Pop() for run/walk context to keep ladder speed contained.
 */
bool CFFBot::UpdateLadderMovement( void )
{
	if (m_pathLadder == NULL)
		return false;

	bool giveUp = false;

	// check for timeout
	const float ladderTimeoutDuration = 10.0f;
	if (gpGlobals->curtime - m_pathLadderTimestamp > ladderTimeoutDuration && !cv_bot_debug.GetBool())
	{
		PrintIfWatched( "Ladder timeout!\n" );
		giveUp = true;
	}
	// Check m_pathLadderState against defined enums
	else if (m_pathLadderState == APPROACH_ASCENDING_LADDER || 
			 m_pathLadderState == APPROACH_DESCENDING_LADDER || 
			 m_pathLadderState == ASCEND_LADDER || 
			 m_pathLadderState == DESCEND_LADDER || 
			 m_pathLadderState == DISMOUNT_ASCENDING_LADDER ||
			 m_pathLadderState == MOVE_TO_DESTINATION)
	{
		if (m_isStuck)
		{
			PrintIfWatched( "Giving up ladder - stuck\n" );
			giveUp = true;
		}
	}

	if (giveUp)
	{
		// jump off ladder and give up
		Jump( MUST_JUMP ); // MUST_JUMP needs to be defined
		Wiggle();
		ResetStuckMonitor();
		DestroyPath();
		Run();
		return false;
	}
	else
	{
		ResetStuckMonitor();
	}

	Vector myOrigin = GetCentroid( this );

	// check if somehow we totally missed the ladder
	// Check m_pathLadderState against defined enums
	switch( m_pathLadderState )
	{
		case MOUNT_ASCENDING_LADDER:
		case MOUNT_DESCENDING_LADDER:
		case ASCEND_LADDER:
		case DESCEND_LADDER:
		{
			const float farAway = 200.0f;
			const Vector &ladderPos = (m_pathLadderState == MOUNT_ASCENDING_LADDER ||
				m_pathLadderState == ASCEND_LADDER) ? m_pathLadder->m_bottom : m_pathLadder->m_top;
			if ((ladderPos.AsVector2D() - myOrigin.AsVector2D()).IsLengthGreaterThan( farAway ))
			{
				PrintIfWatched( "Missed ladder\n" );
				Jump( MUST_JUMP ); // MUST_JUMP
				DestroyPath();
				Run();
				return false;
			}
			break;
		}
	}


	m_areaEnteredTimestamp = gpGlobals->curtime;

	const float tolerance = 10.0f;
	const float closeToGoal = 25.0f;

	// Check m_pathLadderState against defined enums
	switch( m_pathLadderState )
	{
		case APPROACH_ASCENDING_LADDER:
		{
			bool approached = false;

			Vector2D d( myOrigin.x - m_goalPosition.x, myOrigin.y - m_goalPosition.y );

			if (d.x * m_pathLadder->GetNormal().x + d.y * m_pathLadder->GetNormal().y < 0.0f)
			{
				Vector2D perp( -m_pathLadder->GetNormal().y, m_pathLadder->GetNormal().x );

				if (fabs(d.x * perp.x + d.y * perp.y) < tolerance && d.Length() < closeToGoal)
					approached = true;
			}

			// small radius will just slow them down a little for more accuracy in hitting their spot
			const float walkRange = 50.0f;
			if (d.IsLengthLessThan( walkRange ))
			{
				Walk();
				StandUp();
			}

			if ( d.IsLengthLessThan( 100.0f ) )
			{
				if ( !IsOnLadder() && (m_pathLadder->m_bottom.z - GetAbsOrigin().z > JumpCrouchHeight ) ) // JumpCrouchHeight
				{
					// find yaw to directly aim at ladder
					QAngle idealAngle;
					VectorAngles( GetAbsVelocity(), idealAngle );
					const float angleTolerance = 15.0f;
					if (AnglesAreEqual( EyeAngles().y, idealAngle.y, angleTolerance ))
					{
						Jump();
					}
				}
			}

			/// @todo Check that we are on the ladder we think we are
			if (IsOnLadder())
			{
				m_pathLadderState = ASCEND_LADDER;
				PrintIfWatched( "ASCEND_LADDER\n" );

				// find actual top in case m_pathLadder penetrates the ceiling
				ComputeLadderEndpoint( true );
			}
			else if (approached)
			{
				// face the m_pathLadder
				m_pathLadderState = FACE_ASCENDING_LADDER;
				PrintIfWatched( "FACE_ASCENDING_LADDER\n" );
			}
			else
			{
				// move toward ladder mount point
				MoveTowardsPosition( m_goalPosition );
			}
			break;
		}

		case APPROACH_DESCENDING_LADDER:
		{
			// fall check
			if (GetFeetZ() <= m_pathLadder->m_bottom.z + HalfHumanHeight) // HalfHumanHeight
			{
				PrintIfWatched( "Fell from ladder.\n" );

				m_pathLadderState = MOVE_TO_DESTINATION;
				if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) // Bounds and null checks
					m_path[ m_pathIndex ].area->GetClosestPointOnArea( m_pathLadder->m_bottom, &m_goalPosition );
				else { DestroyPath(); return false; } // Error case
				m_goalPosition += m_pathLadder->GetNormal() * HalfHumanWidth; // HalfHumanWidth

				PrintIfWatched( "MOVE_TO_DESTINATION\n" );
			}
			else
			{
				bool approached = false;

				Vector2D d( myOrigin.x - m_goalPosition.x, myOrigin.y - m_goalPosition.y );

				if (d.x * m_pathLadder->GetNormal().x + d.y * m_pathLadder->GetNormal().y > 0.0f)
				{
					Vector2D perp( -m_pathLadder->GetNormal().y, m_pathLadder->GetNormal().x );

					if (fabs(d.x * perp.x + d.y * perp.y) < tolerance && d.Length() < closeToGoal)
						approached = true;
				}

				// if approaching ladder from the side or "ahead", walk
				if (!m_lastKnownArea || m_pathLadder->m_topBehindArea != m_lastKnownArea) // Null check m_lastKnownArea
				{
					const float walkRange = 150.0f;
					if (!IsCrouching() && d.IsLengthLessThan( walkRange ))
						Walk();
				}

				/// @todo Check that we are on the ladder we think we are
				if (IsOnLadder())
				{
					// we slipped onto the ladder - climb it
					m_pathLadderState = DESCEND_LADDER;
					Run();
					PrintIfWatched( "DESCEND_LADDER\n" );

					// find actual bottom in case m_pathLadder penetrates the floor
					ComputeLadderEndpoint( false );
				}
				else if (approached)
				{
					// face the ladder
					m_pathLadderState = FACE_DESCENDING_LADDER;
					PrintIfWatched( "FACE_DESCENDING_LADDER\n" );
				}
				else
				{
					// move toward ladder mount point
					MoveTowardsPosition( m_goalPosition );
				}
			}
			break;
		}

		case FACE_ASCENDING_LADDER:
		{
			// find yaw to directly aim at ladder
			Vector to = m_pathLadder->GetPosAtHeight(myOrigin.z) - myOrigin;

			QAngle idealAngle;
			VectorAngles( to, idealAngle );

			if (m_pathIndex >= MAX_PATH_LENGTH || !m_path[m_pathIndex].area) { DestroyPath(); return false; } // Bounds and null check

			if (m_path[ m_pathIndex ].area == m_pathLadder->m_topForwardArea)
			{
				m_pathLadderDismountDir = FORWARD; // Enum needs definition
			}
			else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topLeftArea)
			{
				m_pathLadderDismountDir = LEFT; // Enum needs definition
				idealAngle[ YAW ] = AngleNormalizePositive( idealAngle[ YAW ] + 90.0f );
			}
			else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topRightArea)
			{
				m_pathLadderDismountDir = RIGHT; // Enum needs definition
				idealAngle[ YAW ] = AngleNormalizePositive( idealAngle[ YAW ] - 90.0f );
			}

			const float angleTolerance = 5.0f;
			if (AnglesAreEqual( EyeAngles().y, idealAngle.y, angleTolerance ))
			{
				// move toward ladder until we become "on" it
				Run();
				ResetStuckMonitor();
				m_pathLadderState = MOUNT_ASCENDING_LADDER;
				switch (m_pathLadderDismountDir)
				{
					case LEFT:		PrintIfWatched( "MOUNT_ASCENDING_LADDER LEFT\n" );		break;
					case RIGHT:		PrintIfWatched( "MOUNT_ASCENDING_LADDER RIGHT\n" );		break;
					default:		PrintIfWatched( "MOUNT_ASCENDING_LADDER FORWARD\n" );	break;
				}
			}
			break;
		}

		case FACE_DESCENDING_LADDER:
		{
			// find yaw to directly aim at ladder
			Vector to = m_pathLadder->GetPosAtHeight(myOrigin.z) - myOrigin;

			QAngle idealAngle;
			VectorAngles( to, idealAngle );

			const float angleTolerance = 5.0f;
			if (AnglesAreEqual( EyeAngles().y, idealAngle.y, angleTolerance ))
			{
				// move toward ladder until we become "on" it
				m_pathLadderState = MOUNT_DESCENDING_LADDER;
				ResetStuckMonitor();
				PrintIfWatched( "MOUNT_DESCENDING_LADDER\n" );
			}
			break;
		}

		case MOUNT_ASCENDING_LADDER:
			if (IsOnLadder())
			{
				m_pathLadderState = ASCEND_LADDER;
				PrintIfWatched( "ASCEND_LADDER\n" );

				// find actual top in case m_pathLadder penetrates the ceiling
				ComputeLadderEndpoint( true );
			}

			// move toward ladder mount point
			if ( !IsOnLadder() && (m_pathLadder->m_bottom.z - GetAbsOrigin().z > JumpCrouchHeight ) ) // JumpCrouchHeight
			{
				Jump();
			}

			switch( m_pathLadderDismountDir )
			{
				case RIGHT:		StrafeLeft();	break;
				case LEFT:		StrafeRight();	break;
				default:		MoveForward();	break;
			}
			break;

		case MOUNT_DESCENDING_LADDER:
			// fall check
			if (GetFeetZ() <= m_pathLadder->m_bottom.z + HalfHumanHeight) // HalfHumanHeight
			{
				PrintIfWatched( "Fell from ladder.\n" );

				m_pathLadderState = MOVE_TO_DESTINATION;
				if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) // Bounds and null checks
					m_path[ m_pathIndex ].area->GetClosestPointOnArea( m_pathLadder->m_bottom, &m_goalPosition );
				else { DestroyPath(); return false; }
				m_goalPosition += m_pathLadder->GetNormal() * HalfHumanWidth; // HalfHumanWidth

				PrintIfWatched( "MOVE_TO_DESTINATION\n" );
			}
			else
			{
				if (IsOnLadder())
				{
					m_pathLadderState = DESCEND_LADDER;
					PrintIfWatched( "DESCEND_LADDER\n" );

					// find actual bottom in case m_pathLadder penetrates the floor
					ComputeLadderEndpoint( false );
				}

				// move toward ladder mount point
				MoveForward();
			}
			break;

		case ASCEND_LADDER:
			// run, so we can make our dismount jump to the side, if necessary
			Run();

			// if our destination area requires us to crouch, do it
			if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area && (m_path[ m_pathIndex ].area->GetAttributes() & NAV_MESH_CROUCH)) // Bounds and null checks
				Crouch();

			// did we reach the top?
			if (GetFeetZ() >= m_pathLadderEnd)
			{
				// we reached the top - dismount
				m_pathLadderState = DISMOUNT_ASCENDING_LADDER;
				PrintIfWatched( "DISMOUNT_ASCENDING_LADDER\n" );

				if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) { // Bounds and null checks
					if (m_path[ m_pathIndex ].area == m_pathLadder->m_topForwardArea)
						m_pathLadderDismountDir = FORWARD;
					else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topLeftArea)
						m_pathLadderDismountDir = LEFT;
					else if (m_path[ m_pathIndex ].area == m_pathLadder->m_topRightArea)
						m_pathLadderDismountDir = RIGHT;
				} else { DestroyPath(); return false; }


				m_pathLadderDismountTimestamp = gpGlobals->curtime;
			}
			else if (!IsOnLadder())
			{
				// we fall off the ladder, repath
				DestroyPath();
				return false;
			}

			// move up ladder
			switch( m_pathLadderDismountDir )
			{
				case RIGHT:		StrafeLeft();	break;
				case LEFT:		StrafeRight();	break;
				default:		MoveForward();	break;
			}
			break;

		case DESCEND_LADDER:
		{
			Run();
			float destHeight = m_pathLadderEnd;
			// Check attributes on valid area pointer
			if ( m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area && (m_path[ m_pathIndex ].area->GetAttributes() & NAV_MESH_NO_JUMP) == 0 ) // NAV_MESH_NO_JUMP
			{
				destHeight += HalfHumanHeight; // HalfHumanHeight
			}
			if ( !IsOnLadder() || GetFeetZ() <= destHeight )
			{
				// we reached the bottom, or we fell off - dismount
				m_pathLadderState = MOVE_TO_DESTINATION;
				if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) // Bounds and null checks
					m_path[ m_pathIndex ].area->GetClosestPointOnArea( m_pathLadder->m_bottom, &m_goalPosition );
				else { DestroyPath(); return false; }
				m_goalPosition += m_pathLadder->GetNormal() * HalfHumanWidth; // HalfHumanWidth

				PrintIfWatched( "MOVE_TO_DESTINATION\n" );
			}

			// Move down ladder
			MoveForward();

			break;
		}

		case DISMOUNT_ASCENDING_LADDER:
		{
			if (gpGlobals->curtime - m_pathLadderDismountTimestamp >= 0.4f)
			{
				m_pathLadderState = MOVE_TO_DESTINATION;
				if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) // Bounds and null checks
					m_path[ m_pathIndex ].area->GetClosestPointOnArea( myOrigin, &m_goalPosition );
				else { DestroyPath(); return false; }
				PrintIfWatched( "MOVE_TO_DESTINATION\n" );
			}

			// We should already be facing the dismount point
			MoveForward();
			break;
		}

		case MOVE_TO_DESTINATION:
			if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area && m_path[ m_pathIndex ].area->Contains( myOrigin )) // Bounds and null checks
			{
				// successfully traversed ladder and reached destination area
				// exit ladder state machine
				PrintIfWatched( "Ladder traversed.\n" );
				m_pathLadder = NULL;

				// incrememnt path index to next step beyond this ladder
				SetPathIndex( m_pathIndex+1 );

				ClearLookAt();

				return false;
			}

			MoveTowardsPosition( m_goalPosition );
			break;
	}

	if ( ( cv_bot_traceview.GetInt() == 1 && IsLocalPlayerWatchingMe() ) || cv_bot_traceview.GetInt() == 10 )
	{
		DrawPath();
	}

	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Compute closest point on path to given point
 * NOTE: This does not do line-of-sight tests, so closest point may be thru the floor, etc
 */
bool CFFBot::FindClosestPointOnPath( const Vector &worldPos, int startIndex, int endIndex, Vector *close ) const
{
	if (!HasPath() || close == NULL)
		return false;

	if (startIndex < 1 || endIndex >= m_pathLength || startIndex > endIndex) return false; // Bounds check

	Vector along, toWorldPos;
	Vector pos;
	const Vector *from, *to;
	float length;
	float closeLength;
	float closeDistSq = 9999999999.9;
	float distSq;

	for( int i=startIndex; i<=endIndex; ++i ) // Iterate up to and including endIndex
	{
		from = &m_path[i-1].pos;
		to = &m_path[i].pos;

		// compute ray along this path segment
		along = *to - *from;

		// make it a unit vector along the path
		length = along.NormalizeInPlace();
		if (length < FLT_EPSILON) continue; // Skip zero-length segments

		// compute vector from start of segment to our point
		toWorldPos = worldPos - *from;

		// find distance of closest point on ray
		closeLength = DotProduct( toWorldPos, along );

		// constrain point to be on path segment
		if (closeLength <= 0.0f)
			pos = *from;
		else if (closeLength >= length)
			pos = *to;
		else
			pos = *from + closeLength * along;

		distSq = (pos - worldPos).LengthSqr();

		// keep the closest point so far
		if (distSq < closeDistSq)
		{
			closeDistSq = distSq;
			*close = pos;
		}
	}

	return (closeDistSq < 9999999999.0); // Return true if a point was found
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return the closest point to our current position on our current path
 * If "local" is true, only check the portion of the path surrounding m_pathIndex.
 */
int CFFBot::FindOurPositionOnPath( Vector *close, bool local ) const
{
	if (!HasPath())
		return -1;

	Vector along, toFeet;
	Vector feet = GetAbsOrigin();
	Vector eyes = feet + Vector( 0, 0, HalfHumanHeight );	// HalfHumanHeight
	Vector pos;
	const Vector *from, *to;
	float length;
	float closeLength;
	float closeDistSq = 9999999999.9;
	int closeIndex = -1;
	float distSq;

	int start, end;

	if (local)
	{
		start = m_pathIndex - 3;
		if (start < 1)
			start = 1;

		end = m_pathIndex + 3;
		if (end > m_pathLength) // Should be >= m_pathLength for loop condition i < end
			end = m_pathLength;
	}
	else
	{
		start = 1;
		end = m_pathLength;
	}

	if (start >= end) return -1; // No segments to check

	for( int i=start; i<end; ++i ) // Iterate up to end-1 for segments [i-1] to [i]
	{
		if (i-1 < 0 || i >= MAX_PATH_LENGTH) continue; // Bounds check

		from = &m_path[i-1].pos;
		to = &m_path[i].pos;

		// compute ray along this path segment
		along = *to - *from;

		// make it a unit vector along the path
		length = along.NormalizeInPlace();
		if (length < FLT_EPSILON) continue;

		// compute vector from start of segment to our point
		toFeet = feet - *from;

		// find distance of closest point on ray
		closeLength = DotProduct( toFeet, along );

		// constrain point to be on path segment
		if (closeLength <= 0.0f)
			pos = *from;
		else if (closeLength >= length)
			pos = *to;
		else
			pos = *from + closeLength * along;

		distSq = (pos - feet).LengthSqr();

		// keep the closest point so far
		if (distSq < closeDistSq)
		{
			// don't use points we cant see
			Vector probe = pos + Vector( 0, 0, HalfHumanHeight ); // HalfHumanHeight
			if (!IsWalkableTraceLineClear( eyes, probe, WALK_THRU_DOORS | WALK_THRU_BREAKABLES )) // WALK_THRU enums
				continue;

			// don't use points we cant reach
			if (!IsStraightLinePathWalkable( pos ))
				continue;

			closeDistSq = distSq;
			if (close)
				*close = pos;
			closeIndex = i-1;
		}
	}

	return closeIndex;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Test for un-jumpable height change, or unrecoverable fall
 */
bool CFFBot::IsStraightLinePathWalkable( const Vector &goal ) const
{
// this is causing hang-up problems when crawling thru ducts/windows that drop off into rooms (they fail the "falling" check)
return true; // Original code had this return true always

	const float inc = GenerationStepSize; // GenerationStepSize

	Vector feet = GetAbsOrigin();
	Vector dir = goal - feet;
	float length = dir.NormalizeInPlace();
	if (length < FLT_EPSILON) return true; // Zero length path is walkable

	float lastGround;
	lastGround = feet.z;


	float along=0.0f;
	Vector pos;
	float ground;
	bool done = false;
	while( !done )
	{
		along += inc;
		if (along > length)
		{
			along = length;
			done = true;
		}

		// compute step along path
		pos = feet + along * dir;

		pos.z += HalfHumanHeight; // HalfHumanHeight

		if (!TheNavMesh || TheNavMesh->GetSimpleGroundHeight( pos, &ground ) == false) // Null check TheNavMesh
			return false;

		// check for falling
		if (ground - lastGround < -StepHeight) // StepHeight
			return false;

		// check for unreachable jump
		// use slightly shorter jump limit, to allow for some fudge room
		if (ground - lastGround > JumpHeight) // JumpHeight
			return false;

		lastGround = ground;
	}

	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Compute a point a fixed distance ahead along our path.
 * Returns path index just after point.
 */
int CFFBot::FindPathPoint( float aheadRange, Vector *point, int *prevIndex )
{
	if (!point) return m_pathIndex; // Null check

	Vector myOrigin = GetCentroid( this );

	// find path index just past aheadRange
	int afterIndex;

	// finds the closest point on local area of path, and returns the path index just prior to it
	Vector close;
	int startIndex = FindOurPositionOnPath( &close, true );

	if (prevIndex)
		*prevIndex = startIndex;

	if (startIndex < 0 || startIndex >= m_pathLength) // Corrected bounds check for startIndex
	{
		// went off the end of the path
		// or next point in path is unwalkable (ie: jump-down)
		// keep same point
		return m_pathIndex;
	}

	if (startIndex == 0 && m_pathLength > 0) { // If at the very beginning of path
		 close = m_path[0].pos; // Start from the first point
	}


	// if we are crouching, just follow the path exactly
	if (IsCrouching())
	{
		// we want to move to the immediately next point along the path from where we are now
		int index = startIndex+1;
		if (index >= m_pathLength)
			index = m_pathLength-1;

		if (index < 0) index = 0; // Ensure index is not negative
		if (index >= MAX_PATH_LENGTH) index = MAX_PATH_LENGTH -1; // Ensure within bounds for m_path

		*point = m_path[ index ].pos;

		// if we are very close to the next point in the path, skip ahead to the next one to avoid wiggling
		// we must do a 2D check here, in case the goal point is floating in space due to jump down, etc
		const float closeEpsilon = 20.0f;	// 10
		while ((*point - close).AsVector2D().IsLengthLessThan( closeEpsilon ))
		{
			++index;

			if (index >= m_pathLength)
			{
				index = m_pathLength-1;
				break;
			}
			if (index < 0) { index = 0; break; } // Safety break
			if (index >= MAX_PATH_LENGTH) { index = MAX_PATH_LENGTH -1; break; } // Safety break


			*point = m_path[ index ].pos;
		}

		return index;
	}

	// make sure we use a node a minimum distance ahead of us, to avoid wiggling 
	while (startIndex < m_pathLength-1)
	{
		if (startIndex+1 >= MAX_PATH_LENGTH) break; // Bounds check
		Vector pos = m_path[ startIndex+1 ].pos;

		// we must do a 2D check here, in case the goal point is floating in space due to jump down, etc
		const float closeEpsilon = 20.0f;
		if ((pos - close).AsVector2D().IsLengthLessThan( closeEpsilon ))
		{
			++startIndex;
		}
		else
		{
			break;
		}
	}

	// if we hit a ladder, stop, or jump area, must stop (dont use ladder behind us)
	if (startIndex > m_pathIndex && startIndex < m_pathLength && startIndex < MAX_PATH_LENGTH && m_path[ startIndex ].area && // Added area null check
		(m_path[ startIndex ].ladder || m_path[ startIndex ].area->GetAttributes() & (NAV_MESH_JUMP | NAV_MESH_STOP))) // NAV_MESH_ enums
	{
		*point = m_path[ startIndex ].pos;
		return startIndex;
	}

	// we need the point just *ahead* of us
	++startIndex;
	if (startIndex >= m_pathLength)
		startIndex = m_pathLength-1;
	if (startIndex < 0) startIndex = 0; // Safety

	// if we hit a ladder, stop, or jump area, must stop
	if (startIndex < m_pathLength && startIndex < MAX_PATH_LENGTH && m_path[ startIndex ].area && // Added area null check
		(m_path[ startIndex ].ladder || m_path[ startIndex ].area->GetAttributes() & (NAV_MESH_JUMP | NAV_MESH_STOP))) // NAV_MESH_ enums
	{
		*point = m_path[ startIndex ].pos;
		return startIndex;
	}

	// note direction of path segment we are standing on
	if (startIndex == 0 || startIndex >= MAX_PATH_LENGTH) return m_pathIndex; // Cannot compute initDir
	Vector initDir = m_path[ startIndex ].pos - m_path[ startIndex-1 ].pos;
	initDir.NormalizeInPlace();

	Vector feet = GetAbsOrigin();
	Vector eyes = feet + Vector( 0, 0, HalfHumanHeight ); // HalfHumanHeight
	float rangeSoFar = 0;

	// this flag is true if our ahead point is visible
	bool visible = true;

	Vector prevDir = initDir;

	// step along the path until we pass aheadRange
	bool isCorner = false;
	int i;
	for( i=startIndex; i<m_pathLength; ++i )
	{
		if (i >= MAX_PATH_LENGTH || i-1 < 0) break; // Bounds check
		Vector pos = m_path[i].pos;
		Vector to = pos - m_path[i-1].pos;
		Vector dir = to;
		dir.NormalizeInPlace();

		// don't allow path to double-back from our starting direction (going upstairs, down curved passages, etc)
		if (DotProduct( dir, initDir ) < 0.0f) // -0.25f
		{
			--i;
			break;
		}

		// if the path turns a corner, we want to move towards the corner, not into the wall/stairs/etc
		if (DotProduct( dir, prevDir ) < 0.5f)
		{
			isCorner = true;
			--i;
			break;
		}
		prevDir = dir;

		// don't use points we cant see
		Vector probe = pos + Vector( 0, 0, HalfHumanHeight ); // HalfHumanHeight
		if (!IsWalkableTraceLineClear( eyes, probe, WALK_THRU_BREAKABLES )) // WALK_THRU_BREAKABLES
		{
			// presumably, the previous point is visible, so we will interpolate
			visible = false;
			break;
		}

		// if we encounter a ladder or jump area, we must stop
		if (i < m_pathLength && i < MAX_PATH_LENGTH && m_path[i].area && // Added area null check
				(m_path[ i ].ladder || m_path[ i ].area->GetAttributes() & NAV_MESH_JUMP)) // NAV_MESH_JUMP
			break;

		// Check straight-line path from our current position to this position
		// Test for un-jumpable height change, or unrecoverable fall
		if (!IsStraightLinePathWalkable( pos ))
		{
			--i;
			break;
		}

		Vector along = (i == startIndex) ? (pos - feet) : (pos - m_path[i-1].pos);
		rangeSoFar += along.Length2D();

		// stop if we have gone farther than aheadRange
		if (rangeSoFar >= aheadRange)
			break;
	}

	if (i < 0) i = 0; // Ensure i is not negative before use

	if (i < startIndex)
		afterIndex = startIndex;
	else if (i < m_pathLength)
		afterIndex = i;
	else
		afterIndex = m_pathLength-1;

	if (afterIndex < 0) afterIndex = 0; // Ensure not negative


	// compute point on the path at aheadRange
	if (afterIndex == 0)
	{
		if (m_pathLength > 0 && 0 < MAX_PATH_LENGTH) *point = m_path[0].pos; // Bounds check
		else { *point = myOrigin; return m_pathIndex; } // Error case
	}
	else
	{
		if (afterIndex >= MAX_PATH_LENGTH || afterIndex-1 < 0) return m_pathIndex; // Bounds check
		// interpolate point along path segment
		const Vector *afterPoint = &m_path[ afterIndex ].pos;
		const Vector *beforePoint = &m_path[ afterIndex-1 ].pos;

		Vector to = *afterPoint - *beforePoint;
		float length = to.Length2D();
		if (length < FLT_EPSILON) { *point = *beforePoint; return afterIndex; } // Avoid division by zero


		float t = 1.0f - ((rangeSoFar - aheadRange) / length);

		if (t < 0.0f)
			t = 0.0f;
		else if (t > 1.0f)
			t = 1.0f;

		*point = *beforePoint + t * to;

		// if afterPoint wasn't visible, slide point backwards towards beforePoint until it is
		if (!visible)
		{
			const float sightStepSize = 25.0f;
			float dt = sightStepSize / length;

			Vector probe = *point + Vector( 0, 0, HalfHumanHeight ); // HalfHumanHeight
			while( t > 0.0f && !IsWalkableTraceLineClear( eyes, probe, WALK_THRU_BREAKABLES ) ) // WALK_THRU_BREAKABLES
			{
				t -= dt;
				*point = *beforePoint + t * to;
				probe = *point + Vector( 0, 0, HalfHumanHeight ); // Update probe
			}

			if (t <= 0.0f)
				*point = *beforePoint;
		}
	}

	// if position found is too close to us, or behind us, force it farther down the path so we don't stop and wiggle
	if (!isCorner)
	{
		const float epsilon = 50.0f;
		Vector2D toPoint;
		toPoint.x = point->x - myOrigin.x;
		toPoint.y = point->y - myOrigin.y;
		if (DotProduct2D( toPoint, initDir.AsVector2D() ) < 0.0f || toPoint.IsLengthLessThan( epsilon ))
		{
			int k; // Renamed from i to avoid conflict
			for( k=startIndex; k<m_pathLength; ++k )
			{
				if (k >= MAX_PATH_LENGTH || !m_path[k].area) break; // Bounds and null check
				toPoint.x = m_path[k].pos.x - myOrigin.x;
				toPoint.y = m_path[k].pos.y - myOrigin.y;
				if (m_path[k].ladder || m_path[k].area->GetAttributes() & NAV_MESH_JUMP || toPoint.IsLengthGreaterThan( epsilon )) // NAV_MESH_JUMP
				{
					*point = m_path[k].pos;
					startIndex = k;
					break;
				}
			}

			if (k == m_pathLength)
			{
				*point = GetPathEndpoint();
				startIndex = m_pathLength-1;
				if (startIndex < 0) startIndex = 0; // Safety
			}
		}
	}

	// m_pathIndex should always be the next point on the path, even if we're not moving directly towards it
	return startIndex;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Set the current index along the path
 */
void CFFBot::SetPathIndex( int newIndex )
{
	m_pathIndex = MIN( newIndex, m_pathLength-1 );
	if (m_pathIndex < 0) m_pathIndex = 0; // Ensure not negative

	m_areaEnteredTimestamp = gpGlobals->curtime;

	if (m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].ladder) // Bounds check
	{
		SetupLadderMovement();
	}
	else
	{
		// get our "encounter spots" for this leg of the path
		if (m_pathIndex < m_pathLength && m_pathIndex >= 2 &&
			m_pathIndex < MAX_PATH_LENGTH && m_pathIndex-1 < MAX_PATH_LENGTH && m_pathIndex-2 < MAX_PATH_LENGTH &&
			m_path[ m_pathIndex-1 ].area && m_path[ m_pathIndex-2 ].area && m_path[ m_pathIndex ].area) // Bounds and null checks
			m_spotEncounter = m_path[ m_pathIndex-1 ].area->GetSpotEncounter( m_path[ m_pathIndex-2 ].area, m_path[ m_pathIndex ].area );
		else
			m_spotEncounter = NULL;

		m_pathLadder = NULL;
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if nearing a jump in the path
 */
bool CFFBot::IsNearJump( void ) const
{
	if (m_pathIndex == 0 || m_pathIndex >= m_pathLength)
		return false;

	for( int i=m_pathIndex-1; i<m_pathIndex; ++i ) // Loop seems to only run for i = m_pathIndex-1
	{
		if (i < 0 || i >= MAX_PATH_LENGTH || i+1 >= MAX_PATH_LENGTH || !m_path[i].area) continue; // Bounds and null check
		if (m_path[ i ].area->GetAttributes() & NAV_MESH_JUMP) // NAV_MESH_JUMP
		{
			float dz = m_path[ i+1 ].pos.z - m_path[ i ].pos.z;

			if (dz > 0.0f)
				return true;
		}
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return approximately how much damage will will take from the given fall height
 */
float CFFBot::GetApproximateFallDamage( float height ) const
{
	// empirically discovered height values
	const float slope = 0.2178f;
	const float intercept = 26.0f;

	float damage = slope * height - intercept;

	if (damage < 0.0f)
		return 0.0f;

	return damage;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if a friend is between us and the given position
 */
bool CFFBot::IsFriendInTheWay( const Vector &goalPos )
{
	// do this check less often to ease CPU burden
	if (!m_avoidFriendTimer.IsElapsed())
	{
		return m_isFriendInTheWay;
	}

	const float avoidFriendInterval = 0.5f;
	m_avoidFriendTimer.Start( avoidFriendInterval );

	// compute ray along intended path
	Vector myOrigin = GetCentroid( this );
	Vector moveDir = goalPos - myOrigin;

	// make it a unit vector 
	float length = moveDir.NormalizeInPlace();
	if (length < FLT_EPSILON) return false; // No movement

	m_isFriendInTheWay = false;

	// check if any friends are overlapping this linear path
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CFFPlayer *player = static_cast<CFFPlayer *>( UTIL_PlayerByIndex( i ) ); // Should be CFFPlayer

		if (player == NULL)
			continue;

		if (!player->IsAlive())
			continue;

		if (!player->InSameTeam( this ))
			continue;

		if (player->entindex() == entindex())
			continue;

		// compute vector from us to our friend
		Vector toFriend = player->GetAbsOrigin() - GetAbsOrigin();

		// check if friend is in our "personal space"
		const float personalSpace = 100.0f;
		if (toFriend.IsLengthGreaterThan( personalSpace ))
			continue;

		// find distance of friend along our movement path
		float friendDistAlong = DotProduct( toFriend, moveDir );

		// if friend is behind us, ignore him
		if (friendDistAlong <= 0.0f)
			continue;

		// constrain point to be on path segment
		Vector pos;
		if (friendDistAlong >= length)
			pos = goalPos;
		else
			pos = myOrigin + friendDistAlong * moveDir;

		// check if friend overlaps our intended line of movement
		const float friendRadius = 30.0f; // This is an approximation of player radius
		if ((pos - GetCentroid( player )).IsLengthLessThan( friendRadius ))
		{
			// friend is in our personal space and overlaps our intended line of movement
			m_isFriendInTheWay = true;
			break;
		}
	}

	return m_isFriendInTheWay;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Do reflex avoidance movements if our "feelers" are touched
 */
void CFFBot::FeelerReflexAdjustment( Vector *goalPosition )
{
	if (!goalPosition) return; // Null check
	// if we are in a "precise" area, do not do feeler adjustments
	if (m_lastKnownArea && m_lastKnownArea->GetAttributes() & NAV_MESH_PRECISE) // NAV_MESH_PRECISE, m_lastKnownArea can be null
		return;

	Vector dir( BotCOS( m_forwardAngle ), BotSIN( m_forwardAngle ), 0.0f ); // BotCOS, BotSIN
	Vector lat( -dir.y, dir.x, 0.0f );

	const float feelerOffset = (IsCrouching()) ? 15.0f : 20.0f;
	const float feelerLengthRun = 50.0f;	// 100 - too long for tight hallways (cs_747)
	const float feelerLengthWalk = 30.0f;
	const float feelerHeight = StepHeight + 0.1f;	// StepHeight. if obstacle is lower than StepHeight, we'll walk right over it

	float feelerLength = (IsRunning()) ? feelerLengthRun : feelerLengthWalk;

	feelerLength = (IsCrouching()) ? 20.0f : feelerLength;

	//
	// Feelers must follow floor slope
	//
	float ground;
	Vector normal;
	Vector eye = EyePosition();
	if (GetSimpleGroundHeightWithFloor( eye, &ground, &normal ) == false)
		return;

	// get forward vector along floor
	dir = CrossProduct( lat, normal );
	dir.NormalizeInPlace(); // Ensure it's a unit vector

	// correct the sideways vector
	lat = CrossProduct( dir, normal );
	lat.NormalizeInPlace(); // Ensure it's a unit vector


	Vector feet = GetAbsOrigin();
	feet.z += feelerHeight;

	Vector from = feet + feelerOffset * lat;
	Vector to = from + feelerLength * dir;

	bool leftClear = IsWalkableTraceLineClear( from, to, WALK_THRU_DOORS | WALK_THRU_BREAKABLES ); // WALK_THRU enums

	if ( ( cv_bot_traceview.GetInt() == 1 && IsLocalPlayerWatchingMe() ) || cv_bot_traceview.GetInt() == 10 )
	{
		if (leftClear)
			NDebugOverlay::Line( from, to, 0, 255, 0, true, 0.1f ); // NDebugOverlay
		else
			NDebugOverlay::Line( from, to, 255, 0, 0, true, 0.1f ); // NDebugOverlay
	}

	from = feet - feelerOffset * lat;
	to = from + feelerLength * dir;

	bool rightClear = IsWalkableTraceLineClear( from, to, WALK_THRU_DOORS | WALK_THRU_BREAKABLES ); // WALK_THRU enums


	if ( ( cv_bot_traceview.GetInt() == 1 && IsLocalPlayerWatchingMe() ) || cv_bot_traceview.GetInt() == 10 )
	{
		if (rightClear)
			NDebugOverlay::Line( from, to, 0, 255, 0, true, 0.1f ); // NDebugOverlay
		else
			NDebugOverlay::Line( from, to, 255, 0, 0, true, 0.1f ); // NDebugOverlay
	}

	const float avoidRange = (IsCrouching()) ? 150.0f : 300.0f;		// 50, 300

	if (!rightClear)
	{
		if (leftClear)
		{
			// right hit, left clear - veer left
			*goalPosition = *goalPosition + avoidRange * lat;
		}
	}
	else if (!leftClear)
	{
		// right clear, left hit - veer right
		*goalPosition = *goalPosition - avoidRange * lat;
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Allows the current nav area to make us run/walk without messing with our state
 */
bool CFFBot::IsRunning( void ) const
{
	// if we've forced running, go with it
	if ( !m_mustRunTimer.IsElapsed() )
	{
		return BaseClass::IsRunning();
	}

	if ( m_lastKnownArea && m_lastKnownArea->GetAttributes() & NAV_MESH_RUN ) // NAV_MESH_RUN, m_lastKnownArea can be null
	{
		return true;
	}

	if ( m_lastKnownArea && m_lastKnownArea->GetAttributes() & NAV_MESH_WALK ) // NAV_MESH_WALK, m_lastKnownArea can be null
	{
		return false;
	}

	return BaseClass::IsRunning();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move along the path. Return false if end of path reached.
 */
CFFBot::PathResult CFFBot::UpdatePathMovement( bool allowSpeedChange )
{
	VPROF_BUDGET( "CFFBot::UpdatePathMovement", VPROF_BUDGETGROUP_NPCS );

	if (m_pathLength == 0)
		return PATH_FAILURE; // PATH_FAILURE needs to be defined in PathResult enum

	if (cv_bot_walk.GetBool())
		Walk();

	//
	// If we are navigating a ladder, it overrides all other path movement until complete
	//
	if (UpdateLadderMovement())
		return PROGRESSING; // PROGRESSING needs to be defined in PathResult enum

	// ladder failure can destroy the path
	if (m_pathLength == 0)
		return PATH_FAILURE;


	// we are not supposed to be on a ladder - if we are, jump off
	if (IsOnLadder())
		Jump( MUST_JUMP ); // MUST_JUMP


	if (m_pathIndex >= m_pathLength || m_pathIndex < 0) return PATH_FAILURE; // Bounds check

	//
	// Stop path attribute
	//
	if (!IsUsingLadder())
	{
		// if the m_isStopping flag is set, clear our movement
		// if the m_isStopping flag is set and movement is stopped, clear m_isStopping
		if ( m_lastKnownArea && m_isStopping ) // m_lastKnownArea can be null
		{
			ResetStuckMonitor();
			ClearMovement();

			if ( GetAbsVelocity().LengthSqr() < 0.1f )
			{
				m_isStopping = false;
			}
			else
			{
				return PROGRESSING;
			}
		}
	}	// end stop logic


	//
	// Check if reached the end of the path
	//
	bool nearEndOfPath = false;
	if (m_pathIndex >= m_pathLength-1)
	{
		Vector toEnd = GetPathEndpoint() - GetAbsOrigin();
		Vector d = toEnd;	// can't use 2D because path end may be below us (jump down)

		const float walkRange = 200.0f;

		// walk as we get close to the goal position to ensure we hit it
		if (d.IsLengthLessThan( walkRange ))
		{
			// don't walk if crouching - too slow
			if (allowSpeedChange && !IsCrouching())
				Walk();

			// note if we are near the end of the path
			const float nearEndRange = 50.0f;
			if (d.IsLengthLessThan( nearEndRange ))
				nearEndOfPath = true;

			const float closeEpsilon = 20.0f;
			if (d.IsLengthLessThan( closeEpsilon ))
			{
				// reached goal position - path complete
				DestroyPath();

				/// @todo We should push and pop walk state here, in case we want to continue walking after reaching goal
				if (allowSpeedChange)
					Run();

				return END_OF_PATH; // END_OF_PATH needs to be defined
			}
		}
	}


	//
	// To keep us moving smoothly, we will move towards
	// a point farther ahead of us down our path.
	//
	int prevIndex = 0;	// closest index on path just prior to where we are now
	const float aheadRange = 300.0f;
	int newIndex = FindPathPoint( aheadRange, &m_goalPosition, &prevIndex );

	// BOTPORT: Why is prevIndex sometimes -1?
	if (prevIndex < 0)
		prevIndex = 0;

	// if goal position is near to us, we must be about to go around a corner - so look ahead!
	Vector myOrigin = GetCentroid( this );
	const float nearCornerRange = 100.0f;
	if (m_pathIndex < m_pathLength-1 && (m_goalPosition - myOrigin).IsLengthLessThan( nearCornerRange ))
	{
		if (!IsLookingAtSpot( PRIORITY_HIGH )) // PRIORITY_HIGH
		{
			ClearLookAt();
			InhibitLookAround( 0.5f );
		}
	}

	// if we moved to a new node on the path, setup movement
	if (newIndex > m_pathIndex)
	{
		SetPathIndex( newIndex );
	}

	if (m_pathIndex >= MAX_PATH_LENGTH || m_pathIndex < 0) return PATH_FAILURE; // Bounds check after SetPathIndex

	//
	// Crouching
	//
	if (!IsUsingLadder())
	{
		// if we are approaching a crouch area, crouch
		// if there are no crouch areas coming up, stand
		const float crouchRange = 50.0f;
		bool didCrouch = false;
		for( int i=prevIndex; i<m_pathLength; ++i )
		{
			if (i >= MAX_PATH_LENGTH || !m_path[i].area) break; // Bounds and null check
			const CNavArea *toArea = m_path[i].area; // Renamed to avoid conflict with 'to' vector

			// if there is a jump area on the way to the crouch area, don't crouch as it messes up the jump
			// unless we are already higher than the jump area - we must've jumped already but not moved into next area
			if (toArea->GetAttributes() & NAV_MESH_JUMP && toArea->GetCenter().z > GetFeetZ()) // NAV_MESH_JUMP
				break;

			Vector close;
			toArea->GetClosestPointOnArea( myOrigin, &close );

			if ((close - myOrigin).AsVector2D().IsLengthGreaterThan( crouchRange ))
				break;

			if (toArea->GetAttributes() & NAV_MESH_CROUCH) // NAV_MESH_CROUCH
			{
				Crouch();
				didCrouch = true;
				ResetStuckMonitor();
				break;
			}
		}

		if (!didCrouch && !IsJumping())
		{
			// no crouch areas coming up
			StandUp();
		}

	}	// end crouching logic


	// compute our forward facing angle
	m_forwardAngle = UTIL_VecToYaw( m_goalPosition - myOrigin );

	//
	// Look farther down the path to "lead" our view around corners
	//
	Vector toGoal;
	bool isWaitingForLadder = false;

	// if we are crouching, look towards where we are moving to negotiate tight corners
	if (IsCrouching())
	{
		m_lookAheadAngle = m_forwardAngle;
	}
	else
	{
		if (m_pathIndex == 0) // Should be m_pathIndex <= 0 or m_pathIndex == 0 && m_pathLength > 1
		{
			if (m_pathLength > 1 && 1 < MAX_PATH_LENGTH) toGoal = m_path[1].pos; // Bounds check
			else toGoal = m_goalPosition - myOrigin; // Fallback
		}
		else if (m_pathIndex < m_pathLength && m_pathIndex < MAX_PATH_LENGTH) // Bounds check
		{
			toGoal = m_path[ m_pathIndex ].pos - myOrigin;

			// actually aim our view farther down the path
			const float lookAheadRange = 500.0f;
			if (!m_path[ m_pathIndex ].ladder &&
				!IsNearJump() &&
				toGoal.AsVector2D().IsLengthLessThan( lookAheadRange ))
			{
				float along = toGoal.Length2D();
				int i;
				for( i=m_pathIndex+1; i<m_pathLength; ++i )
				{
					if (i >= MAX_PATH_LENGTH || i-1 < 0 || !m_path[i].area || !m_path[i-1].area) break; // Bounds and null checks
					Vector delta = m_path[i].pos - m_path[i-1].pos;
					float segmentLength = delta.Length2D();
					if (segmentLength < FLT_EPSILON) continue; // Avoid division by zero if segment is point

					if (along + segmentLength >= lookAheadRange)
					{
						// interpolate between points to keep look ahead point at fixed distance
						float t = (lookAheadRange - along) / (segmentLength); // Removed +along from denominator
						Vector target;

						if (t <= 0.0f)
							target = m_path[i-1].pos;
						else if (t >= 1.0f)
							target = m_path[i].pos;
						else
							target = m_path[i-1].pos + t * delta;

						toGoal = target - myOrigin;
						break;
					}

					// if we are coming up to a ladder or a jump, look at it
					if (m_path[i].ladder ||
						(m_path[i].area->GetAttributes() & NAV_MESH_JUMP) || // NAV_MESH_JUMP
						(m_path[i].area->GetAttributes() & NAV_MESH_PRECISE) || // NAV_MESH_PRECISE
						(m_path[i].area->GetAttributes() & NAV_MESH_STOP)) // NAV_MESH_STOP
					{
						toGoal = m_path[i].pos - myOrigin;

						// if anyone is on the ladder, wait
						if (m_path[i].ladder && m_path[i].ladder->IsInUse( this ))
						{
							isWaitingForLadder = true;
							ResetStuckMonitor();

							// if we are too close to the ladder, back off a bit
							const float tooCloseRange = 100.0f;
							Vector2D delta2D( m_path[i].ladder->m_top.x - myOrigin.x,
											m_path[i].ladder->m_top.y - myOrigin.y );
							if (delta2D.IsLengthLessThan( tooCloseRange ))
							{
								MoveAwayFromPosition( m_path[i].ladder->m_top );
							}
						}

						break;
					}

					along += segmentLength;
				}

				if (i == m_pathLength)
					toGoal = GetPathEndpoint() - myOrigin;
			}
		}
		else
		{
			toGoal = GetPathEndpoint() - myOrigin;
		}

		m_lookAheadAngle = UTIL_VecToYaw( toGoal );
	}

	// initialize "adjusted" goal to current goal
	Vector adjustedGoal = m_goalPosition;

	//
	// Use short "feelers" to veer away from close-range obstacles
	// Feelers come from our ankles, just above StepHeight, so we avoid short walls, too
	// Don't use feelers if very near the end of the path, or about to jump
	//
	/// @todo Consider having feelers at several heights to deal with overhangs, etc.
	if (!nearEndOfPath && !IsNearJump() && !IsJumping())
	{
		FeelerReflexAdjustment( &adjustedGoal );
	}

	// draw debug visualization
	if ( ( cv_bot_traceview.GetInt() == 1 && IsLocalPlayerWatchingMe() ) || cv_bot_traceview.GetInt() == 10 )
	{
		DrawPath();
		if (m_pathIndex < MAX_PATH_LENGTH) { // Bounds check
			const Vector *pos = &m_path[ m_pathIndex ].pos;
			NDebugOverlay::Line( *pos, *pos + Vector( 0, 0, 50 ), 255, 255, 0, true, 0.1f ); // NDebugOverlay
		}

		NDebugOverlay::Line( adjustedGoal, adjustedGoal + Vector( 0, 0, 50 ), 255, 0, 255, true, 0.1f ); // NDebugOverlay
		NDebugOverlay::Line( myOrigin, adjustedGoal + Vector( 0, 0, 50 ), 255, 0, 255, true, 0.1f ); // NDebugOverlay
	}

	// dont use adjustedGoal, as it can vary wildly from the feeler adjustment
	if (!IsAttacking() && IsFriendInTheWay( m_goalPosition ))
	{
		if (!m_isWaitingBehindFriend)
		{
			m_isWaitingBehindFriend = true;

			const float politeDuration = 5.0f - 3.0f * GetProfile()->GetAggression();
			m_politeTimer.Start( politeDuration );
		}
		else if (m_politeTimer.IsElapsed())
		{
			// we have run out of patience
			m_isWaitingBehindFriend = false;
			ResetStuckMonitor();

			// repath to avoid clump of friends in the way
			DestroyPath();
		}
	}
	else if (m_isWaitingBehindFriend)
	{
		// we're done waiting for our friend to move
		m_isWaitingBehindFriend = false;
		ResetStuckMonitor();
	}

	//
	// Move along our path if there are no friends blocking our way,
	// or we have run out of patience
	//
	if (!isWaitingForLadder && (!m_isWaitingBehindFriend || m_politeTimer.IsElapsed()))
	{
		//
		// Move along path
		//
		MoveTowardsPosition( adjustedGoal );

		//
		// Stuck check
		//
		if (m_isStuck && !IsJumping())
		{
			Wiggle();
		}
	}

	// if our goal is high above us, we must have fallen
	bool didFall = false;
	if (m_goalPosition.z - GetFeetZ() > JumpCrouchHeight) // JumpCrouchHeight
	{
		const float closeRange = 75.0f;
		Vector2D to( myOrigin.x - m_goalPosition.x, myOrigin.y - m_goalPosition.y );
		if (to.IsLengthLessThan( closeRange ))
		{
			// we can't reach the goal position
			// check if we can reach the next node, in case this was a "jump down" situation
			if (m_pathIndex < m_pathLength-1 && m_pathIndex+1 < MAX_PATH_LENGTH) // Bounds check
			{
				if (m_path[ m_pathIndex+1 ].pos.z - GetFeetZ() > JumpCrouchHeight) // JumpCrouchHeight
				{
					// the next node is too high, too - we really did fall of the path
					didFall = true;

					for ( int i=m_pathIndex; i<=m_pathIndex+1; ++i )
					{
						if (i >= MAX_PATH_LENGTH) break; // Bounds check
						if ( m_path[i].how == GO_LADDER_UP ) // GO_LADDER_UP
						{
							// if we're going up a ladder, and we're within reach of the ladder bottom, we haven't fallen
							if ( m_path[i].pos.z - GetFeetZ() <= JumpCrouchHeight ) // JumpCrouchHeight
							{
								didFall = false;
								break;
							}
						}
					}
				}
			}
			else
			{
				// fell trying to get to the last node in the path
				didFall = true;
			}
		}
	}

	//
	// This timeout check is needed if the bot somehow slips way off 
	// of its path and cannot progress, but also moves around
	// enough that it never becomes "stuck"
	//
	const float giveUpDuration = 4.0f;
	if (didFall || gpGlobals->curtime - m_areaEnteredTimestamp > giveUpDuration)
	{
		if (didFall)
		{
			PrintIfWatched( "I fell off!\n" );
			if (IsLocalPlayerWatchingMe() && cv_bot_debug.GetBool() && UTIL_GetListenServerHost())
			{
				CBasePlayer *localPlayer = UTIL_GetListenServerHost();
				if (localPlayer) { // Null check
					CSingleUserRecipientFilter filter( localPlayer );
					EmitSound( filter, localPlayer->entindex(), "Bot.FellOff" ); // TODO: Update sound name for FF
				}
			}
		}

		// if we havent made any progress in a long time, give up
		if (m_pathIndex < m_pathLength-1 && m_pathIndex < MAX_PATH_LENGTH && m_path[ m_pathIndex ].area) // Bounds and null checks
		{
			PrintIfWatched( "Giving up trying to get to area #%d\n", m_path[ m_pathIndex ].area->GetID() );
		}
		else
		{
			PrintIfWatched( "Giving up trying to get to end of path\n" );
		}

		Run();
		StandUp();
		DestroyPath();
		ClearLookAt();

		// See if we should be on a different nav area
		if (TheNavMesh) { // Null check
			CNavArea *area = TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 500.0f, true );
			if (area && area != m_lastNavArea) // m_lastNavArea can be null
			{
				if (m_lastNavArea)
				{
					m_lastNavArea->DecrementPlayerCount( GetTeamNumber(), entindex() ); // TODO: Ensure GetTeamNumber is 0/1 indexed for FF
				}

				area->IncrementPlayerCount( GetTeamNumber(), entindex() ); // TODO: Ensure GetTeamNumber is 0/1 indexed for FF

				m_lastNavArea = area;
				if ( area->GetPlace() != UNDEFINED_PLACE ) // UNDEFINED_PLACE
				{
					const char *placeName = TheNavMesh->PlaceToName( area->GetPlace() );
					if ( placeName && *placeName )
					{
						V_strcpy_safe( m_szLastPlaceName.GetForModify(), placeName ); // MAX_PLACE_NAME_LENGTH
					}
				}
			}
		}


		return PATH_FAILURE; // PATH_FAILURE
	}

	return PROGRESSING; // PROGRESSING
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Build trivial path to goal, assuming we are already in the same area
 */
void CFFBot::BuildTrivialPath( const Vector &goal )
{
	Vector myOrigin = GetCentroid( this );

	m_pathIndex = 1;
	m_pathLength = 2;

	if (!m_lastKnownArea) return; // Null check

	if (0 < MAX_PATH_LENGTH) { // Bounds check
		m_path[0].area = m_lastKnownArea;
		m_path[0].pos = myOrigin;
		m_path[0].pos.z = m_lastKnownArea->GetZ( myOrigin );
		m_path[0].ladder = NULL;
		m_path[0].how = NUM_TRAVERSE_TYPES; // NUM_TRAVERSE_TYPES
	}

	if (1 < MAX_PATH_LENGTH) { // Bounds check
		m_path[1].area = m_lastKnownArea;
		m_path[1].pos = goal;
		m_path[1].pos.z = m_lastKnownArea->GetZ( goal );
		m_path[1].ladder = NULL;
		m_path[1].how = NUM_TRAVERSE_TYPES; // NUM_TRAVERSE_TYPES
	}


	m_areaEnteredTimestamp = gpGlobals->curtime;
	m_spotEncounter = NULL;
	m_pathLadder = NULL;

	m_goalPosition = goal;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Compute shortest path to goal position via A* algorithm
 * If 'goalArea' is NULL, path will get as close as it can.
 */
bool CFFBot::ComputePath( const Vector &goal, RouteType route ) // RouteType
{
	VPROF_BUDGET( "CFFBot::ComputePath", VPROF_BUDGETGROUP_NPCS );

	//
	// Throttle re-pathing
	//
	if (!m_repathTimer.IsElapsed())
		return false;

	// randomize to distribute CPU load
	m_repathTimer.Start( RandomFloat( 0.4f, 0.6f ) );


	DestroyPath();

	m_pathLadder = NULL;

	if (!TheNavMesh) return false; // Null check
	CNavArea *goalArea = TheNavMesh->GetNearestNavArea( goal );

	CNavArea *startArea = m_lastKnownArea;
	if (startArea == NULL)
		return false;

	// if we fell off a ledge onto an area off the mesh, we will path from the
	// ledge above our heads, resulting in a path we can't follow.
	Vector close;
	startArea->GetClosestPointOnArea( EyePosition(), &close );
	if (close.z - GetAbsOrigin().z > JumpCrouchHeight) // JumpCrouchHeight
	{
		// we can't reach our last known area - find nearest area to us
		PrintIfWatched( "Last known area is above my head - resetting to nearest area.\n" );
		m_lastKnownArea = (CNavArea*)TheNavMesh->GetNearestNavArea( GetAbsOrigin(), false, 500.0f, true ); // Cast from CNavArea to CNavArea (was CCSNavArea)
		if (m_lastKnownArea == NULL)
		{
			return false;
		}

		startArea = m_lastKnownArea;
	}

	// note final specific position
	Vector pathEndPosition = goal;

	// make sure path end position is on the ground
	if (goalArea)
		pathEndPosition.z = goalArea->GetZ( pathEndPosition );
	else
		TheNavMesh->GetGroundHeight( pathEndPosition, &pathEndPosition.z );

	// if we are already in the goal area, build trivial path
	if (startArea == goalArea)
	{
		BuildTrivialPath( pathEndPosition );
		return true;
	}

	//
	// Compute shortest path to goal
	//
	CNavArea *closestArea = NULL;
	PathCost cost( this, route ); // PathCost needs to be defined or included
	bool pathToGoalExists = NavAreaBuildPath( startArea, goalArea, &goal, cost, &closestArea, TheNavMesh->GetMaxPathNodes() ); // Added MaxNodes

	CNavArea *effectiveGoalArea = (pathToGoalExists) ? goalArea : closestArea;
	if (!effectiveGoalArea) return false; // Pathfinding failed to find any reachable area

	//
	// Build path by following parent links
	//

	// get count
	int count = 0;
	CNavArea *area;
	for( area = effectiveGoalArea; area; area = area->GetParent() )
		++count;

	// save room for endpoint
	if (count > MAX_PATH_LENGTH-1) // MAX_PATH_LENGTH
		count = MAX_PATH_LENGTH-1;

	if (count == 0)
		return false;

	if (count == 1)
	{
		BuildTrivialPath( pathEndPosition );
		return true;
	}

	// build path
	m_pathLength = count;
	for( area = effectiveGoalArea; count && area; area = area->GetParent() )
	{
		--count;
		if (count < 0 || count >= MAX_PATH_LENGTH) return false; // Bounds check
		m_path[ count ].area = area;
		m_path[ count ].how = area->GetParentHow();
	}

	// compute path positions
	if (ComputePathPositions() == false)
	{
		PrintIfWatched( "Error building path\n" );
		DestroyPath();
		return false;
	}

	// append path end position
	if (m_pathLength >= MAX_PATH_LENGTH) return false; // Bounds check
	m_path[ m_pathLength ].area = effectiveGoalArea;
	m_path[ m_pathLength ].pos = pathEndPosition;
	m_path[ m_pathLength ].ladder = NULL;
	m_path[ m_pathLength ].how = NUM_TRAVERSE_TYPES; // NUM_TRAVERSE_TYPES
	++m_pathLength;

	// do movement setup
	m_pathIndex = 1;
	m_areaEnteredTimestamp = gpGlobals->curtime;
	m_spotEncounter = NULL;

	if (m_pathIndex < MAX_PATH_LENGTH) { // Bounds check before accessing m_path[1]
		m_goalPosition = m_path[1].pos;
		if (m_path[1].ladder)
			SetupLadderMovement();
		else
			m_pathLadder = NULL;
	} else {
		DestroyPath(); // Path is too short or invalid
		return false;
	}


	// find initial encounter area along this path, if we are in the early part of the round
	if (IsSafe())
	{
		int myTeam = GetTeamNumber();
		int enemyTeam = OtherTeam( myTeam ); // OtherTeam needs to be FF compatible
		int k; // Renamed from i to avoid conflict

		for( k=0; k<m_pathLength; ++k )
		{
			if (k >= MAX_PATH_LENGTH || !m_path[k].area) break; // Bounds and null checks
			if (m_path[k].area->GetEarliestOccupyTime( myTeam ) > m_path[k].area->GetEarliestOccupyTime( enemyTeam ))
			{
				break;
			}
		}

		if (k < m_pathLength && k < MAX_PATH_LENGTH && m_path[k].area) // Bounds and null checks
		{
			SetInitialEncounterArea( m_path[k].area );
		}
		else
		{
			SetInitialEncounterArea( NULL );
		}
	}

	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return estimated distance left to travel along path
 */
float CFFBot::GetPathDistanceRemaining( void ) const
{
	if (!HasPath())
		return -1.0f;

	int idx = (m_pathIndex < m_pathLength) ? m_pathIndex : m_pathLength-1;
	if (idx < 0 || idx >= MAX_PATH_LENGTH || !m_path[idx].area) return -1.0f; // Bounds and null checks

	float dist = 0.0f;
	Vector prevCenter = m_path[idx].area->GetCenter(); // Use idx instead of m_pathIndex directly

	for( int i=idx+1; i<m_pathLength; ++i )
	{
		if (i >= MAX_PATH_LENGTH || !m_path[i].area) break; // Bounds and null checks
		dist += (m_path[i].area->GetCenter() - prevCenter).Length();
		prevCenter = m_path[i].area->GetCenter();
	}

	return dist;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Draw a portion of our current path for debugging.
 */
void CFFBot::DrawPath( void )
{
	if (!HasPath())
		return;

	for( int i=1; i<m_pathLength; ++i )
	{
		if (i-1 < 0 || i >= MAX_PATH_LENGTH) break; // Bounds check
		NDebugOverlay::Line( m_path[i-1].pos, m_path[i].pos, 255, 75, 0, true, 0.1f ); // NDebugOverlay
	}

	Vector close;
	if (FindOurPositionOnPath( &close, true ) >= 0)
	{
		NDebugOverlay::Line( close + Vector( 0, 0, 25 ), close, 0, 255, 0, true, 0.1f ); // NDebugOverlay
		NDebugOverlay::Line( close + Vector( 25, 0, 0 ), close + Vector( -25, 0, 0 ), 0, 255, 0, true, 0.1f ); // NDebugOverlay
		NDebugOverlay::Line( close + Vector( 0, 25, 0 ), close + Vector( 0, -25, 0 ), 0, 255, 0, true, 0.1f ); // NDebugOverlay
	}
}
