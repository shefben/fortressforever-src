//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Manager for Fortress Forever Bots.
//
// $NoKeywords: $
//=============================================================================//

#pragma warning( disable : 4530 )

#include "cbase.h"
#include "ff_bot_manager.h"
#include "ff_bot.h"
// #include "ff_bot_chatter.h" // Removed
#include "bot_profile.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"
#include "nav_area.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "bot_constants.h"
#include "bot_util.h"
#include "../../shared/ff/ff_shareddefs.h"
#include "shared_util.h"
#include "KeyValues.h"
#include "tier0/icommandline.h"
#include "filesystem.h"
#include "inputsystem/InputEnums.h"
#include "usermessages.h"
#include "gameeventdefs.h"
#include "IEngineTrace.h"
#include "world.h"
#include "props.h"
#include "entitylist.h" // For gEntList

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)
#endif

CBotManager *TheBots = NULL;

bool CFFBotManager::m_isMapDataLoaded = false;
int g_nClientPutInServerOverrides = 0;

// Forward declarations & ConVars
void DrawOccupyTime( void );
ConVar bot_show_occupy_time( "bot_show_occupy_time", "0", FCVAR_GAMEDLL | FCVAR_CHEAT );
void DrawBattlefront( void );
ConVar bot_show_battlefront( "bot_show_battlefront", "0", FCVAR_GAMEDLL | FCVAR_CHEAT );
int UTIL_FFSBotsInGame( void );
ConVar bot_join_delay( "bot_join_delay", "0", FCVAR_GAMEDLL );

// Placeholder entity classnames for FF objectives
#define FF_FLAG_SPAWN_CLASSNAME "item_teamflag"
#define FF_CP_CLASSNAME "trigger_controlpoint"
#define FF_VIP_ESCAPEZONE_CLASSNAME "func_escapezone"

// Helper to cast CBaseEntity to CFFPlayer
inline CFFPlayer *ToFFPlayer( CBaseEntity *pEntity )
{
    if ( !pEntity || !pEntity->IsPlayer() )
        return NULL;
    return static_cast<CFFPlayer *>( pEntity );
}

inline bool AreBotsAllowed() {
	if ( CommandLine() ) {
		if ( CommandLine()->CheckParm( "-nobots" ) ) return false;
	}
	return true;
}
void InstallBotControl( void ) { if ( TheBots != NULL ) delete TheBots; TheBots = new CFFBotManager; }
void RemoveBotControl( void ) { if ( TheBots != NULL ) delete TheBots; TheBots = NULL; }
CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername ) {
	CBasePlayer *pPlayer = TheBots->AllocateAndBindBotEntity( pEdict );
	if ( pPlayer ) pPlayer->SetPlayerName( playername );
	++g_nClientPutInServerOverrides;
	return pPlayer;
}

CFFBotManager::CFFBotManager() :
	m_PlayerFootstepEvent(this),
	m_PlayerRadioEvent(this),
	m_PlayerDeathEvent(this),
	m_PlayerFallDamageEvent(this),
	m_RoundEndEvent(this),
	m_RoundStartEvent(this),
	m_RoundFreezeEndEvent(this),
	m_DoorMovingEvent(this),
	m_BreakPropEvent(this),
	m_BreakBreakableEvent(this),
	m_WeaponFireEvent(this),
	m_WeaponFireOnEmptyEvent(this),
	m_WeaponReloadEvent(this),
	m_WeaponZoomEvent(this),
	m_BulletImpactEvent(this),
	m_HEGrenadeDetonateEvent(this),
	m_FlashbangDetonateEvent(this),
	m_SmokeGrenadeDetonateEvent(this),
	m_GrenadeBounceEvent(this),
	m_NavBlockedEvent(this),
	m_ServerShutdownEvent(this),
	m_FF_FlagCapturedEvent(this),
	m_FF_FlagDroppedEvent(this),
	m_FF_FlagPickedUpEvent(this),
	m_FF_FlagReturnedEvent(this),
	m_FF_PointCapturedEvent(this),
	m_FF_PointStatusUpdateEvent(this),
	m_FF_PointBlockedEvent(this),
	m_FF_VIPSelectedEvent(this),
	m_FF_VIPKilledEvent(this),
	m_FF_VIPEscapedEvent(this),
	m_FF_PlayerSpawnEvent(this)
{
	m_zoneCount = 0;
	m_serverActive = false;
	m_roundStartTimestamp = 0.0f;
	m_eventListenersEnabled = true;
	if (!TheBotPhrases) TheBotPhrases = new BotPhraseManager;
	if (!TheBotProfiles) TheBotProfiles = new BotProfileManager;
}

