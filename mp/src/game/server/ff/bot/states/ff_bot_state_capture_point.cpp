#include "cbase.h"
#include "ff_bot_state_capture_point.h"
#include "../ff_bot.h"
#include "../ff_gamestate.h"
#include "../ff_bot_manager.h"
#include "../../../shared/ff/ff_shareddefs.h"
#include "../nav_mesh.h"
#include "../bot_constants.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CP_HOLD_DISTANCE 200.0f
#define CP_REPATH_INTERVAL 5.0f
#define CP_CONTESTED_THRESHOLD 0.1f // If enemy progress is > 10%, it's contested

static int GetEnemyTeamForCaptureState(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    return TEAM_ID_NONE;
}

CapturePointState::CapturePointState()
{
    m_hCurrentCP = NULL;
    m_currentCP_ID = -1;
    m_nextActionTimestamp = 0.0f;
    m_repathTimer = 0.0f;
}

void CapturePointState::OnEnter( CFFBot *bot )
{
    if (!bot) return;
    bot->PrintIfWatched( "Entering CapturePointState.\n" );

    m_hCurrentCP = bot->GetTaskEntity();
    if (!m_hCurrentCP.Get()) {
        bot->Warning("CapturePointState: OnEnter: Task entity is NULL. Transitioning to Idle.\n");
        bot->Idle();
        return;
    }

    FFGameState *gameState = bot->GetGameState();
    if (!gameState) {
        bot->Warning("CapturePointState: OnEnter: GameState is NULL. Transitioning to Idle.\n");
        bot->Idle();
        return;
    }

    m_currentCP_ID = gameState->GetCPIDFromEntity(m_hCurrentCP.Get());

    if (m_currentCP_ID == -1) {
        bot->Warning( "CapturePointState: OnEnter: Could not determine CP ID for entity '%s'. Transitioning to Idle.\n", STRING(m_hCurrentCP.Get()->GetEntityName()));
        bot->Idle();
        return;
    }

    bot->GetBodyInterface()->StuckCheck();
    bot->Stop();
    bot->ClearPath();

    bot->PrintIfWatched( "CapturePointState: Task %s for CP ID %d ('%s').\n", bot->GetTaskName(), m_currentCP_ID, STRING(m_hCurrentCP.Get()->GetEntityName()));
    m_nextActionTimestamp = gpGlobals->curtime + RandomFloat(1.0f, 2.0f); // Initial look around sooner
    m_repathTimer = gpGlobals->curtime + CP_REPATH_INTERVAL;
}

