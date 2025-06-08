//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encapsulation of the current scenario/game state for Fortress Forever bots.
//
// $NoKeywords: $
//=============================================================================//

#ifndef _FF_GAME_STATE_H_
#define _FF_GAME_STATE_H_

#include "igameevents.h" // For IGameEvent
// TODO: Determine if ff_gamerules.h is strictly needed here, or if CFFGameRules is accessed via CFFBot or CFFBotManager
// #include "../../shared/ff/ff_gamerules.h"

class CFFBot; // Forward declaration for CFFBot
// class CFFPlayer; // Forward declaration if CFFPlayer is directly used by FFGameState members/methods

/**
 * This class represents the game state as known by a particular bot
 */
class FFGameState
{
public:
	FFGameState( CFFBot *owner );

	void Reset( void );

	// Generic Event handling
	void OnRoundEnd( IGameEvent *event );
	void OnRoundStart( IGameEvent *event );
	// TODO: Add FF-specific event handlers (e.g., OnFlagCapture, OnControlPointCaptured, OnVIPKilled etc.)

	bool IsRoundOver( void ) const;								///< true if round has been won or lost (but not yet reset)

	// General scenario information
	enum { UNKNOWN_ZONE = -1 }; // Used for objectives, control points, etc.

	// TODO: Add members and methods for CTF (Capture The Flag) state tracking
	// Example:
	// FlagState m_ourFlagState;
	// FlagState m_theirFlagState;
	// CFFPlayer* m_ourFlagCarrier;
	// CFFPlayer* m_theirFlagCarrier;
	// Vector m_ourFlagHomePosition;
	// Vector m_theirFlagHomePosition;
	// bool IsOurFlagHome() const;
	// bool IsTheirFlagHome() const;
	// CFFPlayer* GetOurFlagCarrier() const;
	// etc.

	// TODO: Add members and methods for CP (Control Point) state tracking
	// Example:
	// struct ControlPointInfo {
	//     int pointID;
	//     TeamNum controllingTeam; // TeamNum would be FF specific (TEAM_RED, TEAM_BLUE)
	//     float captureProgress;
	//     bool isBeingCaptured;
	// };
	// CUtlVector<ControlPointInfo> m_controlPoints;
	// int GetControllingTeam(int pointID) const;
	// etc.

	// TODO: Add members and methods for VIP Escort state tracking (if applicable to FF)
	// Example:
	// CFFPlayer* m_vip;
	// bool IsVIPEscaped() const;
	// etc.


private:
	CFFBot *m_owner;											///< who owns this gamestate

	bool m_isRoundOver;											///< true if round is over, but no yet reset

	// TODO: Add common state variables relevant to FF game modes
	// Example:
	// float m_roundTimer; // Time left in the round
	// int m_myTeamScore;
	// int m_enemyTeamScore;
};

#endif // _FF_GAME_STATE_H_
