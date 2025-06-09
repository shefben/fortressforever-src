#include "cbase.h"
#include "ff_bot_state_carry_flag.h"
#include "../ff_bot.h"
#include "../ff_gamestate.h"
#include "../ff_bot_manager.h"
#include "../../../shared/ff/ff_shareddefs.h"
#include "../bot_constants.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int GetEnemyTeamHelper(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    return TEAM_ID_NONE;
}

CarryFlagState::CarryFlagState()
{
    m_hOurCaptureZone = NULL;
}

void CarryFlagState::OnEnter( CFFBot *bot )
{
    if (!bot) return;
    bot->PrintIfWatched( "Entering CarryFlagState.\n" );

    FFGameState *gameState = bot->GetGameState();
    if (!gameState) {
        bot->Warning("CarryFlagState: GameState is NULL. Idling.\n");
        bot->Idle();
        return;
    }

    // Determine our capture zone entity using the new manager method
    m_hOurCaptureZone = TheFFBots()->GetTeamCaptureZone(bot->GetTeamNumber());

    if (!m_hOurCaptureZone.Get()) {
        // Fallback: If specific capture zone not found by manager, try using own flag stand location.
        // This is a secondary fallback, primary should be the manager's direct knowledge.
        const FFGameState::FF_FlagState* myTeamFlagInfo = gameState->GetFlagInfo(bot->GetTeamNumber());
        if (myTeamFlagInfo && myTeamFlagInfo->entity.IsValid() && myTeamFlagInfo->entitySpawnLocation != vec3_invalid) {
             m_hOurCaptureZone = myTeamFlagInfo->entity.Get(); // Target the flag stand entity itself
             bot->PrintIfWatched("CarryFlagState: Capture zone not found via TheFFBots(). Using own flag stand '%s' location as fallback nav goal.\n", STRING(m_hOurCaptureZone->GetEntityName()));
        } else {
            bot->Warning( "CarryFlagState: Could not find our capture zone via TheFFBots() or fallback to flag stand! Going Idle.\n" );
            bot->Idle();
            return;
        }
    } else {
         bot->PrintIfWatched( "CarryFlagState: Found our capture zone '%s' via TheFFBots().\n", STRING(m_hOurCaptureZone->GetEntityName()) );
    }

    bot->SetTask(CFFBot::BOT_TASK_CARRY_FLAG_TO_CAP);
    // The TaskEntity is still the enemy flag the bot is carrying.
    // The goal of this state is to move to m_hOurCaptureZone.
    bot->MoveTo(m_hOurCaptureZone.Get()->GetAbsOrigin(), SAFEST_ROUTE);
}

void CarryFlagState::OnUpdate( CFFBot *bot )
{
    if (!bot) return;
    FFGameState *gameState = bot->GetGameState();
    if (!gameState || !m_hOurCaptureZone.Get()) {
        bot->PrintIfWatched( "CarryFlagState: GameState or Capture Zone became invalid during update. Idling.\n" );
        bot->Idle();
        return;
    }

    int enemyTeam = GetEnemyTeamHelper(bot->GetTeamNumber());
    if (enemyTeam == TEAM_ID_NONE && bot->GetTeamNumber() != TEAM_ID_NONE) {
        bot->Warning("CarryFlagState: Could not determine enemy team for bot on team %d. Idling.\n", bot->GetTeamNumber());
        bot->Idle();
        return;
    }

    const FF_FlagState* enemyFlagInfo = gameState->GetFlagInfo(enemyTeam);

    if (!enemyFlagInfo || enemyFlagInfo->currentState != FF_FLAG_STATE_CARRIED || enemyFlagInfo->carrier.Get() != bot) {
        bot->PrintIfWatched( "CarryFlagState: No longer carrying the enemy flag (state %d, carrier %p vs bot %p). Going Idle.\n",
            enemyFlagInfo ? enemyFlagInfo->currentState : -1,
            enemyFlagInfo ? enemyFlagInfo->carrier.Get() : NULL,
            bot);
        bot->Idle();
        return;
    }

    CFFBot::PathResult pathResult = bot->UpdatePathMovement();
    if (pathResult != CFFBot::PROGRESSING) {
        if (pathResult == CFFBot::END_OF_PATH) {
             bot->PrintIfWatched( "CarryFlagState: Arrived at capture zone with flag. Idling for game to process capture.\n" );
            bot->Stop();
            bot->LookAround();
            bot->Wait(1.0f);
        } else {
            bot->PrintIfWatched( "CarryFlagState: Path to capture zone failed. Idling.\n");
        }
        bot->Idle();
        return;
    }

    if (bot->LookForEnemies()) {
        bot->PrintIfWatched( "CarryFlagState: Encountered enemy while carrying flag. Engaging.\n" );
        bot->Attack(bot->GetBotEnemy());
        return;
    }
}

void CarryFlagState::OnExit( CFFBot *bot )
{
    if (!bot) return;
    bot->PrintIfWatched( "Exiting CarryFlagState.\n" );
    m_hOurCaptureZone = NULL;
}
