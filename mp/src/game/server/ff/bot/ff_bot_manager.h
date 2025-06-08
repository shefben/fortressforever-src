//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#ifndef FF_BOT_MANAGER_H
#define FF_BOT_MANAGER_H


#include "bot_manager.h"
#include "nav_area.h"
#include "bot_util.h"
#include "bot_profile.h"
#include "ff_shareddefs.h"
#include "ff_player.h"
#include "ff_bot.h" // For CFFBot::BuildableType
#include "util_player_by_index.h" // For UTIL_PlayerByIndex used in macros

//=============================================================================
// FF Team Definitions (Assumed values - replace with actual engine/game definitions if available)
// These are typical values used in Source Engine games.
// Note: ff_shareddefs.h defines team *colors* but not these specific enum IDs.
// CTeamManager::GetTeamName(int i) might be another source if available.
#define FF_TEAM_UNASSIGNED 0  // Typically Unassigned
#define FF_TEAM_SPECTATOR 1   // Typically Spectator
#define FF_TEAM_RED 2         // Fortress Forever Red Team (example value)
#define FF_TEAM_BLUE 3        // Fortress Forever Blue Team (example value)
// Add FF_TEAM_YELLOW, FF_TEAM_GREEN if they are primary playable teams with distinct IDs
// FF_TEAM_NEUTRAL could be a special value if needed for objectives, e.g., -1 or another enum.
// FF_TEAM_COUNT should reflect the number of actual playable teams for array indexing etc.
// For Red vs Blue, this is often 2 (if indexing starts at 0 for playable teams) or 4 (if including unassigned/spec).
// For radio messages, it's likely for playable teams.
#define FF_TEAM_COUNT 2 // Assuming 2 primary playable teams (Red, Blue) for things like radio message arrays.
                        // Adjust if FF has more (e.g. 4-team maps).
#define FF_TEAM_AUTOASSIGN -1 // Or another appropriate value for auto-assignment logic
#define FF_TEAM_NEUTRAL -2    // Example for neutral objectives, if needed
//=============================================================================


extern ConVar friendlyfire;

class CBasePlayerWeapon;

// FF Specific: Define game scenario types
enum FFGameScenarioType
{
	SCENARIO_FF_UNKNOWN,        // Default or unknown
	SCENARIO_FF_ITEM_SCRIPT,    // Maps with info_ff_script (flags, custom objectives)
	SCENARIO_FF_MINECART,       // Maps with ff_minecart (payload-like)
	SCENARIO_FF_MIXED           // Maps with a mix of objectives
};


class CFFBotManager;
extern CFFBotManager g_FFBotManager; // Global instance

// accessor for FF-specific bots
inline CFFBotManager *TheFFBots( void )
{
	// return reinterpret_cast< CFFBotManager * >( TheBots ); // Old way
	return &g_FFBotManager; // New way, directly access the global instance
}

//--------------------------------------------------------------------------------------------------------------
// FF: Added OtherTeam - assuming Red vs Blue for now. Adapt if more primary teams.
inline int OtherTeam( int team )
{
	if ( team == FF_TEAM_RED )
		return FF_TEAM_BLUE;
	if ( team == FF_TEAM_BLUE )
		return FF_TEAM_RED;
	return team; // Return original if not Red or Blue (e.g. spectator, unassigned)
}
//--------------------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------------------
//
// The manager for Fortress Forever specific bots
//
class CFFBotManager : public CBotManager
{
public:
	CFFBotManager();
	virtual ~CFFBotManager(); // Added Destructor

	virtual CBasePlayer *AllocateBotEntity( void );			///< factory method to allocate the appropriate entity for the bot

	virtual void ClientDisconnect( CBaseEntity *entity );
	virtual bool ClientCommand( CBasePlayer *player, const CCommand &args );

	virtual void ServerActivate( void );
	virtual void ServerDeactivate( void );
	virtual bool ServerCommand( const char *cmd );
	bool IsServerActive( void ) const { return m_serverActive; }

