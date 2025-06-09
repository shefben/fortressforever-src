//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Manager for Fortress Forever Bots.
//
// $NoKeywords: $
//=============================================================================//

#ifndef FF_BOT_MANAGER_H
#define FF_BOT_MANAGER_H

#include "bot_manager.h"
#include "nav_area.h"
#include "bot_util.h"
#include "bot_profile.h"
#include "../../shared/ff/ff_shareddefs.h"
#include "../ff_player.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "bot_constants.h"
#include "igameevents.h"
#include "ehandle.h" // For CHandle

extern ConVar friendlyfire;
extern ConVar cv_bot_difficulty;
extern ConVar mp_freezetime;

class CFFBot;
class CBasePlayerWeapon;
class IGameEvent;
class CCommand;

#ifndef TEAM_UNASSIGNED
#define TEAM_UNASSIGNED 0
#endif
#ifndef TEAM_RED
#define TEAM_RED 1
#endif
#ifndef TEAM_BLUE
#define TEAM_BLUE 2
#endif
// Assuming MAX_PLAYABLE_TEAMS_FF is defined in bot_constants.h or similar if used for array sizes
// For now, let's assume it's available or use MAX_TEAMS_FF from here.
#ifndef MAX_PLAYABLE_TEAMS_FF // If not defined elsewhere for CFFBotManager context
#define MAX_PLAYABLE_TEAMS_FF 2 // Default for Red vs Blue CTF
#endif


// Placeholder for bot goal type, ensure this matches definition used in Lua scripts
// This might ideally be in bot_constants.h or a shared enum header.
#ifndef BOT_GOAL_TYPE_FLAG_CAP // Using a more C++ like naming convention
#define BOT_GOAL_TYPE_FLAG_CAP 6 // Example value, must match Lua's Bot.kFlagCap
#endif


inline int OtherTeam( int team ) {
    if (team == TEAM_RED) return TEAM_BLUE;
    if (team == TEAM_BLUE) return TEAM_RED;
    return TEAM_UNASSIGNED;
}

class CFFBotManager;
inline CFFBotManager *TheFFBots( void ) { return reinterpret_cast< CFFBotManager * >( TheBots ); }

class BotEventInterface : public IGameEventListener2 {
public:
	virtual const char *GetEventName( void ) const = 0;
};

