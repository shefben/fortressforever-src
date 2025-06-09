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
#include "../nav_pathfind.h"

#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define motion states if not in bot_constants.h or other central place yet
enum FollowLeaderMotionStateType
{
	INVALID_MOTION_STATE = -1, // To avoid conflict if 0 is a valid state elsewhere
	MOTION_STOPPED,
	MOTION_WALKING,
	MOTION_RUNNING
};


//--------------------------------------------------------------------------------------------------------------
void FollowState::OnEnter( CFFBot *me )
{
	if (!me) return;

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
	m_leaderMotionState = INVALID_MOTION_STATE;
	m_idleTimer.Start( RandomFloat( 2.0f, 5.0f ) );

    if (me->GetFollowLeader()) {
        me->PrintIfWatched("Entering FollowState: Following %s.\n", me->GetFollowLeader()->GetPlayerName());
    } else {
        me->PrintIfWatched("Entering FollowState: No leader set!\n");
        me->Idle(); // No leader to follow
    }
}

//--------------------------------------------------------------------------------------------------------------
void FollowState::ComputeLeaderMotionState( float leaderSpeed )
{
	const float runWalkThreshold = 140.0f;
	const float walkStopThreshold = 10.0f;
	LeaderMotionStateType prevState = m_leaderMotionState;

	if (leaderSpeed > runWalkThreshold) { m_leaderMotionState = MOTION_RUNNING; m_isAtWalkSpeed = false; }
	else if (leaderSpeed > walkStopThreshold)
	{
		if (!m_isAtWalkSpeed) { m_walkTime.Start(); m_isAtWalkSpeed = true; }
		if (m_walkTime.GetElapsedTime() > 0.25f) m_leaderMotionState = MOTION_WALKING;
	}
	else { m_leaderMotionState = MOTION_STOPPED; m_isAtWalkSpeed = false; }

	if (prevState != m_leaderMotionState)
	{
		m_leaderMotionStateTime.Start();
		m_waitTime = RandomFloat( 1.0f, 3.0f );
	}
}