	virtual void RestartRound( void );						///< (EXTEND) invoked when a new round begins
	virtual void StartFrame( void );						///< (EXTEND) called each frame

	virtual unsigned int GetPlayerPriority( CBasePlayer *player ) const;	///< return priority of player (0 = max pri)
	virtual bool IsImportantPlayer( CFFPlayer *player ) const;				///< return true if player is important to scenario (capture carrier, etc)

	void ExtractScenarioData( void );							///< search the map entities to determine the game scenario and define important zones

	// difficulty levels -----------------------------------------------------------------------------------------
	static BotDifficultyType GetDifficultyLevel( void )		
	{ 
		if (cv_bot_difficulty.GetFloat() < 0.9f)
			return BOT_EASY;
		if (cv_bot_difficulty.GetFloat() < 1.9f)
			return BOT_NORMAL;
		if (cv_bot_difficulty.GetFloat() < 2.9f)
			return BOT_HARD;

		return BOT_EXPERT;
	}

	// the supported game scenarios ------------------------------------------------------------------------------
	FFGameScenarioType GetScenario( void ) const		{ return m_gameScenario; }

	// "zones" ---------------------------------------------------------------------------------------------------
	// For FF, these can represent flags, control points, minecart paths/goals etc.

	enum { MAX_ZONES = 16 };
	enum { MAX_ZONE_NAV_AREAS = 32 };
	struct Zone
	{
		EHANDLE m_entity;
		CNavArea *m_area[ MAX_ZONE_NAV_AREAS ];
		int m_areaCount;
		Vector m_center;
		int m_zoneID;
		bool m_isBlocked;
		Extent m_extent;
		int m_team;
	};

	const Zone *GetZone( int i ) const				{ return &m_zone[i]; }
	const Zone *GetZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const CBaseEntity *entity ) const;
	int GetZoneCount( void ) const					{ return m_zoneCount; }
	void CheckForBlockedZones( void );


	const Vector *GetRandomPositionInZone( const Zone *zone ) const;
	CNavArea *GetRandomAreaInZone( const Zone *zone ) const;

	template< typename CostFunctor >
	const Zone *GetClosestZone( CNavArea *startArea, CostFunctor costFunc, float *travelDistance = NULL ) const
	{
		const Zone *closeZone = NULL;
		float closeDist = 99999999.9f;

		if (startArea == NULL)
			return NULL;

		for( int i=0; i<m_zoneCount; ++i )
		{
			if (m_zone[i].m_areaCount == 0)
				continue;

			if ( m_zone[i].m_isBlocked )
				continue;

			float dist = NavAreaTravelDistance( startArea, m_zone[i].m_area[0], costFunc );

			if (dist >= 0.0f && dist < closeDist)
			{
				closeZone = &m_zone[i];
				closeDist = dist;
			}
		}

		if (travelDistance)
			*travelDistance = closeDist;

		return closeZone;
	}

	const Zone *GetRandomZone( void ) const
	{
		if (m_zoneCount == 0)
			return NULL;

		int i;
		CUtlVector< const Zone * > unblockedZones;
		for ( i=0; i<m_zoneCount; ++i )
		{
			if ( m_zone[i].m_isBlocked )
				continue;

			unblockedZones.AddToTail( &(m_zone[i]) );
		}

		if ( unblockedZones.Count() == 0 )
			return NULL;

		return unblockedZones[ RandomInt( 0, unblockedZones.Count()-1 ) ];
	}


	CBaseEntity *GetRandomSpawn( int team = FF_TEAM_AUTOASSIGN ) const;


	float GetRadioMessageTimestamp( RadioType event, int teamID ) const;
	float GetRadioMessageInterval( RadioType event, int teamID ) const;
	void SetRadioMessageTimestamp( RadioType event, int teamID );
	void ResetRadioMessageTimestamps( void );

	float GetLastSeenEnemyTimestamp( void ) const	{ return m_lastSeenEnemyTimestamp; }
	void SetLastSeenEnemyTimestamp( void ) 			{ m_lastSeenEnemyTimestamp = gpGlobals->curtime; }

	float GetRoundStartTime( void ) const			{ return m_roundStartTimestamp; }
	float GetElapsedRoundTime( void ) const			{ return gpGlobals->curtime - m_roundStartTimestamp; }

	bool AllowFriendlyFireDamage( void ) const		{ return friendlyfire.GetBool(); }

	bool IsWeaponUseable( const CBasePlayerWeapon *weapon ) const;

	bool IsRoundOver( void ) const					{ return m_isRoundOver; }

	#define FROM_CONSOLE true
	bool BotAddCommand( int team, bool isFromConsole = false, const char *profileName = NULL, int weaponType = 0, BotDifficultyType difficulty = NUM_DIFFICULTY_LEVELS );