void CFFBotManager::RestartRound( void ) {
	CBotManager::RestartRound();
	m_lastSeenEnemyTimestamp = -9999.9f;
	m_roundStartTimestamp = gpGlobals->curtime + mp_freezetime.GetFloat();
	const float defenseRushChance = 33.3f;
	m_isDefenseRushing = (RandomFloat( 0.0f, 100.0f ) <= defenseRushChance) ? true : false;
	if (TheBotPhrases) TheBotPhrases->OnRoundRestart();
	m_isRoundOver = false;
}

void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue ) { /* ... (implementation unchanged) ... */ }
void CFFBotManager::EnableEventListeners( bool enable ) { /* ... (implementation unchanged) ... */ }
void CFFBotManager::StartFrame( void ) { /* ... (implementation unchanged) ... */ }
bool CFFBotManager::IsWeaponUseable( const CFFWeaponBase *weapon ) const { /* ... (implementation unchanged) ... */
	if (weapon == NULL) return false;
	if ((!AllowShotguns() && weapon->IsKindOf( WEAPONTYPE_SHOTGUN )) ||
		(!AllowMachineGuns() && weapon->IsKindOf( WEAPONTYPE_MACHINEGUN )) || 
		(!AllowRifles() && weapon->IsKindOf( WEAPONTYPE_RIFLE )) || 
		(!AllowSnipers() && weapon->IsKindOf( WEAPONTYPE_SNIPER_RIFLE )) || 
		(!AllowSubMachineGuns() && weapon->IsKindOf( WEAPONTYPE_SUBMACHINEGUN )) || 
		(!AllowPistols() && weapon->IsKindOf( WEAPONTYPE_PISTOL )) ||
		(!AllowGrenades() && weapon->IsKindOf( WEAPONTYPE_GRENADE ))) {
		return false;
	}
	return true;
}
bool CFFBotManager::IsOnDefense( const CFFPlayer *player ) const { /* ... (placeholder, needs FF logic) ... */ return false; }
bool CFFBotManager::IsOnOffense( const CFFPlayer *player ) const { return !IsOnDefense( player ); }

void CFFBotManager::ServerActivate( void ) {
	m_isMapDataLoaded = false;
	if (TheBotPhrases) {
		TheBotPhrases->Reset();
		TheBotPhrases->Initialize( "BotChatter.db", 0 );
	}
	if (TheBotProfiles) {
		TheBotProfiles->Reset();
		TheBotProfiles->FindVoiceBankIndex( "BotChatter.db" );
		// ... (rest of profile loading) ...
	}
	ExtractScenarioData();
	RestartRound();
	if (TheBotPhrases) TheBotPhrases->OnMapChange();
	m_serverActive = true;
}
void CFFBotManager::ServerDeactivate( void ) { m_serverActive = false; }
void CFFBotManager::ClientDisconnect( CBaseEntity *entity ) { /* ... */ }

void BotArgumentsFromArgv( const CCommand &args, const char **name, FFWeaponID *weaponID, BotDifficultyType *difficulty, int *team = NULL, bool *all = NULL ) { /* ... (implementation needs FF team/weapon parsing) ... */
	static char s_name[MAX_PLAYER_NAME_LENGTH];
	s_name[0] = 0;
	*name = s_name;
	*difficulty = NUM_DIFFICULTY_LEVELS;
	if ( team ) *team = TEAM_UNASSIGNED;
	if ( all ) *all = false;
	*weaponID = FF_WEAPON_NONE;
	for ( int arg=1; arg<args.ArgC(); ++arg ) { /* ... */ }
}

CON_COMMAND_F( bot_add, "bot_add <team> <type> <difficulty> <name> - Adds a bot matching the given criteria.", FCVAR_GAMEDLL ) { /* ... */ }
class CollectBots { /* ... */ };
// ... other concommands ...
CON_COMMAND_F( bot_goto_mark, "Sends a bot to the selected nav area.", FCVAR_GAMEDLL | FCVAR_CHEAT ) { /* ... */ }
CON_COMMAND_F( nav_check_connectivity, "Checks to be sure every (or just the marked) nav area can get to every goal area for the map.", FCVAR_CHEAT ) { /* ... */ }

