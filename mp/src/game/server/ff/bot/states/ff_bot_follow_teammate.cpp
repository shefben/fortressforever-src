//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements the state for a bot following a specific teammate.
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_follow_teammate.h"
#include "bot/ff_bot.h"       // For CFFBot
#include "bot/ff_player.h"    // For CFFPlayer

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const float DEFAULT_FOLLOW_DISTANCE = 200.0f;
const float REPATH_INTERVAL = 1.0f; // How often to repath if needed
const float FOLLOW_DISTANCE_BUFFER = 50.0f; // Buffer zone for desired distance

//--------------------------------------------------------------------------------------------------------------
FollowTeammateState::FollowTeammateState( void )
{
	m_playerToFollow = NULL;
	m_desiredFollowDistance = DEFAULT_FOLLOW_DISTANCE;
}

//--------------------------------------------------------------------------------------------------------------
void FollowTeammateState::SetPlayerToFollow(CFFPlayer* pPlayer)
{
	m_playerToFollow = pPlayer;
}

//--------------------------------------------------------------------------------------------------------------
void FollowTeammateState::OnEnter( CFFBot *me )
{
	if ( !m_playerToFollow.Get() || !m_playerToFollow->IsAlive() )
	{
		Warning("%s: FollowTeammateState: Target player is NULL or dead. Idling.\n", me->GetPlayerName());
		me->Idle();
		return;
	}

	PrintIfWatched( "%s: Entering FollowTeammateState, following %s.\n", me->GetPlayerName(), m_playerToFollow->GetPlayerName() );
	m_desiredFollowDistance = DEFAULT_FOLLOW_DISTANCE; // Could be adjusted based on bot class or leader's class
	m_repathTimer.Start(REPATH_INTERVAL);
}

//--------------------------------------------------------------------------------------------------------------
void FollowTeammateState::OnUpdate( CFFBot *me )
{
	CFFPlayer *pFollowTarget = m_playerToFollow.Get();

	// Check if target is still valid
	if ( !pFollowTarget || !pFollowTarget->IsAlive() || pFollowTarget->GetTeamNumber() != me->GetTeamNumber() )
	{
		PrintIfWatched( "%s: FollowTeammateState: Target invalid or changed team. Idling.\n", me->GetPlayerName() );
		me->Idle();
		return;
	}

	// Check for enemies
	if ( me->GetBotEnemy() && me->IsEnemyVisible() )
	{
		PrintIfWatched( "%s: FollowTeammateState: Enemy visible. Attacking.\n", me->GetPlayerName() );
		me->Attack( me->GetBotEnemy() );
		return;
	}

	float currentDistSq = me->GetAbsOrigin().DistToSqr(pFollowTarget->GetAbsOrigin());

	// Too far, try to move closer
	if ( currentDistSq > Square(m_desiredFollowDistance + FOLLOW_DISTANCE_BUFFER) )
	{
		if ( m_repathTimer.IsElapsed() || !me->HasPath() || (me->GetPathEndpoint() - pFollowTarget->GetAbsOrigin()).IsLengthGreaterThan(FOLLOW_DISTANCE_BUFFER * 0.5f) )
		{
			me->ComputePath(pFollowTarget->GetAbsOrigin(), FASTEST_ROUTE);
			m_repathTimer.Start(REPATH_INTERVAL);
		}
		me->UpdatePathMovement();
	}
	// Too close, stop or back up
	else if ( currentDistSq < Square(m_desiredFollowDistance - FOLLOW_DISTANCE_BUFFER) )
	{
		me->StopMovement();
		// FF_TODO_AI_BEHAVIOR: Could do a small backing up move if very cramped and leader is stationary.
		// For now, just stop and look.
		me->SetLookAt("Follow Target (too close)", pFollowTarget->EyePosition(), PRIORITY_LOW);
	}
	// Good distance
	else
	{
		me->StopMovement();
		me->SetLookAt("Follow Target (good distance)", pFollowTarget->EyePosition(), PRIORITY_LOW);
		// FF_TODO_AI_BEHAVIOR: If leader stops for too long, bot might also idle or look around,
		// or switch to a different task if it gets bored or sees a better opportunity.
	}
}

//--------------------------------------------------------------------------------------------------------------
void FollowTeammateState::OnExit( CFFBot *me )
{
	PrintIfWatched( "%s: Exiting FollowTeammateState.\n", me->GetPlayerName() );
	m_playerToFollow = NULL;
	me->DestroyPath();
	me->StopMovement();
}

//--------------------------------------------------------------------------------------------------------------
const char *FollowTeammateState::GetName( void ) const
{
	return "FollowTeammate";
}
