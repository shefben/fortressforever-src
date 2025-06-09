//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
// #include "ff_bot_state_move_to.h" // .h file for this state might not exist if class def is in ff_bot.h
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"

#include "../bot_constants.h" // For BotTaskType, TEAM_ID_RED, etc.
#include "../bot_profile.h"
#include "../bot_util.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Placeholder for bot speak events
#define BOT_MSG_GOT_ENEMY_FLAG 1001
#define BOT_MSG_SECURED_OUR_FLAG 1002
#define BOT_MSG_MOVING_TO_OBJECTIVE 1003
#define BOT_MSG_CAPTURING_POINT 1004
#define BOT_MSG_DEFENDING_POINT 1005


static int GetEnemyTeamFor(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    // TODO_FF: Add support for more teams if FF has them (Yellow, Green)
    return TEAM_ID_NONE;
}


//--------------------------------------------------------------------------------------------------------------
void MoveToState::OnEnter( CFFBot *me )
{
	if (!me) return;

	me->Run();

	RouteType route = SAFEST_ROUTE;
	switch (me->GetTask())
	{
		case CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION:
        case CFFBot::BOT_TASK_CAPTURE_FLAG:
        case CFFBot::BOT_TASK_RETURN_FLAG:
        case CFFBot::BOT_TASK_CAPTURE_POINT: // Capture point can also be urgent
			route = FASTEST_ROUTE;
			break;
		default:
			route = SAFEST_ROUTE;
			break;
	}

	if (m_goalPosition == vec3_invalid && me->GetTaskEntity()) {
		m_goalPosition = me->GetTaskEntity()->GetAbsOrigin(); // Ensure m_goalPosition is set if task entity exists
	}

	if (m_goalPosition != vec3_invalid) {
		me->ComputePath( m_goalPosition, route );
	} else if (me->GetTaskEntity()) {
		// If goal position is invalid but entity exists, try pathing to entity's current pos
		me->ComputePath( me->GetTaskEntity()->GetAbsOrigin(), route );
	} else {
		me->Warning("MoveToState: No goal position or task entity specified!\n");
		me->Idle(); // No where to go
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void MoveToState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh) return;

	// Check if task entity became invalid
	BotTaskType currentTask = me->GetTask();
	CBaseEntity *pTaskEntity = me->GetTaskEntity();

	if (currentTask == CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION ||
	    currentTask == CFFBot::BOT_TASK_ESCORT_VIP_FF ||
	    currentTask == CFFBot::BOT_TASK_ASSASSINATE_VIP_FF)
	{
		if (pTaskEntity) {
			CFFPlayer *taskPlayer = ToFFPlayer(pTaskEntity);
			if (taskPlayer && !taskPlayer->IsAlive()) {
				me->PrintIfWatched( "MoveToState: Task entity/player %s is no longer alive. Going Idle.\n", taskPlayer->GetPlayerName() );
				me->Idle();
				return;
			}
		} else if (currentTask != CFFBot::BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION) {
            me->PrintIfWatched( "MoveToState: Task %s requires an entity, but it's NULL. Going Idle.\n", me->GetTaskName());
            me->Idle();
            return;
        }
	}

	me->UpdateLookAround();

	if (me->UpdatePathMovement() != CFFBot::PROGRESSING)
	{
        FFGameState *gameState = me->GetGameState();
        if (!gameState) { me->Idle(); return; }

        if (me->GetPathDistanceRemaining() > 0 && me->HasPath() && !me->IsStuck()) {
             return; // Still have a path but not progressing (but not stuck yet) - wait for stuck timer or path success
        }

        me->PrintIfWatched( "MoveToState: Reached destination or path failed for task %s.\n", me->GetTaskName());

        if (currentTask == CFFBot::BOT_TASK_CAPTURE_FLAG)
        {
            if (!pTaskEntity) { me->PrintIfWatched("MoveToState: CAPTURE_FLAG task entity NULL.\n"); me->Idle(); return; }

            int myTeam = me->GetTeamNumber();
            int enemyTeam = GetEnemyTeamFor(myTeam);
            const FFGameState::FF_FlagState *enemyFlagInfo = gameState->GetFlagInfo(enemyTeam);

            if (enemyFlagInfo && enemyFlagInfo->entity.Get() == pTaskEntity)
            {
                enemyFlagInfo = gameState->GetFlagInfo(enemyTeam); // Re-fetch, state might have changed
                if (enemyFlagInfo && enemyFlagInfo->currentState == FF_FLAG_STATE_CARRIED && enemyFlagInfo->carrier.Get() == me)
                {
                    me->Speak(BOT_MSG_GOT_ENEMY_FLAG);
                    me->PrintIfWatched( "MoveToState: Successfully picked up enemy flag.\n");
                    const FFGameState::FF_FlagState* myTeamFlagInfo = gameState->GetFlagInfo(myTeam);
                    if (myTeamFlagInfo && myTeamFlagInfo->entity.IsValid()) {
                        CBaseEntity* pMyCapturePointEntity = myTeamFlagInfo->entity.Get(); // This is our flag stand.
                        if (pMyCapturePointEntity) {
                            me->PrintIfWatched( "MoveToState: New task: Move to capture point (our flag stand).\n");
                            me->SetTask(CFFBot::BOT_TASK_MOVE_TO_OBJECTIVE, pMyCapturePointEntity);
                            me->MoveTo(myTeamFlagInfo->entitySpawnLocation);
                        } else { me->Idle(); }
                    } else { me->Idle(); }
                    return;
                }
                else if (enemyFlagInfo && (enemyFlagInfo->currentState == FF_FLAG_STATE_HOME || enemyFlagInfo->currentState == FF_FLAG_STATE_DROPPED))
                {
                    me->PrintIfWatched( "MoveToState: Enemy flag is still here, trying to 're-touch'.\n");
                    me->GetBodyInterface()->StuckCheck();
                    me->Wait(0.5f);
                    me->ComputePath(m_goalPosition, FASTEST_ROUTE);
                    return;
                }
                else { me->PrintIfWatched( "MoveToState: Enemy flag not in expected state. Going Idle.\n"); me->Idle(); return; }
            }
            else { me->PrintIfWatched( "MoveToState: Task entity/flag info mismatch for CAPTURE_FLAG. Going Idle.\n"); me->Idle(); return; }
        }
        else if (currentTask == CFFBot::BOT_TASK_RETURN_FLAG)
        {
            if (!pTaskEntity) { me->PrintIfWatched("MoveToState: RETURN_FLAG task entity NULL.\n"); me->Idle(); return; }
            const FFGameState::FF_FlagState *myTeamFlagInfo = gameState->GetFlagInfo(me->GetTeamNumber());

            if (myTeamFlagInfo && myTeamFlagInfo->entity.Get() == pTaskEntity)
            {
                myTeamFlagInfo = gameState->GetFlagInfo(me->GetTeamNumber()); // Re-fetch
                if (myTeamFlagInfo && myTeamFlagInfo->currentState == FF_FLAG_STATE_HOME)
                {
                    me->Speak(BOT_MSG_SECURED_OUR_FLAG);
                    me->PrintIfWatched( "MoveToState: Successfully returned our flag.\n");
                    me->Idle(); return;
                }
                else if (myTeamFlagInfo && myTeamFlagInfo->currentState == FF_FLAG_STATE_DROPPED)
                {
                    me->PrintIfWatched( "MoveToState: Our flag is still dropped here, trying to 're-touch'.\n");
                    me->GetBodyInterface()->StuckCheck();
                    me->Wait(0.5f);
                    me->ComputePath(m_goalPosition, FASTEST_ROUTE);
                    return;
                }
                else { me->PrintIfWatched( "MoveToState: Our flag not in expected DROPPED state. Going Idle.\n"); me->Idle(); return; }
            }
            else { me->PrintIfWatched( "MoveToState: Task entity/flag info mismatch for RETURN_FLAG. Going Idle.\n"); me->Idle(); return; }
        }
        else if (currentTask == CFFBot::BOT_TASK_CAPTURE_POINT)
        {
            if (!pTaskEntity) { me->PrintIfWatched("MoveToState: CAPTURE_POINT task entity NULL.\n"); me->Idle(); return; }

            // Reached a Control Point. Transition to CapturePointState.
            me->PrintIfWatched("MoveToState: Reached CP %s. Transitioning to CapturePointState.\n", STRING(pTaskEntity->GetEntityName()));
            me->CapturePoint(pTaskEntity); // This will set the new state
            return;
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
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_move_to.cpp]
