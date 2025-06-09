//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
// #include "../../shared/ff/weapons/ff_weapon_parse.h"
#include "ff_gamestate.h"
#include "nav_mesh.h"
#include "bot_constants.h"
#include "KeyValues.h"      // Already included, seems fine


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombPickedUp( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// TODO_FF: This entire function is CS bomb logic.
	// If FF has a similar "item carrier" mechanic (like a flag), this could be adapted.
	// For now, ensure CFFPlayer cast if GetGameState()->UpdateBomber expects CFFPlayer.
	// if (GetTeamNumber() == TEAM_DEFENDERS_FF && player)  // Example FF Team
	// {
	// 	const float objectivePickupHearRangeSq = 1000.0f * 1000.0f;
	// 	Vector myOrigin = GetCentroid( this );
	// 	if ((myOrigin - player->GetAbsOrigin()).LengthSqr() < objectivePickupHearRangeSq)
	// 	{
	// 		// GetChatter()->TheyPickedUpTheObjective(); // Chatter system removed
	// 		// GetGameState()->UpdateObjectiveCarrier( static_cast<CFFPlayer*>(player), player->GetAbsOrigin() ); // Example
	// 	}
	// }
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombPlanted( IGameEvent *event ) // TODO_FF: Rename to OnObjectiveActivated or similar for FF
{
	// m_gameState.OnObjectiveActivated( event ); // Example for FF

	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// TODO_FF: CS-specific team logic (TEAM_CT). Adapt for FF defender team.
	// if (GetTeamNumber() == TEAM_DEFENDERS_FF)
	// {
	// 	Idle();
	// }

	// if we are following someone, stop following
	if (IsFollowing())
	{
		StopFollowing();
		Idle();
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombBeep( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) ); // This is likely the bomber, not relevant for beep source
	// The entity that beeped is the bomb entity itself
	CBaseEntity *entity = UTIL_EntityByIndex( event->GetInt( "entindex" ) );
	Vector myOrigin = GetCentroid( this );

	// if we don't know where the bomb is, but heard it beep, we've discovered it
	// TODO_FF: This is CS bomb beep logic. Adapt for FF objectives (e.g., flag capture sound, point ticking).
	// if (GetGameState()->IsObjectiveLocationKnown() == false && entity)
	// {
	// 	const float objectiveSoundHearRangeSq = 1500.0f * 1500.0f;
	// 	if ((myOrigin - entity->GetAbsOrigin()).LengthSqr() < objectiveSoundHearRangeSq)
	// 	{
	// 		// TODO_FF: Update for FF teams and chatter.
	// 		// if (GetTeamNumber() == TEAM_DEFENDERS_FF && GetGameState()->GetActiveObjectiveZone() == FFGameState::UNKNOWN_ZONE_FF)
	// 		// {
	// 		// 	const CFFBotManager::Zone *zone = TheFFBots()->GetZone( entity->GetAbsOrigin() );
	// 		// 	if (zone)
	// 		// 		// GetChatter()->FoundActiveObjective( zone->m_index ); // Chatter system removed
	// 		// }
	// 		// GetGameState()->UpdateActiveObjective( entity->GetAbsOrigin() ); // Example
	// 	}
	// }
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefuseBegin( IGameEvent *event ) // TODO_FF: Rename to OnObjectiveInteractionBegin or similar
{
	// TODO_FF: Logic for FF objective interaction start.
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefused( IGameEvent *event ) // TODO_FF: Rename to OnObjectiveCompleted or similar
{
	// m_gameState.OnObjectiveCompleted( event ); // Example

	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// TODO_FF: Update for FF teams and objective logic. Chatter system removed.
	// if (GetTeamNumber() == TEAM_DEFENDERS_FF)
	// {
	// 	if (TheFFBots()->GetObjectiveTimeLeft() < 2.0f)
	// 		// GetChatter()->Say( "BarelyCompletedObjective" ); // Chatter system removed
	// }
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefuseAbort( IGameEvent *event ) // TODO_FF: Rename to OnObjectiveInteractionAbort or similar
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	PrintIfWatched( "OBJECTIVE INTERACTION ABORTED\n" ); // Example FF message
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombExploded( IGameEvent *event ) // TODO_FF: Rename to OnObjectiveFailed or similar
{
	// m_gameState.OnObjectiveFailed( event ); // Example
}
