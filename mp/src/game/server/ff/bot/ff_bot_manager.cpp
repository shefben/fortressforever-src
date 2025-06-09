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
#include "bot_constants.h"  // For BotGoalType, assuming BOT_GOAL_TYPE_FLAG_CAP is here
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
#include "script_object.h" // For CScriptObject

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)
#endif

CBotManager *TheBots = NULL;

bool CFFBotManager::m_isMapDataLoaded = false;
int g_nClientPutInServerOverrides = 0;

void DrawOccupyTime( void );
ConVar bot_show_occupy_time( "bot_show_occupy_time", "0", FCVAR_GAMEDLL | FCVAR_CHEAT );
void DrawBattlefront( void );
ConVar bot_show_battlefront( "bot_show_battlefront", "0", FCVAR_GAMEDLL | FCVAR_CHEAT );
int UTIL_FFSBotsInGame( void );
ConVar bot_join_delay( "bot_join_delay", "0", FCVAR_GAMEDLL );

#define FF_FLAG_STAND_RED_NAME_MGR_DEF "flag_red_stand"
#define FF_FLAG_STAND_BLUE_NAME_MGR_DEF "flag_blue_stand"
#define FF_CAPTURE_ZONE_RED_NAME_DEF "red_cap"
#define FF_CAPTURE_ZONE_BLUE_NAME_DEF "blue_cap"
#define FF_CAPTURE_ZONE_YELLOW_NAME_DEF "yellow_cap"
#define FF_CAPTURE_ZONE_GREEN_NAME_DEF "green_cap"
#define FF_CTF_CAPTURE_ZONE_CLASSNAME "trigger_ff_script" // basecap inherits from this

#define FF_CP_ENTITY_CLASSNAME_MGR_DEF "trigger_controlpoint"
#define FF_VIP_ESCAPEZONE_CLASSNAME_MGR_DEF "func_escapezone"

struct CPManagerZoneInitData { /* ... (definition unchanged) ... */ };
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
{
    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) { // MAX_PLAYABLE_TEAMS_FF from .h
        m_teamCaptureZones[i] = NULL;
    }
	m_zoneCount = 0;
	m_serverActive = false;
	m_roundStartTimestamp = 0.0f;
	m_eventListenersEnabled = true;
	if (!TheBotPhrases) TheBotPhrases = new BotPhraseManager;
	if (!TheBotProfiles) TheBotProfiles = new BotProfileManager;
}

