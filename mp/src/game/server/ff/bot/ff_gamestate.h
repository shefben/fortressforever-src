//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encapsulation of the current scenario/game state for Fortress Forever bots.
//
// $NoKeywords: $
//=============================================================================//

#ifndef _FF_GAME_STATE_H_
#define _FF_GAME_STATE_H_

#include "igameevents.h"
#include "ehandle.h"
#include "mathlib/vector.h"
#include "string_t.h" // For string_t

// Forward declarations
class CFFBot;
class CFFPlayer;
class CBaseEntity;
struct lua_State;

#define MAX_TEAMS_FF 4
#define MAX_PLAYABLE_TEAMS_FF 2
#define MAX_CONTROL_POINTS_FF 8

#define TEAM_ID_NONE -1
#define TEAM_ID_RED 0
#define TEAM_ID_BLUE 1

#define FF_FLAG_STATE_HOME 0
#define FF_FLAG_STATE_CARRIED 1
#define FF_FLAG_STATE_DROPPED 2

class FFGameState
{
public:
	FFGameState( CFFBot *owner );

	void Reset( void );
	void Update( void );

	void OnRoundEnd( IGameEvent *event );
	void OnRoundStart( IGameEvent *event );

	void OnFlagPickedUp(CBaseEntity* pFlagEntity, CFFPlayer* pPlayer);
	void OnFlagDropped(CBaseEntity* pFlagEntity, const Vector& dropLocation);
	void OnFlagCaptured(CBaseEntity* pFlagEntity, CFFPlayer* pCapturer);
	void OnFlagReturned(CBaseEntity* pFlagEntity);

	void OnControlPointCaptured(CBaseEntity* pCPEntity, int newOwnerTeam);
	void OnControlPointProgress(CBaseEntity* pCPEntity, int teamMakingProgress, float newProgress);
	void OnControlPointBlocked(CBaseEntity* pCPEntity, bool isBlocked);

	void OnVIPKilled(CFFPlayer* pVIPVictim, CBaseEntity* pKiller);
	void OnVIPEscaped(CFFPlayer* pVIP);
	void OnPlayerSpawn(CFFPlayer* pPlayer);

	bool IsRoundOver( void ) const;

	enum { UNKNOWN_ZONE = -1 };

	struct FF_FlagState {
		CHandle<CBaseEntity> entity;
		string_t m_iszEntityName; // Store entity name for reference
		int teamAffiliation;
		int currentState;
		CHandle<CFFPlayer> carrier;
		Vector dropLocation;
		Vector entitySpawnLocation; // Position of the flag stand/spawn
		float returnTime;
		FF_FlagState() { Reset(); }
		void Reset() {
			entity = NULL; m_iszEntityName = NULL_STRING; teamAffiliation = TEAM_ID_NONE; currentState = FF_FLAG_STATE_HOME;
			carrier = NULL; dropLocation.Init(); entitySpawnLocation.Init(); returnTime = -1.0f;
		}
	};

	const FF_FlagState* GetFlagInfo(int team) const;
	bool IsTeamFlagHome(int team) const;
	bool IsTeamFlagCarried(int team, CFFPlayer** pCarrier = NULL) const;
	bool IsTeamFlagDropped(int team, Vector* pDropLocation = NULL) const;
    bool IsOtherTeamFlagAtOurBase(int myTeam) const;

	struct FF_ControlPointState {
		CHandle<CBaseEntity> entity;
		string_t m_iszEntityName; // Store entity name for reference
		int pointID; // 0-indexed, must correspond to Lua command_points[pointID+1]
		int owningTeam;
		float captureProgress[MAX_TEAMS_FF];
		bool isLocked;
		FF_ControlPointState() { Reset(); }
		void Reset() {
			entity = NULL; m_iszEntityName = NULL_STRING; pointID = -1; owningTeam = TEAM_ID_NONE;
			for(int i=0; i < MAX_TEAMS_FF; ++i) captureProgress[i] = 0.0f;
			isLocked = false;
		}
	};

	const FF_ControlPointState* GetControlPointInfo(int cpID) const; // cpID is 0-indexed
	int GetControlPointOwner(int cpID) const;
	float GetControlPointCaptureProgress(int cpID, int team) const;
	bool IsControlPointLocked(int cpID) const;
	int GetNumControlPoints( void ) const { return m_numControlPoints; }

	CFFPlayer* GetVIP() const;
	bool IsVIPAlive() const;
	bool IsVIPEscaped() const;

private:
    // Flag entity name is the actual flag item, stand name is its base/spawn location point
	void InitializeFlagState(int teamID, const char* flagEntityName, const char* flagStandEntityName);
	int GetFlagTeamFromEntity(CBaseEntity* pFlagEntity) const;

	void InitializeControlPointStates(const char* cpEntityClassName);
	int GetCPIDFromEntity(const CBaseEntity* pCPEntity) const;

	void InitializeVIPState( void );
    bool IsPlayerVIP(CFFPlayer* pPlayer) const;

	CFFBot *m_owner;
	bool m_isRoundOver;
	float m_nextCPPollTime;

	FF_FlagState m_Flags[MAX_PLAYABLE_TEAMS_FF];
	FF_ControlPointState m_ControlPoints[MAX_CONTROL_POINTS_FF];
	int m_numControlPoints;
	CHandle<CFFPlayer> m_vipPlayer;
	bool m_isVIPAlive;
	bool m_isVIPEscaped;
};

#endif // _FF_GAME_STATE_H_
