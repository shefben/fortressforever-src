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
#include "entitylist.h"
#include <stdio.h>
#include "utlvector.h"
#include <algorithm> // For std::sort

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

// Placeholder entity names/classnames for FF objectives
// These should align with names used in FFGameState::InitializeFlagState
#define FF_FLAG_STAND_RED_NAME_MGR "flag_red_stand"
#define FF_FLAG_STAND_BLUE_NAME_MGR "flag_blue_stand"
// TODO_FF: Define names for capture zone triggers if they are distinct entities to be made into zones
#define FF_CAPTURE_ZONE_RED_NAME "capture_zone_red"   // Example
#define FF_CAPTURE_ZONE_BLUE_NAME "capture_zone_blue" // Example

#define FF_CP_ENTITY_CLASSNAME_MGR "trigger_controlpoint" // Should match FFGameState
#define FF_VIP_ESCAPEZONE_CLASSNAME_MGR "func_escapezone" // Should match FFGameState

// Helper structure for sorting CPs during discovery (if needed in manager too)
struct CPManagerZoneInitData {
    CBaseEntity* entity;
    string_t entityName;
    int tentativeID;

    bool operator<(const CPManagerZoneInitData& other) const {
        return tentativeID < other.tentativeID;
    }
};


inline CFFPlayer *ToFFPlayer( CBaseEntity *pEntity ) { /* ... (implementation unchanged) ... */ }
inline bool AreBotsAllowed() { /* ... (implementation unchanged) ... */ }
void InstallBotControl( void ) { /* ... (implementation unchanged) ... */ }
void RemoveBotControl( void ) { /* ... (implementation unchanged) ... */ }
CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername ) { /* ... (implementation unchanged) ... */ }

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
	m_FF_PlayerSpawnEvent(this),
	m_LuaEventEvent(this)
{ /* ... (constructor body unchanged) ... */ }

void CFFBotManager::RestartRound( void ) { /* ... (implementation unchanged) ... */ }
void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue ) { /* ... (implementation unchanged) ... */ }
void CFFBotManager::EnableEventListeners( bool enable ) { /* ... (implementation unchanged) ... */ }
void CFFBotManager::StartFrame( void ) { /* ... (implementation unchanged) ... */ }
bool CFFBotManager::IsWeaponUseable( const CFFWeaponBase *weapon ) const { /* ... (implementation unchanged) ... */ }
bool CFFBotManager::IsOnDefense( const CFFPlayer *player ) const { /* ... (placeholder, needs FF logic) ... */ return false; }
bool CFFBotManager::IsOnOffense( const CFFPlayer *player ) const { return !IsOnDefense( player ); }
void CFFBotManager::ServerActivate( void ) { /* ... (implementation unchanged, calls ExtractScenarioData) ... */ }
void CFFBotManager::ServerDeactivate( void ) { m_serverActive = false; }
void CFFBotManager::ClientDisconnect( CBaseEntity *entity ) { /* ... */ }
void BotArgumentsFromArgv( const CCommand &args, const char **name, FFWeaponID *weaponID, BotDifficultyType *difficulty, int *team = NULL, bool *all = NULL ) { /* ... */ }
CON_COMMAND_F( bot_add, "bot_add <team> <type> <difficulty> <name> - Adds a bot matching the given criteria.", FCVAR_GAMEDLL ) { /* ... */ }
// ... other concommands and CollectBots class ...
CON_COMMAND_F( bot_goto_mark, "Sends a bot to the selected nav area.", FCVAR_GAMEDLL | FCVAR_CHEAT ) { /* ... */ }
CON_COMMAND_F( nav_check_connectivity, "Checks to be sure every (or just the marked) nav area can get to every goal area for the map.", FCVAR_GAMEDLL | FCVAR_CHEAT ) { /* ... */ }

