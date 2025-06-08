//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../ff_bot.h" // Changed from cs_bot.h
#include "../ff_player.h" // For CFFPlayer, though ff_bot.h should include it
#include "../ff_bot_manager.h" // For TheFFBots() and potentially other manager functions

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Move towards currently heard noise
 */
void InvestigateNoiseState::AttendCurrentNoise( CFFBot *me ) // Changed CCSBot to CFFBot
{
	// if (!me->IsNoiseHeard() && me->GetNoisePosition()) // GetNoisePosition() returns const Vector*
	// 	return;
	// Corrected logic: if noise not heard OR position is null
	if (!me->IsNoiseHeard() || me->GetNoisePosition() == NULL)
	{
		me->PrintIfWatched("AttendCurrentNoise: No noise heard or position is NULL.\n");
		return;
	}


	// remember where the noise we heard was
	m_checkNoisePosition = *me->GetNoisePosition();

	// tell our teammates (unless the noise is obvious, like gunfire)
	// FF_TODO_AI_BEHAVIOR: IsWellPastSafe and HasNotSeenEnemyForLongTime might need FF tuning for timings
	if (me->IsWellPastSafe() && me->HasNotSeenEnemyForLongTime() && me->GetNoisePriority() != PRIORITY_HIGH)
		me->GetChatter()->HeardNoise( *me->GetNoisePosition() ); // FF_TODO_AI_BEHAVIOR: FF Chatter equivalent for HeardNoise

	// figure out how to get to the noise		
	me->PrintIfWatched( "Attending to noise...\n" );
	me->ComputePath( m_checkNoisePosition, FASTEST_ROUTE );

	const float minAttendTime = 3.0f;
	const float maxAttendTime = 10.0f;
	m_minTimer.Start( RandomFloat( minAttendTime, maxAttendTime ) );

	// consume the noise
	me->ForgetNoise();
}

//--------------------------------------------------------------------------------------------------------------
void InvestigateNoiseState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	AttendCurrentNoise( me );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * @todo Use TravelDistance instead of distance...
 */
void InvestigateNoiseState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	Vector myOrigin = GetCentroid( me );

	if (m_minTimer.IsElapsed())
	{
		const float nearbyRange = 500.0f;
		if (me->HeardInterestingNoise() && me->GetNoiseRange() < nearbyRange)
		{
			AttendCurrentNoise( me );
		}
	}

	if (!me->HasPath())
	{
		me->Idle();
		return;
	}

	me->UpdateLookAround();
	float range = me->GetPathDistanceRemaining();

	// FF_TODO_WEAPON_STATS: IsUsingKnife() needs to be FF specific if this logic is to be kept.
	// if (me->IsUsingKnife())
	// {
	// 	if (me->IsHurrying()) me->Run();
	// 	else me->Walk();
	// }
	// else
	{
		const float closeToNoiseRange = 1500.0f;
		if (range < closeToNoiseRange)
		{
			if ((me->GetNearbyFriendCount() == 0 || me->GetFriendsRemaining() <= 2) && !me->IsHurrying())
			{
				me->Walk();
			}
			else
			{
				me->Run();
			}
		}
		else
		{
			me->Run();
		}
	}

	const float closeRange = 500.0f;
	if (range < closeRange)
	{
		if (me->IsVisible( m_checkNoisePosition, CHECK_FOV ))
		{
			me->PrintIfWatched( "Noise location is clear.\n" );
			me->ForgetNoise();
			me->Idle();
			return;
		}
	}

	if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	{
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void InvestigateNoiseState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->Run();
}
