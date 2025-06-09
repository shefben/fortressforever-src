//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
// #include "cs_simple_hostage.h" // Removed CS-specific include
#include "ff_bot.h"
#include "../ff_bot_manager.h" // Added for TheFFBots, CFFBotManager::SCENARIO_DEFUSE_BOMB etc.
#include "../ff_gamestate.h"   // Added for FFGameState::UNKNOWN_ZONE
#include "../nav_mesh.h"       // Added for TheNavMesh
#include "../nav_pathfind.h"   // Added for PathCost
#include "../bot_constants.h"  // Added for BotTaskType, DispositionType, TEAM_ID_RED etc.
#include "../bot_profile.h"    // Added for GetProfile()

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// range for snipers to select a hiding spot
const float sniperHideRange = 2000.0f;

//--------------------------------------------------------------------------------------------------------------
/**
 * The Idle state.
 * We never stay in the Idle state - it is a "home base" for the state machine that
 * does various checks to determine what we should do next.
 */
void IdleState::OnEnter( CFFBot *me )
{
	me->DestroyPath();
	me->SetBotEnemy( NULL );

	// lurking death
	if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying())
		me->Walk();

	//
	// Since Idle assigns tasks, we assume that coming back to Idle means our task is complete
	//
	me->SetTask( BOT_TASK_SEEK_AND_DESTROY ); // Use global BotTaskType
	me->SetDisposition( ENGAGE_AND_INVESTIGATE ); // Use global DispositionType
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Determine what we should do next
 */
