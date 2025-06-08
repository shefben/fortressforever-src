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
#include "../../ff_gamerules.h" // Changed from cs_gamerules.h
#include "../ff_player.h"    // Added for CFFPlayer
#include "../ff_bot_manager.h" // For TheFFBots() and team definitions


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a potentially far away position.
 */
void MoveToState::OnEnter( CFFBot *me ) // Changed CCSBot to CFFBot
{
	// FF_TODO_WEAPON_STATS: Review IsUsingKnife, IsWellPastSafe, IsHurrying for FF item/weapon logic
	// if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying())
	// {
	// 	me->Walk();
	// }
	// else
	// {
		me->Run();
	// }

	RouteType route;
	// FF_TODO_AI_BEHAVIOR: Determine appropriate RouteType based on FF tasks
	// Original CS logic for bomb/defuse related tasks is removed here.
	// MOVE_TO_LAST_KNOWN_ENEMY_POSITION is a generic task that can remain.
	if (me->GetTask() == CFFBot::MOVE_TO_LAST_KNOWN_ENEMY_POSITION)
	{
		route = FASTEST_ROUTE;
	}
	else
	{
		route = SAFEST_ROUTE;
	}

	me->ComputePath( m_goalPosition, route );

	m_radioedPlan = false;
	m_askedForCover = false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a potentially far away position.
 */
void MoveToState::OnUpdate( CFFBot *me ) // Changed CCSBot to CFFBot
{
	Vector myOrigin = GetCentroid( me );

	if (me->GetTask() == CFFBot::MOVE_TO_LAST_KNOWN_ENEMY_POSITION) // Changed CCSBot to CFFBot
	{
		CFFPlayer *victim = dynamic_cast<CFFPlayer *>( me->GetTaskEntity() ); // Changed CBasePlayer to CFFPlayer
		if (victim == NULL || !victim->IsAlive())
		{
			me->PrintIfWatched( "The enemy I was chasing was killed - giving up.\n" );
			me->Idle();
			return;
		}
	}

	me->UpdateLookAround();

	// FF_TODO_GAME_MECHANIC: Scenario logic needs complete overhaul for FF objectives.
	// The entire CS-specific switch statement is commented out.
	/*
	switch (TheFFBots()->GetScenario()) // Changed TheCSBots to TheFFBots
	{
		case CFFBotManager::SCENARIO_DEFUSE_BOMB: // This would be an FF-specific scenario enum
		{
			// ... (All CS bomb logic removed) ...
			break;
		}
		case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // This would be an FF-specific scenario enum
		{
			// ... (All CS Hostage logic removed) ...
			break;
		}
	}
	*/

	if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // Changed CCSBot to CFFBot
	{
		// reached destination
		// FF_TODO_AI_BEHAVIOR: Adapt task completion logic for FF objectives
		/*
		switch( me->GetTask() )
		{
			// case CFFBot::PLANT_BOMB: // CS-specific
			// 	if (me->IsAtBombsite() && me->HasC4()) // CS-specific
			// 	{
			// 		me->PlantBomb(); // CS-specific
			// 		return;
			// 	}
			// 	break;

			case CFFBot::MOVE_TO_LAST_KNOWN_ENEMY_POSITION: // Generic enough
			{
				CFFPlayer *victim = static_cast<CFFPlayer *>( me->GetTaskEntity() ); // Changed
				if (victim && victim->IsAlive())
				{
					// FF_TODO_AI_BEHAVIOR: Adapt chatter
					// BotStatement *say = new BotStatement( me->GetChatter(), REPORT_ENEMY_LOST, 8.0f );
					// say->AppendPhrase( TheBotPhrases->GetPhrase( "LostEnemy" ) );
					// say->SetStartTime( gpGlobals->curtime + RandomFloat( 3.0f, 5.0f ) );
					// me->GetChatter()->AddStatement( say );
				}
				break;
			}
		}
		*/

		me->Idle();
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void MoveToState::OnExit( CFFBot *me ) // Changed CCSBot to CFFBot
{
	me->Run();
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // Changed CCSBot to CFFBot
}
