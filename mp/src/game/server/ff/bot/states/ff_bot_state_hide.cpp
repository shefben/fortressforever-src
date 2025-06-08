//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_hide.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"       // For TheNavMesh, CNavArea, Place, NAV_MESH_STAND, etc.
#include "../nav_hiding_spot.h"// For HidingSpot, FindNearbyHidingSpot, FindInitialEncounterSpot
#include "../nav_pathfind.h"   // For PathCost, FASTEST_ROUTE (if RouteType is here)

// Local bot utility headers
#include "../bot_constants.h"  // For BotTaskType, DispositionType, TEAM_TERRORIST, TEAM_CT, PriorityType, CHECK_FOV, etc.
#include "../bot_profile.h"    // For GetProfile()
#include "../bot_util.h"       // For PrintIfWatched, IsSpotOccupied, UTIL_GetClosestPlayer

// TODO: cs_simple_hostage.h is CS-specific. Remove or replace if FF has hostages.
// #include "cs_simple_hostage.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin moving to a nearby hidey-hole.
 */
void HideState::OnEnter( CFFBot *me )
{
	if (!me || !me->GetProfile()) return; // Null checks

	m_isAtSpot = false;
	m_isLookingOutward = false;

	if (m_duration < 0.0f) m_duration = RandomFloat( 30.0f, 60.0f );
	if (RandomFloat( 0.0f, 100.0f ) < 50.0f) m_isHoldingPosition = true;

	if (m_isHoldingPosition) m_holdPositionTime = RandomFloat( 3.0f, 10.0f );
	else m_holdPositionTime = 0.0f;

	m_heardEnemy = false;
	m_firstHeardEnemyTime = 0.0f;
	m_retry = 0;

	if (me->IsFollowing() && me->GetFollowLeader()) // Null check GetFollowLeader
	{
		m_leaderAnchorPos = GetCentroid( me->GetFollowLeader() );
	}

	if (me->IsSniper()) // TODO_FF: Sniper logic
	{
		m_isPaused = false;
		m_pauseTimer.Invalidate();
	}
}

