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
// #include "../../ff_player.h" // Not directly used, CFFBot is enough
// #include "../../../shared/ff/weapons/ff_weapon_base.h" // Not directly used
#include "../ff_gamestate.h"
#include "../nav_mesh.h"
// #include "../nav_pathfind.h"   // Not directly used in this simplified version
// #include "../nav_hiding_spot.h"// Only for sniper logic which is simplified for now

// Bot-specific headers from parent directory
#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"

// Assuming ff_gamerules.h might be needed for proper game type detection eventually
// #include "../../../../shared/ff/ff_gamerules.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Helper function (can be moved to CFFBot or a utility class later)
// Basic two-team enemy determination
static int GetEnemyTeamFor(int myTeam)
{
    if (myTeam == TEAM_ID_RED) return TEAM_ID_BLUE;
    if (myTeam == TEAM_ID_BLUE) return TEAM_ID_RED;
    // Add more for 4-team modes if necessary
    return TEAM_ID_NONE; // No enemy or undefined
}


//--------------------------------------------------------------------------------------------------------------
void IdleState::OnEnter( CFFBot *me )
{
	if (!me) return;

	me->DestroyPath();
	me->SetBotEnemy( NULL );

	// Default task unless overridden by game mode logic
	me->SetTask( CFFBot::BOT_TASK_SEEK_AND_DESTROY );
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE );
}

//--------------------------------------------------------------------------------------------------------------
void IdleState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh) return;

	if (me->GetLastKnownArea() == NULL && me->StayOnNavMesh() == false)
		return;

	if (cv_bot_zombie.GetBool())
	{
		me->ResetStuckMonitor();
		return;
	}

	// Early "safe" time logic (simplified, kept from original)
	if (me->IsSafe())
	{
		if (!me->EquipGrenade(true)) // true for noSmoke, adapt if FF grenades have similar concept
		{
			// TODO_FF: Re-evaluate knife equipping logic based on FF movement speeds/weapons
			// if (me->GetProfile() && me->GetProfile()->GetSkill() > 0.33f /* && !me->IsUsing(FF_WEAPON_SCOUT_EQUIVALENT) */ )
			// {
			//	 me->EquipKnife();
			// }
		}
	}

	if (me->GetGameState()->IsRoundOver())
	{
		me->Hunt(); // Default behavior if round is over
		return;
	}

	// If we were following someone, continue following them (unless overridden by urgent objective)
	// This check might be better placed after objective logic if objectives are higher priority.
	if (me->IsFollowing())
	{
		me->ContinueFollowing();
		return;
	}

	// --- Fortress Forever Game Mode Logic ---
	FFGameState *gameState = me->GetGameState();
	int myTeam = me->GetTeamNumber();
	int enemyTeam = GetEnemyTeamFor(myTeam); // Use helper

	// Game Mode Detection (Proxy)
	bool isCTF = (gameState->GetFlagInfo(TEAM_ID_RED) != nullptr && gameState->GetFlagInfo(TEAM_ID_BLUE) != nullptr &&
	              gameState->GetFlagInfo(TEAM_ID_RED)->entity.IsValid() && gameState->GetFlagInfo(TEAM_ID_BLUE)->entity.IsValid());
	bool isCP = (gameState->GetNumControlPoints() > 0);
	CFFPlayer* vipPlayer = gameState->GetVIP(); // Store VIP for checks
	bool isVIPMode = (vipPlayer != nullptr && gameState->IsVIPAlive()); // Only active if VIP is known and alive

	// CTF Logic
	if (isCTF)
	{
		const FFGameState::FF_FlagState* enemyFlagState = gameState->GetFlagInfo(enemyTeam);
		const FFGameState::FF_FlagState* myFlagState = gameState->GetFlagInfo(myTeam);
		Vector droppedFlagLoc;

		if (enemyFlagState && enemyFlagState->entity.IsValid())
		{
			if (enemyFlagState->currentState == FF_FLAG_STATE_HOME ||
			    (enemyFlagState->currentState == FF_FLAG_STATE_DROPPED && gameState->IsTeamFlagDropped(enemyTeam, &droppedFlagLoc)))
			{
				Vector targetPos = (enemyFlagState->currentState == FF_FLAG_STATE_HOME) ?
				                   enemyFlagState->entitySpawnLocation : droppedFlagLoc;

				PrintIfWatched(me, "Task: Going for enemy flag!\n");
				me->SetTask( CFFBot::BOT_TASK_CAPTURE_FLAG, enemyFlagState->entity.Get() );
				me->MoveTo( targetPos );
				return;
			}
		}

		if (myFlagState && myFlagState->entity.IsValid() && gameState->IsTeamFlagDropped(myTeam, &droppedFlagLoc))
		{
			PrintIfWatched(me, "Task: Returning our flag!\n");
			me->SetTask( CFFBot::BOT_TASK_RETURN_FLAG, myFlagState->entity.Get() );
			me->MoveTo( droppedFlagLoc );
			return;
		}
		// TODO_FF: Add logic for defending own flag carrier, defending own base, intercepting enemy carrier
	}
	// Control Point Logic
	else if (isCP)
	{
		for (int i = 0; i < gameState->GetNumControlPoints(); ++i)
		{
			const FFGameState::FF_ControlPointState* cpInfo = gameState->GetControlPointInfo(i);
			if (cpInfo && cpInfo->entity.IsValid() && cpInfo->owningTeam != myTeam && !cpInfo->isLocked)
			{
				PrintIfWatched(me, "Task: Capturing control point %d!\n", cpInfo->pointID);
				me->SetTask( CFFBot::BOT_TASK_CAPTURE_POINT, cpInfo->entity.Get() );
				me->MoveTo( cpInfo->entity.Get()->GetAbsOrigin() );
				return;
			}
		}
		// TODO_FF: Add logic for defending owned points, prioritizing contested points
	}
	// VIP Logic
	else if (isVIPMode && vipPlayer) // vipPlayer already checked for null in isVIPMode but good practice
	{
		if (me == vipPlayer) // Bot is the VIP
		{
			PrintIfWatched(me, "Task: I am the VIP, attempting to escape!\n");
			me->SetTask(CFFBot::BOT_TASK_VIP_ESCAPE_FF);
			// TODO_FF: Add actual movement logic to escape points
			// For now, VIP might just Hunt or need a specific "EscapeState"
			me->Hunt(); // Placeholder action for VIP
			return;
		}
		else if (myTeam == vipPlayer->GetTeamNumber()) // Bot is on VIP's team (Bodyguard)
		{
			PrintIfWatched(me, "Task: Escorting VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(CFFBot::BOT_TASK_ESCORT_VIP_FF, vipPlayer);
			me->Follow(vipPlayer); // Follow is a state transition
			return;
		}
		else // Bot is on opposing team (Assassin)
		{
			PrintIfWatched(me, "Task: Hunting VIP %s!\n", vipPlayer->GetPlayerName());
			me->SetTask(CFFBot::BOT_TASK_ASSASSINATE_VIP_FF, vipPlayer);
			me->SetBotEnemy(vipPlayer); // Set enemy directly
			me->Hunt(); // Transition to Hunt state to engage
			return;
		}
	}

	// Default action if no specific objective-based task was assigned
	// OnEnter already sets BOT_TASK_SEEK_AND_DESTROY.
	// If an objective was chosen, we returned. If not, proceed to Hunt.
	PrintIfWatched(me, "Task: No specific objectives, going hunting.\n");
	me->Hunt();
}

[end of mp/src/game/server/ff/bot/states/ff_bot_state_idle.cpp]
