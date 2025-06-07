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
	m_huntArea = NULL; // Initialize hunt area
	m_medicScanTimer.Start(RandomFloat(0.5f, 1.5f)); // Initialize medic scan timer
	m_engineerSentryScanTimer.Start(RandomFloat(2.0f, 3.5f)); // Renamed from m_engineerScanTimer
	m_engineerDispenserScanTimer.Start(RandomFloat(3.0f, 5.0f)); // Initialize dispenser scan timer
	m_engineerRepairScanTimer.Start(RandomFloat(1.5f, 2.5f)); // Initialize repair scan timer
	m_engineerResourceScanTimer.Start(RandomFloat(4.0f, 6.0f)); // Initialize resource scan timer
	m_engineerGuardScanTimer.Start(RandomFloat(5.0f, 7.0f)); // Initialize guard scan timer
	m_spyInfiltrateScanTimer.Start(RandomFloat(3.0f, 6.0f)); // Initialize spy scan timer


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
	// Medic Behavior: Periodically scan for teammates to heal
	if (me->IsMedic() && m_medicScanTimer.IsElapsed())
	{
		CFFPlayer* patient = me->FindNearbyInjuredTeammate();
		if (patient)
		{
			me->StartHealing(patient);
			return; // Transitioned to HealTeammateState
		}
		m_medicScanTimer.Start(RandomFloat(1.0f, 2.0f)); // Reschedule scan
	}

	// Engineer Behavior: Periodically consider building a sentry
	if (me->IsEngineer() && !me->HasSentry() && m_engineerSentryScanTimer.IsElapsed()) // Renamed timer
	{
		// FF_TODO_ENGINEER: Add more sophisticated logic for when/where to build sentries.
		// For now, if near a friendly Lua objective that the bot's team owns, try to build there.
		const CFFBotManager::LuaObjectivePoint* obj = me->GetClosestLuaObjectivePoint(me->GetAbsOrigin(), me->GetTeamNumber());
		if (obj && obj->currentOwnerTeam == me->GetTeamNumber() && (me->GetAbsOrigin() - obj->position).IsLengthLessThan(750.0f))
		{
			Vector buildPos = obj->position + Vector(RandomFloat(-100, 100), RandomFloat(-100,100), 0);
			me->PrintIfWatched("Engineer: Decided to build sentry near friendly objective '%s'.\n", obj->name);
			me->TryToBuildSentry(&buildPos);
			return;
		}
		m_engineerSentryScanTimer.Start(RandomFloat(5.0f, 10.0f));
	}

	// Engineer Behavior: Periodically consider building a dispenser
	if (me->IsEngineer() && !me->HasDispenser() && m_engineerDispenserScanTimer.IsElapsed())
	{
		// FF_TODO_ENGINEER: Add more sophisticated logic for when/where to build dispensers.
		// Example: Build near a cluster of teammates or a chokepoint/defensive position.
		// For now, if near a (possibly different) friendly Lua objective.
		const CFFBotManager::LuaObjectivePoint* obj = me->GetClosestLuaObjectivePoint(me->GetAbsOrigin(), me->GetTeamNumber());
		if (obj && obj->currentOwnerTeam == me->GetTeamNumber() && (me->GetAbsOrigin() - obj->position).IsLengthLessThan(600.0f))
		{
			Vector buildPos = obj->position + Vector(RandomFloat(-50, 50), RandomFloat(-50,50), 20); // Slightly different offset
			me->PrintIfWatched("Engineer: Decided to build dispenser near friendly objective '%s'.\n", obj->name);
			me->TryToBuildDispenser(&buildPos);
			return;
		}
		// Could also add logic to build if teammates nearby are low on health/ammo.
		m_engineerDispenserScanTimer.Start(RandomFloat(10.0f, 15.0f)); // Dispensers might be built less frequently
		if (me->GetState() != this) return; // Return if state changed
	}

	// Engineer Behavior: Periodically scan for damaged buildables to repair
	if (me->IsEngineer() && m_engineerRepairScanTimer.IsElapsed())
	{
		me->TryToRepairBuildable();
		m_engineerRepairScanTimer.Start(RandomFloat(2.0f, 4.0f));
		if (me->GetState() != this) return;
	}

	// Engineer Behavior: If low on resources, try to find some
	if (me->IsEngineer() && me->GetAmmoCount(AMMO_CELLS) < CFFBot::ENGINEER_LOW_CELL_THRESHOLD && m_engineerResourceScanTimer.IsElapsed())
	{
		me->TryToFindResources();
		m_engineerResourceScanTimer.Start(RandomFloat(5.0f, 8.0f));
		if (me->GetState() != this) return;
	}

	// Engineer Behavior: Periodically consider guarding their sentry
	if (me->IsEngineer() && me->HasSentry() && m_engineerGuardScanTimer.IsElapsed())
	{
		// Check if sentry is alive and built before trying to guard
		CFFSentryGun *sentry = me->GetSentryGun();
		if (sentry && sentry->IsAlive() && sentry->IsBuilt()) // isBuilt() is important
		{
			// Don't switch if already doing a higher priority Engineer task or already guarding
			if (me->GetState() != &me->m_buildSentryState &&
				me->GetState() != &me->m_buildDispenserState &&
				me->GetState() != &me->m_repairBuildableState && // Or allow interrupting repair for guard if sentry is fine
				me->GetState() != &me->m_guardSentryState)
			{
				me->TryToGuardSentry();
				if (me->GetState() != this) return;
			}
		}
		m_engineerGuardScanTimer.Start(RandomFloat(10.0f, 15.0f)); // Reschedule guard scan
	}

	// Spy Behavior: Periodically try to infiltrate
	if (me->IsSpy() && m_spyInfiltrateScanTimer.IsElapsed())
	{
		me->TryToInfiltrate();
		m_spyInfiltrateScanTimer.Start(RandomFloat(10.0f, 20.0f)); // Reschedule infiltrate scan
		if (me->GetState() != this) return;
	}


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
		// FF_LUA_INTEGRATION_TODO: Prioritize Lua objectives before generic hunting
		if (me->GetLuaObjectivePointCount() > 0)
		{
			// Example: Try to find a neutral or own-team objective first.
			// More complex logic would involve checking objective type, status, bot's current task, etc.
			int desiredTeamAffiliation = me->GetTeamNumber();
			if (desiredTeamAffiliation < FF_TEAM_RED) // If bot is unassigned or spectator, look for neutral
			    desiredTeamAffiliation = FF_TEAM_NEUTRAL;

			const CFFBotManager::LuaObjectivePoint* obj = me->GetClosestLuaObjectivePoint(me->GetAbsOrigin(), desiredTeamAffiliation);
			if (!obj && desiredTeamAffiliation != FF_TEAM_NEUTRAL) // If no team objective, try neutral
			{
				obj = me->GetClosestLuaObjectivePoint(me->GetAbsOrigin(), FF_TEAM_NEUTRAL);
			}
            // FF_TODO: Could also check for objectives specifically for the "other" team if the bot's role is to attack.

			if (obj)
			{
				// Use Msg for general server console, PrintIfWatched for bot-specific debug target
				Msg("[FF_BOT_IDLE] Bot %s found Lua objective: %s at (%.f, %.f, %.f), Team: %d\n",
					me->GetPlayerName(), obj->name, obj->position.x, obj->position.y, obj->position.z, obj->teamAffiliation);
				me->PrintIfWatched("Found Lua objective: %s at (%.f, %.f, %.f), Team: %d\n",
					obj->name, obj->position.x, obj->position.y, obj->position.z, obj->teamAffiliation);

				// FF_TODO_OBJECTIVES: This is where a bot would decide to move to an objective,
				// potentially transitioning to a new state like "CaptureObjectiveState" or "MoveToObjectiveState".
				// For now, just demonstrating data access. If we MoveTo, it might spam.
				// Example:
				me->CaptureObjective(obj); // FF_LUA_OBJECTIVES: Transition to CaptureObjectiveState
				return; // Exit IdleState once an objective is chosen
			}
			else
			{
				me->PrintIfWatched("No suitable Lua objectives found nearby or for my team.\n");
			}
		}
		me->Hunt();
	}
}

// OnExit for IdleState is not present in cs_bot_idle.cpp, so not adding one here.
