//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h" // For TheFFBots()
#include "../ff_player.h"     // For CFFPlayer
#include "../../shared/ff/ff_gamerules.h" // For FFGameRules() and team/scenario defines
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase (potentially used via CFFBot)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For CNavArea (potentially, though not directly used here, often via manager)
#include "bot_constants.h"  // For TEAM_CT etc. (assuming this is where they are defined)
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

	// TODO: Update for FF teams if necessary
	if (GetTeamNumber() == TEAM_CT && player)
	{
		// check if we're close enough to hear it
		const float bombPickupHearRangeSq = 1000.0f * 1000.0f;
		Vector myOrigin = GetCentroid( this );

		if ((myOrigin - player->GetAbsOrigin()).LengthSqr() < bombPickupHearRangeSq)
		{
			GetChatter()->TheyPickedUpTheBomb();
			GetGameState()->UpdateBomber( player->GetAbsOrigin() );
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombPlanted( IGameEvent *event )
{
	m_gameState.OnBombPlanted( event );

	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// if we're a TEAM_CT, forget what we're doing and go after the bomb
	// TODO: Update for FF teams if necessary
	if (GetTeamNumber() == TEAM_CT)
	{
		Idle();
	}

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
	if (GetGameState()->IsPlantedBombLocationKnown() == false && entity)
	{
		// check if we're close enough to hear it
		const float bombBeepHearRangeSq = 1500.0f * 1500.0f;
		if ((myOrigin - entity->GetAbsOrigin()).LengthSqr() < bombBeepHearRangeSq)
		{
			// radio the news to our team
			// TODO: Update for FF teams if necessary
			if (GetTeamNumber() == TEAM_CT && GetGameState()->GetPlantedBombsite() == FFGameState::UNKNOWN)
			{
				const CFFBotManager::Zone *zone = TheFFBots()->GetZone( entity->GetAbsOrigin() ); // Changed TheCSBots to TheFFBots
				if (zone)
					GetChatter()->FoundPlantedBomb( zone->m_index );
			}

			// remember where the bomb is
			GetGameState()->UpdatePlantedBomb( entity->GetAbsOrigin() );
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefuseBegin( IGameEvent *event )
{
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefused( IGameEvent *event )
{
	m_gameState.OnBombDefused( event );

	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// TODO: Update for FF teams if necessary
	if (GetTeamNumber() == TEAM_CT)
	{
		// TODO: Update for FF if bomb timer works differently
		if (TheFFBots()->GetBombTimeLeft() < 2.0f) // Changed TheCSBots to TheFFBots
			GetChatter()->Say( "BarelyDefused" );
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombDefuseAbort( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	PrintIfWatched( "BOMB DEFUSE ABORTED\n" );
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBombExploded( IGameEvent *event )
{
	m_gameState.OnBombExploded( event );
}