#define DECLARE_BOTMANAGER_EVENT_LISTENER( BotManagerSingleton, EventClass, EventName ) \
	public: \
	virtual void On##EventClass( IGameEvent *data ); \
	private: \
	class EventClass##Event : public BotEventInterface \
	{ \
		bool m_enabled; \
	public: \
		EventClass##Event( CFFBotManager* pMgr ) \
		{ \
			gameeventmanager->AddListener( pMgr->Get##EventClass##Listener(), #EventName, true ); \
			m_enabled = true; \
		} \
		~EventClass##Event( void ) {} \
		virtual const char *GetEventName( void ) const { return #EventName; } \
		void Enable( bool enable, CFFBotManager* pMgr ) { \
			m_enabled = enable; \
			if (!gameeventmanager) return; \
			if ( enable ) gameeventmanager->AddListener( pMgr->Get##EventClass##Listener(), #EventName, true ); \
			else gameeventmanager->RemoveListener( pMgr->Get##EventClass##Listener() ); \
		} \
		bool IsEnabled( void ) const { return m_enabled; } \
	}; \
	friend class EventClass##Event; \
	EventClass##Event* Get##EventClass##Listener() { return &m_##EventClass##Event; } \
	EventClass##Event m_##EventClass##Event;

#define DECLARE_FFBOTMANAGER_EVENT_LISTENER( EventClass, EventName ) DECLARE_BOTMANAGER_EVENT_LISTENER( TheFFBots, EventClass, EventName )

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

class CFFBotManager : public CBotManager {
public:
	CFFBotManager();
	virtual CBasePlayer *AllocateBotEntity( void );
	virtual void ClientDisconnect( CBaseEntity *entity );
	virtual bool ClientCommand( CBasePlayer *player, const CCommand &args );
	virtual void ServerActivate( void );
	virtual void ServerDeactivate( void );
	virtual bool ServerCommand( const char *cmd );
	bool IsServerActive( void ) const { return m_serverActive; }
	virtual void RestartRound( void );
	virtual void StartFrame( void );
	virtual unsigned int GetPlayerPriority( CBasePlayer *player ) const;
	virtual bool IsImportantPlayer( CFFPlayer *player ) const;
	void ExtractScenarioData( void );
	static BotDifficultyType GetDifficultyLevel( void );
	enum GameScenarioType {
		SCENARIO_UNKNOWN, SCENARIO_ARENA, SCENARIO_CAPTURETHEFLAG,
		SCENARIO_CONTROLPOINT, SCENARIO_VIP, SCENARIO_INVADE
	};
	GameScenarioType GetScenario( void ) const { return m_gameScenario; }
	enum { MAX_ZONES = 8 };
	enum { MAX_ZONE_NAV_AREAS = 32 };
	struct Zone {
		CHandle<CBaseEntity> m_entity; // Use CHandle for safety
        string_t m_iszEntityName;     // Store entity name for reference/debugging
		CNavArea *m_area[ MAX_ZONE_NAV_AREAS ];
        int m_areaCount;
		Vector m_center;
        int m_index; // For CPs, this could be the 0-indexed pointID. For flags, team or type.
        int m_team;  // For team-specific zones like flag stands, cap points
		bool m_isBlocked;
        Extent m_extent;
	};
	const Zone *GetZone( int i ) const;
	const Zone *GetZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const CBaseEntity *entity ) const;
	int GetZoneCount( void ) const { return m_zoneCount; }
	void CheckForBlockedZones( void );
	const Vector *GetRandomPositionInZone( const Zone *zone ) const;
	CNavArea *GetRandomAreaInZone( const Zone *zone ) const;
	template< typename CostFunctor >
	const Zone *GetClosestZone( CNavArea *startArea, CostFunctor costFunc, float *travelDistance = NULL ) const;
	const Zone *GetRandomZone( void ) const;
	CBaseEntity *GetRandomSpawn( int team = TEAM_UNASSIGNED ) const;
    CBaseEntity *GetTeamCaptureZone(int teamID) const; // New accessor

	float GetLastSeenEnemyTimestamp( void ) const { return m_lastSeenEnemyTimestamp; }
	void SetLastSeenEnemyTimestamp( void ) { m_lastSeenEnemyTimestamp = gpGlobals->curtime; }
	float GetRoundStartTime( void ) const { return m_roundStartTimestamp; }
	float GetElapsedRoundTime( void ) const { return gpGlobals->curtime - m_roundStartTimestamp; }
	bool AllowRogues( void ) const;
	bool AllowPistols( void ) const;
	bool AllowShotguns( void ) const;
	bool AllowSubMachineGuns( void ) const;
	bool AllowRifles( void ) const;
	bool AllowMachineGuns( void ) const;
	bool AllowGrenades( void ) const;
	bool AllowSnipers( void ) const;
	bool AllowFriendlyFireDamage( void ) const;
	bool IsWeaponUseable( const CFFWeaponBase *weapon ) const;
	bool IsDefenseRushing( void ) const { return m_isDefenseRushing; }
	bool IsOnDefense( const CFFPlayer *player ) const;
	bool IsOnOffense( const CFFPlayer *player ) const;
	bool IsRoundOver( void ) const { return m_isRoundOver; }
	bool BotAddCommand( int team, bool isFromConsole = false, const char *profileName = NULL, FFWeaponID specificWeaponID = FF_WEAPON_NONE, BotDifficultyType difficulty = NUM_DIFFICULTY_LEVELS );

private:
	void MaintainBotQuota( void );
	static bool m_isMapDataLoaded;
	bool m_serverActive;
	GameScenarioType m_gameScenario;
	Zone m_zone[ MAX_ZONES ];							
	int m_zoneCount;
    CHandle<CBaseEntity> m_teamCaptureZones[MAX_PLAYABLE_TEAMS_FF]; // Store capture zone entities per team

	bool m_isRoundOver;
	CountdownTimer m_checkTransientAreasTimer;
	float m_lastSeenEnemyTimestamp;
	float m_roundStartTimestamp;
	bool m_isDefenseRushing;

	// Event Handlers
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFootstep,		player_footstep )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerRadio,			player_radio )
	// ... (other DECLARE_FFBOTMANAGER_EVENT_LISTENER calls as before) ...
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerDeath,			player_death )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFallDamage,		player_falldamage )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundEnd,				round_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundStart,			round_start )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundFreezeEnd,		round_freeze_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DoorMoving,			door_moving )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakProp,				break_prop )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakBreakable,		break_breakable )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFire,			weapon_fire )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFireOnEmpty,		weapon_fire_on_empty )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponReload,			weapon_reload )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponZoom,			weapon_zoom )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BulletImpact,			bullet_impact )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HEGrenadeDetonate,		hegrenade_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FlashbangDetonate,		flashbang_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SmokeGrenadeDetonate,	smokegrenade_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( GrenadeBounce,			grenade_bounce )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( NavBlocked,			nav_blocked )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( ServerShutdown,		server_shutdown )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagCaptured, "ff_flag_captured" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagDropped, "ff_flag_dropped" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagPickedUp, "ff_flag_picked_up" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagReturned, "ff_flag_returned" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointCaptured, "ff_point_captured" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointStatusUpdate, "ff_point_status" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointBlocked, "ff_point_blocked" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPSelected, "ff_vip_selected" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPKilled, "ff_vip_killed" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPEscaped, "ff_vip_escaped" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PlayerSpawn, "player_spawn" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( LuaEvent, "luaevent" )

	CUtlVector< BotEventInterface * > m_commonEventListeners;
	bool m_eventListenersEnabled;
	void EnableEventListeners( bool enable );
};

inline CBasePlayer *CFFBotManager::AllocateBotEntity( void ) { return static_cast<CBasePlayer *>( CreateEntityByName( "ff_bot" ) ); }
inline const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const CBaseEntity *entity ) const {
	if (entity == NULL) return NULL;
	Vector centroid = entity->GetAbsOrigin();
	return GetClosestZone( centroid );
}

#endif // FF_BOT_MANAGER_H
