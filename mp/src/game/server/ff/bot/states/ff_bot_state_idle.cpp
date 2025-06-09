//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Idle state for Fortress Forever bots.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_bot_state_idle.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"

#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int GetEnemyTeamFor(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    return TEAM_ID_NONE;
}

//--------------------------------------------------------------------------------------------------------------
void IdleState::OnEnter( CFFBot *me )
{
	if (!me) return;
	me->DestroyPath();
	me->SetBotEnemy( NULL );
	me->SetTask( CFFBot::BOT_TASK_SEEK_AND_DESTROY );
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE );
}

//--------------------------------------------------------------------------------------------------------------
void IdleState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh) return;

	if (me->GetLastKnownArea() == NULL && me->StayOnNavMesh() == false)
		return;

	if (cv_bot_zombie.GetBool()) {
		me->ResetStuckMonitor();
		return;
	}

	if (me->IsSafe()) { /* ... (safe time logic unchanged) ... */ }

	if (me->GetGameState()->IsRoundOver()) {
		me->Hunt();
		return;
	}

	if (me->IsFollowing()) {
		me->ContinueFollowing();
		return;
	}

	FFGameState *gameState = me->GetGameState();
	int myTeam = me->GetTeamNumber();
	int enemyTeam = GetEnemyTeamFor(myTeam);

	bool isCTF = (gameState->GetFlagInfo(TEAM_ID_RED) != nullptr && gameState->GetFlagInfo(TEAM_ID_BLUE) != nullptr &&
	              gameState->GetFlagInfo(TEAM_ID_RED)->entity.IsValid() && gameState->GetFlagInfo(TEAM_ID_BLUE)->entity.IsValid());
	bool isCP = (gameState->GetNumControlPoints() > 0);
	CFFPlayer* vipPlayer = gameState->GetVIP();
	bool isVIPMode = (vipPlayer != nullptr && gameState->IsVIPAlive());

	if (isCTF) { /* ... (CTF logic unchanged from previous step) ... */
		const FFGameState::FF_FlagState* enemyFlagState = gameState->GetFlagInfo(enemyTeam);
		const FFGameState::FF_FlagState* myFlagState = gameState->GetFlagInfo(myTeam);
		Vector droppedFlagLoc;

		if (enemyFlagState && enemyFlagState->entity.IsValid()) {
			if (enemyFlagState->currentState == FF_FLAG_STATE_HOME ||
			    (enemyFlagState->currentState == FF_FLAG_STATE_DROPPED && gameState->IsTeamFlagDropped(enemyTeam, &droppedFlagLoc))) {
				Vector targetPos = (enemyFlagState->currentState == FF_FLAG_STATE_HOME) ?
				                   enemyFlagState->entitySpawnLocation : droppedFlagLoc;
				me->PrintIfWatched( "Task: Going for enemy flag!\n");
				me->SetTask( CFFBot::BOT_TASK_CAPTURE_FLAG, enemyFlagState->entity.Get() );
				me->MoveTo( targetPos );
				return;
			}
		}
		if (myFlagState && myFlagState->entity.IsValid() && gameState->IsTeamFlagDropped(myTeam, &droppedFlagLoc)) {
			me->PrintIfWatched( "Task: Returning our flag!\n");
			me->SetTask( CFFBot::BOT_TASK_RETURN_FLAG, myFlagState->entity.Get() );
			me->MoveTo( droppedFlagLoc );
			return;
		}
	}
	else if (isCP)
	{
        // Priority 1: Defend owned, contested CP
        for (int i = 0; i < gameState->GetNumControlPoints(); ++i)
        {
            const FFGameState::FF_ControlPointState* cpInfo = gameState->GetControlPointInfo(i);
            if (cpInfo && cpInfo->entity.IsValid() && cpInfo->owningTeam == myTeam && !cpInfo->isLocked)
            {
                if (gameState->GetControlPointCaptureProgress(i, enemyTeam) > 0.1f) // Check if enemy is capturing
                {
                    me->PrintIfWatched( "Task: Defending contested control point %d!\n", cpInfo->pointID);
                    me->SetTask(CFFBot::BOT_TASK_DEFEND_POINT, cpInfo->entity.Get());
                    // Transition to CapturePointState, which will handle being on the point (defending or capping)
                    me->CapturePoint(cpInfo->entity.Get());
                    return;
                }
            }
        }

        // Priority 2: Capture neutral/enemy CP
		for (int i = 0; i < gameState->GetNumControlPoints(); ++i)
		{
			const FFGameState::FF_ControlPointState* cpInfo = gameState->GetControlPointInfo(i);
			if (cpInfo && cpInfo->entity.IsValid() && cpInfo->owningTeam != myTeam && !cpInfo->isLocked)
			{
				me->PrintIfWatched( "Task: Capturing control point %d!\n", cpInfo->pointID);
				me->SetTask( CFFBot::BOT_TASK_CAPTURE_POINT, cpInfo->entity.Get() );
                // Transition to CapturePointState, which will handle being on the point
                me->CapturePoint(cpInfo->entity.Get());
				return;
			}
		}
        // TODO_FF: Priority 3 (Reinforce owned, uncontested CP) would go here if GetNumPlayersOnPoint was available.
	}
	else if (isVIPMode && vipPlayer) { /* ... (VIP logic unchanged from previous step) ... */
		if (me == vipPlayer) {
			me->PrintIfWatched( "Task: I am the VIP, attempting to escape!\n");
			me->SetTask(CFFBot::BOT_TASK_VIP_ESCAPE_FF);
			me->Hunt();
			return;
		} else if (myTeam == vipPlayer->GetTeamNumber()) {
			me->PrintIfWatched( "Task: Escorting VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(CFFBot::BOT_TASK_ESCORT_VIP_FF, vipPlayer);
			me->Follow(vipPlayer);
			return;
		} else {
			me->PrintIfWatched( "Task: Hunting VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(CFFBot::BOT_TASK_ASSASSINATE_VIP_FF, vipPlayer);
			me->SetBotEnemy(vipPlayer);
			me->Hunt();
			return;
		}
	}

	me->PrintIfWatched( "Task: No specific objectives, going hunting.\n");
	me->Hunt();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_idle.cpp]
