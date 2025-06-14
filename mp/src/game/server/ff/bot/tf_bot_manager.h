//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_manager.h
// Team Fortress NextBotManager
// Tom Bui, May 2010

#ifndef FF_BOT_MANAGER_H
#define FF_BOT_MANAGER_H

#include "NextBotManager.h"
#include "ff_team.h"

class CFFBot;
class CFFPlayer;
class CFFBotSquad;
class CStuckBotEvent;



//----------------------------------------------------------------------------------------------
// For parsing and displaying stuck events from server logs.
class CStuckBot
{
public:
	CStuckBot( int id, const char *name )
	{
		m_id = id;
		Q_strncpy( m_name, name, 256 );
	}

	bool IsMatch( int id, const char *name )
	{
		return ( id == m_id && FStrEq( name, m_name ) );
	}

	char m_name[256];
	int m_id;

	CUtlVector< CStuckBotEvent * > m_stuckEventVector;
};



//----------------------------------------------------------------------------------------------
// For parsing and displaying stuck events from server logs.
class CStuckBotEvent
{
public:
	Vector m_stuckSpot;
	float m_stuckDuration;
	Vector m_goalSpot;
	bool m_isGoalValid;

	void Draw( float deltaT = 0.1f )
	{
		NDebugOverlay::Cross3D( m_stuckSpot, 5.0f, 255, 255, 0, true, deltaT );

		if ( m_isGoalValid )
		{
			if ( m_stuckDuration > 6.0f )
			{
				NDebugOverlay::HorzArrow( m_stuckSpot, m_goalSpot, 2.0f, 255, 0, 0, 255, true, deltaT );
			}
			else if ( m_stuckDuration > 3.0f )
			{
				NDebugOverlay::HorzArrow( m_stuckSpot, m_goalSpot, 2.0f, 255, 255, 0, 255, true, deltaT );
			}
			else
			{
				NDebugOverlay::HorzArrow( m_stuckSpot, m_goalSpot, 2.0f, 0, 255, 0, 255, true, deltaT );
			}
		}
	}
};


//----------------------------------------------------------------------------------------------
class CFFBotManager : public NextBotManager
{
public:
	CFFBotManager();
	virtual ~CFFBotManager();

	virtual void Update();
	void LevelShutdown();

	virtual void OnMapLoaded( void );						// when the server has changed maps
	virtual void OnRoundRestart( void );					// when the scenario restarts

	bool IsAllBotTeam( int iTeam );
	bool IsInOfflinePractice() const;
	bool IsMeleeOnly() const;

	CFFBot* GetAvailableBotFromPool();
	
	void OnForceAddedBots( int iNumAdded );
	void OnForceKickedBots( int iNumKicked );

	void ClearStuckBotData();
	CStuckBot *FindOrCreateStuckBot( int id, const char *playerClass );	// for parsing and debugging stuck bot server logs
	void DrawStuckBotData( float deltaT = 0.1f );

#ifdef FF_CREEP_MODE
	void OnCreepKilled( CFFPlayer *killer );
#endif

	bool RemoveBotFromTeamAndKick( int nTeam );

protected:
	void MaintainBotQuota();
	void SetIsInOfflinePractice( bool bIsInOfflinePractice );
	void RevertOfflinePracticeConvars();

	float m_flNextPeriodicThink;

#ifdef FF_CREEP_MODE
	void UpdateCreepWaves();
	CountdownTimer m_creepWaveTimer;

	void SpawnCreep( int team, CFFBotSquad *squad );
	void SpawnCreepWave( int team );

	int m_creepExperience[ FF_TEAM_COUNT ];
#endif

	void UpdateMedievalBossScenario();
	bool m_isMedeivalBossScenarioSetup;
	void SetupMedievalBossScenario();

	CUtlVector< CBaseEntity * > m_archerSpawnVector;

	struct ArcherAssignmentInfo
	{
		CHandle< CBaseCombatCharacter > m_archer;
		CHandle< CBaseEntity > m_mark;
	};
	CUtlVector< ArcherAssignmentInfo > m_archerMarkVector;

	CountdownTimer m_archerTimer;

	CUtlVector< CStuckBot * > m_stuckBotVector;
	CountdownTimer m_stuckDisplayTimer;
};

// singleton accessor
CFFBotManager &TheTFBots( void );

#endif // FF_BOT_MANAGER_H
