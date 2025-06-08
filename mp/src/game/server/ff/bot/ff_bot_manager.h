//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#ifndef FF_BOT_MANAGER_H
#define FF_BOT_MANAGER_H

#include "bot_manager.h" // Base class
#include "nav_area.h"    // For CNavArea, Extent
#include "bot_util.h"
#include "bot_profile.h"
#include "../../shared/ff/ff_shareddefs.h" // Changed from cs_shareddefs.h
#include "../ff_player.h"                   // Changed path
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase
#include "bot_constants.h"  // For TEAM_TERRORIST, TEAM_CT, RadioType, BotDifficultyType, FFWeaponType (was CSWeaponType) etc.
#include "igameevents.h"    // For IGameEventListener2

extern ConVar friendlyfire; // This should be fine if it's a global convar
extern ConVar cv_bot_difficulty; // Already used, ensure it's declared elsewhere or here
extern ConVar mp_freezetime; // Used in RestartRound, ensure it's declared

// Forward declarations
class CFFBot; // Forward declare CFFBot, as CFFBotManager iterates/manages them
class CBasePlayerWeapon;
class IGameEvent;
class CCommand;


/**
 * Given one team, return the other
 */
// TODO: Update for FF Teams if they differ from CS:S TEAM_TERRORIST/TEAM_CT
inline int OtherTeam( int team )
{
	return (team == TEAM_TERRORIST) ? TEAM_CT : TEAM_TERRORIST; // TEAM_TERRORIST and TEAM_CT need to be FF enums
}

class CFFBotManager;

// accessor for FF-specific bots
inline CFFBotManager *TheFFBots( void )
{
	return reinterpret_cast< CFFBotManager * >( TheBots ); // TheBots is the global CBotManager instance
}

