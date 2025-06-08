//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_follow.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"
#include "../nav_pathfind.h" // For PathCost, NavAreaTravelDistance, SearchSurroundingAreas

// Local bot utility headers
#include "../bot_constants.h"  // For BotTaskType, RouteType, NUM_DIRECTIONS, etc.
#include "../bot_profile.h"    // For GetProfile() (potentially used by CFFBot methods)
#include "../bot_util.h"       // For PrintIfWatched


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Follow our leader
 */
void FollowState::OnEnter( CFFBot *me )
{
	if (!me) return; // Null check

	me->StandUp();
	me->Run();
	me->DestroyPath();

	m_isStopped = false;
	m_stoppedTimestamp = 0.0f;

	m_lastLeaderPos = Vector( -99999999.9f, -99999999.9f, -99999999.9f );
	m_lastSawLeaderTime = 0;
	m_repathInterval.Invalidate();
	m_isSneaking = false;
	m_walkTime.Invalidate();
	m_isAtWalkSpeed = false;
	m_leaderMotionState = INVALID_MOTION_STATE; // Use an enum from bot_constants.h or define locally if specific
	m_idleTimer.Start( RandomFloat( 2.0f, 5.0f ) );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Determine the leader's motion state by tracking his speed
 */
void FollowState::ComputeLeaderMotionState( float leaderSpeed )
{
	const float runWalkThreshold = 140.0f;
	const float walkStopThreshold = 10.0f;
	LeaderMotionStateType prevState = m_leaderMotionState;

	if (leaderSpeed > runWalkThreshold) { m_leaderMotionState = MOTION_RUNNING; m_isAtWalkSpeed = false; } // Use enums
	else if (leaderSpeed > walkStopThreshold)
	{
		if (!m_isAtWalkSpeed) { m_walkTime.Start(); m_isAtWalkSpeed = true; }
		if (m_walkTime.GetElapsedTime() > 0.25f) m_leaderMotionState = MOTION_WALKING; // Use enum
	}
	else { m_leaderMotionState = MOTION_STOPPED; m_isAtWalkSpeed = false; } // Use enum

	if (prevState != m_leaderMotionState)
	{
		m_leaderMotionStateTime.Start();
		m_waitTime = RandomFloat( 1.0f, 3.0f );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Functor to collect all areas in the forward direction of the given player within a radius
 */
class FollowTargetCollector
{
public:
	FollowTargetCollector( CBasePlayer *player )
	{
		m_player = player;
		m_targetAreaCount = 0;
		if (!player) { m_forward.Init(); m_cutoff.Init(); return; } // Null check

		Vector playerVel = player->GetAbsVelocity();
		m_forward.x = playerVel.x;
		m_forward.y = playerVel.y;
		float speed = m_forward.NormalizeInPlace();
		Vector playerOrigin = GetCentroid( player );

		if (speed < 100.0f) { m_cutoff = playerOrigin.AsVector2D(); m_forward.Init(); }
		else
		{
			float trimSpeed = MIN( speed, 200.0f );
			m_cutoff.x = playerOrigin.x + 1.5f * trimSpeed * m_forward.x;
			m_cutoff.y = playerOrigin.y + 1.5f * trimSpeed * m_forward.y;
		}
	}
	enum { MAX_TARGET_AREAS = 128 };
	bool operator() ( CNavArea *area )
	{
		if (!area || m_targetAreaCount >= MAX_TARGET_AREAS) return false;
		if (!area->GetParent() || area->IsConnected( area->GetParent(), NUM_DIRECTIONS )) // NUM_DIRECTIONS from bot_constants.h
		{
			if (m_forward.IsZero()) m_targetArea[ m_targetAreaCount++ ] = area;
			else
			{
				Vector2D to( area->GetCenter().x - m_cutoff.x, area->GetCenter().y - m_cutoff.y );
				to.NormalizeInPlace();
				if (DotProduct2D( to, m_forward ) > 0.7071f) m_targetArea[ m_targetAreaCount++ ] = area;
			}
		}
		return (m_targetAreaCount < MAX_TARGET_AREAS);
	}
	CBasePlayer *m_player; Vector2D m_forward; Vector2D m_cutoff;
	CNavArea *m_targetArea[ MAX_TARGET_AREAS ]; int m_targetAreaCount;
};

//--------------------------------------------------------------------------------------------------------------
void FollowState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetChatter() || !TheNavMesh) return; // Null checks

	if (m_leader == NULL || !m_leader->IsAlive()) { me->Idle(); return; }

	// TODO_FF: C4 logic for FF
	// if (me->HasC4() && me->IsAtBombsite()) { /* ... plant logic ... */ return; }

	me->UpdateLookAround();
	if (!me->IsNotMoving()) m_idleTimer.Start( RandomFloat( 2.0f, 5.0f ) );

	Vector leaderOrigin = GetCentroid( m_leader.Get() ); // Use .Get()
	ComputeLeaderMotionState( m_leader->GetAbsVelocity().AsVector2D().Length() );

	bool isLeaderVisible = me->IsVisible( leaderOrigin );
	if (isLeaderVisible) m_lastSawLeaderTime = gpGlobals->curtime;

	Vector myOrigin = GetCentroid( me );
	if ((leaderOrigin - myOrigin).IsLengthGreaterThan( 750.0f )) m_isSneaking = false;
	else if (isLeaderVisible)
	{
		if (m_leaderMotionState == MOTION_WALKING) m_isSneaking = true; // Enum
		if (m_isSneaking && m_leaderMotionState == MOTION_RUNNING) m_isSneaking = false; // Enum
	}
	if (gpGlobals->curtime - m_lastSawLeaderTime > 20.0f) m_isSneaking = false;

	if (m_isSneaking) me->Walk(); else me->Run();

	bool repath = false;
	if (!me->HasPath() && m_leaderMotionState == MOTION_STOPPED && m_leaderMotionStateTime.GetElapsedTime() > m_waitTime) // Enum
	{
		m_waitTime += RandomFloat( 1.0f, 3.0f );
		if ((leaderOrigin - myOrigin).IsLengthLessThan( 250.0f ))
		{
			if (me->TryToHide( NULL, -1.0f, 250.0f, false, true )) { me->ResetStuckMonitor(); return; } // USE_NEAREST = true
		}
	}
	if (m_idleTimer.IsElapsed()) { repath = true; m_isSneaking = true; }
	if (m_leader->GetAbsVelocity().AsVector2D().Length() > 100.0f && m_leaderMotionState != MOTION_STOPPED) repath = true; // Enum

	if (me->UpdatePathMovement( false ) != CFFBot::PROGRESSING) me->DestroyPath(); // NO_SPEED_CHANGE = false

	if (repath && m_repathInterval.IsElapsed() && !me->IsOnLadder())
	{
		m_lastLeaderPos = leaderOrigin;
		me->ResetStuckMonitor();
		CNavArea* leaderNavArea = TheNavMesh->GetNearestNavArea( m_lastLeaderPos );
		if (leaderNavArea) // Null check
		{
			FollowTargetCollector collector( m_leader.Get() ); // Use .Get()
			SearchSurroundingAreas( leaderNavArea, m_lastLeaderPos, collector, (m_leader->GetAbsVelocity().AsVector2D().Length() > 200.0f) ? 600.0f : 400.0f ); // SearchSurroundingAreas
			if (cv_bot_debug.GetBool()) { /* ... debug draw ... */ }
			if (collector.m_targetAreaCount)
			{
				CNavArea *target = NULL; Vector targetPos;
				if (m_idleTimer.IsElapsed())
				{
					target = collector.m_targetArea[ RandomInt( 0, collector.m_targetAreaCount-1 ) ];
					if (target) targetPos = target->GetCenter(); // Null check
					PrintIfWatched(me, "%4.1f: Bored. Repathing to a new nearby area\n", gpGlobals->curtime ); // Updated
				}
				else
				{
					PrintIfWatched(me, "%4.1f: Repathing to stay with leader.\n", gpGlobals->curtime ); // Updated
					float closeRangeSq = FLT_MAX; Vector close;
					for( int a=0; a<collector.m_targetAreaCount; ++a )
					{
						CNavArea* area = collector.m_targetArea[a]; if (!area) continue;
						area->GetClosestPointOnArea( myOrigin, &close );
						float rangeSq = (myOrigin - close).LengthSqr();
						if (rangeSq < closeRangeSq) { target = area; targetPos = close; closeRangeSq = rangeSq; }
					}
				}
				if (target && me->ComputePath( target->GetCenter(), FASTEST_ROUTE ) == false) PrintIfWatched(me, "Pathfind to leader failed.\n" ); // Updated, FASTEST_ROUTE
				m_repathInterval.Start( 0.5f );
				m_idleTimer.Reset();
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void FollowState::OnExit( CFFBot *me )
{
	// TODO_FF: Add any exit logic if needed
}