// Standard Event Handlers
void CFFBotManager::OnPlayerFootstep( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFootstep, event ); }
void CFFBotManager::OnPlayerRadio( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerRadio, event ); }
void CFFBotManager::OnPlayerDeath( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerDeath, event ); }
void CFFBotManager::OnPlayerFallDamage( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFallDamage, event ); }
void CFFBotManager::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; CFFBOTMANAGER_ITERATE_BOTS( OnRoundEnd, event ); }
void CFFBotManager::OnRoundStart( IGameEvent *event ) { RestartRound(); CFFBOTMANAGER_ITERATE_BOTS( OnRoundStart, event ); }
void CFFBotManager::OnDoorMoving( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnDoorMoving, event ); }
void CFFBotManager::OnBreakProp( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnBreakBreakable( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnWeaponFire( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFire, event ); }
void CFFBotManager::OnWeaponFireOnEmpty( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFireOnEmpty, event ); }
void CFFBotManager::OnWeaponReload( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponReload, event ); }
void CFFBotManager::OnWeaponZoom( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponZoom, event ); }
void CFFBotManager::OnBulletImpact( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnBulletImpact, event ); }
void CFFBotManager::OnHEGrenadeDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnHEGrenadeDetonate, event ); }
void CFFBotManager::OnFlashbangDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnFlashbangDetonate, event ); }
void CFFBotManager::OnSmokeGrenadeDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnSmokeGrenadeDetonate, event ); }
void CFFBotManager::OnGrenadeBounce( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnGrenadeBounce, event ); }

// --- New FF Event Handler Implementations ---

void CFFBotManager::OnFF_FlagCaptured(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names ("userid", "flagname", etc.)
    CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0)));
    const char *flagName = event->GetString("flagname", "unknown_flag");
    CBaseEntity *pFlagEntity = gEntList.FindEntityByName(NULL, flagName, NULL);

    DevMsg("CFFBotManager: Event %s received (Player: %s, Flag: %s).\n", event->GetName(), pPlayer ? pPlayer->GetPlayerName() : "NULL", flagName);
    if (pPlayer && pFlagEntity && GetGameState()) {
        GetGameState()->OnFlagCaptured(pFlagEntity, pPlayer);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventFlagCaptured, event); // If bots need direct notification
}

void CFFBotManager::OnFF_FlagDropped(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0))); // Player who dropped it (might be 0 if system dropped)
    const char *flagName = event->GetString("flagname", "unknown_flag");
    CBaseEntity *pFlagEntity = gEntList.FindEntityByName(NULL, flagName, NULL);
    Vector dropLocation(event->GetFloat("pos_x", 0.0f), event->GetFloat("pos_y", 0.0f), event->GetFloat("pos_z", 0.0f));

    DevMsg("CFFBotManager: Event %s received (Flag: %s).\n", event->GetName(), flagName);
    if (pFlagEntity && GetGameState()) {
        GetGameState()->OnFlagDropped(pFlagEntity, dropLocation);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventFlagDropped, event);
}

void CFFBotManager::OnFF_FlagPickedUp(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0)));
    const char *flagName = event->GetString("flagname", "unknown_flag");
    CBaseEntity *pFlagEntity = gEntList.FindEntityByName(NULL, flagName, NULL);

    DevMsg("CFFBotManager: Event %s received (Player: %s, Flag: %s).\n", event->GetName(), pPlayer ? pPlayer->GetPlayerName() : "NULL", flagName);
    if (pPlayer && pFlagEntity && GetGameState()) {
        GetGameState()->OnFlagPickedUp(pFlagEntity, pPlayer);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventFlagPickedUp, event);
}

void CFFBotManager::OnFF_FlagReturned(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    const char *flagName = event->GetString("flagname", "unknown_flag");
    CBaseEntity *pFlagEntity = gEntList.FindEntityByName(NULL, flagName, NULL);

    DevMsg("CFFBotManager: Event %s received (Flag: %s).\n", event->GetName(), flagName);
    if (pFlagEntity && GetGameState()) {
        GetGameState()->OnFlagReturned(pFlagEntity);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventFlagReturned, event);
}

void CFFBotManager::OnFF_PointCaptured(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    const char *cpName = event->GetString("cpname", "unknown_cp");
    CBaseEntity *pCPEntity = gEntList.FindEntityByName(NULL, cpName, NULL);
    int newOwnerTeam = event->GetInt("newteam", TEAM_UNASSIGNED);

    DevMsg("CFFBotManager: Event %s received (CP: %s, New Team: %d).\n", event->GetName(), cpName, newOwnerTeam);
    if (pCPEntity && GetGameState()) {
        GetGameState()->OnControlPointCaptured(pCPEntity, newOwnerTeam);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventPointCaptured, event);
}

