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

// range for snipers to select a hiding spot
const float sniperHideRange = 2000.0f; // This can be tuned for FF

//--------------------------------------------------------------------------------------------------------------
/**
 * The Idle state.
 * We never stay in the Idle state - it is a "home base" for the state machine that
 * does various checks to determine what we should do next.
 */
void IdleState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->DestroyPath();
	me->SetBotEnemy( NULL );

	// FF_TODO: Review if IsUsingKnife, IsWellPastSafe, IsHurrying concepts translate directly for FF item/weapon logic
	// if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying())
	// 	me->Walk();

	me->SetTask( CFFBot::SEEK_AND_DESTROY ); // SEEK_AND_DESTROY is generic
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // ENGAGE_AND_INVESTIGATE is generic
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Determine what we should do next
 */
void IdleState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	if (me->GetLastKnownArea() == NULL && me->StayOnNavMesh() == false)
		return;

	if (cv_bot_zombie.GetBool())
	{
		me->ResetStuckMonitor();
		return;
	}

	// FF_TODO: "Safe time" weapon equip logic needs FF item/weapon specifics
	// if (me->IsSafe())
	// {
	// 	if (!me->EquipGrenade()) // EquipGrenade needs FF logic
	// 	{
	// 		// if (me->GetProfile()->GetSkill() > 0.33f && !me->IsUsing( FF_WEAPON_SCOUT_PRIMARY_EXAMPLE )) // IsUsing needs FF weapon ID/alias
	// 		// {
	// 		// 	me->EquipKnife(); // EquipKnife needs FF logic
	// 		// }
	// 	}
	// }

	if (me->GetGameState()->IsRoundOver())
	{
		// FF_TODO: Logic for end-of-round objectives if any (e.g. last flag cap attempt)
		// if (me->GetHostageEscortCount()) // FF No Hostages
		// {
		// }
		me->Hunt();
		return;
	}

	// const float defenseSniperCampChance = 75.0f; // FF_TODO: Tune for FF balance
	// const float offenseSniperCampChance = 10.0f; // FF_TODO: Tune for FF balance

	if (me->IsFollowing())
	{
		me->ContinueFollowing();
		return;
	}

	// FF_TODO: Major scenario logic overhaul needed here.
	// The entire switch statement below is CS-specific and has been commented out.
	// FF bots will need to check FFGameRules and CFFBotManager for scenario state
	// (e.g., flag status, control point status, payload progress) and decide actions.
	/*
	switch (TheFFBots()->GetScenario()) // Changed TheCSBots to TheFFBots
	{
		//======================================================================================================
		case CFFBotManager::SCENARIO_DEFUSE_BOMB: // This would be an FF-specific scenario enum
		{
			// ... (All CS bomb logic removed) ...
			break;
		}

		//======================================================================================================
		case CFFBotManager::SCENARIO_ESCORT_VIP: // This would be an FF-specific scenario enum
		{
			// ... (All CS VIP logic removed) ...
			break;
		}

		//======================================================================================================
		case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // This would be an FF-specific scenario enum
		{
			// ... (All CS Hostage logic removed) ...
			break;
		}

		default:	// Handles SCENARIO_FF_UNKNOWN, SCENARIO_FF_ITEM_SCRIPT, SCENARIO_FF_MINECART, SCENARIO_FF_MIXED
		{
			// FF_TODO: Implement logic for FF's actual game modes.
			// This might involve:
			// - Checking if a flag needs to be captured or returned.
			// - Checking if a control point needs to be captured or defended.
			// - Checking if a payload cart needs to be pushed or stopped.
			// - Interacting with Lua-defined objectives via CFFInfoScript entities.

			// Example placeholder for a generic objective game mode:
			// if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_FF_ITEM_SCRIPT)
			// {
			//    CFFInfoScript* pObjectiveItem = TheFFBots()->FindObjectiveItem(); // Hypothetical
			//    if (pObjectiveItem)
			//    {
			//        if (me->CanInteractWithObjective(pObjectiveItem)) // Hypothetical
			//        {
			//            me->SetTask(CFFBot::INTERACT_OBJECTIVE, pObjectiveItem); // Hypothetical task
			//            me->MoveTo(pObjectiveItem->GetAbsOrigin());
			//            return;
			//        }
			//    }
			// }

			// Fallback to hunting if no specific scenario action is determined yet for FF modes.
			// me->Hunt();
			// return;
			break;
		}
	}
	*/

	if (me->HeardInterestingNoise())
	{
		me->InvestigateNoise();
		return;
	}

	me->UpdateLookAround();

	// If idle for too long, or no specific scenario task, default to hunting.
	// This part of the logic can remain more generic.
	if (me->GetLastKnownArea() == m_huntArea || me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	{
		m_huntArea = NULL; // Clear previous hunt area before finding a new one.
		const float earlyGameTime = 45.0f; // This can be tuned for FF
		if (TheFFBots()->GetElapsedRoundTime() < earlyGameTime && !me->HasVisitedEnemySpawn())
		{
			// FF_TODO: Ensure OtherTeam and GetRandomSpawn work with FF team definitions correctly.
			CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( OtherTeam( me->GetTeamNumber() ) );
			if ( enemySpawn )
			{
				m_huntArea = TheNavMesh->GetNavArea( enemySpawn->WorldSpaceCenter() );
			}
		}
		else
		{
			// ... (logic for finding a new m_huntArea as in original, seems generic enough) ...
			float oldest = 0.0f; int areaCount = 0; const float minSize = 150.0f;
			FOR_EACH_VEC( TheNavAreas, it )
			{
				CNavArea *area = TheNavAreas[ it ]; ++areaCount;
				Extent extent; area->GetExtent(&extent);
				if (extent.hi.x - extent.lo.x < minSize || extent.hi.y - extent.lo.y < minSize) continue;
				// Assuming GetTeamNumber() returns valid FF team ID for GetClearedTimestamp
				float age = gpGlobals->curtime - area->GetClearedTimestamp( me->GetTeamNumber() );
				if (age > oldest) { oldest = age; m_huntArea = area; }
			}
			if (!m_huntArea && areaCount > 0)
			{
				int which = RandomInt( 0, areaCount-1 ); areaCount = 0;
				FOR_EACH_VEC( TheNavAreas, hit ) { m_huntArea = TheNavAreas[ hit ]; if (which == areaCount) break; areaCount++; }
			}
		}

		if (m_huntArea)
		{
			me->ComputePath( m_huntArea->GetCenter() );
		}
		else
		{
			// If truly no area to hunt, just stand by for a moment.
			// The main OnUpdate loop will eventually call Idle again.
			return;
		}
	}

	// If all else fails and bot is just idle without a path, make it hunt.
	if(!me->HasPath())
	{
		me->Hunt();
	}
}

// OnExit for IdleState is not present in cs_bot_idle.cpp, so not adding one here.
