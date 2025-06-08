//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encapsulation of the current scenario/game state for Fortress Forever bots.
//
// $NoKeywords: $
//=============================================================================//

#ifndef _FF_GAME_STATE_H_
#define _FF_GAME_STATE_H_

#include "igameevents.h" // For IGameEvent
#include "ehandle.h"     // For CHandle
#include "mathlib/vector.h" // For Vector

// Forward declarations
class CFFBot;
class CFFPlayer;
class CBaseEntity;

// Constants for Teams & Objectives
#define MAX_TEAMS_FF 4
#define MAX_PLAYABLE_TEAMS_FF 2
#define MAX_CONTROL_POINTS_FF 8

#define TEAM_ID_NONE -1
#define TEAM_ID_RED 0
#define TEAM_ID_BLUE 1

// CTF Flag States
#define FF_FLAG_STATE_HOME 0
#define FF_FLAG_STATE_CARRIED 1
#define FF_FLAG_STATE_DROPPED 2

// VIP Game Mode (Example placeholders)
#define CLASS_CIVILIAN_FF 10 // Example: Assuming a class ID for VIP/Civilian
#define VIP_TEAM TEAM_ID_BLUE // Example: Assuming VIP is always on BLUE team

class FFGameState
{
public:
	FFGameState( CFFBot *owner );

	void Reset( void );
	void Update( void );

	// Generic Event handling
	void OnRoundEnd( IGameEvent *event );
	void OnRoundStart( IGameEvent *event );

	// --- FF-specific CTF Event Handlers ---
	void OnFlagPickedUp(CBaseEntity* pFlagEntity, CFFPlayer* pPlayer);
	void OnFlagDropped(CBaseEntity* pFlagEntity, const Vector& dropLocation);
	void OnFlagCaptured(CBaseEntity* pFlagEntity, CFFPlayer* pCapturer);
	void OnFlagReturned(CBaseEntity* pFlagEntity);

	// --- FF-specific Control Point Event Handlers ---
	void OnControlPointCaptured(CBaseEntity* pCPEntity, int newOwnerTeam);
	void OnControlPointProgress(CBaseEntity* pCPEntity, int teamMakingProgress, float newProgress);
	void OnControlPointBlocked(CBaseEntity* pCPEntity, bool isBlocked);

	// --- FF-specific VIP Event Handlers ---
	void OnVIPKilled(CFFPlayer* pVIPVictim, CBaseEntity* pKiller); // Added pKiller for context
	void OnVIPEscaped(CFFPlayer* pVIP);
	void OnPlayerSpawn(CFFPlayer* pPlayer); // To update VIP info if needed on spawn/re-spawn/class change

	bool IsRoundOver( void ) const;

	enum { UNKNOWN_ZONE = -1 };

	// --- CTF (Capture The Flag) State Tracking ---
	struct FF_FlagState {
		CHandle<CBaseEntity> entity;
		int teamAffiliation;
		int currentState;
		CHandle<CFFPlayer> carrier;
		Vector dropLocation;
		Vector entitySpawnLocation;
		float returnTime;

		FF_FlagState() { Reset(); }
		void Reset() {
			entity = NULL;
			teamAffiliation = TEAM_ID_NONE;
			currentState = FF_FLAG_STATE_HOME;
			carrier = NULL;
			dropLocation.Init();
			entitySpawnLocation.Init();
			returnTime = -1.0f;
		}
	};

	const FF_FlagState* GetFlagInfo(int team) const;
	bool IsTeamFlagHome(int team) const;
	bool IsTeamFlagCarried(int team, CFFPlayer** pCarrier = NULL) const;
	bool IsTeamFlagDropped(int team, Vector* pDropLocation = NULL) const;
    bool IsOtherTeamFlagAtOurBase(int myTeam) const;


	// --- Control Point (CP) State Tracking ---
	struct FF_ControlPointState {
		CHandle<CBaseEntity> entity;
		int pointID;
		int owningTeam;
		float captureProgress[MAX_TEAMS_FF];
		bool isLocked;

		FF_ControlPointState() { Reset(); }
		void Reset() {
			entity = NULL;
			pointID = -1;
			owningTeam = TEAM_ID_NONE;
			for(int i=0; i < MAX_TEAMS_FF; ++i) captureProgress[i] = 0.0f;
			isLocked = false;
		}
	};

	const FF_ControlPointState* GetControlPointInfo(int cpID) const;
	int GetControlPointOwner(int cpID) const;
	float GetControlPointCaptureProgress(int cpID, int team) const;
	bool IsControlPointLocked(int cpID) const;
	int GetNumControlPoints( void ) const { return m_numControlPoints; }


	// --- VIP Escort State Tracking ---
	CFFPlayer* GetVIP() const;
	bool IsVIPAlive() const;
	bool IsVIPEscaped() const;

private:
	// CTF Helpers
	void InitializeFlagState(int teamID, const char* flagEntityName);
	int GetFlagTeamFromEntity(CBaseEntity* pFlagEntity) const;

	// CP Helpers
	void InitializeControlPointStates(const char* cpEntityClassName);
	int GetCPIDFromEntity(CBaseEntity* pCPEntity) const;

	// VIP Helper
	void InitializeVIPState( void );
    bool IsPlayerVIP(CFFPlayer* pPlayer) const; // Helper to check if a player is the VIP

	CFFBot *m_owner;
	bool m_isRoundOver;

	// For CTF
	FF_FlagState m_Flags[MAX_PLAYABLE_TEAMS_FF];

	// For Control Points
	FF_ControlPointState m_ControlPoints[MAX_CONTROL_POINTS_FF];
	int m_numControlPoints;

	// For VIP
	CHandle<CFFPlayer> m_vipPlayer;
	bool m_isVIPAlive;
	bool m_isVIPEscaped;
};

#endif // _FF_GAME_STATE_H_
