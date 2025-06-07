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
#include "../ff_player.h"    // Added for CFFPlayer (though ff_bot.h includes it)
#include "../ff_bot_manager.h" // For TheFFBots() and team definitions

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the hunt
 */
void HuntState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	// FF_TODO: Review if IsUsingKnife, IsWellPastSafe, IsHurrying concepts translate directly
	if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying())
		me->Walk();
	else
		me->Run();

	me->StandUp();
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // Changed CCSBot to CFFBot
	me->SetTask( CFFBot::SEEK_AND_DESTROY ); // Changed CCSBot to CFFBot

	me->DestroyPath();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Hunt down our enemies
 */
void HuntState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	const float huntingTooLongTime = 30.0f;
	if (gpGlobals->curtime - me->GetStateTimestamp() > huntingTooLongTime)
	{
		me->PrintIfWatched( "Giving up hunting.\n" );
		me->SetRogue( false ); // SetRogue is a CFFBot method
		me->Idle();
		return;
	}

	// FF_TODO: Scenario logic needs complete overhaul for FF objectives.
	// The CS bomb/hostage logic below is commented out.
	/*
	if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB) // Example: This would be an FF scenario type
	{
		if (me->GetTeamNumber() == FF_TEAM_RED) // Example: Red team as T equivalent
		{
			// if (me->HasC4()) // HasC4 is CS-specific
			// {
			// 	const float safeTime = 3.0f;
			// 	if (TheFFBots()->IsTimeToPlantBomb() || // IsTimeToPlantBomb is CS-specific
			// 			(me->IsAtBombsite() && gpGlobals->curtime - me->GetLastSawEnemyTimestamp() > safeTime)) // IsAtBombsite is CS-specific
			// 	{
			// 		me->Idle();
			// 		return;
			// 	}
			// }
			// if (me->NoticeLooseBomb()) // NoticeLooseBomb is CS-specific
			// {
			// 	me->FetchBomb(); // FetchBomb is CS-specific
			// 	return;
			// }
			// const Vector *bombPos = me->GetGameState()->GetBombPosition(); // GetBombPosition is CS-specific
			// if (!me->IsRogue() && me->GetGameState()->IsBombPlanted() && bombPos) // IsBombPlanted is CS-specific
			// {
			// 	me->SetTask( CFFBot::GUARD_TICKING_BOMB ); // GUARD_TICKING_BOMB is CS-specific
			// 	me->Hide( TheNavMesh->GetNavArea( *bombPos ) );
			// 	return;
			// }
		}
		else // FF_TEAM_BLUE as CT equivalent
		{
			// if (!me->IsRogue() && me->CanSeeLooseBomb()) // CanSeeLooseBomb is CS-specific
			// {
			// 	me->SetTask( CFFBot::GUARD_LOOSE_BOMB ); // GUARD_LOOSE_BOMB is CS-specific
			// 	me->Hide( TheFFBots()->GetLooseBombArea() ); // GetLooseBombArea is CS-specific
			// 	// me->GetChatter()->GuardingLooseBomb( TheFFBots()->GetLooseBomb() ); // CS-specific chatter
			// 	return;
			// }
			// else if (TheFFBots()->IsBombPlanted()) // IsBombPlanted is CS-specific
			// {
			// 	if (!me->IsRogue() || !TheFFBots()->GetBombDefuser()) // GetBombDefuser is CS-specific
			// 	{
			// 		me->Idle();
			// 		return;
			// 	}
			// }
		}
	}
	else if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_RESCUE_HOSTAGES) // This scenario type would be FF specific
	{
		// ... (CS hostage logic removed) ...
	}
	*/

	if (me->HeardInterestingNoise())
	{
		me->InvestigateNoise();
		return;
	}

	me->UpdateLookAround();

	if (me->GetLastKnownArea() == m_huntArea || me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	{
		const float earlyGameTime = 45.0f;
		if (TheFFBots()->GetElapsedRoundTime() < earlyGameTime && !me->HasVisitedEnemySpawn())
		{
			// FF_TODO: Ensure OtherTeam and GetRandomSpawn work with FF team definitions correctly.
			// TheFFBots()->GetRandomSpawn might return NULL if no spawns for that team are found.
			CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( OtherTeam( me->GetTeamNumber() ) );
			if ( enemySpawn )
			{
				m_huntArea = TheNavMesh->GetNavArea( enemySpawn->WorldSpaceCenter() );
			}
			else
			{
				m_huntArea = NULL; // Fallback if no enemy spawn found
			}
		}
		else
		{
			m_huntArea = NULL; float oldest = 0.0f; int areaCount = 0; const float minSize = 150.0f;
			FOR_EACH_VEC( TheNavAreas, it )
			{
				CNavArea *area = TheNavAreas[ it ]; ++areaCount;
				Extent extent; area->GetExtent(&extent);
				if (extent.hi.x - extent.lo.x < minSize || extent.hi.y - extent.lo.y < minSize) continue;
				// Assuming GetTeamNumber() returns valid FF team ID for GetClearedTimestamp
				float age = gpGlobals->curtime - area->GetClearedTimestamp( me->GetTeamNumber() );
				if (age > oldest) { oldest = age; m_huntArea = area; }
			}
			if (!m_huntArea && areaCount > 0) // if all areas were too small, pick one at random
			{
				int which = RandomInt( 0, areaCount-1 );
				// This re-iteration is inefficient, but matches original. Consider optimizing if problematic.
				areaCount = 0;
				FOR_EACH_VEC( TheNavAreas, hit ) { m_huntArea = TheNavAreas[ hit ]; if (which == areaCount) break; areaCount++; }
			}
		}

		if (m_huntArea)
		{
			me->ComputePath( m_huntArea->GetCenter() );
		}
		else
		{
			// No valid hunt area found, go idle to reconsider.
			me->Idle();
			return;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Done hunting
 */
void HuntState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
}