//--------------------------------------------------------------------------------------------------------------
class BotEventInterface : public IGameEventListener2
{
public:
	virtual const char *GetEventName( void ) const = 0;
	// FireGameEvent is part of IGameEventListener2, no need to redefine unless overriding
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
		EventClass##Event( CFFBotManager* pMgr ) /* Pass manager to constructor */ \
		{ \
			gameeventmanager->AddListener( pMgr->Get##EventClass##Listener(), #EventName, true ); \
			m_enabled = true; \
		} \
		~EventClass##Event( void ) \
		{ \
			/* if ( m_enabled && gameeventmanager ) gameeventmanager->RemoveListener( this ); */ \
			/* Removal should be handled by manager or a more robust system */ \
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
		/* FireGameEvent is inherited from IGameEventListener2 */ \
		/* void FireGameEvent( IGameEvent *event ) \
		{ \
			BotManagerSingleton()->On##EventClass( event ); \
		} \ */ \
	}; \
	friend class EventClass##Event; /* Allow access to Get##EventClass##Listener */ \
	EventClass##Event* Get##EventClass##Listener() { return &m_##EventClass##Event; } \
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
	// ~CFFBotManager() override; // If virtual destructor is needed

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
	virtual bool IsImportantPlayer( CFFPlayer *player ) const; // TODO: Adapt for FF roles

	void ExtractScenarioData( void );

	static BotDifficultyType GetDifficultyLevel( void );

	// TODO: Update GameScenarioType for FF (CTF, CP, Arena, etc.)
	enum GameScenarioType
	{
		SCENARIO_DEATHMATCH,    // Or Arena?
		SCENARIO_CAPTURETHEFLAG,
		SCENARIO_CONTROLPOINT,
		// SCENARIO_DEFUSE_BOMB,    // CS Specific
		// SCENARIO_RESCUE_HOSTAGES, // CS Specific
		// SCENARIO_ESCORT_VIP       // CS Specific
		SCENARIO_INVADE // Example for FF:Invade
	};
	GameScenarioType GetScenario( void ) const		{ return m_gameScenario; }

	// "zones" - adaptable for objectives like capture points, flag stands
	enum { MAX_ZONES = 8 }; // Increased for potentially more complex FF maps
	enum { MAX_ZONE_NAV_AREAS = 32 }; // Increased
	struct Zone
	{
		CBaseEntity *m_entity;
		CNavArea *m_area[ MAX_ZONE_NAV_AREAS ];
		int m_areaCount;
		Vector m_center;
		// bool m_isLegacy; // May not be relevant for FF
		int m_index;
		bool m_isBlocked;
		Extent m_extent;
		// FF specific zone data:
		// int m_owningTeam; // TEAM_RED, TEAM_BLUE, TEAM_NEUTRAL
		// float m_captureProgress;
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

	CBaseEntity *GetRandomSpawn( int team = 0 ) const; // team = 0 for any, or FF team numbers


	// TODO: Bomb-specific logic needs removal or heavy adaptation for FF objectives
	bool IsBombPlanted( void ) const			{ return m_isBombPlanted; }
	float GetBombPlantTimestamp( void ) const	{ return m_bombPlantTimestamp; }
	bool IsTimeToPlantBomb( void ) const;
	CFFPlayer *GetBombDefuser( void ) const		{ return m_bombDefuser; }
	float GetBombTimeLeft( void ) const;
	CBaseEntity *GetLooseBomb( void )			{ return m_looseBomb; }
	CNavArea *GetLooseBombArea( void ) const	{ return m_looseBombArea; }
	void SetLooseBomb( CBaseEntity *bomb );


	float GetRadioMessageTimestamp( RadioType event, int teamID ) const;
	float GetRadioMessageInterval( RadioType event, int teamID ) const;
	void SetRadioMessageTimestamp( RadioType event, int teamID );
	void ResetRadioMessageTimestamps( void );

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
	// bool AllowTacticalShield( void ) const; // CS Specific

	bool AllowFriendlyFireDamage( void ) const;

	bool IsWeaponUseable( const CFFWeaponBase *weapon ) const;

	bool IsDefenseRushing( void ) const				{ return m_isDefenseRushing; }
	bool IsOnDefense( const CFFPlayer *player ) const;
	bool IsOnOffense( const CFFPlayer *player ) const;

	bool IsRoundOver( void ) const					{ return m_isRoundOver; }

	// TODO: Update CSWeaponType to FFWeaponType
	bool BotAddCommand( int team, bool isFromConsole = false, const char *profileName = NULL, /*CSWeaponType*/ FFWeaponType weaponType = WEAPONTYPE_UNDEFINED, BotDifficultyType difficulty = NUM_DIFFICULTY_LEVELS );

private:
	// enum SkillType { LOW, AVERAGE, HIGH, RANDOM }; // Already in bot_profile.h likely

	void MaintainBotQuota( void );

	static bool m_isMapDataLoaded;
	bool m_serverActive;

	GameScenarioType m_gameScenario;

	Zone m_zone[ MAX_ZONES ];							
	int m_zoneCount;

	// TODO: Bomb-specific members, adapt/remove for FF
	bool m_isBombPlanted;
	float m_bombPlantTimestamp;
	float m_earliestBombPlantTimestamp;
	CHandle<CFFPlayer> m_bombDefuser; // Changed to CHandle
	CHandle<CBaseEntity> m_looseBomb; // Changed to CHandle
	CNavArea *m_looseBombArea;

	bool m_isRoundOver;

	CountdownTimer m_checkTransientAreasTimer;

	// TODO: RADIO_END and RADIO_START_1 are CS specific. Adapt for FF radio events.
	float m_radioMsgTimestamp[ BOT_RADIO_COUNT ][ 2 ]; // BOT_RADIO_COUNT should be FF specific

	float m_lastSeenEnemyTimestamp;
	float m_roundStartTimestamp;

	bool m_isDefenseRushing;

	// Event Handlers --------------------------------------------------------------------------------------------
	// TODO: Update event names for FF if they differ from CS
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFootstep,		player_footstep )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerRadio,			player_radio )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerDeath,			player_death )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( PlayerFallDamage,		player_falldamage )

	// TODO: Bomb events are CS specific. Replace with FF objective events.
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombPickedUp,			bomb_pickup )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombPlanted,			bomb_planted )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombBeep,				bomb_beep )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombDefuseBegin,		bomb_begindefuse )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombDefused,			bomb_defused )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombDefuseAbort,		bomb_abortdefuse )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BombExploded,			bomb_exploded )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundEnd,				round_end )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundStart,			round_start )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( RoundFreezeEnd,		round_freeze_end )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( DoorMoving,			door_moving )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakProp,				break_prop )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BreakBreakable,		break_breakable )

	// TODO: Hostage events are CS specific.
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HostageFollows,		hostage_follows )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HostageRescuedAll,		hostage_rescued_all )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFire,			weapon_fire )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponFireOnEmpty,		weapon_fire_on_empty )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponReload,			weapon_reload )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( WeaponZoom,			weapon_zoom )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( BulletImpact,			bullet_impact )

	// TODO: Grenade event names might differ for FF
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( HEGrenadeDetonate,		hegrenade_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( FlashbangDetonate,		flashbang_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( SmokeGrenadeDetonate,	smokegrenade_detonate )
	DECLARE_FFBOTMANAGER_EVENT_LISTENER( GrenadeBounce,			grenade_bounce )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( NavBlocked,			nav_blocked )

	DECLARE_FFBOTMANAGER_EVENT_LISTENER( ServerShutdown,		server_shutdown )

	// TODO: Add FF specific event listeners (flag_capture, point_captured, etc.)


	CUtlVector< BotEventInterface * > m_commonEventListeners;
	bool m_eventListenersEnabled;
	void EnableEventListeners( bool enable );
};

inline CBasePlayer *CFFBotManager::AllocateBotEntity( void )
{
	return static_cast<CBasePlayer *>( CreateEntityByName( "ff_bot" ) );
}

// TODO: Bomb logic needs FF adaptation
inline bool CFFBotManager::IsTimeToPlantBomb( void ) const
{
	return (gpGlobals->curtime >= m_earliestBombPlantTimestamp);
}

// TODO: HalfHumanHeight needs to be defined
inline const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const CBaseEntity *entity ) const
{
	if (entity == NULL)
		return NULL;

	Vector centroid = entity->GetAbsOrigin();
	// centroid.z += HalfHumanHeight; // This might not be needed if GetClosestZone uses center
	return GetClosestZone( centroid );
}

#endif // FF_BOT_MANAGER_H
