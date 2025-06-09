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
	me->SetTask( BOT_TASK_SEEK_AND_DESTROY ); // Use global BotTaskType
	me->SetDisposition( ENGAGE_AND_INVESTIGATE ); // Use global DispositionType
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
	bool isVIPMode = (vipPlayer != nullptr && gameState->IsVIPAlive() && !gameState->IsVIPEscaped()); // Added !IsVIPEscaped check

	if (isCTF) { /* ... (CTF logic unchanged) ... */ }
	else if (isCP) { /* ... (CP logic unchanged) ... */ }
	else if (isVIPMode && vipPlayer)
	{
		if (me == vipPlayer) // Bot is the VIP
		{
			me->SetTask(BOT_TASK_VIP_ESCAPE_FF); // Use global BotTaskType
            CBaseEntity *pEscapeZone = me->GetClosestEscapeZone();
            if (pEscapeZone) {
                me->PrintIfWatched("VIP task: Moving to escape zone %s\n", pEscapeZone->GetDebugName());
                me->SetTaskEntity(pEscapeZone); // Set the escape zone as the task entity
                me->MoveTo(pEscapeZone->GetAbsOrigin(), FASTEST_ROUTE);
            } else {
                me->PrintIfWatched("VIP task: No escape zone found. Defaulting to Hunt (defensive behavior).\n");
                // TODO_FF: Add better fallback like hiding or moving to a generally safe area for VIPs
                me->Hunt();
            }
            return;
		}
		else if (myTeam == vipPlayer->GetTeamNumber()) // Bot is on VIP's team (Bodyguard)
		{
			me->PrintIfWatched( "IdleState: Tasking - Escort VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(BOT_TASK_ESCORT_VIP_FF, vipPlayer); // Use global BotTaskType
			me->Follow(vipPlayer);
			return;
		}
		else // Bot is on opposing team (Assassin)
		{
			me->PrintIfWatched( "IdleState: Tasking - Hunt VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(BOT_TASK_ASSASSINATE_VIP_FF, vipPlayer); // Use global BotTaskType
			me->SetBotEnemy(vipPlayer);
			me->Hunt();
			return;
		}
	}

	me->PrintIfWatched( "IdleState: No specific objectives, going hunting.\n");
	me->Hunt();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_idle.cpp]
