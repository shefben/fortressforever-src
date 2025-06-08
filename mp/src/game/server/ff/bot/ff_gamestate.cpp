//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encapsulation of the current scenario/game state. Allows each bot imperfect knowledge.
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_gamestate.h" // Own header
#include "../ff_bot.h"      // Corrected path
#include "../ff_bot_manager.h" // Corrected path
#include "../../shared/bot/bot_constants.h"  // Changed path
#include "../../shared/ff/ff_gamerules.h" // Added as per instruction (replaces cs_gamerules.h)
// #include "../ff_player.h"  // Not directly used, but CFFBot uses it.

// TODO: cs_simple_hostage.h is CS-specific. Remove or replace if FF has hostages.
// #include "cs_simple_hostage.h"
#include "KeyValues.h" // Included but not used in this snippet.


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
FFGameState::FFGameState( CFFBot *owner )
{
	m_owner = owner;
	m_isRoundOver = false;

	// TODO: Adapt bomb states for FF if it has a similar objective.
	// If not, this bomb logic should be removed.
	// m_bombState = FFGameState::MOVING; // Ensure MOVING is part of FFGameState::BombState enum (defined in header)
	m_lastSawBomber.Invalidate();
	m_lastSawLooseBomb.Invalidate();
	m_isPlantedBombPosKnown = false;
	// m_plantedBombsite = FFGameState::UNKNOWN_ZONE; // Ensure UNKNOWN_ZONE is defined for FF (was UNKNOWN)

	m_bombsiteCount = 0;
	m_bombsiteSearchIndex = 0;

	// TODO: Hostage logic is CS-specific. Remove if FF doesn't have hostages.
	m_hostageCount = 0;
	m_allHostagesRescued = false;
	m_haveSomeHostagesBeenTaken = false;
	// InitializeHostageInfo(); // Call explicitly if needed after object construction
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Reset at round start
 */
void FFGameState::Reset( void )
{
	m_isRoundOver = false;

	// TODO: Adapt bomb logic for FF.
	// m_bombState = FFGameState::MOVING;
	m_lastSawBomber.Invalidate();
	m_lastSawLooseBomb.Invalidate();
	m_isPlantedBombPosKnown = false;
	// m_plantedBombsite = FFGameState::UNKNOWN_ZONE;

	if (TheFFBots()) m_bombsiteCount = TheFFBots()->GetZoneCount();
	else m_bombsiteCount = 0;

	int i;
	// MAX_ZONES is defined in ff_bot_manager.h
	for( i=0; i<m_bombsiteCount && i < CFFBotManager::MAX_ZONES; ++i )
	{
		m_isBombsiteClear[i] = false;
		m_bombsiteSearchOrder[i] = i;
	}
	if (m_bombsiteCount > 0) {
		for( i=0; i < m_bombsiteCount && i < CFFBotManager::MAX_ZONES; ++i )
		{
			int swap = m_bombsiteSearchOrder[i];
			int rnd = RandomInt( i, m_bombsiteCount-1 );
			if (rnd < 0 || rnd >= CFFBotManager::MAX_ZONES) continue;
			m_bombsiteSearchOrder[i] = m_bombsiteSearchOrder[ rnd ];
			m_bombsiteSearchOrder[ rnd ] = swap;
		}
	}
	m_bombsiteSearchIndex = 0;

	// TODO: Hostage logic for FF.
	InitializeHostageInfo();

    // TODO: Add reset logic for FF specific game states (CTF flags, CP ownership etc.)
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
// TODO: Hostage logic for FF.
void FFGameState::OnHostageRescuedAll( IGameEvent *event )
{
	m_allHostagesRescued = true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
void FFGameState::OnRoundEnd( IGameEvent *event )
{
	m_isRoundOver = true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
void FFGameState::OnRoundStart( IGameEvent *event )
{
	Reset();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
// TODO: Bomb logic for FF.
void FFGameState::OnBombPlanted( IGameEvent *event )
{
	// SetBombState( FFGameState::PLANTED );
	// CBasePlayer *plantingPlayer = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	// if (m_owner && m_owner->GetTeamNumber() == TEAM_TERRORIST && plantingPlayer)
	// {
	//	UpdatePlantedBomb( plantingPlayer->GetAbsOrigin() );
	// }
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
// TODO: Bomb logic for FF.
void FFGameState::OnBombDefused( IGameEvent *event )
{
	// SetBombState( FFGameState::DEFUSED );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Update game state based on events we have received
 */
// TODO: Bomb logic for FF.
void FFGameState::OnBombExploded( IGameEvent *event )
{
	// SetBombState( FFGameState::EXPLODED );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * True if round has been won or lost (but not yet reset)
 */
bool FFGameState::IsRoundOver( void ) const
{
	return m_isRoundOver;
}

//--------------------------------------------------------------------------------------------------------------
// All bomb-related methods (SetBombState, UpdateLooseBomb, etc.) were removed in the header refactoring (Subtask 9)
// and their implementations should be removed here as well.
// The stubs were kept in the previous pass as the header still had declarations. Now they are fully removed.
//--------------------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------------------
/**
 * Initialize our knowledge of the number and location of hostages
 */
// TODO: Hostage logic is CS-specific. Adapt or remove for FF.
void FFGameState::InitializeHostageInfo( void )
{
	m_hostageCount = 0;
	m_allHostagesRescued = false;
	m_haveSomeHostagesBeenTaken = false;
    // CS-specific g_Hostages iteration removed.
}

//--------------------------------------------------------------------------------------------------------------
// All other hostage-related methods (GetNearestFreeHostage, ValidateHostagePositions, etc.) were removed
// in the header refactoring (Subtask 9) and their implementations are removed here.
//--------------------------------------------------------------------------------------------------------------

// TODO: Implement FF-specific game state tracking methods here.
// Examples:
// bool FFGameState::IsOurFlagHome() const { /* ... */ return false; }
// CFFPlayer* FFGameState::GetTheirFlagCarrier() const { /* ... */ return nullptr; }
// TeamNum FFGameState::GetControlPointOwner(int cpIndex) const { /* ... */ return TEAM_UNASSIGNED; }
// ... etc. ...
