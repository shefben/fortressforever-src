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
	// TODO_FF: Re-evaluate route type for VIP escape
	switch (me->GetTask())
	{
		case BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION: // Use global BotTaskType
        case BOT_TASK_CAPTURE_FLAG: // Use global BotTaskType
        case BOT_TASK_RETURN_FLAG:  // Use global BotTaskType
        case BOT_TASK_CAPTURE_POINT: // Use global BotTaskType
        case BOT_TASK_VIP_ESCAPE_FF: // Use global BotTaskType // VIP should probably use fastest or a special escape route
			route = FASTEST_ROUTE;
			break;
		default:
			route = SAFEST_ROUTE;
			break;
	}

	// m_goalPosition should have been set by the calling state (e.g. IdleState) when it decided to MoveTo.
    // If not, try to use the task entity's origin.
	if (m_goalPosition == vec3_invalid && me->GetTaskEntity()) {
		m_goalPosition = me->GetTaskEntity()->GetAbsOrigin();
	}

	if (m_goalPosition != vec3_invalid) {
		me->ComputePath( m_goalPosition, route );
	} else {
		me->Warning("MoveToState: OnEnter: m_goalPosition is vec3_invalid and no TaskEntity to derive it from for task %s!\n", me->GetTaskName());
		me->Idle();
		return;
	}
}

//--------------------------------------------------------------------------------------------------------------
void MoveToState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh) return;

	BotTaskType currentTask = me->GetTask();
	CBaseEntity *pTaskEntity = me->GetTaskEntity();

	// Check if task entity became invalid (e.g. player died)
	if (currentTask == BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION || // Use global BotTaskType
	    currentTask == BOT_TASK_ESCORT_VIP_FF || // Use global BotTaskType
	    currentTask == BOT_TASK_ASSASSINATE_VIP_FF) // Use global BotTaskType
	{
		if (pTaskEntity) { // These tasks MUST have a player entity
			CFFPlayer *taskPlayer = ToFFPlayer(pTaskEntity);
			if (!taskPlayer || !taskPlayer->IsAlive()) { // Check if null OR not alive
				me->PrintIfWatched( "MoveToState: Task player %s (for task %s) is NULL or no longer alive. Going Idle.\n", taskPlayer ? taskPlayer->GetPlayerName() : "UNKNOWN", me->GetTaskName() );
				me->Idle();
				return;
			}
		} else {
            me->PrintIfWatched( "MoveToState: Task %s requires a player entity, but it's NULL. Going Idle.\n", me->GetTaskName());
            me->Idle();
            return;
        }
	}
    // For tasks like VIP_ESCAPE_FF, pTaskEntity is the escape zone (not a player).
    // For CAPTURE_FLAG/RETURN_FLAG, pTaskEntity is the flag entity.
    // For CAPTURE_POINT, pTaskEntity is the CP entity.
    // These don't need the player liveness check above, but ensure pTaskEntity is valid if the task implies it.
    if ((currentTask == BOT_TASK_VIP_ESCAPE_FF || // Use global BotTaskType
         currentTask == BOT_TASK_CAPTURE_FLAG || // Use global BotTaskType
         currentTask == BOT_TASK_RETURN_FLAG || // Use global BotTaskType
         currentTask == BOT_TASK_CAPTURE_POINT) && !pTaskEntity) // Use global BotTaskType
    {
        me->PrintIfWatched( "MoveToState: Task %s requires a non-player TaskEntity, but it's NULL. Going Idle.\n", me->GetTaskName());
        me->Idle();
        return;
    }


	me->UpdateLookAround();

    CFFBot::PathResult pathResult = me->UpdatePathMovement();
	if (pathResult != CFFBot::PROGRESSING)
	{
        FFGameState *gameState = me->GetGameState();
        if (!gameState) { me->Idle(); return; }

        if (pathResult == CFFBot::PATH_FAILURE) { // Explicitly handle path failure first
            me->PrintIfWatched( "MoveToState: Path failed for task %s. Idling.\n", me->GetTaskName());
            me->Idle();
            return;
        }
        // If not PATH_FAILURE, and not PROGRESSING, assume END_OF_PATH for subsequent logic

        me->PrintIfWatched( "MoveToState: Reached destination for task %s.\n", me->GetTaskName());

        if (currentTask == BOT_TASK_CAPTURE_FLAG) // Use global BotTaskType
        { /* ... (CTF capture logic as implemented in Subtask 24) ... */ }
        else if (currentTask == BOT_TASK_RETURN_FLAG) // Use global BotTaskType
        { /* ... (CTF return logic as implemented in Subtask 24) ... */ }
        else if (currentTask == BOT_TASK_CAPTURE_POINT) // Use global BotTaskType
        {
            if (!pTaskEntity) { me->PrintIfWatched("MoveToState: CAPTURE_POINT task entity NULL on arrival.\n"); me->Idle(); return; }
            me->PrintIfWatched("MoveToState: Reached CP %s. Transitioning to CapturePointState.\n", STRING(pTaskEntity->GetEntityName()));
            me->CapturePoint(pTaskEntity);
            return;
        }
        else if (currentTask == BOT_TASK_VIP_ESCAPE_FF) // Use global BotTaskType
        {
            // pTaskEntity here is the escape zone the bot was moving to (set in IdleState).
            if (pTaskEntity && pathResult == CFFBot::END_OF_PATH)
            {
                me->PrintIfWatched("VIP has reached escape zone %s.\n", STRING(pTaskEntity->GetEntityName()));
                // Game logic/trigger should handle actual escape.
                // FFGameState will be updated by an event if successful.
                if (gameState->IsVIPEscaped()) { // Check if game state confirms escape
                     me->PrintIfWatched("VIP successfully escaped! (Confirmed by FFGameState)\n");
                } else {
                     me->PrintIfWatched("VIP at escape zone. Waiting for escape confirmation or further action.\n");
                     // TODO_FF: Potentially 'Use' the escape zone entity if required by map logic.
                     me->Stop(); // Stop movement.
                }
            } else if (pathResult == CFFBot::PATH_FAILURE) { // Should have been caught above
                me->PrintIfWatched("VIP path to escape zone failed. Re-evaluating.\n");
            }
            me->Idle(); // Go idle to re-evaluate state (escaped? or try new zone/task?)
            return;
        }
        // Default for other tasks or if no specific action taken upon arrival
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