private:
	// Event listener management
	class BotEventListener : public IGameEventListener2
	{
	public:
		BotEventListener(CFFBotManager *manager) : m_pManager(manager) {}
		virtual void FireGameEvent(IGameEvent *event) { m_pManager->OnGameEvent(event); }
		virtual int GetEventDebugID(void) { return EVENT_DEBUG_ID_INIT; } // Required by IGameEventListener2
	protected:
		CFFBotManager *m_pManager;
	};
	BotEventListener m_gameEventListener;
	void OnGameEvent(IGameEvent *event); // Centralized event handler
	void RegisterGameEventListeners();
	void UnregisterGameEventListeners();


	enum SkillType { LOW, AVERAGE, HIGH, RANDOM };

	void MaintainBotQuota( void );

	static bool m_isMapDataLoaded;
	bool m_serverActive;

	FFGameScenarioType m_gameScenario;

	Zone m_zone[ MAX_ZONES ];
	int m_zoneCount;

	bool m_isRoundOver;

	CountdownTimer m_checkTransientAreasTimer;

	float m_radioMsgTimestamp[ RADIO_END - RADIO_START_1 ][ FF_TEAM_COUNT ];

	float m_lastSeenEnemyTimestamp;
	float m_roundStartTimestamp;

	// FF_TODO_LUA: Placeholder structures for Lua-defined objective data
	struct LuaObjectivePoint
	{
		char name[MAX_PATH];
		Vector position;
		int teamAffiliation;      // e.g., FF_TEAM_RED, FF_TEAM_BLUE, FF_TEAM_NEUTRAL, or specific team if it's a capture point for one team.
		int type;                 // FF_TODO_LUA: Define enum: e.g., 0=Generic, 1=ControlPoint, 2=FlagSpawn, 3=FlagCapture, 4=PayloadCheckpoint, 5=PayloadGoal. Conceptual mapping from Omnibot::GoalType: 0=Generic/Unknown, 1=FlagGoal(CapturePoint/OmniCapPoint), 2=Item_Flag(OmniFlag), 3=PayloadCheckpoint, 4=PayloadGoal, 5=BombTarget, 6=RescueZone etc.
		float radius;             // Radius for proximity checks.
		bool isActive;            // Is the objective currently active/relevant?
		int currentOwnerTeam;     // For capturable points, which team currently owns it.
		// Add other relevant properties as identified from Lua scripts.
		LuaObjectivePoint() : position(vec3_origin), teamAffiliation(FF_TEAM_NEUTRAL), type(0), radius(100.0f), isActive(true), currentOwnerTeam(FF_TEAM_NEUTRAL) { name[0] = '\0'; }
	};
	CUtlVector<LuaObjectivePoint> m_luaObjectivePoints;

	struct LuaPathPoint // For things like minecart paths defined in Lua
	{
		char name[MAX_PATH];
		Vector position;
		int order;                // Sequence order for the path.
		float waitTime;           // How long to potentially wait at this node.
		int pathID;               // If multiple paths exist.
		// Add other properties like "isCartStop", "triggersEvent", etc.
		LuaPathPoint() : position(vec3_origin), order(0), waitTime(0.0f), pathID(0) { name[0] = '\0'; }
	};
	CUtlVector<LuaPathPoint> m_luaPathPoints;
	// FF_TODO_LUA: Add more structures as needed for different types of Lua-driven game elements relevant to bots