void IdleState::OnUpdate( CFFBot *me )
{
	// all other states assume GetLastKnownArea() is valid, ensure that it is
	if (me->GetLastKnownArea() == NULL && me->StayOnNavMesh() == false)
		return;

	// zombies never leave the Idle state
	if (cv_bot_zombie.GetBool())
	{
		me->ResetStuckMonitor();
		return;
	}

	// if we are in the early "safe" time, grab a knife or grenade
	if (me->IsSafe())
	{
		// if we have a grenade, use it
		if (!me->EquipGrenade()) // TODO_FF: Grenade logic
		{
			// high-skill bots run with the knife
			// TODO_FF: WEAPON_SCOUT is CS-specific. FF might not have a direct equivalent or this logic might change.
			// For now, removing the IsUsing(WEAPON_SCOUT) check as it's CS-specific item logic.
			if (me->GetProfile()->GetSkill() > 0.33f /* && !me->IsUsing( FF_WEAPON_YOUR_EQUIVALENT_FOR_SCOUT_SPEED_CHECK ) */)
			{
				me->EquipKnife();
			}
		}
	}

	// if round is over, hunt
	if (me->GetGameState()->IsRoundOver())
	{
		// if we are escorting hostages, try to get to the rescue zone // TODO_FF: CS-specific (Hostages)
		if (me->GetHostageEscortCount())
		{
			const CFFBotManager::Zone *zone = TheFFBots()->GetClosestZone( me->GetLastKnownArea(), PathCost( me, FASTEST_ROUTE ) );
			const Vector *zonePos = TheFFBots()->GetRandomPositionInZone( zone );

			if (zonePos)
			{
				me->SetTask( BOT_TASK_RESCUE_HOSTAGES ); // Use global BotTaskType // TODO_FF: CS-specific task
				me->Run();
				me->SetDisposition( SELF_DEFENSE ); // Use global DispositionType
				me->MoveTo( *zonePos, FASTEST_ROUTE );
				me->PrintIfWatched( "Trying to rescue hostages at the end of the round\n" );
				return;
			}
		}

		me->Hunt();
		return;
	}

	const float defenseSniperCampChance = 75.0f; // TODO_FF: Sniper logic
	const float offenseSniperCampChance = 10.0f;

	// if we were following someone, continue following them
	if (me->IsFollowing())
	{
		me->ContinueFollowing();
		return;
	}

	//
	// Scenario logic
	//
	switch (TheFFBots()->GetScenario())
	{
		//======================================================================================================
		//======================================================================================================
		// TODO_FF: The entire switch statement below is CS-specific scenario logic (Bomb, VIP, Hostages).
		// It needs to be replaced with FF-specific scenario logic (CTF, CP, AD, etc.)
		// For this refactoring pass, the main goal is to fix class names, enums, and remove chatter.
		// Placeholder team definitions (TEAM_ATTACKERS_FF, TEAM_DEFENDERS_FF) are used for illustration.
		// Actual FF team logic will depend on game rules.
		case CFFBotManager::SCENARIO_DEFUSE_BOMB: // TODO_FF: CS Specific Scenario
		{
			// if this is a bomb game and we have the bomb, go plant it
			if (me->GetTeamNumber() == TEAM_ID_RED) // Example: TEAM_ATTACKERS_FF (was TEAM_TERRORIST)
			{
				if (me->GetGameState()->IsBombPlanted()) // TODO_FF: CS Specific GameState check
				{
					if (me->GetGameState()->GetPlantedBombsite() != FFGameState::UNKNOWN_ZONE) // Use UNKNOWN_ZONE
					{
						// T's always know where the bomb is - go defend it
						const CFFBotManager::Zone *zone = TheFFBots()->GetZone( me->GetGameState()->GetPlantedBombsite() );
						if (zone)
						{
							me->SetTask( BOT_TASK_GUARD_TICKING_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task

							Place place = TheNavMesh->GetPlace( zone->m_center );
							if (place != UNDEFINED_PLACE) // UNDEFINED_PLACE from bot_constants.h
							{
								// pick a random hiding spot in this place
								const Vector *spot = FindRandomHidingSpot( me, place, me->IsSniper() ); // TODO_FF: CS Specific (IsSniper)
								if (spot)
								{
									me->Hide( *spot );
									return;
								}
							}

							// hide nearby
							me->Hide( TheNavMesh->GetNearestNavArea( zone->m_center ) );
							return;
						}
					}
					else
					{
						// ask our teammates where the bomb is
						// me->GetChatter()->RequestBombLocation(); // Chatter removed

						// we dont know where the bomb is - we must search the bombsites
						int zoneIndex = me->GetGameState()->GetNextBombsiteToSearch();

						// move to bombsite - if we reach it, we'll update its cleared status, causing us to select another
						const Vector *pos = TheFFBots()->GetRandomPositionInZone( TheFFBots()->GetZone( zoneIndex ) );
						if (pos)
						{
							me->SetTask( BOT_TASK_FIND_TICKING_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
							me->MoveTo( *pos );
							return;
						}
					}
				}
				else if (me->HasC4()) // TODO_FF: CS Specific (HasC4)
				{
					// if we're at a bomb site, plant the bomb
					if (me->IsAtBombsite()) // TODO_FF: CS Specific (IsAtBombsite)
					{
						// plant it
						me->SetTask( BOT_TASK_PLANT_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
						me->PlantBomb(); // TODO_FF: CS Specific CFFBot method

						// radio to the team
						// me->GetChatter()->PlantingTheBomb( me->GetPlace() ); // Chatter removed

						return;
					}
					else if (TheFFBots()->IsTimeToPlantBomb()) // TODO_FF: CS Specific
					{
						// move to the closest bomb site
						const CFFBotManager::Zone *zone = TheFFBots()->GetClosestZone( me->GetLastKnownArea(), PathCost( me ) );
						if (zone)
						{
							// pick a random spot within the bomb zone
							const Vector *pos = TheFFBots()->GetRandomPositionInZone( zone );
							if (pos)
							{
								// move to bombsite
								me->SetTask( BOT_TASK_PLANT_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
								me->Run();
								me->MoveTo( *pos );

								return;
							}
						}
					}
				}
				else
				{
					// at the start of the round, we may decide to defend "initial encounter" areas
					// where we will first meet the enemy rush
					if (me->IsSafe())
					{
						float defendRushChance = -17.0f * (me->GetMorale() - 2);

						if (me->IsSniper() || RandomFloat( 0.0f, 100.0f ) < defendRushChance) // TODO_FF: CS Specific (IsSniper)
						{
							if (me->MoveToInitialEncounter())
							{
								me->PrintIfWatched( "I'm guarding an initial encounter area\n" );
								me->SetTask( BOT_TASK_GUARD_INITIAL_ENCOUNTER ); // Use global BotTaskType
								me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
								return;
							}
						}
					}

					// small chance of sniper camping on offense, if we aren't carrying the bomb
					if (me->GetFriendsRemaining() && me->IsSniper() && RandomFloat( 0, 100.0f ) < offenseSniperCampChance) // TODO_FF: CS Specific (IsSniper)
					{
						me->SetTask( BOT_TASK_MOVE_TO_SNIPER_SPOT ); // Use global BotTaskType // TODO_FF: CS Specific Task
						me->Hide( me->GetLastKnownArea(), RandomFloat( 10.0f, 30.0f ), sniperHideRange );
						me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
						me->PrintIfWatched( "Sniping!\n" );
						return;
					}

					// if the bomb is loose (on the ground), go get it
					if (me->NoticeLooseBomb()) // TODO_FF: CS Specific (NoticeLooseBomb)
					{
						me->FetchBomb(); // TODO_FF: CS Specific CFFBot method
						return;
					}

					// if bomb has been planted, and we hear it, move to a hiding spot near the bomb and guard it
					if (!me->IsRogue() && me->GetGameState()->IsBombPlanted() && me->GetGameState()->GetBombPosition()) // TODO_FF: CS Specific
					{
						const Vector *bombPos = me->GetGameState()->GetBombPosition();

						if (bombPos)
						{
							me->SetTask( BOT_TASK_GUARD_TICKING_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
							me->Hide( TheNavMesh->GetNearestNavArea( *bombPos ) );
							return;
						}
					}
				}
			}
			else	// Example: TEAM_DEFENDERS_FF (was TEAM_CT)
			{
				if (me->GetGameState()->IsBombPlanted()) // TODO_FF: CS Specific
				{
					// if the bomb has been planted, attempt to defuse it
					const Vector *bombPos = me->GetGameState()->GetBombPosition();
					if (bombPos)
					{
						// if someone is defusing the bomb, guard them
						if (TheFFBots()->GetBombDefuser()) // TODO_FF: CS Specific
						{
							if (!me->IsRogue())
							{
								me->SetTask( BOT_TASK_GUARD_BOMB_DEFUSER ); // Use global BotTaskType // TODO_FF: CS Specific Task
								me->Hide( TheNavMesh->GetNearestNavArea( *bombPos ) );
								return;
							}
						}
						else if (me->IsDoingScenario())
						{
							// move to the bomb and defuse it
							me->SetTask( BOT_TASK_DEFUSE_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
							me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
							me->MoveTo( *bombPos );
							return;
						}
						else
						{
							// we're not allowed to defuse, guard the bomb zone
							me->SetTask( BOT_TASK_GUARD_BOMB_ZONE ); // Use global BotTaskType // TODO_FF: CS Specific Task
							me->Hide( TheNavMesh->GetNearestNavArea( *bombPos ) );
							me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
							return;
						}
					}
					else if (me->GetGameState()->GetPlantedBombsite() != FFGameState::UNKNOWN_ZONE) // Use UNKNOWN_ZONE
					{
						// we know which bombsite, but not exactly where the bomb is, go there
						const CFFBotManager::Zone *zone = TheFFBots()->GetZone( me->GetGameState()->GetPlantedBombsite() );
						if (zone)
						{
							if (me->IsDoingScenario())
							{
								me->SetTask( BOT_TASK_DEFUSE_BOMB ); // Use global BotTaskType // TODO_FF: CS Specific Task
								me->MoveTo( zone->m_center );
								me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
								return;
							}
							else
							{
								// we're not allowed to defuse, guard the bomb zone
								me->SetTask( BOT_TASK_GUARD_BOMB_ZONE ); // Use global BotTaskType // TODO_FF: CS Specific Task
								me->Hide( TheNavMesh->GetNearestNavArea( zone->m_center ) );
								me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
								return;
							}
						}
					}
					else
					{
						// we dont know where the bomb is - we must search the bombsites
						// ... (CS specific bombsite search logic) ...
						// Example:
						// const CFFBotManager::Zone *zone = TheFFBots()->GetClosestZone( me->GetLastKnownArea(), PathCost( me ) ); // Simplified
						// if (zone) {
						// 	const Vector *pos = TheFFBots()->GetRandomPositionInZone( zone );
						// 	if (pos) {
						// 		me->SetTask( BOT_TASK_FIND_TICKING_BOMB ); // TODO_FF: CS Specific Task
						// 		me->MoveTo( *pos );
						// 		return;
						// 	}
						// }
					}
					AssertMsg( 0, "A CT bot doesn't know what to do while the bomb is planted!\n" ); // TODO_FF: Adjust assert for FF
				}


				// if we have a sniper rifle, we like to camp, whether rogue or not
				if (me->IsSniper() && !me->IsSafe()) // TODO_FF: CS Specific (IsSniper)
				{
					if (RandomFloat( 0, 100 ) <= defenseSniperCampChance)
					{
						// ... (CS specific sniper logic for bomb scenario) ...
						// Example:
						// CNavArea *snipingArea = TheNavMesh->GetRandomArea(); // Highly simplified
						// if (snipingArea) {
						// 	me->SetTask( BOT_TASK_MOVE_TO_SNIPER_SPOT ); // TODO_FF: CS Specific Task
						// 	me->Hide( snipingArea, -1.0, sniperHideRange );
						// 	me->SetDisposition( OPPORTUNITY_FIRE );
						// 	return;
						// }
					}
				}

				// rogues just hunt, unless they want to snipe
				// ... (CS specific rogue/rush/guard logic for bomb scenario) ...
				// Example:
				// if (me->IsRogue() || TheFFBots()->IsDefenseRushing() || me->GetGameState()->IsLooseBombLocationKnown()) { // TODO_FF: CS Specific
				// 	me->Hunt();
				// 	return;
				// }
				// if (me->IsSafe() || me->HasNotSeenEnemyForLongTime()) { /* ... guard logic ... */ }
			}

			break;
		}

		//======================================================================================================
		case CFFBotManager::SCENARIO_ESCORT_VIP: // TODO_FF: CS Specific Scenario
		{
			if (me->GetTeamNumber() == TEAM_ID_RED) // Example: TEAM_ATTACKERS_FF (was TEAM_TERRORIST)
			{
				// ... (CS specific T-side VIP logic, sniper, rogue, guard escape zone) ...
				// Example:
				// if (me->IsSniper() && RandomFloat(0,100) <= defenseSniperCampChance) { /* snipe escape */ }
				// else if (me->IsRogue() || TheFFBots()->IsDefenseRushing()) break; /* hunt */
				// else { /* guard escape zone */ }
			}
			else	// Example: TEAM_DEFENDERS_FF (was TEAM_CT)
			{
				if (me->m_bIsVIP) // TODO_FF: CS Specific (m_bIsVIP)
				{
					// ... (CS specific VIP player logic, move to escape, radio) ...
					// Example:
					// const CFFBotManager::Zone *zone = TheFFBots()->GetClosestZone( me->GetLastKnownArea(), PathCost( me ) );
					// if (zone) {
					// 	const Vector *pos = TheFFBots()->GetRandomPositionInZone( zone );
					// 	if (pos) {
					// 		me->SetTask( BOT_TASK_VIP_ESCAPE ); // TODO_FF: CS Specific Task
					// 		me->MoveTo(*pos);
					// 		// me->SendRadioMessage( RADIO_FF_FOLLOW_ME ); // Radio removed
					// 		return;
					// 	}
					// }
				}
				else
				{
					// ... (CS specific CT bodyguard logic, sniper) ...
				}
			}
			break;
		}

		//======================================================================================================
		case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // TODO_FF: CS Specific Scenario (Hostages)
		{
			if (me->GetTeamNumber() == TEAM_ID_RED) // Example: TEAM_ATTACKERS_FF (was TEAM_TERRORIST)
			{
				// ... (CS specific T-side hostage logic, camp hostages/escape, sniper) ...
			}
			else	// Example: TEAM_DEFENDERS_FF (was TEAM_CT)
			{
				// ... (CS specific CT-side hostage logic, rescue, sniper, guard) ...
				// Example:
				// CHostage* hostage = me->GetGameState()->GetNearestFreeHostage(); // TODO_FF: CS Specific Hostage
				// if (hostage) { /* logic to fetch/rescue */ }
			}
			break;
		}

		default:	// deathmatch (or other FF modes not yet handled)
		{
			// sniping check
			if (me->GetFriendsRemaining() && me->IsSniper() && RandomFloat( 0, 100.0f ) < offenseSniperCampChance) // TODO_FF: CS Specific (IsSniper)
			{
				me->SetTask( BOT_TASK_MOVE_TO_SNIPER_SPOT ); // Use global BotTaskType // TODO_FF: CS Specific Task
				me->Hide( me->GetLastKnownArea(), RandomFloat( 10.0f, 30.0f ), sniperHideRange );
				me->SetDisposition( OPPORTUNITY_FIRE ); // Use global DispositionType
				me->PrintIfWatched( "Sniping!\n" );
				return;
			}
			break;
		}
	}

	// if we have nothing special to do, go hunting for enemies
	me->Hunt();
}