//--------------------------------------------------------------------------------------------------------------
void HideState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh || !me->GetChatter() || !me->GetProfile()) return;

	Vector myOrigin = GetCentroid( me );

	if (!me->IsReloading())
	{
		if (me->IsFollowing())
		{
			CFFPlayer *leader = me->GetFollowLeader(); // Already EHANDLE, Get() is implicit
			if (leader) // Null check leader
			{
				Vector leaderOrigin = GetCentroid( leader );
				if (leader->GetAbsVelocity().IsLengthGreaterThan( 200.0f )) { me->Follow( leader ); return; }
				if ((m_leaderAnchorPos - leaderOrigin).IsLengthGreaterThan( 250.0f )) { me->Follow( leader ); return; }
			} else { // Leader is null, stop following
				me->StopFollowing();
				me->Idle();
				return;
			}
		}

		// TODO_FF: CS Specific scenario logic (Bomb, Hostages) needs heavy adaptation or removal
		// switch( TheFFBots()->GetScenario() ) { /* ... CS logic ... */ }

		bool isSettledInSniper = (me->IsSniper() && m_isAtSpot); // TODO_FF: Sniper logic

		if (!me->IsReloading() && !isSettledInSniper && me->GetDisposition() == CFFBot::ENGAGE_AND_INVESTIGATE)
		{
			if (m_isHoldingPosition && m_heardEnemy && (gpGlobals->curtime - m_firstHeardEnemyTime > m_holdPositionTime))
			{
				me->InvestigateNoise(); return;
			}
			if (me->HeardInterestingNoise())
			{
				if (m_isAtSpot && m_isHoldingPosition)
				{
					if (!m_heardEnemy)
					{
						m_heardEnemy = true; m_firstHeardEnemyTime = gpGlobals->curtime;
						PrintIfWatched(me, "Heard enemy, holding position for %2.1f seconds...\n", m_holdPositionTime ); // Updated
					}
				}
				else { me->InvestigateNoise(); return; }
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
			// TODO_FF: CS Specific task logic (GUARD_LOOSE_BOMB etc.)
			// if (me->GetTask() == CFFBot::BOT_TASK_GUARD_LOOSE_BOMB) { /* ... */ }
			// else if (me->GetTask() == CFFBot::BOT_TASK_GUARD_BOMB_ZONE) { /* ... */ }
			// else if (me->GetTask() == CFFBot::BOT_TASK_GUARD_HOSTAGE_RESCUE_ZONE) { /* ... */ }
			me->Idle(); return;
		}

		// TODO_FF: Shield logic
		// if (me->HasShield() && !me->IsProtectedByShield()) me->SecondaryAttack();

		if (!me->IsEnemyVisible() && me->GetTimeSinceAttacked() < 1.0f) { me->Idle(); return; }

		// TODO_FF: CS Specific "encourage" logic
		// if (!me->IsDoingScenario()) { /* ... */ }
	}
	else
	{
		// TODO_FF: Sniper logic
		// if (me->IsSniper() && me->IsEnemyVisible()) { /* ... sniper pausing logic ... */ }

		float range;
		CFFPlayer *camper = static_cast<CFFPlayer *>( UTIL_GetClosestPlayer( m_hidingSpot, &range ) );
		if (camper && camper != me && range < 75.0f && me->IsVisible( camper, true )) // CHECK_FOV = true
		{
			PrintIfWatched(me, "Someone's in my hiding spot - picking another...\n" ); // Updated
			if (m_retry++ >= 3) { PrintIfWatched(me, "Can't find a free hiding spot, giving up.\n" ); me->Idle(); return; } // Updated
			CNavArea* currentSpotArea = TheNavMesh->GetNavArea(m_hidingSpot);
			if (currentSpotArea) me->Hide( currentSpotArea ); // Pass area to hide in
			else me->Idle(); // Fallback if current spot area is invalid
			return;
		}

		Vector toSpot = m_hidingSpot - myOrigin;
		toSpot.z = m_hidingSpot.z - me->GetFeetZ();
		range = toSpot.Length();

		if (!me->IsEnemyVisible() && !m_isLookingOutward)
		{
			if (range < 200.0f && range > 10.0f)
			{
				m_isLookingOutward = true;
				if(range > FLT_EPSILON) toSpot.NormalizeInPlace(); else toSpot = Vector(1,0,0);
				me->SetLookAt( "Face outward", me->EyePosition() - 1000.0f * toSpot, PRIORITY_HIGH, 3.0f );
			}
		}

		if (range < 20.0f) // atDist
		{
			m_isAtSpot = true; m_hideTimer.Start( m_duration );
			me->ComputeApproachPoints(); me->ClearLookAt();
			me->EquipBestWeapon( me->IsUsingGrenade() ); // TODO_FF: Grenade logic
			me->SetDisposition( CFFBot::OPPORTUNITY_FIRE ); // DispositionType enum

			// TODO_FF: Sniper and other task logic
			// if (me->GetTask() == CFFBot::BOT_TASK_MOVE_TO_SNIPER_SPOT) { /* ... */ }
			// else if (me->GetTask() == CFFBot::BOT_TASK_GUARD_INITIAL_ENCOUNTER) { /* ... */ }

			trace_t result; float outAngle = 0.0f; float outAngleRange = 0.0f;
			for( float angle = 0.0f; angle < 360.0f; angle += 45.0f )
			{
				UTIL_TraceLine( me->EyePosition(), me->EyePosition() + 1000.0f * Vector( cos(DEG2RAD(angle)), sin(DEG2RAD(angle)), 0.0f ), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result ); // BotCOS/SIN to cos/sin
				if (result.fraction > outAngleRange) { outAngle = angle; outAngleRange = result.fraction; }
			}
			me->SetLookAheadAngle( outAngle );
		}

		if (me->UpdatePathMovement() != CFFBot::PROGRESSING && !m_isAtSpot) // PROGRESSING from PathResult
		{
			PrintIfWatched(me, "Can't get to my hiding spot - finding another...\n" ); // Updated
			const Vector *pos = FindNearbyHidingSpot( me, m_hidingSpot, m_range, me->IsSniper() ); // FindNearbyHidingSpot
			if (pos == NULL)
			{
				PrintIfWatched(me, "No available hiding spots - hiding where I'm at.\n" ); // Updated
				m_hidingSpot = myOrigin; m_hidingSpot.z = me->GetFeetZ();
			}
			else m_hidingSpot = *pos;
			if (me->ComputePath( m_hidingSpot, FASTEST_ROUTE ) == false) { PrintIfWatched(me, "Can't pathfind to hiding spot\n" ); me->Idle(); return; } // Updated
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void HideState::OnExit( CFFBot *me )
{
	if (!me) return;
	m_isHoldingPosition = false;
	me->StandUp();
	me->ResetStuckMonitor();
	me->ClearApproachPoints();
	// TODO_FF: Shield logic
	// if (me->HasShield() && me->IsProtectedByShield()) me->SecondaryAttack();
}