public:
	// FF_TODO_LUA: Accessors for Lua-defined objective data
	const CUtlVector<LuaObjectivePoint>& GetAllLuaObjectivePoints() const;
	int GetLuaObjectivePointCount() const;
	const LuaObjectivePoint* GetLuaObjectivePoint(int index) const;

	const CUtlVector<LuaPathPoint>& GetAllLuaPathPoints() const;
	int GetLuaPathPointCount() const;
	const LuaPathPoint* GetLuaPathPoint(int index) const;

	// Helper to get BuildableType from an entity
	static CFFBot::BuildableType GetBuildableTypeFromEntity( CBaseEntity *pBuildable );


	// Event Handlers - These will be called by OnGameEvent
	void OnPlayerDeath( IGameEvent *data );
	void OnPlayerFootstep( IGameEvent *data );
	void OnPlayerRadio( IGameEvent *data );
	void OnPlayerFallDamage( IGameEvent *data );
	void OnDoorMoving( IGameEvent *data );
	void OnBreakProp( IGameEvent *data );
	void OnBreakBreakable( IGameEvent *data );
	void OnWeaponFire( IGameEvent *data );
	void OnWeaponFireOnEmpty( IGameEvent *data );
	void OnWeaponReload( IGameEvent *data );
	void OnBulletImpact( IGameEvent *data );
	void OnHEGrenadeDetonate( IGameEvent *data );
	void OnGrenadeBounce( IGameEvent *data );
	void OnNavBlocked( IGameEvent *data );
	void OnServerShutdown( IGameEvent *data );
	void OnRoundStart( IGameEvent *data );
	void OnRoundEnd( IGameEvent *data );
	void OnRoundFreezeEnd( IGameEvent *data );
	void OnFFRestartRound( IGameEvent *data );
	void OnPlayerChangeClass( IGameEvent *data );
	void OnPlayerChangeTeam( IGameEvent *data );
	void OnDisguiseLost( IGameEvent *data );
	void OnCloakLost( IGameEvent *data );
	void OnBuildDispenser( IGameEvent *data );
	void OnBuildSentryGun( IGameEvent *data );
	void OnBuildDetpack( IGameEvent *data );
	void OnBuildManCannon( IGameEvent *data );
	void OnBuildableBuilt( IGameEvent *data );
	void OnDispenserKilled( IGameEvent *data );
	void OnDispenserDismantled( IGameEvent *data );
	void OnDispenserDetonated( IGameEvent *data );
	void OnDispenserSabotaged( IGameEvent *data );
	void OnBuildableSapperRemoved( IGameEvent *data );
	void OnSentryGunKilled( IGameEvent *data );
	void OnSentryGunDismantled( IGameEvent *data );
	void OnSentryGunDetonated( IGameEvent *data );
	void OnSentryGunUpgraded( IGameEvent *data );
	void OnSentryGunSabotaged( IGameEvent *data );
	void OnDetpackDetonated( IGameEvent *data );
	void OnManCannonDetonated( IGameEvent *data );

};

inline CBasePlayer *CFFBotManager::AllocateBotEntity( void )
{
	return static_cast<CBasePlayer *>( CreateEntityByName( "ff_bot" ) );
}


inline const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const CBaseEntity *entity ) const
{
	if (entity == NULL)
		return NULL;

	Vector centroid = entity->GetAbsOrigin();
	// centroid.z += HalfHumanHeight; // This might need adjustment based on typical FF player bbox
	return GetClosestZone( centroid );
}

#endif // FF_BOT_MANAGER_H