// Standard Event Handlers (implementations unchanged from previous step)
void CFFBotManager::OnPlayerFootstep( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnPlayerRadio( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnPlayerDeath( IGameEvent *event ) { /* ... (VIP kill check is here) ... */ }
void CFFBotManager::OnPlayerFallDamage( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnRoundEnd( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnRoundStart( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnDoorMoving( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnBreakProp( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnBreakBreakable( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnWeaponFire( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnWeaponFireOnEmpty( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnWeaponReload( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnWeaponZoom( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnBulletImpact( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnHEGrenadeDetonate( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnFlashbangDetonate( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnSmokeGrenadeDetonate( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnGrenadeBounce( IGameEvent *event ) { /* ... */ }

// Central Handler for Lua-originated Events (implementation from previous step)
void CFFBotManager::OnLuaEvent(IGameEvent *event) { /* ... */ }

// Specific FF Event Handler Shells (implementations from previous step)
void CFFBotManager::OnFF_FlagCaptured(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_FlagDropped(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_FlagPickedUp(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_FlagReturned(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_PointCaptured(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_PointStatusUpdate(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_PointBlocked(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_VIPSelected(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_VIPKilled(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_VIPEscaped(IGameEvent *event) { /* ... */ }
void CFFBotManager::OnFF_PlayerSpawn(IGameEvent *event) { /* ... */ }

//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::ExtractScenarioData( void )
{
	m_isMapDataLoaded = false;
	m_gameScenario = SCENARIO_ARENA;
	m_zoneCount = 0;

	for(int i=0; i<MAX_ZONES; ++i) {
		m_zone[i].m_entity = NULL;
		m_zone[i].m_areaCount = 0;
	}

	if ( !TheNavMesh || !TheNavMesh->IsLoaded() ) {
		DevWarning( "CFFBotManager::ExtractScenarioData: Nav mesh not loaded. Cannot determine objectives.\n" );
		return;
	}

	bool foundCTFObjectives = false;
	bool foundCPObjectives = false;
	bool foundVIPObjectives = false;

	// Detect CTF Flag Stands and Capture Zones
	// These names must match what's used in FFGameState::InitializeFlagState for spawn locations
	const char* ctfZoneNames[] = {
		FF_FLAG_STAND_RED_NAME_MGR, FF_FLAG_STAND_BLUE_NAME_MGR,
		FF_CAPTURE_ZONE_RED_NAME, FF_CAPTURE_ZONE_BLUE_NAME
		// Add yellow/green if applicable
	};
	for (const char* zoneName : ctfZoneNames) {
		if (m_zoneCount >= MAX_ZONES) break;
		CBaseEntity* pZoneEntity = gEntList.FindEntityByName(NULL, zoneName, NULL);
		if (pZoneEntity) {
			foundCTFObjectives = true; // Finding any of these implies CTF elements exist
			m_zone[m_zoneCount].m_entity = pZoneEntity;
			m_zone[m_zoneCount].m_center = pZoneEntity->GetAbsOrigin();
			pZoneEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
			m_zone[m_zoneCount].m_index = m_zoneCount; // Could later store team or zone type
			// TODO_FF: Could try to parse team from name if "red_flag_stand", "blue_cap_zone" etc.
			DevMsg("CFFBotManager: Found CTF Zone: '%s' at (%.1f, %.1f, %.1f)\n", zoneName, m_zone[m_zoneCount].m_center.x, m_zone[m_zoneCount].m_center.y, m_zone[m_zoneCount].m_center.z);
			m_zoneCount++;
		} else {
			DevMsg("CFFBotManager: CTF Zone entity '%s' not found.\n", zoneName);
		}
	}
    if (foundCTFObjectives) { // If any CTF zone was found, assume CTF.
        m_gameScenario = SCENARIO_CAPTURETHEFLAG;
    }

	// Detect Control Points
    CUtlVector<CPManagerZoneInitData> foundCPsSorted;
    int tempSequentialId = 0;
	CBaseEntity *pCPEntity = NULL;
	for (;(pCPEntity = gEntList.FindEntityByClassname(pCPEntity, FF_CP_ENTITY_CLASSNAME_MGR)) != NULL;)
	{
		if (foundCPsSorted.Count() >= MAX_CONTROL_POINTS_FF) { // MAX_CONTROL_POINTS_FF often same as MAX_ZONES for CP maps
            DevWarning("CFFBotManager::ExtractScenarioData: Found more CP entities than MAX_CONTROL_POINTS_FF. Some ignored.\n");
			break;
        }
        CPManagerZoneInitData cpData;
        cpData.entity = pCPEntity;
        cpData.entityName = pCPEntity->GetEntityName();
        cpData.tentativeID = -1;

        const char *name = STRING(cpData.entityName);
        const char *p = name + Q_strlen(name) - 1;
        while (p >= name && isdigit(*p)) { p--; }
        if (p < (name + Q_strlen(name) - 1)) {
            cpData.tentativeID = atoi(p + 1);
            if (cpData.tentativeID > 0) cpData.tentativeID--; // Adjust 1-based from name to 0-based tentative
        }
        if (cpData.tentativeID == -1) {
            cpData.tentativeID = tempSequentialId++;
            DevWarning("CFFBotManager::ExtractScenarioData: CP '%s' could not parse ID from name, assigned sequential temp ID %d.\n", name, cpData.tentativeID);
        }
		foundCPsSorted.AddToTail(cpData);
	}

    if (foundCPsSorted.Count() > 0) {
        foundCPObjectives = true;
        if (foundCPsSorted.Count() > 1) {
            std::sort(foundCPsSorted.begin(), foundCPsSorted.end());
        }

        for (int i = 0; i < foundCPsSorted.Count(); ++i) {
            if (m_zoneCount >= MAX_ZONES) break;
            m_zone[m_zoneCount].m_entity = foundCPsSorted[i].entity;
            m_zone[m_zoneCount].m_center = foundCPsSorted[i].entity->GetAbsOrigin();
            foundCPsSorted[i].entity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
            m_zone[m_zoneCount].m_index = foundCPsSorted[i].tentativeID; // Store the parsed/sequential (hopefully 0-indexed) ID
                                                                      // This should match FFGameState's pointID after its own sorting.
            DevMsg("CFFBotManager: Found CP Zone: '%s' (Parsed/Tentative ID: %d) at (%.1f, %.1f, %.1f)\n",
                STRING(foundCPsSorted[i].entityName), foundCPsSorted[i].tentativeID,
                m_zone[m_zoneCount].m_center.x, m_zone[m_zoneCount].m_center.y, m_zone[m_zoneCount].m_center.z);
            m_zoneCount++;
        }
    }

	// Detect VIP Escape Zones
	CBaseEntity *pEscapeZoneEntity = NULL;
	while ((pEscapeZoneEntity = gEntList.FindEntityByClassname(pEscapeZoneEntity, FF_VIP_ESCAPEZONE_CLASSNAME_MGR)) != NULL)
	{
		if (m_zoneCount >= MAX_ZONES) break;
		foundVIPObjectives = true;
		m_zone[m_zoneCount].m_entity = pEscapeZoneEntity;
		m_zone[m_zoneCount].m_center = pEscapeZoneEntity->GetAbsOrigin();
		pEscapeZoneEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
		m_zone[m_zoneCount].m_index = m_zoneCount;
		DevMsg("CFFBotManager: Found VIP Escape Zone: '%s'\n", STRING(pEscapeZoneEntity->GetEntityName()));
        m_zoneCount++;
	}

	// Determine Game Scenario based on found objectives (CTF takes precedence if mixed)
	if (foundCTFObjectives) {
		m_gameScenario = SCENARIO_CAPTURETHEFLAG;
		DevMsg("CFFBotManager: Final Scenario Detected: Capture The Flag (%d zones).\n", m_zoneCount);
	} else if (foundCPObjectives) {
		m_gameScenario = SCENARIO_CONTROLPOINT;
		DevMsg("CFFBotManager: Final Scenario Detected: Control Points (%d zones).\n", m_zoneCount);
	} else if (foundVIPObjectives) {
		m_gameScenario = SCENARIO_VIP; // VIP identification also relies on FFGameState finding a VIP player
		DevMsg("CFFBotManager: Final Scenario Detected: VIP (based on %d escape zones).\n", m_zoneCount - (foundCPObjectives ? foundCPsSorted.Count() : 0) - (foundCTFObjectives ? 2 : 0) /* rough count of VIP zones */ );
	} else {
		m_gameScenario = SCENARIO_ARENA;
		DevMsg("CFFBotManager: Final Scenario Detected: Arena/Deathmatch (default, %d zones total).\n", m_zoneCount);
	}

	// Populate nav areas for all identified zones
	for (int i = 0; i < m_zoneCount; ++i) {
		if (m_zone[i].m_entity == NULL) continue;
		if (!TheNavMesh->GetNavAreasOverlappingExtent(m_zone[i].m_extent, m_zone[i].m_area, MAX_ZONE_NAV_AREAS, &m_zone[i].m_areaCount)) {
			DevWarning( "CFFBotManager::ExtractScenarioData: Zone '%s' (Index %d) has no overlapping nav areas!\n", STRING(m_zone[i].m_entity->GetEntityName()), m_zone[i].m_index );
		} else {
             DevMsg("CFFBotManager: Zone '%s' (Index %d) has %d nav areas.\n", STRING(m_zone[i].m_entity->GetEntityName()), m_zone[i].m_index, m_zone[i].m_areaCount);
        }
	}

	m_isMapDataLoaded = true;
}

[end of mp/src/game/server/ff/bot/ff_bot_manager.cpp]
