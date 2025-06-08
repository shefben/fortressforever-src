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
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase and FFWeaponID
#include "bot_constants.h"  // For TEAM_TERRORIST, TEAM_CT (to be replaced), RadioType, BotDifficultyType etc.
#include "igameevents.h"

extern ConVar friendlyfire;
extern ConVar cv_bot_difficulty;
extern ConVar mp_freezetime;

// Forward declarations
class CFFBot;
class CBasePlayerWeapon;
class IGameEvent;
class CCommand;

// Team definitions (ensure these align with your game's constants, e.g., from ff_shareddefs.h)
// These are examples if not already globally defined and included.
#ifndef TEAM_UNASSIGNED
#define TEAM_UNASSIGNED 0 // Or another appropriate value for unassigned/spectator
#endif
#ifndef TEAM_RED
#define TEAM_RED 1 // Example for FF Red team
#endif
#ifndef TEAM_BLUE
#define TEAM_BLUE 2 // Example for FF Blue team
#endif
// Add TEAM_YELLOW, TEAM_GREEN if they are playable combat teams

/**
 * Given one team, return the other
 */
// TODO_FF: This needs to handle FF's full team roster (RED, BLUE, YELLOW, GREEN)
// and correctly determine "enemy" in multi-team scenarios or FFA.
// For a simple 2-team (RED vs BLUE) setup for now:
inline int OtherTeam( int team )
{
    if (team == TEAM_RED) return TEAM_BLUE;
    if (team == TEAM_BLUE) return TEAM_RED;
    return TEAM_UNASSIGNED; // Or some other appropriate default for non-combatant or error
}

class CFFBotManager;

inline CFFBotManager *TheFFBots( void )
{
	return reinterpret_cast< CFFBotManager * >( TheBots );
}

//--------------------------------------------------------------------------------------------------------------
class BotEventInterface : public IGameEventListener2
{
public:
	virtual const char *GetEventName( void ) const = 0;
};