void CFFBotManager::OnFF_PointStatusUpdate(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    const char *cpName = event->GetString("cpname", "unknown_cp");
    CBaseEntity *pCPEntity = gEntList.FindEntityByName(NULL, cpName, NULL);
    int teamMakingProgress = event->GetInt("capteam", TEAM_UNASSIGNED);
    float newProgress = event->GetFloat("progress", 0.0f);

    DevMsg("CFFBotManager: Event %s received (CP: %s, Capping Team: %d, Progress: %.2f).\n", event->GetName(), cpName, teamMakingProgress, newProgress);
    if (pCPEntity && GetGameState()) {
        GetGameState()->OnControlPointProgress(pCPEntity, teamMakingProgress, newProgress);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventPointStatusUpdate, event);
}

void CFFBotManager::OnFF_PointBlocked(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names
    const char *cpName = event->GetString("cpname", "unknown_cp");
    CBaseEntity *pCPEntity = gEntList.FindEntityByName(NULL, cpName, NULL);
    bool isBlocked = event->GetBool("blocked", false);

    DevMsg("CFFBotManager: Event %s received (CP: %s, Blocked: %d).\n", event->GetName(), cpName, isBlocked);
    if (pCPEntity && GetGameState()) {
        GetGameState()->OnControlPointBlocked(pCPEntity, isBlocked);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventPointBlocked, event);
}

void CFFBotManager::OnFF_VIPSelected(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names. Assuming "userid" is the VIP.
    CFFPlayer *pVIP = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0)));

    DevMsg("CFFBotManager: Event %s received (VIP: %s).\n", event->GetName(), pVIP ? pVIP->GetPlayerName() : "NULL");
    if (pVIP && GetGameState()) {
       GetGameState()->OnPlayerSpawn(pVIP); // Re-use OnPlayerSpawn to set/update VIP
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventVIPSelected, event);
}

void CFFBotManager::OnFF_VIPKilled(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names. Assuming "victimid" is the VIP and "attackerid" is the killer's entindex.
    CFFPlayer *pVIP = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("victimid", 0)));
    CBaseEntity *pKiller = UTIL_EntityByIndex(event->GetInt("attackerid", 0));

    DevMsg("CFFBotManager: Event %s received (VIP: %s).\n", event->GetName(), pVIP ? pVIP->GetPlayerName() : "NULL");
    if (pVIP && GetGameState()) {
       GetGameState()->OnVIPKilled(pVIP, pKiller);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventVIPKilled, event);
}

void CFFBotManager::OnFF_VIPEscaped(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names. Assuming "userid" is the VIP.
    CFFPlayer *pVIP = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0)));

    DevMsg("CFFBotManager: Event %s received (VIP: %s).\n", event->GetName(), pVIP ? pVIP->GetPlayerName() : "NULL");
    if (pVIP && GetGameState()) {
       GetGameState()->OnVIPEscaped(pVIP);
    }
    // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventVIPEscaped, event);
}

void CFFBotManager::OnFF_PlayerSpawn(IGameEvent *event) {
    if (!GetGameState() || !event) return;
    // TODO_FF: Verify event parameter names. Assuming "userid" is the spawning player.
    CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByUserId(event->GetInt("userid", 0)));

    DevMsg("CFFBotManager: Event %s received (Player: %s).\n", event->GetName(), pPlayer ? pPlayer->GetPlayerName() : "NULL");
    if (pPlayer && GetGameState()) {
       GetGameState()->OnPlayerSpawn(pPlayer);
       // CFFBOTMANAGER_ITERATE_BOTS(OnGameEventPlayerSpawn, pPlayer); // Example if bots need direct player spawn event
    }
}