//--------------------------------------------------------------------------------------------------------------
class FollowTargetCollector { /* ... (implementation unchanged) ... */
public:
	FollowTargetCollector( CBasePlayer *player ) {
		m_player = player;
		m_targetAreaCount = 0;
		if (!player) { m_forward.Init(); m_cutoff.Init(); return; }
		Vector playerVel = player->GetAbsVelocity();
		m_forward.x = playerVel.x;
		m_forward.y = playerVel.y;
		float speed = m_forward.NormalizeInPlace();
		Vector playerOrigin = GetCentroid( player );
		if (speed < 100.0f) { m_cutoff = playerOrigin.AsVector2D(); m_forward.Init(); }
		else {
			float trimSpeed = MIN( speed, 200.0f );
			m_cutoff.x = playerOrigin.x + 1.5f * trimSpeed * m_forward.x;
			m_cutoff.y = playerOrigin.y + 1.5f * trimSpeed * m_forward.y;
		}
	}
	enum { MAX_TARGET_AREAS = 128 };
	bool operator() ( CNavArea *area ) {
		if (!area || m_targetAreaCount >= MAX_TARGET_AREAS) return false;
		if (!area->GetParent() || area->IsConnected( area->GetParent(), NUM_DIRECTIONS )) {
			if (m_forward.IsZero()) m_targetArea[ m_targetAreaCount++ ] = area;
			else {
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
	if (!me || !TheNavMesh) { // Removed Chatter check as it's gone
        if (me) me->Idle();
        return;
    }

    CFFPlayer* leader = me->GetFollowLeader(); // Leader is set in OnEnter via bot->SetFollowLeader() or directly
    BotTaskType currentTask = me->GetTask();
    FFGameState* gameState = me->GetGameState();

    if (!leader || !leader->IsAlive()) { // Generic check for any leader
        me->PrintIfWatched("FollowState: Leader is NULL or dead. Idling.\n");
        me->Idle();
        return;
    }

    // --- VIP Bodyguard Logic ---
    if (currentTask == CFFBot::BOT_TASK_ESCORT_VIP_FF)
    {
        if (!gameState) {
            me->Warning("FollowState (Bodyguard): GameState is NULL. Idling.\n");
            me->Idle(); return;
        }

        CFFPlayer* actualVIP = gameState->GetVIP();
        // Ensure our leader *is* the VIP and the VIP is alive and not escaped
        if (leader != actualVIP || !gameState->IsVIPAlive()) {
            me->PrintIfWatched("FollowState (Bodyguard): VIP is invalid, dead, or no longer the leader. Idling.\n");
            me->Idle();
            return;
        }
        if (gameState->IsVIPEscaped()) {
             me->PrintIfWatched("FollowState (Bodyguard): VIP Escaped! Bodyguard idling.\n");
             me->Idle();
             return;
        }

        // Threat Assessment for VIP
        if (me->LookForEnemies()) {
            CFFPlayer* enemy = me->GetBotEnemy();
            if (enemy) {
                 // TODO_FF: Prioritize enemies directly threatening the VIP (e.g., closer to VIP or aiming at VIP).
                 me->PrintIfWatched("FollowState (Bodyguard): Enemy %s spotted! Attacking to protect VIP.\n", enemy->GetPlayerName());
                 me->Attack(enemy);
                 return;
            }
        }
        // If no immediate enemy, continue with normal follow logic below to stay with VIP.
        // TODO_FF: Implement more advanced bodyguard positioning (e.g. stay between VIP and threat).
    }
    // --- End VIP Bodyguard Logic ---

	me->UpdateLookAround();
	if (!me->IsNotMoving()) m_idleTimer.Start( RandomFloat( 2.0f, 5.0f ) );

	Vector leaderOrigin = GetCentroid( leader );
	ComputeLeaderMotionState( leader->GetAbsVelocity().AsVector2D().Length() );

	bool isLeaderVisible = me->IsVisible( leaderOrigin );
	if (isLeaderVisible) m_lastSawLeaderTime = gpGlobals->curtime;

	Vector myOrigin = GetCentroid( me );
	if ((leaderOrigin - myOrigin).IsLengthGreaterThan( 750.0f )) m_isSneaking = false;
	else if (isLeaderVisible)
	{
		if (m_leaderMotionState == MOTION_WALKING) m_isSneaking = true;
		if (m_isSneaking && m_leaderMotionState == MOTION_RUNNING) m_isSneaking = false;
	}
	if (gpGlobals->curtime - m_lastSawLeaderTime > 20.0f) m_isSneaking = false;

	if (m_isSneaking) me->Walk(); else me->Run();

	bool repath = false;
	if (!me->HasPath() && m_leaderMotionState == MOTION_STOPPED && m_leaderMotionStateTime.GetElapsedTime() > m_waitTime)
	{
		m_waitTime += RandomFloat( 1.0f, 3.0f );
		if ((leaderOrigin - myOrigin).IsLengthLessThan( 250.0f ))
		{
			if (me->TryToHide( NULL, -1.0f, 250.0f, false, true )) { me->ResetStuckMonitor(); return; }
		}
	}
	if (m_idleTimer.IsElapsed()) { repath = true; m_isSneaking = true; }
	if (leader->GetAbsVelocity().AsVector2D().Length() > 100.0f && m_leaderMotionState != MOTION_STOPPED) repath = true;

	if (me->UpdatePathMovement( false ) != CFFBot::PROGRESSING) me->DestroyPath();

	if (repath && m_repathInterval.IsElapsed() && !me->IsOnLadder())
	{
		m_lastLeaderPos = leaderOrigin;
		me->ResetStuckMonitor();
		CNavArea* leaderNavArea = TheNavMesh->GetNearestNavArea( m_lastLeaderPos );
		if (leaderNavArea)
		{
			FollowTargetCollector collector( leader );
			SearchSurroundingAreas( leaderNavArea, m_lastLeaderPos, collector, (leader->GetAbsVelocity().AsVector2D().Length() > 200.0f) ? 600.0f : 400.0f );
			if (cv_bot_debug.GetBool()) { /* ... debug draw ... */ }
			if (collector.m_targetAreaCount)
			{
				CNavArea *target = NULL; Vector targetPos;
				if (m_idleTimer.IsElapsed())
				{
					target = collector.m_targetArea[ RandomInt( 0, collector.m_targetAreaCount-1 ) ];
					if (target) targetPos = target->GetCenter();
					me->PrintIfWatched("%4.1f: Bored. Repathing to a new nearby area\n", gpGlobals->curtime );
				}
				else
				{
					me->PrintIfWatched("%4.1f: Repathing to stay with leader.\n", gpGlobals->curtime );
					float closeRangeSq = FLT_MAX; Vector close;
					for( int a=0; a<collector.m_targetAreaCount; ++a )
					{
						CNavArea* area = collector.m_targetArea[a]; if (!area) continue;
						area->GetClosestPointOnArea( myOrigin, &close );
						float rangeSq = (myOrigin - close).LengthSqr();
						if (rangeSq < closeRangeSq) { target = area; targetPos = close; closeRangeSq = rangeSq; }
					}
				}
				if (target && me->ComputePath( target->GetCenter(), FASTEST_ROUTE ) == false) me->PrintIfWatched("Pathfind to leader failed.\n" );
				m_repathInterval.Start( 0.5f );
				m_idleTimer.Reset();
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void FollowState::OnExit( CFFBot *me )
{
    if (!me) return;
	me->PrintIfWatched( "Exiting FollowState.\n" );
    // No specific action needed beyond what the new state's OnEnter might do.
    // me->StopFollowing(); // This would clear m_leader, which might not be desired if transitioning to a temporary state.
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_follow.cpp]
