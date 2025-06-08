//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
// #include "cs_simple_hostage.h" // FF No Hostages
#include "../ff_bot.h"       // Changed from cs_bot.h
#include "../ff_player.h"    // Added for CFFPlayer
#include "../ff_bot_manager.h" // For TheFFBots() and team definitions

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin moving to a nearby hidey-hole.
 * NOTE: Do not forget this state may include a very long "move-to" time to get to our hidey spot!
 */
void HideState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	m_isAtSpot = false;
	m_isLookingOutward = false;

	if (m_duration < 0.0f)
	{
		m_duration = RandomFloat( 30.0f, 60.0f );
	}

	if (RandomFloat( 0.0f, 100.0f ) < 50.0f)
	{
		m_isHoldingPosition = true;
	}

	if (m_isHoldingPosition)
	{
		m_holdPositionTime = RandomFloat( 3.0f, 10.0f );
	}
	else
	{
		m_holdPositionTime = 0.0f;
	}

	m_heardEnemy = false;
	m_firstHeardEnemyTime = 0.0f;
	m_retry = 0;

	if (me->IsFollowing())
	{
		CFFPlayer *leader = me->GetFollowLeader(); // GetFollowLeader returns CFFPlayer*
		if (leader)
			m_leaderAnchorPos = GetCentroid( leader );
		else
			m_leaderAnchorPos = vec3_origin; // Should not happen if IsFollowing is true
	}

	if (me->IsSniper()) // Assumes IsSniper() is valid for FF
	{
		m_isPaused = false;
		m_pauseTimer.Invalidate();
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a nearby hidey-hole.
 */
void HideState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	Vector myOrigin = GetCentroid( me );

	if (!me->IsReloading())
	{
		// FF_TODO_AI_BEHAVIOR: This following logic might need review for robustness, especially if leader moves erratically.
		if (me->IsFollowing())
		{
			CFFPlayer *leader = me->GetFollowLeader(); // GetFollowLeader returns CFFPlayer*
			if (leader)
			{
				Vector leaderOrigin = GetCentroid( leader );
				float runThreshold = 200.0f;
				if (leader->GetAbsVelocity().IsLengthGreaterThan( runThreshold ))
				{
					me->Follow( leader );
					return;
				}
				const float followRange = 250.0f;
				if ((m_leaderAnchorPos - leaderOrigin).IsLengthGreaterThan( followRange ))
				{
					me->Follow( leader );
					return;
				}
			}
		}

		// FF_TODO_GAME_MECHANIC: Scenario logic needs complete overhaul for FF objectives.
		// The CS bomb/hostage logic below is commented out.
		/*
		switch( TheFFBots()->GetScenario() ) // Changed TheCSBots to TheFFBots
		{
			case CFFBotManager::SCENARIO_DEFUSE_BOMB: // This scenario type would be FF specific
			{
				if (me->GetTeamNumber() == FF_TEAM_BLUE) // Example: FF_TEAM_BLUE as CT equivalent
				{
					// ... (CS CT bomb logic removed) ...
				}
				else	// FF_TEAM_RED as T equivalent
				{
					// ... (CS T bomb logic removed) ...
				}
				break;
			}
			case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // This scenario type would be FF specific
			{
				// ... (CS hostage logic removed) ...
			}
		}
		*/

		bool isSettledInSniper = (me->IsSniper() && m_isAtSpot) ? true : false;

		if (!me->IsReloading() && !isSettledInSniper && me->GetDisposition() == CFFBot::ENGAGE_AND_INVESTIGATE)
		{
			if (m_isHoldingPosition && m_heardEnemy && (gpGlobals->curtime - m_firstHeardEnemyTime > m_holdPositionTime))
			{
				me->InvestigateNoise();
				return;
			}
			if (me->HeardInterestingNoise())
			{
				if (m_isAtSpot && m_isHoldingPosition)
				{
					if (!m_heardEnemy)
					{
						m_heardEnemy = true;
						m_firstHeardEnemyTime = gpGlobals->curtime;
						me->PrintIfWatched( "Heard enemy, holding position for %f2.1 seconds...\n", m_holdPositionTime );
					}
				}
				else
				{
					me->InvestigateNoise();
					return;
				}
			}
		}
	}

	me->UpdateLookAround();

	if (m_isAtSpot)
	{
		me->ResetStuckMonitor();
		CNavArea *area = TheNavMesh->GetNavArea( m_hidingSpot );
		if ( !area || !( area->GetAttributes() & NAV_MESH_STAND ) ) me->Crouch();

		if (m_hideTimer.IsElapsed())
		{
			// FF_TODO_AI_BEHAVIOR: Adapt task continuation logic for FF objectives
			// Example: if guarding a flag spawn, pick a new spot near it.
			// if (me->GetTask() == CFFBot::GUARD_FLAG_SPAWN) { ... me->Hide(newSpot); return; }
			me->Idle();
			return;
		}

		const float hurtRecentlyTime = 1.0f;
		if (!me->IsEnemyVisible() && me->GetTimeSinceAttacked() < hurtRecentlyTime)
		{
			me->Idle();
			return;
		}
		// FF_TODO_AI_BEHAVIOR: Human player encouragement logic might be different or not needed.
	}
	else
	{
		if (me->IsSniper() && me->IsEnemyVisible())
		{
			if (m_isPaused) { if (m_pauseTimer.IsElapsed()) { m_isPaused = false; m_pauseTimer.Start( RandomFloat( 1.0f, 3.0f ) ); } else { me->Wait( 0.2f ); } }
			else { if (m_pauseTimer.IsElapsed()) { m_isPaused = true; m_pauseTimer.Start( RandomFloat( 0.5f, 1.5f ) ); } }
		}

		float range;
		CFFPlayer *camper = static_cast<CFFPlayer *>( UTIL_GetClosestPlayer( m_hidingSpot, &range ) ); // Changed CCSPlayer

		const float closeRange = 75.0f;
		if (camper && camper != me && range < closeRange && me->IsVisible( camper, CHECK_FOV ))
		{
			me->PrintIfWatched( "Someone's in my hiding spot - picking another...\n" );
			const int maxRetries = 3;
			if (m_retry++ >= maxRetries) { me->PrintIfWatched( "Can't find a free hiding spot, giving up.\n" ); me->Idle(); return; }
			me->Hide( TheNavMesh->GetNavArea( m_hidingSpot ) ); // Hide takes CNavArea*
			return;
		}

		Vector toSpot = m_hidingSpot - myOrigin; // Use myOrigin already calculated
		range = toSpot.Length(); // Recalculate range with full vector

		if (!me->IsEnemyVisible() && !m_isLookingOutward)
		{
			const float lookOutwardRange = 200.0f; const float nearSpotRange = 10.0f;
			if (range < lookOutwardRange && range > nearSpotRange)
			{
				m_isLookingOutward = true;
				toSpot.NormalizeInPlace(); // Normalize before using as direction
				me->SetLookAt( "Face outward", me->EyePosition() - 1000.0f * toSpot, PRIORITY_HIGH, 3.0f );
			}
		}

		const float atDist = 20.0f;
		if (range < atDist)
		{
			m_isAtSpot = true; m_hideTimer.Start( m_duration );
			me->ComputeApproachPoints(); me->ClearLookAt();
			me->EquipBestWeapon( me->IsUsingGrenade() ); // FF_TODO_WEAPON_STATS: EquipBestWeapon and IsUsingGrenade need FF logic
			me->SetDisposition( CFFBot::OPPORTUNITY_FIRE ); // Changed CCSBot to CFFBot

			// FF_TODO_AI_BEHAVIOR: Adapt task update for FF (e.g. if it was MOVE_TO_DEFEND_POINT, change to DEFENDING_POINT)
			// if (me->GetTask() == CFFBot::MOVE_TO_SNIPER_SPOT) me->SetTask( CFFBot::SNIPING );

			trace_t result; float outAngle = 0.0f; float outAngleRange = 0.0f;
			for( float angle = 0.0f; angle < 360.0f; angle += 45.0f )
			{
				UTIL_TraceLine( me->EyePosition(), me->EyePosition() + 1000.0f * Vector( BotCOS(angle), BotSIN(angle), 0.0f ), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result );
				if (result.fraction > outAngleRange) { outAngle = angle; outAngleRange = result.fraction; }
			}
			me->SetLookAheadAngle( outAngle );
		}

		if (me->UpdatePathMovement() != CFFBot::PROGRESSING && !m_isAtSpot) // Changed CCSBot to CFFBot
		{
			me->PrintIfWatched( "Can't get to my hiding spot - finding another...\n" );
			const Vector *pos = FindNearbyHidingSpot( me, m_hidingSpot, m_range, me->IsSniper() ); // FindNearbyHidingSpot takes CFFBot
			if (pos == NULL) { me->PrintIfWatched( "No available hiding spots - hiding where I'm at.\n" ); m_hidingSpot = myOrigin; m_hidingSpot.z = me->GetFeetZ();}
			else { m_hidingSpot = *pos; }
			if (me->ComputePath( m_hidingSpot, FASTEST_ROUTE ) == false) { me->PrintIfWatched( "Can't pathfind to hiding spot\n" ); me->Idle(); return; }
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void HideState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	m_isHoldingPosition = false;
	me->StandUp();
	me->ResetStuckMonitor();
	me->ClearApproachPoints();
	// FF_TODO_GAME_MECHANIC: Shield logic removed
}