//--------------------------------------------------------------------------------------------------------------
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
		~EventClass##Event( void ) \
		{ \
		} \
		virtual const char *GetEventName( void ) const \
		{ \
			return #EventName; \
		} \
		void Enable( bool enable, CFFBotManager* pMgr ) \
		{ \
			m_enabled = enable; \
			if (!gameeventmanager) return; \
			if ( enable ) \
				gameeventmanager->AddListener( pMgr->Get##EventClass##Listener(), #EventName, true ); \
			else \
				gameeventmanager->RemoveListener( pMgr->Get##EventClass##Listener() ); \
		} \
		bool IsEnabled( void ) const { return m_enabled; } \
	}; \
	friend class EventClass##Event; \
	EventClass##Event* Get##EventClass##Listener() { return &m_##EventClass##Event; } \
	EventClass##Event m_##EventClass##Event;

//--------------------------------------------------------------------------------------------------------------
#define DECLARE_FFBOTMANAGER_EVENT_LISTENER( EventClass, EventName ) DECLARE_BOTMANAGER_EVENT_LISTENER( TheFFBots, EventClass, EventName )

//--------------------------------------------------------------------------------------------------------------
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
class CFFBotManager : public CBotManager
{
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

	enum GameScenarioType
	{
		SCENARIO_UNKNOWN,        // Default or if mode can't be determined
		SCENARIO_ARENA,          // For general combat/DM if no specific objectives
		SCENARIO_CAPTURETHEFLAG,
		SCENARIO_CONTROLPOINT,
		SCENARIO_VIP,            // Generic VIP
		SCENARIO_INVADE          // Example additional FF mode
	};
	GameScenarioType GetScenario( void ) const		{ return m_gameScenario; }

	enum { MAX_ZONES = 8 };
	enum { MAX_ZONE_NAV_AREAS = 32 };
	struct Zone
	{
		CBaseEntity *m_entity;
		CNavArea *m_area[ MAX_ZONE_NAV_AREAS ];
		int m_areaCount;
		Vector m_center;
		int m_index;
		bool m_isBlocked;
		Extent m_extent;
	};

	const Zone *GetZone( int i ) const;
	const Zone *GetZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const Vector &pos ) const;
	const Zone *GetClosestZone( const CBaseEntity *entity ) const;
	int GetZoneCount( void ) const					{ return m_zoneCount; }
	void CheckForBlockedZones( void );

	const Vector *GetRandomPositionInZone( const Zone *zone ) const;
	CNavArea *GetRandomAreaInZone( const Zone *zone ) const;

	template< typename CostFunctor >
	const Zone *GetClosestZone( CNavArea *startArea, CostFunctor costFunc, float *travelDistance = NULL ) const;

	const Zone *GetRandomZone( void ) const;
	CBaseEntity *GetRandomSpawn( int team = TEAM_UNASSIGNED ) const;

	// Removed methods related to bot's own radio message cooldowns
	// float GetRadioMessageTimestamp( RadioType event, int teamID ) const;
	// float GetRadioMessageInterval( RadioType event, int teamID ) const;
	// void SetRadioMessageTimestamp( RadioType event, int teamID );
	// void ResetRadioMessageTimestamps( void );

	float GetLastSeenEnemyTimestamp( void ) const	{ return m_lastSeenEnemyTimestamp; }
	void SetLastSeenEnemyTimestamp( void ) 			{ m_lastSeenEnemyTimestamp = gpGlobals->curtime; }

	float GetRoundStartTime( void ) const			{ return m_roundStartTimestamp; }
	float GetElapsedRoundTime( void ) const			{ return gpGlobals->curtime - m_roundStartTimestamp; }

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
	bool IsDefenseRushing( void ) const				{ return m_isDefenseRushing; }
	bool IsOnDefense( const CFFPlayer *player ) const;
	bool IsOnOffense( const CFFPlayer *player ) const;
	bool IsRoundOver( void ) const					{ return m_isRoundOver; }

	// Using FFWeaponID from ff_weapon_base.h for weapon type clarity
	bool BotAddCommand( int team, bool isFromConsole = false, const char *profileName = NULL, FFWeaponID specificWeaponID = FF_WEAPON_NONE, BotDifficultyType difficulty = NUM_DIFFICULTY_LEVELS );

private:
	void MaintainBotQuota( void );

	static bool m_isMapDataLoaded;
	bool m_serverActive;
	GameScenarioType m_gameScenario;
	Zone m_zone[ MAX_ZONES ];							
	int m_zoneCount;
	bool m_isRoundOver;
	CountdownTimer m_checkTransientAreasTimer;
	// float m_radioMsgTimestamp[ BOT_RADIO_COUNT ][ 2 ]; // Removed for bot radio system removal
	float m_lastSeenEnemyTimestamp;
	float m_roundStartTimestamp;
	bool m_isDefenseRushing;

	// Event Handlers --------------------------------------------------------------------------------------------
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFootstep,		player_footstep )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerRadio,			player_radio )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerDeath,			player_death )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFallDamage,		player_falldamage )

	// Removed CS Bomb Event Listeners
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombPickedUp,			bomb_pickup )
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombPlanted,			bomb_planted )
	// ... (all other bomb events)

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundEnd,				round_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundStart,			round_start )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundFreezeEnd,		round_freeze_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DoorMoving,			door_moving )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakProp,				break_prop )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakBreakable,		break_breakable )

	// Removed CS Hostage Event Listeners
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( HostageFollows,		hostage_follows )
	// DECLARE_FFBOTMANAGER_EVENT_LISTENER( HostageRescuedAll,		hostage_rescued_all )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFire,			weapon_fire )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFireOnEmpty,		weapon_fire_on_empty )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponReload,			weapon_reload )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponZoom,			weapon_zoom )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BulletImpact,			bullet_impact )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HEGrenadeDetonate,		hegrenade_detonate ) // Keep generic, might be FF equivalent
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FlashbangDetonate,		flashbang_detonate ) // Keep generic
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SmokeGrenadeDetonate,	smokegrenade_detonate ) // Keep generic
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( GrenadeBounce,			grenade_bounce ) // Keep generic
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( NavBlocked,			nav_blocked )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( ServerShutdown,		server_shutdown )

	// FF CTF Events
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagCaptured, "ff_flag_captured" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagDropped, "ff_flag_dropped" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagPickedUp, "ff_flag_picked_up" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_FlagReturned, "ff_flag_returned" )
	// FF Control Point Events
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointCaptured, "ff_point_captured" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointStatusUpdate, "ff_point_status" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PointBlocked, "ff_point_blocked" )
	// FF VIP Events
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPSelected, "ff_vip_selected" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPKilled, "ff_vip_killed" )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_VIPEscaped, "ff_vip_escaped" )
	// FF Player Spawn Event
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FF_PlayerSpawn, "player_spawn" )


	CUtlVector< BotEventInterface * > m_commonEventListeners;
	bool m_eventListenersEnabled;
	void EnableEventListeners( bool enable );
};

inline CBasePlayer *CFFBotManager::AllocateBotEntity( void )
{
	return static_cast<CBasePlayer *>( CreateEntityByName( "ff_bot" ) );
}

// TODO_FF: Remove or adapt CS bomb logic for FF objectives if any equivalent exists.
// For now, IsTimeToPlantBomb is CS-specific. Commenting out or removing.
/*
inline bool CFFBotManager::IsTimeToPlantBomb( void ) const
{
	return (gpGlobals->curtime >= m_earliestBombPlantTimestamp);
}
*/

inline const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const CBaseEntity *entity ) const
{
	if (entity == NULL)
		return NULL;
	Vector centroid = entity->GetAbsOrigin();
	return GetClosestZone( centroid );
}

#endif // FF_BOT_MANAGER_H