void CFFBotManager::RestartRound( void ) {
	CBotManager::RestartRound();
    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
        m_teamCaptureZones[i] = NULL;
    }
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
CBaseEntity *CFFBotManager::GetTeamCaptureZone(int teamID) const
{
    if (teamID >= TEAM_ID_RED && teamID < (TEAM_ID_RED + MAX_PLAYABLE_TEAMS_FF)) // Assuming teamIDs are contiguous e.g. RED=1, BLUE=2
    {
        int index = teamID - TEAM_ID_RED; // Convert to 0-based index if TEAM_ID_RED is not 0
        if (index >= 0 && index < MAX_PLAYABLE_TEAMS_FF) {
             return m_teamCaptureZones[index].Get();
        }
    }
    DevWarning("GetTeamCaptureZone: Invalid or unhandled teamID %d requested.\n", teamID);
    return NULL;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::ExtractScenarioData( void )
{
	m_isMapDataLoaded = false;
	m_gameScenario = SCENARIO_ARENA;
	m_zoneCount = 0;

	for(int i=0; i<MAX_ZONES; ++i) {
		m_zone[i].m_entity = NULL;
		m_zone[i].m_areaCount = 0;
        m_zone[i].m_iszEntityName = NULL_STRING;
        m_zone[i].m_team = TEAM_UNASSIGNED;
        m_zone[i].m_index = i;
	}
    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
        m_teamCaptureZones[i] = NULL;
    }

	if ( !TheNavMesh || !TheNavMesh->IsLoaded() ) {
		DevWarning( "CFFBotManager::ExtractScenarioData: Nav mesh not loaded. Cannot determine objectives.\n" );
		return;
	}

	bool foundCTFObjectives = false;
	bool foundCPObjectives = false;
	bool foundVIPObjectives = false;

    // --- CTF Objective Detection ---
    const char* flagStandNames[] = { FF_FLAG_STAND_RED_NAME_MGR_DEF, FF_FLAG_STAND_BLUE_NAME_MGR_DEF /*, yellow, green */ };
    int ctfTeamsWithStands = 0;
    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
        if (m_zoneCount >= MAX_ZONES) break;
        if (i >= (sizeof(flagStandNames)/sizeof(flagStandNames[0]))) break;

        CBaseEntity* pZoneEntity = gEntList.FindEntityByName(NULL, flagStandNames[i]);
        if (pZoneEntity) {
            foundCTFObjectives = true;
            ctfTeamsWithStands++;
            m_zone[m_zoneCount].m_entity = pZoneEntity;
            m_zone[m_zoneCount].m_iszEntityName = pZoneEntity->GetEntityName();
            m_zone[m_zoneCount].m_center = pZoneEntity->GetAbsOrigin();
            pZoneEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
            m_zone[m_zoneCount].m_team = TEAM_ID_RED + i; // Assign team based on order
            m_zone[m_zoneCount].m_index = m_zoneCount;
            DevMsg("CFFBotManager: Found CTF Flag Stand Zone: '%s' for team %d\n", STRING(m_zone[m_zoneCount].m_iszEntityName), m_zone[m_zoneCount].m_team);
            m_zoneCount++;
        } else {
            DevMsg("CFFBotManager: CTF Flag Stand entity '%s' not found.\n", flagStandNames[i]);
        }
    }

    const char* captureZoneNames[] = { FF_CAPTURE_ZONE_RED_NAME_DEF, FF_CAPTURE_ZONE_BLUE_NAME_DEF, FF_CAPTURE_ZONE_YELLOW_NAME_DEF, FF_CAPTURE_ZONE_GREEN_NAME_DEF };
    int teamMapping[] = {TEAM_ID_RED, TEAM_ID_BLUE, TEAM_ID_YELLOW, TEAM_ID_GREEN}; // Ensure these match game constants

    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) { // Only for playable teams usually RED/BLUE
        if (i >= (sizeof(captureZoneNames)/sizeof(captureZoneNames[0]))) break;

        CBaseEntity* pCapEntity = gEntList.FindEntityByName(NULL, captureZoneNames[i]);
        if (pCapEntity) {
            // Check if it's a script entity and has the correct botgoaltype
            CScriptObject* pLuaObj = dynamic_cast<CScriptObject*>(pCapEntity);
            if (pLuaObj && pLuaObj->GetBotGoalType() == BOT_GOAL_TYPE_FLAG_CAP) { // BOT_GOAL_TYPE_FLAG_CAP from bot_constants.h
                foundCTFObjectives = true;
                int teamID = teamMapping[i];
                if (teamID >= TEAM_ID_RED && teamID < (TEAM_ID_RED + MAX_PLAYABLE_TEAMS_FF)) { // Ensure valid index
                    int arrayIndex = teamID - TEAM_ID_RED; // Convert game team ID to 0-based array index
                    m_teamCaptureZones[arrayIndex] = pCapEntity;
                    DevMsg("CFFBotManager: Found CTF Capture Zone: '%s' (botgoaltype) for team %d and stored.\n", STRING(pCapEntity->GetEntityName()), teamID);

                    // Optionally add to generic m_zone list as well if needed for general navigation/visualization
                    if (m_zoneCount < MAX_ZONES) {
                        m_zone[m_zoneCount].m_entity = pCapEntity;
                        m_zone[m_zoneCount].m_iszEntityName = pCapEntity->GetEntityName();
                        m_zone[m_zoneCount].m_center = pCapEntity->GetAbsOrigin();
                        pCapEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
                        m_zone[m_zoneCount].m_team = teamID;
                        m_zone[m_zoneCount].m_index = m_zoneCount;
                        m_zoneCount++;
                    }
                } else {
                    DevWarning("CFFBotManager: Capture zone '%s' has unmappable team ID %d.\n", STRING(pCapEntity->GetEntityName()), teamID);
                }
            } else if (pLuaObj) {
                 DevMsg("CFFBotManager: Entity '%s' is CScriptObject but not Bot.kFlagCap (type %d).\n", STRING(pCapEntity->GetEntityName()), pLuaObj->GetBotGoalType());
            }
        } else {
            DevMsg("CFFBotManager: CTF Capture Zone entity '%s' not found by name.\n", captureZoneNames[i]);
        }
    }

    if (ctfTeamsWithStands >= MAX_PLAYABLE_TEAMS_FF || (m_teamCaptureZones[TEAM_ID_RED-TEAM_ID_RED].IsValid() && m_teamCaptureZones[TEAM_ID_BLUE-TEAM_ID_RED].IsValid())) {
        m_gameScenario = SCENARIO_CAPTURETHEFLAG;
    }

	// Detect Control Points (logic unchanged from previous step)
    CUtlVector<CPManagerZoneInitData> foundCPsSorted; /* ... */
	// ... (CP detection and sorting logic remains here) ...

	// Detect VIP Escape Zones (logic unchanged from previous step)
	CBaseEntity *pEscapeZoneEntity = NULL; /* ... */
	// ... (VIP escape zone detection logic remains here) ...

	// Determine Game Scenario (logic unchanged from previous step)
	if (m_gameScenario != SCENARIO_CAPTURETHEFLAG && foundCPObjectives) { /* ... */ }
    else if (m_gameScenario != SCENARIO_CAPTURETHEFLAG && m_gameScenario != SCENARIO_CONTROLPOINT && foundVIPObjectives) { /* ... */ }
    // ... rest of scenario determination and nav area population ...
	for (int i = 0; i < m_zoneCount; ++i) { /* ... (Populate nav areas) ... */ }
	m_isMapDataLoaded = true;
}

[end of mp/src/game/server/ff/bot/ff_bot_manager.cpp]
