//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_move_to.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../../shared/ff/ff_gamerules.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"
#include "../ff_bot_chatter.h" // For BotStatement, TheBotPhrases
#include "../nav_mesh.h"       // For TheNavMesh, Place

// Local bot utility headers
#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"       // For PrintIfWatched


// TODO: cs_simple_hostage.h is CS-specific. Remove or replace if FF has hostages.
// #include "cs_simple_hostage.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a potentially far away position.
 */
void MoveToState::OnEnter( CFFBot *me )
{
	if (!me) return; // Null check

	// TODO_FF: Knife logic
	if (me->IsUsingKnife() && me->IsWellPastSafe() && !me->IsHurrying())
	{
		me->Walk();
	}
	else
	{
		me->Run();
	}

	RouteType route;
	// TODO_FF: Update TaskType enums for FF
	switch (me->GetTask())
	{
		case CFFBot::BOT_TASK_FIND_TICKING_BOMB:  // CS Specific
		case CFFBot::BOT_TASK_DEFUSE_BOMB:         // CS Specific
		case CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION:
			route = FASTEST_ROUTE;
			break;

		default:
			route = SAFEST_ROUTE;
			break;
	}

	me->ComputePath( m_goalPosition, route );
	m_radioedPlan = false;
	m_askedForCover = false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Move to a potentially far away position.
 */
void MoveToState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !me->GetChatter() || !TheNavMesh) return; // Null checks

	Vector myOrigin = GetCentroid( me );

	// TODO_FF: Update TaskType enums for FF
	if (me->GetTask() == CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION)
	{
		CBasePlayer *victim = static_cast<CBasePlayer *>( me->GetTaskEntity() );
		if (victim == NULL || !victim->IsAlive())
		{
			PrintIfWatched(me, "The enemy I was chasing was killed - giving up.\n" ); // Updated PrintIfWatched
			me->Idle();
			return;
		}
	}

	me->UpdateLookAround();

	//
	// Scenario logic
	// TODO_FF: This entire switch needs to be adapted for FF game modes
	//
	switch (TheFFBots()->GetScenario())
	{
		case CFFBotManager::SCENARIO_DEFUSE_BOMB: // CS Specific
		{
			// ... (CS Bomb logic removed for brevity) ...
			break;
		}
		case CFFBotManager::SCENARIO_RESCUE_HOSTAGES: // CS Specific
		{
			// ... (CS Hostage logic removed for brevity) ...
			break;
		}
	}


	if (me->UpdatePathMovement() != CFFBot::PROGRESSING) // PROGRESSING from PathResult enum
	{
		// reached destination
		// TODO_FF: Update TaskType enums for FF
		switch( me->GetTask() )
		{
			case CFFBot::BOT_TASK_PLANT_BOMB: // CS Specific
				// if (me->IsAtBombsite() && me->HasC4()) // CS Specific
				// { me->PlantBomb(); return; }
				break;

			case CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION:
			{
				CBasePlayer *victim = static_cast<CBasePlayer *>( me->GetTaskEntity() );
				if (victim && victim->IsAlive())
				{
					// BotStatement *say = new BotStatement( me->GetChatter(), REPORT_ENEMY_LOST, 8.0f ); // REPORT_ENEMY_LOST enum
					// if (TheBotPhrases) say->AppendPhrase( TheBotPhrases->GetPhrase( "LostEnemy" ) ); // Null check TheBotPhrases
					// say->SetStartTime( gpGlobals->curtime + RandomFloat( 3.0f, 5.0f ) );
					// me->GetChatter()->AddStatement( say );
				}
				break;
			}
		}
		me->Idle();
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void MoveToState::OnExit( CFFBot *me )
{
	if (!me) return;
	me->Run();
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE ); // DispositionType enum
}
