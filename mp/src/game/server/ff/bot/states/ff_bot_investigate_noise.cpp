//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_investigate_noise.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"

// Local bot utility headers
#include "../bot_constants.h"  // For PriorityType, RouteType, CHECK_FOV, etc.
#include "../bot_profile.h"    // For GetProfile() (potentially used by CFFBot methods)
#include "../bot_util.h"       // For PrintIfWatched


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Move towards currently heard noise
 */
void InvestigateNoiseState::AttendCurrentNoise( CFFBot *me )
{
	if (!me || !me->GetChatter()) return; // Null checks

	if (!me->IsNoiseHeard() && me->GetNoisePosition()) // GetNoisePosition can be null if not heard
		return;

	const Vector *noisePos = me->GetNoisePosition();
	if (!noisePos) return; // Must have a noise position to attend to

	// remember where the noise we heard was
	m_checkNoisePosition = *noisePos;

	// tell our teammates (unless the noise is obvious, like gunfire)
	if (me->IsWellPastSafe() && me->HasNotSeenEnemyForLongTime() && me->GetNoisePriority() != PRIORITY_HIGH)
		me->GetChatter()->HeardNoise( *noisePos );

	// figure out how to get to the noise		
	PrintIfWatched(me, "Attending to noise...\n" ); // Updated PrintIfWatched
	me->ComputePath( m_checkNoisePosition, FASTEST_ROUTE );

	const float minAttendTime = 3.0f;
	const float maxAttendTime = 10.0f;
	m_minTimer.Start( RandomFloat( minAttendTime, maxAttendTime ) );

	// consume the noise
	me->ForgetNoise();
}

//--------------------------------------------------------------------------------------------------------------
void InvestigateNoiseState::OnEnter( CFFBot *me )
{
	if (!me) return;
	AttendCurrentNoise( me );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * @todo Use TravelDistance instead of distance...
 */
void InvestigateNoiseState::OnUpdate( CFFBot *me )
{
	if (!me) return;
	Vector myOrigin = GetCentroid( me );

	// keep an ear out for closer noises...
	if (m_minTimer.IsElapsed())
	{
		const float nearbyRange = 500.0f;
		if (me->HeardInterestingNoise() && me->GetNoiseRange() < nearbyRange)
		{
			AttendCurrentNoise( me );
		}
	}


	// if the pathfind fails, give up
	if (!me->HasPath())
	{
		me->Idle();
		return;
	}

	// look around
	me->UpdateLookAround();

	// get distance remaining on our path until we reach the source of the noise
	float range = me->GetPathDistanceRemaining();

	// TODO_FF: Knife logic
	if (me->IsUsingKnife())
	{
		if (me->IsHurrying())
			me->Run();
		else
			me->Walk();
	}
	else
	{
		const float closeToNoiseRange = 1500.0f;
		if (range < closeToNoiseRange)
		{
			// if we dont have many friends left, or we are alone, and we are near noise source, sneak quietly
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


	// if we can see the noise position and we're close enough to it and looking at it, 
	// we don't need to actually move there (it's checked enough)
	const float closeRange = 500.0f;
	if (range < closeRange)
	{
		if (me->IsVisible( m_checkNoisePosition, true )) // Was CHECK_FOV
		{
			// can see noise position
			PrintIfWatched(me, "Noise location is clear.\n" ); // Updated PrintIfWatched
			me->ForgetNoise();
			me->Idle();
			return;
		}
	}

	// move towards noise
	if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // PROGRESSING from PathResult enum
	{
		me->Idle();
	}
}

//--------------------------------------------------------------------------------------------------------------
void InvestigateNoiseState::OnExit( CFFBot *me )
{
	if (!me) return;
	// reset to run mode in case we were sneaking about
	me->Run();
}
