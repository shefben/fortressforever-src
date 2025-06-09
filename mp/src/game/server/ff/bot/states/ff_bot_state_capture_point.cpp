#include "cbase.h"
#include "ff_bot_state_capture_point.h"
#include "../ff_bot.h"
#include "../ff_gamestate.h"
#include "../ff_bot_manager.h"
#include "../../../shared/ff/ff_shareddefs.h"
#include "../nav_mesh.h"
#include "../bot_constants.h" // For BotTaskType

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define CP_HOLD_DISTANCE 200.0f
#define CP_REPATH_INTERVAL 5.0f

// Static helper, assuming it's not in a shared utility header yet for this context
static int GetEnemyTeamForPointState(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    // TODO_FF: Handle >2 team scenarios if applicable
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

    bot->PrintIfWatched( "CapturePointState: Task %s for CP %d ('%s').\n", bot->GetTaskName(), m_currentCP_ID, STRING(m_hCurrentCP.Get()->GetEntityName()));
    m_nextActionTimestamp = gpGlobals->curtime + 1.0f;
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

    // If task changed externally, or CP entity became different
    if ((currentTask != CFFBot::BOT_TASK_CAPTURE_POINT && currentTask != CFFBot::BOT_TASK_DEFEND_POINT) ||
        bot->GetTaskEntity() != m_hCurrentCP.Get()) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Task changed or task entity mismatch for CP %d. Transitioning to Idle.\n", m_currentCP_ID);
        bot->Idle();
        return;
    }

    if (currentTask == CFFBot::BOT_TASK_DEFEND_POINT) {
        if (cpState->owningTeam != bot->GetTeamNumber()) {
            bot->PrintIfWatched("CapturePointState: CP %d (Defend Task) no longer owned by our team. Re-evaluating (Idle).\n", m_currentCP_ID);
            bot->Idle();
            return;
        }
        // If an enemy starts capping significantly, we might want to be more alert or reposition.
        // For now, general "hold and fight" logic below applies.
    }
    else if (currentTask == CFFBot::BOT_TASK_CAPTURE_POINT) {
        if (cpState->isLocked) {
            bot->PrintIfWatched( "CapturePointState: CP %d (Capture Task) is now LOCKED. Transitioning to Idle.\n", m_currentCP_ID);
            bot->Idle();
            return;
        }
        if (cpState->owningTeam == bot->GetTeamNumber()) {
            // We (or our team) captured it.
             bool ourTeamFullProgress = (cpState->captureProgress[bot->GetTeamNumber()] >= 0.99f);
            if (ourTeamFullProgress) { // Check if fully capped by us
                bot->PrintIfWatched( "CapturePointState: CP %d (Capture Task) is now owned by our team (%d) and fully capped. Transitioning to Idle.\n", m_currentCP_ID, bot->GetTeamNumber());
                bot->Idle();
                return;
            }
            // If we own it but not fully capped (e.g. progress decaying), continue "capturing" to ensure it's full.
        }
        else if (cpState->owningTeam == GetEnemyTeamForPointState(bot->GetTeamNumber())) {
            // Enemy owns it (possibly just capped it from us/neutral)
            bot->PrintIfWatched("CapturePointState: CP %d (Capture Task) is owned by enemy. Continuing to fight/cap.\n", m_currentCP_ID);
            // If bot is in combat, Attack state will handle it. If not, existing logic will make it hold.
        }
    }

    // Common logic for being on a point: Hold position, fight, look around.

    // Stay on point - if bot moves too far, path back.
    float distToCPSqr = (bot->GetAbsOrigin() - m_hCurrentCP.Get()->GetAbsOrigin()).LengthSqr();
    if (distToCPSqr > Square(CP_HOLD_DISTANCE)) {
         bot->PrintIfWatched( "CapturePointState: OnUpdate: Too far (%.0f units) from CP %d, moving back.\n", sqrt(distToCPSqr), m_currentCP_ID);
         bot->MoveTo(m_hCurrentCP.Get()->GetAbsOrigin(), SAFEST_ROUTE);
         return;
    } else if (bot->IsMoving() && gpGlobals->curtime > m_repathTimer) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Periodically re-centering on CP %d.\n", m_currentCP_ID);
        bot->MoveTo(m_hCurrentCP.Get()->GetAbsOrigin(), SAFEST_ROUTE);
        m_repathTimer = gpGlobals->curtime + CP_REPATH_INTERVAL;
        return;
    }

    if (bot->LookForEnemies()) {
        bot->PrintIfWatched( "CapturePointState: OnUpdate: Engaging enemy while at CP %d.\n", m_currentCP_ID);
        bot->Attack(bot->GetBotEnemy());
        return;
    }

    if (!bot->IsMoving()) {
        bot->Stop();
    }

    if (gpGlobals->curtime > m_nextActionTimestamp) {
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
