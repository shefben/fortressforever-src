//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_hunt.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"       // For TheNavMesh, CNavArea, Place, Extent, TheNavAreas
#include "../nav_hiding_spot.h"// For FindRandomHidingSpot

// Local bot utility headers
#include "../bot_constants.h"  // For BotTaskType, DispositionType, TEAM_TERRORIST, TEAM_CT, etc.
#include "../bot_profile.h"    // For GetProfile(), GetMorale()
#include "../bot_util.h"       // For PrintIfWatched

// TODO: cs_simple_hostage.h is CS-specific. Remove or replace if FF has hostages.
// #include "cs_simple_hostage.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the hunt
 */
void HuntState::OnEnter( CFFBot *me )
{
	if (!me) return; // Null check

	// lurking death
	if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying()) // TODO_FF: Knife logic
		me->Walk();
	else
		me->Run();


	me->StandUp();
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // DispositionType enum
	me->SetTask( CFFBot::BOT_TASK_SEEK_AND_DESTROY );    // BotTaskType enum

	me->DestroyPath();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Hunt down our enemies
 */
void HuntState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh || !me->GetChatter() || !me->GetProfile()) return; // Null checks

	// if we've been hunting for a long time, drop into Idle for a moment to
	// select something else to do
	const float huntingTooLongTime = 30.0f;
	if (gpGlobals->curtime - me->GetStateTimestamp() > huntingTooLongTime)
	{
		// stop being a rogue and do the scenario, since there must not be many enemies left to hunt
		PrintIfWatched(me, "Giving up hunting.\n" ); // Updated PrintIfWatched
		me->SetRogue( false );
		me->Idle();
		return;
	}

	// scenario logic
	// TODO_FF: This entire scenario logic section is CS-specific (bomb, hostages) and needs FF adaptation
	if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB) // CS Specific
	{
		if (me->GetTeamNumber() == TEAM_TERRORIST) // CS Specific Team
		{
			// if (me->HasC4()) // CS Specific
			// {
			//	if (TheFFBots()->IsTimeToPlantBomb() || (me->IsAtBombsite() && gpGlobals->curtime - me->GetLastSawEnemyTimestamp() > 3.0f))
			//	{ me->Idle(); return; }
			// }
			// if (me->NoticeLooseBomb()) { me->FetchBomb(); return; }
			// const Vector *bombPos = me->GetGameState()->GetBombPosition();
			// if (!me->IsRogue() && me->GetGameState()->IsBombPlanted() && bombPos)
			// { me->SetTask( CFFBot::GUARD_TICKING_BOMB ); me->Hide( TheNavMesh->GetNavArea( *bombPos ) ); return; }
		}
		else // CT
		{
			// if (!me->IsRogue() && me->CanSeeLooseBomb())
			// { me->SetTask( CFFBot::GUARD_LOOSE_BOMB ); me->Hide( TheFFBots()->GetLooseBombArea() ); me->GetChatter()->GuardingLooseBomb( TheFFBots()->GetLooseBomb() ); return; }
			// else if (TheFFBots()->IsBombPlanted())
			// { if (!me->IsRogue() || !TheFFBots()->GetBombDefuser()) { me->Idle(); return; } }
		}
	}
	// else if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES) // CS Specific
	// {
	//	if (me->GetTeamNumber() == TEAM_TERRORIST) // CS Specific Team
	//	{
	//		if (me->GetGameState()->AreAllHostagesBeingRescued())
	//		{ if (me->GuardRandomZone()) { /* ... */ return; } }
	//		if (!me->IsRogue() && !me->IsSafe())
	//		{ CHostage *hostage = me->GetGameState()->GetNearestVisibleFreeHostage(); /* ... */ }
	//	}
	// }

	// listen for enemy noises
	if (me->HeardInterestingNoise())
	{
		me->InvestigateNoise();
		return;
	}

	// look around
	me->UpdateLookAround();

	// if we have reached our destination area, pick a new one
	// if our path fails, pick a new one
	if (me->GetLastKnownArea() == m_huntArea || me->UpdatePathMovement() != CFFBot::PROGRESSING) // PROGRESSING from PathResult enum
	{
		// pick a new hunt area
		const float earlyGameTime = 45.0f;
		if (TheFFBots()->GetElapsedRoundTime() < earlyGameTime && !me->HasVisitedEnemySpawn())
		{
			// in the early game, rush the enemy spawn
			// TODO_FF: Update for FF teams
			CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( OtherTeam( me->GetTeamNumber() ) );
			if ( enemySpawn ) m_huntArea = TheNavMesh->GetNavArea( enemySpawn->WorldSpaceCenter() );
		}
		else
		{
			m_huntArea = NULL; float oldest = 0.0f; int areaCount = 0; const float minSize = 150.0f;
			if (TheNavAreas) { // Null check for global TheNavAreas
				FOR_EACH_VEC( (*TheNavAreas), it )
				{
					CNavArea *area = (*TheNavAreas)[ it ]; if (!area) continue;
					++areaCount;
					Extent extent; area->GetExtent(&extent);
					if (extent.hi.x - extent.lo.x < minSize || extent.hi.y - extent.lo.y < minSize) continue;
					float age = gpGlobals->curtime - area->GetClearedTimestamp( me->GetTeamNumber()-1 ); // TODO_FF: Team indexing
					if (age > oldest) { oldest = age; m_huntArea = area; }
				}
				if (areaCount > 0 && !m_huntArea) // if all areas were too small, pick one at random from all
				{
					int which = RandomInt( 0, areaCount-1 );
					m_huntArea = (*TheNavAreas)[which]; // Potential issue if areaCount was from filtered list
				}
			}
		}

		if (m_huntArea)
		{
			me->ComputePath( m_huntArea->GetCenter(), SAFEST_ROUTE ); // SAFEST_ROUTE from RouteType enum
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Done hunting
 */
void HuntState::OnExit( CFFBot *me )
{
	// TODO_FF: Add any exit logic if needed
}