void CFFBotManager::ExtractScenarioData( void ) { /* ... (implementation as per previous subtask, unchanged here) ... */
	m_isMapDataLoaded = false;
	m_gameScenario = SCENARIO_ARENA;
	m_zoneCount = 0;
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() ) {
		DevMsg( "CFFBotManager::ExtractScenarioData: Nav mesh not loaded. Cannot determine objectives.\n" );
		return;
	}
	bool foundCTFObjectives = false;
	bool foundCPObjectives = false;
	bool foundVIPObjectives = false;
	CBaseEntity *pFlagEntity = NULL;
	int teamFlagsFound[MAX_PLAYABLE_TEAMS_FF] = {0};
	const char *flagClassnames[] = { FF_FLAG_SPAWN_CLASSNAME, "info_ff_objective_flagstand" };
	for (const char* flagClassname : flagClassnames) {
		pFlagEntity = NULL;
		while ((pFlagEntity = gEntList.FindEntityByClassname(pFlagEntity, flagClassname)) != NULL) {
			if (m_zoneCount >= MAX_ZONES) break;
			int flagTeam = TEAM_UNASSIGNED;
			const char* entName = STRING(pFlagEntity->GetEntityName());
			if (FStrEq(entName, "flag_red_stand") || FStrEq(entName, "item_flag_red")) flagTeam = TEAM_RED;
			else if (FStrEq(entName, "flag_blue_stand") || FStrEq(entName, "item_flag_blue")) flagTeam = TEAM_BLUE;
			if (flagTeam != TEAM_UNASSIGNED && flagTeam < MAX_PLAYABLE_TEAMS_FF) {
				teamFlagsFound[flagTeam]++;
				m_zone[m_zoneCount].m_entity = pFlagEntity;
				m_zone[m_zoneCount].m_center = pFlagEntity->GetAbsOrigin();
				pFlagEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
				m_zone[m_zoneCount].m_index = m_zoneCount;
				m_zoneCount++;
				DevMsg("Found CTF Flag Zone: %s for team %d\n", entName, flagTeam);
			}
		}
		if (m_zoneCount >= MAX_ZONES) break;
	}
	int distinctTeamFlags = 0;
	for(int i=0; i<MAX_PLAYABLE_TEAMS_FF; ++i) { if(teamFlagsFound[i] > 0) distinctTeamFlags++; }
	if (distinctTeamFlags >= 2) {
		foundCTFObjectives = true;
	}
	CBaseEntity *pCPEntity = NULL;
	while ((pCPEntity = gEntList.FindEntityByClassname(pCPEntity, FF_CP_CLASSNAME)) != NULL) {
		if (m_zoneCount >= MAX_ZONES) break;
		foundCPObjectives = true;
		m_zone[m_zoneCount].m_entity = pCPEntity;
		m_zone[m_zoneCount].m_center = pCPEntity->GetAbsOrigin();
		pCPEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
		m_zone[m_zoneCount].m_index = m_zoneCount;
		m_zoneCount++;
		DevMsg("Found Control Point Zone: %s\n", STRING(pCPEntity->GetEntityName()));
	}
	CBaseEntity *pEscapeZoneEntity = NULL;
	while ((pEscapeZoneEntity = gEntList.FindEntityByClassname(pEscapeZoneEntity, FF_VIP_ESCAPEZONE_CLASSNAME)) != NULL) {
		if (m_zoneCount >= MAX_ZONES) break;
		foundVIPObjectives = true;
		m_zone[m_zoneCount].m_entity = pEscapeZoneEntity;
		m_zone[m_zoneCount].m_center = pEscapeZoneEntity->GetAbsOrigin();
		pEscapeZoneEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
		m_zone[m_zoneCount].m_index = m_zoneCount;
		m_zoneCount++;
		DevMsg("Found VIP Escape Zone: %s\n", STRING(pEscapeZoneEntity->GetEntityName()));
	}
	if (foundCTFObjectives) {
		m_gameScenario = SCENARIO_CAPTURETHEFLAG;
		DevMsg("Scenario Detected: Capture The Flag\n");
	} else if (foundCPObjectives) {
		m_gameScenario = SCENARIO_CONTROLPOINT;
		DevMsg("Scenario Detected: Control Points\n");
	} else if (foundVIPObjectives) {
		m_gameScenario = SCENARIO_VIP;
		DevMsg("Scenario Detected: VIP (based on escape zones)\n");
	} else {
		m_gameScenario = SCENARIO_ARENA;
		DevMsg("Scenario Detected: Arena/Deathmatch (default)\n");
	}
	for (int i = 0; i < m_zoneCount; ++i) {
		if (m_zone[i].m_entity == NULL) continue;
		if (!TheNavMesh->GetNavAreasOverlappingExtent(m_zone[i].m_extent, m_zone[i].m_area, MAX_ZONE_NAV_AREAS, &m_zone[i].m_areaCount)) {
			DevMsg( "CFFBotManager::ExtractScenarioData: Zone '%s' has no overlapping nav areas!\n", STRING(m_zone[i].m_entity->GetEntityName()) );
		}
	}
	m_isMapDataLoaded = true;
	DevMsg( "CFFBotManager::ExtractScenarioData: Found %d objective zones. Scenario: %d\n", m_zoneCount, m_gameScenario );
}
[end of mp/src/game/server/ff/bot/ff_bot_manager.cpp]