void CapturePointState::OnUpdate( CFFBot *bot )
{
    if (!bot) return;
    if (!m_hCurrentCP.Get() || m_currentCP_ID == -1) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Current CP is invalid. Transitioning to Idle.\n");
        bot->Idle();
        return;
    }

    FFGameState *gameState = bot->GetGameState();
    if (!gameState) {
        bot->Warning("CapturePointState: OnUpdate: GameState is NULL. Transitioning to Idle.\n");
        bot->Idle();
        return;
    }

    const FF_ControlPointState* cpState = gameState->GetControlPointInfo(m_currentCP_ID);
    if (!cpState || !cpState->entity.IsValid()) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: CP info for ID %d not found or entity invalid. Transitioning to Idle.\n", m_currentCP_ID);
        bot->Idle();
        return;
    }

    BotTaskType currentTask = bot->GetTask();
    int myTeam = bot->GetTeamNumber();
    int enemyTeam = GetEnemyTeamForCaptureState(myTeam);

    // Universal: If task changed or CP entity changed, re-evaluate by going to Idle
    if ((currentTask != CFFBot::BOT_TASK_CAPTURE_POINT && currentTask != CFFBot::BOT_TASK_DEFEND_POINT) ||
        bot->GetTaskEntity() != m_hCurrentCP.Get()) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Task changed (%s) or task entity mismatch for CP %d. Transitioning to Idle.\n", bot->GetTaskName(), m_currentCP_ID);
        bot->Idle();
        return;
    }

    // Task-specific exit/behavior conditions
    if (currentTask == CFFBot::BOT_TASK_DEFEND_POINT) {
        if (cpState->owningTeam != myTeam) {
            bot->PrintIfWatched("CapturePointState: CP %d (Defend Task) no longer owned by our team (Owner: %d). Re-evaluating (Idle).\n", m_currentCP_ID, cpState->owningTeam);
            bot->Idle();
            return;
        }
        if (enemyTeam != TEAM_ID_NONE && cpState->captureProgress[enemyTeam] > CP_CONTESTED_THRESHOLD) {
            bot->PrintIfWatched("CapturePointState: Our CP %d is heavily contested (Enemy Prog: %.2f)! Defending aggressively!\n", m_currentCP_ID, cpState->captureProgress[enemyTeam]);
            // Prioritize combat - LookForEnemies() below will handle this.
            // Could add more aggressive positioning/behavior here later.
        }
    }
    else if (currentTask == CFFBot::BOT_TASK_CAPTURE_POINT) {
        if (cpState->isLocked) {
            bot->PrintIfWatched( "CapturePointState: CP %d (Capture Task) is now LOCKED. Transitioning to Idle.\n", m_currentCP_ID);
            bot->Idle();
            return;
        }
        if (cpState->owningTeam == myTeam) {
            // We (or our team) captured it.
            // Check if fully capped by us (progress might still be ticking up from multiple cappers)
            if (cpState->captureProgress[myTeam] >= 0.99f) {
                bot->PrintIfWatched( "CapturePointState: CP %d (Capture Task) is now owned by our team (%d) and fully capped. Transitioning to Idle.\n", m_currentCP_ID, myTeam);
                bot->Idle();
                return;
            }
            // If we own it but not fully capped (e.g. progress decaying from enemy presence earlier, or not yet full), continue "capturing" to ensure it's full.
            bot->PrintIfWatched("CapturePointState: CP %d (Capture Task) now owned by us, but progress %.2f. Continuing to secure.\n", m_currentCP_ID, cpState->captureProgress[myTeam]);
        }
        else if (enemyTeam != TEAM_ID_NONE && cpState->owningTeam == enemyTeam) {
            bot->PrintIfWatched("CapturePointState: CP %d (Capture Task) is owned by enemy team %d. Engaging to capture!\n", m_currentCP_ID, enemyTeam);
            // Prioritize combat - LookForEnemies() below will handle this.
        } else { // Neutral or owned by other non-enemy team (if >2 teams)
             bot->PrintIfWatched("CapturePointState: Continuing to capture neutral/other-owned CP %d. Our progress: %.2f\n", m_currentCP_ID, cpState->captureProgress[myTeam]);
        }
    }

    // Common logic for being on a point: Look for enemies first.
    if (bot->LookForEnemies()) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Engaging enemy while at CP %d ('%s').\n", m_currentCP_ID, STRING(m_hCurrentCP.Get()->GetEntityName()));
        bot->Attack(bot->GetBotEnemy());
        return;
    }

    // If no enemies, hold position near the CP.
    float distToCPSqr = (bot->GetAbsOrigin() - m_hCurrentCP.Get()->GetAbsOrigin()).LengthSqr();
    if (distToCPSqr > Square(CP_HOLD_DISTANCE)) {
         bot->PrintIfWatched( "CapturePointState: OnUpdate: Too far (%.0f units) from CP %d, moving back.\n", sqrt(distToCPSqr), m_currentCP_ID);
         bot->MoveTo(m_hCurrentCP.Get()->GetAbsOrigin(), SAFEST_ROUTE); // Re-path to CP center
         // Setting m_repathTimer ensures we don't immediately try to re-path again if MoveTo results in being slightly off center.
         m_repathTimer = gpGlobals->curtime + CP_REPATH_INTERVAL;
         return;
    } else if (bot->IsMoving() && gpGlobals->curtime > m_repathTimer) { // If bot is close but still in IsMoving(), and timer passed
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Close to CP %d but still moving; periodically re-pathing to center.\n", m_currentCP_ID);
        bot->MoveTo(m_hCurrentCP.Get()->GetAbsOrigin(), SAFEST_ROUTE);
        m_repathTimer = gpGlobals->curtime + CP_REPATH_INTERVAL;
        return;
    }

    // If not fighting and not repositioning, ensure bot is stopped and looking around.
    if (!bot->IsMoving()) {
        bot->Stop();
    }

    if (gpGlobals->curtime > m_nextActionTimestamp) {
        // TODO_FF: For defense, look towards likely enemy approaches. For capture, just general awareness.
        bot->UpdateLookAround();
        m_nextActionTimestamp = gpGlobals->curtime + RandomFloat(1.5f, 3.0f);
    }
}

void CapturePointState::OnExit( CFFBot *bot )
{
    if (!bot) return;
    bot->PrintIfWatched( "Exiting CapturePointState for CP ID %d (Entity: %s).\n", m_currentCP_ID, m_hCurrentCP.Get() ? STRING(m_hCurrentCP.Get()->GetEntityName()) : "NULL" );
    m_hCurrentCP = NULL;
    m_currentCP_ID = -1;
}
