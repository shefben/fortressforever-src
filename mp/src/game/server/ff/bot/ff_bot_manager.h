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

// accessor for FF-specific bots
inline CFFBotManager *TheFFBots( void )
{
	return reinterpret_cast< CFFBotManager * >( TheBots );
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
class BotEventInterface : public IGameEventListener2
{
public:
	virtual const char *GetEventName( void ) const = 0;
};

//--------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------
/**
 * Macro to set up an OnEventClass() in TheFFBots.
 */
#define DECLARE_BOTMANAGER_EVENT_LISTENER( BotManagerSingleton, EventClass, EventName ) \
	public: \
	virtual void On##EventClass( IGameEvent *data ); \
	private: \
	class EventClass##Event : public BotEventInterface \
	{ \
		bool m_enabled; \
	public: \
		EventClass##Event( void ) \
		{ \
			gameeventmanager->AddListener( this, #EventName, true ); \
			m_enabled = true; \
		} \
		~EventClass##Event( void ) \
		{ \
			if ( m_enabled ) gameeventmanager->RemoveListener( this ); \
		} \
		virtual const char *GetEventName( void ) const \
		{ \
			return #EventName; \
		} \
		void Enable( bool enable ) \
		{ \
			m_enabled = enable; \
			if ( enable ) \
				gameeventmanager->AddListener( this, #EventName, true ); \
			else \
				gameeventmanager->RemoveListener( this ); \
		} \
		bool IsEnabled( void ) const { return m_enabled; } \
		void FireGameEvent( IGameEvent *event ) \
		{ \
			BotManagerSingleton()->On##EventClass( event ); \
		} \
	}; \
	EventClass##Event m_##EventClass##Event;


//--------------------------------------------------------------------------------------------------------------
#define DECLARE_FFBOTMANAGER_EVENT_LISTENER( EventClass, EventName ) DECLARE_BOTMANAGER_EVENT_LISTENER( TheFFBots, EventClass, EventName )


//--------------------------------------------------------------------------------------------------------------
/**
 * Macro to propogate an event from the bot manager to all bots
 */
#define CFFBOTMANAGER_ITERATE_BOTS( Callback, arg1 ) \
	{ \
		for ( int idx = 1; idx <= gpGlobals->maxClients; ++idx ) \
		{ \
			CBasePlayer *player = UTIL_PlayerByIndex( idx ); \
			if (player == NULL) continue; \
			if (!player->IsBot()) continue; \
			CFFBot *bot = dynamic_cast< CFFBot * >(player); \
			if ( !bot ) continue; \
			bot->Callback( arg1 ); \
		} \
	}


//--------------------------------------------------------------------------------------------------------------
//
// The manager for Fortress Forever specific bots
//
class CFFBotManager : public CBotManager
{
public:
	CFFBotManager();

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

	// FF_TODO_WEAPONS: Consider adding AllowPistols, AllowShotguns etc. if FF bots need ConVar checks for weapon categories
	// bool AllowPistols( void ) const				{ return cv_bot_allow_pistols.GetBool(); }
	// bool AllowShotguns( void ) const				{ return cv_bot_allow_shotguns.GetBool(); }
	// etc.

	bool AllowFriendlyFireDamage( void ) const		{ return friendlyfire.GetBool(); }

	bool IsWeaponUseable( const CBasePlayerWeapon *weapon ) const;	// In CS, this checks weapon allowance ConVars. Adapt for FF if needed.

	bool IsRoundOver( void ) const					{ return m_isRoundOver; }

	#define FROM_CONSOLE true
	// FF_TODO_WEAPONS: weaponType parameter might be better as FFWeaponID or similar if specific weapon spawning is desired for bots.
	bool BotAddCommand( int team, bool isFromConsole = false, const char *profileName = NULL, int weaponType = 0, BotDifficultyType difficulty = NUM_DIFFICULTY_LEVELS );

private:
	enum SkillType { LOW, AVERAGE, HIGH, RANDOM };

	void MaintainBotQuota( void );

	static bool m_isMapDataLoaded;
	bool m_serverActive;

	FFGameScenarioType m_gameScenario;

	Zone m_zone[ MAX_ZONES ];
	int m_zoneCount;

	// CS-specific members removed:
	// bool m_isBombPlanted;
	// float m_bombPlantTimestamp;
	// float m_earliestBombPlantTimestamp;
	// CFFPlayer *m_bombDefuser; // Was CCSPlayer
	// EHANDLE m_looseBomb;
	// CNavArea *m_looseBombArea;
	// bool m_isDefenseRushing;

	bool m_isRoundOver;

	CountdownTimer m_checkTransientAreasTimer;

	float m_radioMsgTimestamp[ RADIO_END - RADIO_START_1 ][ FF_TEAM_COUNT ];

	float m_lastSeenEnemyTimestamp;
	float m_roundStartTimestamp;

	// Event Handlers --------------------------------------------------------------------------------------------
	// Generic events likely still relevant for FF
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerDeath,			player_death )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFootstep,		player_footstep )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerRadio,			player_radio )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFallDamage,		player_falldamage )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DoorMoving,			door_moving )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakProp,				break_prop )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakBreakable,		break_breakable )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFire,			weapon_fire )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFireOnEmpty,		weapon_fire_on_empty )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponReload,			weapon_reload )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BulletImpact,			bullet_impact )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HEGrenadeDetonate,		hegrenade_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( GrenadeBounce,			grenade_bounce )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( NavBlocked,			nav_blocked )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( ServerShutdown,		server_shutdown )

	// Round events - ff_restartround might be more specific
	// FF_TODO_EVENTS: Verify if round_start, round_end, round_freeze_end are directly used or if ff_restartround covers all needs.
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundStart,			round_start )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundEnd,				round_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundFreezeEnd,		round_freeze_end ) // If FF has freeze period, keep this

	// FF Specific event listeners
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FFRestartRound,		ff_restartround ) // Specific FF event for round restarts
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerChangeClass,		player_changeclass )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerChangeTeam,		player_changeteam ) // Added: useful for bot team balancing/management
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DisguiseLost,			disguise_lost ) // Spy related
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( CloakLost,			    cloak_lost )    // Spy related

	// Buildable events (These seem comprehensive for FF)
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BuildDispenser,		build_dispenser )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BuildSentryGun,		build_sentrygun )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BuildDetpack,		    build_detpack )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BuildManCannon,		build_mancannon )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DispenserKilled,		dispenser_killed )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DispenserDismantled,	dispenser_dismantled )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DispenserDetonated,	dispenser_detonated )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DispenserSabotaged,	dispenser_sabotaged ) // Engineer building sabotaged by Spy

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SentryGunKilled,		sentrygun_killed )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SentryGunDismantled,	sentrygun_dismantled ) // Corrected from sentry_dismantled
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SentryGunDetonated,	sentrygun_detonated )  // Corrected from sentry_detonated
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SentryGunUpgraded,		sentrygun_upgraded )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SentryGunSabotaged,	sentrygun_sabotaged )  // Engineer building sabotaged by Spy

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DetpackDetonated,	    detpack_detonated )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( ManCannonDetonated,	mancannon_detonated )

	// FF_TODO_EVENTS: Consider other game events like flag_captured, flag_dropped, point_captured, etc.
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( FlagCaptured,          item_captured ) // Example, verify actual event name
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( CapturePointCaptured,  point_captured ) // Example, verify actual event name


	CUtlVector< BotEventInterface * > m_commonEventListeners;
	bool m_eventListenersEnabled;
	void EnableEventListeners( bool enable );

	// FF_LUA_INTEGRATION: Placeholder structures for Lua-defined objective data
	struct LuaObjectivePoint
	{
		char name[MAX_PATH];
		Vector position;
		int teamAffiliation;      // e.g., FF_TEAM_RED, FF_TEAM_BLUE, FF_TEAM_NEUTRAL, or specific team if it's a capture point for one team.
		int type;                 // FF_LUA_TODO: Define enum: e.g., 0=Generic, 1=ControlPoint, 2=FlagSpawn, 3=FlagCapture, 4=PayloadCheckpoint, 5=PayloadGoal
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
	// Add more structures as needed for different types of Lua-driven game elements relevant to bots

public:
	// FF_LUA_INTEGRATION: Accessors for Lua-defined objective data
	const CUtlVector<LuaObjectivePoint>& GetAllLuaObjectivePoints() const;
	int GetLuaObjectivePointCount() const;
	const LuaObjectivePoint* GetLuaObjectivePoint(int index) const;

	const CUtlVector<LuaPathPoint>& GetAllLuaPathPoints() const;
	int GetLuaPathPointCount() const;
	const LuaPathPoint* GetLuaPathPoint(int index) const;
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
