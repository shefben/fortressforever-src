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
	m_demomanStickyTrapTimer.Start(RandomFloat(15.0f, 25.0f)); // Initialize Demoman sticky trap timer
	m_assessFollowTimer.Start(RandomFloat(5.0f, 10.0f)); // Initialize follow assessment timer
	m_defendObjectiveScanTimer.Start(RandomFloat(7.0f, 12.0f)); // Initialize defend objective scan timer


	// FF_TODO_WEAPON_STATS: Review if IsUsingKnife, IsWellPastSafe, IsHurrying concepts translate directly for FF item/weapon logic
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
	// Check if carrying enemy flag first - this is high priority
	if (me->HasEnemyFlag())
	{
		const CFFBotManager::LuaObjectivePoint* pBestCapturePoint = NULL;
		float flBestDistSq = FLT_MAX;
		const CFFInfoScript* carriedFlagEntity = me->GetCarriedFlag(); // Get the entity of the flag we are carrying

		const CUtlVector<CFFBotManager::LuaObjectivePoint>& objectives = me->GetAllLuaObjectivePoints();
		for (int i = 0; i < objectives.Count(); ++i)
		{
			const CFFBotManager::LuaObjectivePoint& point = objectives[i];
			// Must be a capture point (type 1), active, and belong to our team (or neutral if that's how caps work)
			if (point.type == 1 && point.m_state == LUA_OBJECTIVE_ACTIVE &&
				(point.teamAffiliation == me->GetTeamNumber() || point.teamAffiliation == FF_TEAM_NEUTRAL))
			{
				// FF_LUA_TODO: Ensure this capture point is valid for the flag we are carrying (e.g. some maps have multiple flags/caps)
				// For now, any friendly/neutral active capture point will do.
				float distSq = (point.position - me->GetAbsOrigin()).LengthSqr();
				if (distSq < flBestDistSq)
				{
					pBestCapturePoint = &point;
					flBestDistSq = distSq;
				}
			}
		}

		if (pBestCapturePoint)
		{
			me->PrintIfWatched("IdleState: Carrying enemy flag '%s', moving to capture point '%s'!\n",
				carriedFlagEntity ? (carriedFlagEntity->GetEntityNameAsCStr() ? carriedFlagEntity->GetEntityNameAsCStr() : "unnamed_flag") : "unknown_flag",
				pBestCapturePoint->name);
			me->CarryFlagToCapturePoint(pBestCapturePoint);
			return; // Exit IdleState
		}
		else
		{
			me->PrintIfWatched("IdleState: Carrying enemy flag, but no suitable capture point found. Hunting.\n");
			// No capture point found, maybe just hunt or hold position.
			me->Hunt(); // Fallback behavior
			return;
		}
	}

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
		// FF_TODO_CLASS_ENGINEER: Add more sophisticated logic for when/where to build sentries.
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
		// FF_TODO_CLASS_ENGINEER: Add more sophisticated logic for when/where to build dispensers.
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

	// FF_TODO_CLASS_ENGINEER: Add logic for Teleporter building in IdleState
	// if (me->IsEngineer() && m_engineerTeleporterScanTimer.IsElapsed()) // Need a new timer m_engineerTeleporterScanTimer
	// {
	//    if (me->GetTeleporterEntranceLevel() == 0)
	//    {
	//        // FF_TODO_AI_BEHAVIOR: Find good spot for tele entrance (e.g., near spawn, secure area)
	//        // Vector teleEntrancePos = FindGoodTeleporterEntranceSpot();
	//        // me->TryToBuildTeleporterEntrance(&teleEntrancePos);
	//        // return;
	//    }
	//    else if (me->GetTeleporterExitLevel() == 0)
	//    {
	//        // FF_TODO_AI_BEHAVIOR: Find good spot for tele exit (e.g., near active objective, forward base)
	//        // Vector teleExitPos = FindGoodTeleporterExitSpot();
	//        // me->TryToBuildTeleporterExit(&teleExitPos);
	//        // return;
	//    }
	//    // m_engineerTeleporterScanTimer.Start(RandomFloat(15.0f, 25.0f));
	// }
	// if (me->GetState() != this) return;


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

	// Demoman Behavior: Periodically consider laying a sticky trap
	if (me->IsDemoman() && me->m_deployedStickiesCount < CFFBot::MAX_BOT_STICKIES && m_demomanStickyTrapTimer.IsElapsed())
	{
		// FF_TODO_CLASS_DEMOMAN: More sophisticated logic for when/where to lay traps.
		// Example: Near a defended objective, or a known chokepoint.
		// For now, if near a friendly Lua objective that the bot's team owns.
		const CFFBotManager::LuaObjectivePoint* obj = me->GetClosestLuaObjectivePoint(me->GetAbsOrigin(), me->GetTeamNumber());
		if (obj && obj->currentOwnerTeam == me->GetTeamNumber() && (me->GetAbsOrigin() - obj->position).IsLengthSqr() < Square(1000.0f)) // Within 1000 units of friendly objective
		{
			// Try to find a chokepoint near this objective, or just use the objective position as the trap target.
			// For simplicity, target the objective position itself for now.
			Vector trapTargetPos = obj->position;
			// FF_TODO_AI_BEHAVIOR: Find actual chokepoint near 'obj->position' or visible enemy path.
			// For now, just pick a point slightly offset from the objective center.
			trapTargetPos.x += RandomFloat(-50.f, 50.f);
			trapTargetPos.y += RandomFloat(-50.f, 50.f);
			// Ensure Z is on the ground, or use nav mesh to find a valid ground position.
			CNavArea *targetArea = TheNavMesh->GetNearestNavArea(trapTargetPos, true, 150.0f);
			if (targetArea)
			{
				trapTargetPos = targetArea->GetCenter(); // Snap to nav mesh center
				me->PrintIfWatched("Demoman: Decided to lay sticky trap near objective '%s' at (%.1f, %.1f, %.1f).\n",
					obj->name, trapTargetPos.x, trapTargetPos.y, trapTargetPos.z);
				me->StartLayingStickyTrap(trapTargetPos); // This will change state
				return;
			}
		}
		m_demomanStickyTrapTimer.Start(RandomFloat(20.0f, 40.0f)); // Reschedule
		if (me->GetState() != this) return; // Check if state changed due to other logic before sticky trap decision
	}

	// Periodically assess if the bot should follow a nearby teammate
	if (m_assessFollowTimer.IsElapsed())
	{
		// FF_TODO_AI_BEHAVIOR: Add more conditions here, e.g., don't follow if currently defending a point,
		// or if specific class (Sniper/Engineer) has important solo tasks.
		// For now, any class might try to follow if idle and other conditions aren't met.
		bool shouldConsiderFollowing = true;
		if (me->IsEngineer() && (me->HasSentry() || me->HasDispenser())) // Engineer with nest might not follow
		{
			shouldConsiderFollowing = false;
		}
		if (me->IsSniper() && me->IsSniping()) // Sniper actively sniping shouldn't follow
		{
			shouldConsiderFollowing = false;
		}
		// FF_TODO_GAME_MECHANIC: Could also be triggered by a "follow me" radio command if implemented.

		if (shouldConsiderFollowing)
		{
			// Only try to follow if not already in an important state (like attacking, building, healing etc.)
			// The IdleState itself is the "lowest priority" state, so if we're here, we're generally not busy.
			// However, GetTask() might still be something other than SEEK_AND_DESTROY if a high-level order was given.
			if (me->GetTask() == CFFBot::SEEK_AND_DESTROY || me->GetTask() == CFFBot::HOLD_POSITION) // Only consider following if not on a specific task
			{
				me->TryToFollowNearestTeammate();
				if (me->GetState() != this) return; // State changed to FollowTeammateState or another state
			}
		}
		m_assessFollowTimer.Start(RandomFloat(10.0f, 20.0f)); // Reschedule follow assessment
	}


	if (me->GetLastKnownArea() == NULL && me->StayOnNavMesh() == false)
		return;

	// If bot is stuck, jump
	// This was missing from the previous merge, re-adding it here.
	// Standard behavior for any bot that's stuck.
	if (me->IsStuck())
	{
		me->Jump(); // Standard jump first

		// If Scout and stuck, try a double jump as well
		if (me->IsScout())
		{
			// FF_TODO_CLASS_SCOUT: More advanced logic would be to check if the target is on a higher ledge that a double jump might reach.
			me->TryDoubleJump();
		}
	}

	if (cv_bot_zombie.GetBool())
	{
		me->ResetStuckMonitor();
		return;
	}

	// FF_TODO_WEAPON_STATS: "Safe time" weapon equip logic needs FF item/weapon specifics
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
		// FF_TODO_AI_BEHAVIOR: Logic for end-of-round objectives if any (e.g. last flag cap attempt)
		// if (me->GetHostageEscortCount()) // FF No Hostages
		// {
		// }
		me->Hunt();
		return;
	}

	// const float defenseSniperCampChance = 75.0f; // FF_TODO_AI_BEHAVIOR: Tune for FF balance
	// const float offenseSniperCampChance = 10.0f; // FF_TODO_AI_BEHAVIOR: Tune for FF balance

	if (me->IsFollowing())
	{
		me->ContinueFollowing();
		return;
	}

	// FF_TODO_GAME_MECHANIC: Major scenario logic overhaul needed here.
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
			// FF_TODO_AI_BEHAVIOR: Implement logic for FF's actual game modes.
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
			// FF_TODO_AI_BEHAVIOR: Ensure OtherTeam and GetRandomSpawn work with FF team definitions correctly.
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
		// Before generic hunting, check if we should proactively defend a friendly objective
		if (m_defendObjectiveScanTimer.IsElapsed())
		{
			m_defendObjectiveScanTimer.Start(RandomFloat(10.0f, 20.0f)); // Reschedule

			// FF_TODO_AI_BEHAVIOR: Add more nuanced conditions for defending (e.g., class roles, game state)
			// For now, any bot might consider defending if idle.
			bool shouldConsiderDefending = true;
			if (me->IsScout() || me->IsSpy()) // Example: Scouts and Spies might be less inclined to static defense
			{
				// shouldConsiderDefending = false;
			}

			if (shouldConsiderDefending)
			{
				const CFFBotManager::LuaObjectivePoint* pBestFriendlyObjectiveToDefend = NULL;
				float flBestDistSq = FLT_MAX;

				const CUtlVector<CFFBotManager::LuaObjectivePoint>& objectives = me->GetAllLuaObjectivePoints();
				for (int i = 0; i < objectives.Count(); ++i)
				{
					const CFFBotManager::LuaObjectivePoint& point = objectives[i];
					if (point.m_state == LUA_OBJECTIVE_ACTIVE && point.currentOwnerTeam == me->GetTeamNumber())
					{
						// Optionally, only defend certain types of objectives (e.g., type 1 control points)
						// if (point.type != 1) continue;

						// Don't defend if already defending this one (or a task for it)
						if (me->GetTask() == CFFBot::TaskType::DEFEND_LUA_OBJECTIVE && me->GetTaskEntity() == point.m_entity.Get())
						{
							continue;
						}

						float distSq = (point.position - me->GetAbsOrigin()).LengthSqr();
						if (distSq < flBestDistSq)
						{
							pBestFriendlyObjectiveToDefend = &point;
							flBestDistSq = distSq;
						}
					}
				}

				if (pBestFriendlyObjectiveToDefend)
				{
					me->PrintIfWatched("IdleState: Found friendly objective '%s' to defend. Transitioning to DefendObjectiveState.\n", pBestFriendlyObjectiveToDefend->name);
					me->DefendObjective(pBestFriendlyObjectiveToDefend);
					return; // Exit IdleState
				}
			}
		}

		// FF_TODO_LUA: Prioritize Lua objectives before generic hunting
		if (me->GetLuaObjectivePointCount() > 0)
		{
			const CFFBotManager::LuaObjectivePoint* pBestObjective = NULL;
			float flBestObjectiveDistSq = FLT_MAX;

			const CUtlVector<CFFBotManager::LuaObjectivePoint>& objectives = me->GetAllLuaObjectivePoints();
			for (int i = 0; i < objectives.Count(); ++i)
			{
				const CFFBotManager::LuaObjectivePoint& point = objectives[i];

				// Primary filter: Only consider objectives that are currently ACTIVE or DROPPED (for flags).
				if (point.m_state != LUA_OBJECTIVE_ACTIVE && point.m_state != LUA_OBJECTIVE_DROPPED)
					continue;

				// Decision to Capture: Type 1 (ControlPoint/FlagGoal) - if not carrying a flag
				if (point.type == 1 && point.m_state == LUA_OBJECTIVE_ACTIVE)
				{
					bool bTargetObjective = false;
					if (me->IsScout())
					{
						// Scouts are more aggressive and will go for neutral or enemy-controlled points.
						if (point.currentOwnerTeam != me->GetTeamNumber())
						{
							bTargetObjective = true;
						}
					}
					else // Other classes might be more conservative (e.g., only go if enemy-controlled, or different logic)
					{
						if (point.currentOwnerTeam != me->GetTeamNumber() && point.currentOwnerTeam != FF_TEAM_NEUTRAL) // Example: Non-scouts might only go for clearly enemy points
						{
							bTargetObjective = true;
						}
						// FF_TODO_AI_BEHAVIOR: More sophisticated "should I capture this now?" logic for non-Scouts.
					}

					if (bTargetObjective)
					{
						float distSq = (point.position - me->GetAbsOrigin()).LengthSqr();
						// FF_TODO_AI_BEHAVIOR: Add more advanced prioritization (e.g., fewer defenders, importance) for all classes.
						if (distSq < flBestObjectiveDistSq)
						{
							pBestObjective = &point;
							flBestObjectiveDistSq = distSq;
						}
					}
				}
				// Decision to Pickup Flag: Type 2 (Item_Flag) - if not carrying a flag
				else if (point.type == 2 && (point.m_state == LUA_OBJECTIVE_ACTIVE || point.m_state == LUA_OBJECTIVE_DROPPED))
				{
					// Target enemy flags at their base (ACTIVE) or any dropped flag (DROPPED + neutral owner)
					// or our own flag if it's dropped (DROPPED + neutral owner + our teamAffiliation)
					bool bTargetFlag = false;
					if (point.teamAffiliation != me->GetTeamNumber() && point.currentOwnerTeam == FF_TEAM_NEUTRAL) // Enemy flag (at base or dropped by enemy/us)
					{
						bTargetFlag = true;
						me->PrintIfWatched("IdleState: Considering enemy flag '%s' (state: %d, owner: %d).\n", point.name, point.m_state, point.currentOwnerTeam);
					}
					// FF_TODO_AI_BEHAVIOR: Logic for recovering own team's dropped flag.
					// else if (point.teamAffiliation == me->GetTeamNumber() && point.m_state == LUA_OBJECTIVE_DROPPED && point.currentOwnerTeam == FF_TEAM_NEUTRAL)
					// {
					//     bTargetFlag = true; // Our flag is dropped
					//     me->PrintIfWatched("IdleState: Considering to recover our dropped flag '%s'.\n", point.name);
					// }

					if (bTargetFlag)
					{
						float distSq = (point.position - me->GetAbsOrigin()).LengthSqr();
						// FF_TODO_AI_BEHAVIOR: Flags are usually high priority. Could override other objectives unless very far.
						if (distSq < flBestObjectiveDistSq) // Simple distance check for now
						{
							pBestObjective = &point;
							flBestObjectiveDistSq = distSq;
						}
					}
				}
			}

			if (pBestObjective)
			{
				me->PrintIfWatched("IdleState: Found active, capturable Lua objective: %s (type %d) at (%.f, %.f, %.f), Owner: %d. Moving to capture.\n",
					pBestObjective->name, pBestObjective->type, pBestObjective->position.x, pBestObjective->position.y, pBestObjective->position.z, pBestObjective->currentOwnerTeam);
				me->CaptureObjective(pBestObjective);
				return; // Exit IdleState once an objective is chosen
			}
			else
			{
				me->PrintIfWatched("IdleState: No suitable active, capturable Lua objectives found.\n");
			}
		}
		me->Hunt();
	}
}

// OnExit for IdleState is not present in cs_bot_idle.cpp, so not adding one here.

[end of mp/src/game/server/ff/bot/states/ff_bot_idle.cpp]
