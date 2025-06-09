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
#include "bot_constants.h"  // For BotGoalType constants, TEAM_ID_RED etc.
#include "bot_util.h"       // For GetEntityBotGoalTypeFromLua, GetEntityCpNumberFromLua
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

// Define names used to find entities, now primarily as fallbacks or for specific non-scripted entities
#define FF_FLAG_STAND_RED_NAME_MGR_DEF "flag_red_stand"
#define FF_FLAG_STAND_BLUE_NAME_MGR_DEF "flag_blue_stand"
// Capture zone names might still be used if botgoaltype isn't set on all of them, but botgoaltype is preferred
// #define FF_CAPTURE_ZONE_RED_NAME_DEF "red_cap"
// #define FF_CAPTURE_ZONE_BLUE_NAME_DEF "blue_cap"

#define FF_CP_ENTITY_CLASSNAME_MGR_DEF "trigger_controlpoint" // Used to find potential CP entities
// VIP Escape zones should ideally be found by botgoaltype
// #define FF_VIP_ESCAPEZONE_CLASSNAME_MGR_DEF "func_escapezone"


// Helper struct for sorting CPs, moved here for local use in ExtractScenarioData
struct CPManagerZoneInitData {
    CBaseEntity* pEntity;
    string_t iszEntityName;
    int tentativeID; // 0-indexed
    bool operator<(const CPManagerZoneInitData& other) const { return tentativeID < other.tentativeID; }
};

// TODO_FF: Implement this helper in bot_util.cpp to get team from Lua property
// For now, it's a stub.
int GetEntityTeamFromLua(CBaseEntity* pEntity) {
    // Placeholder: Attempt to get "team" property.
    // In a real implementation, this would call GetIntPropertyFromLua(pEntity, "team")
    // and map known Lua team enums (Team.kBlue, etc.) to engine team IDs (TEAM_ID_BLUE, etc.)
    // For example:
    // int luaTeam = GetIntPropertyFromLua(pEntity, "team");
    // if (luaTeam == 1) return TEAM_ID_BLUE; // Assuming Team.kBlue is 1 in Lua
    // if (luaTeam == 2) return TEAM_ID_RED;   // Assuming Team.kRed is 2 in Lua
    // return TEAM_UNASSIGNED;
    if (pEntity) { // Basic fallback based on name if specific Lua property isn't available yet
        if (strstr(STRING(pEntity->GetEntityName()), "blue")) return TEAM_ID_BLUE;
        if (strstr(STRING(pEntity->GetEntityName()), "red")) return TEAM_ID_RED;
    }
    return TEAM_UNASSIGNED;
}

// AddZone helper method (ensure it exists or is added to CFFBotManager)
// This is a simplified version; the actual might be more complex or already exist.
void CFFBotManager::AddZone(CBaseEntity *pEntity, int team, int specific_index)
{
    if (m_zoneCount >= MAX_ZONES || !pEntity) return;

    m_zone[m_zoneCount].m_entity = pEntity;
    m_zone[m_zoneCount].m_iszEntityName = pEntity->GetEntityName();
    m_zone[m_zoneCount].m_center = pEntity->GetAbsOrigin();
    pEntity->CollisionProp()->WorldSpaceSurroundingBounds(&m_zone[m_zoneCount].m_extent.lo, &m_zone[m_zoneCount].m_extent.hi);
    m_zone[m_zoneCount].m_team = team;

    if (specific_index != -1) {
        m_zone[m_zoneCount].m_index = specific_index;
    } else {
        m_zone[m_zoneCount].m_index = m_zoneCount; // Default sequential index if not a CP
    }

    // Populate nav areas for this zone
    if (TheNavMesh) {
        TheNavMesh->GetNavAreasOverlappingExtent(m_zone[m_zoneCount].m_extent, m_zone[m_zoneCount].m_area, MAX_ZONE_NAV_AREAS, &m_zone[m_zoneCount].m_areaCount);
    } else {
        m_zone[m_zoneCount].m_areaCount = 0;
    }
    m_zoneCount++;
}


inline CFFPlayer *ToFFPlayer( CBaseEntity *pEntity ) { /* ... (implementation unchanged) ... */ }
inline bool AreBotsAllowed() { /* ... (implementation unchanged) ... */ }
void InstallBotControl( void ) { /* ... (implementation unchanged) ... */ }
void RemoveBotControl( void ) { /* ... (implementation unchanged) ... */ }
CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername ) { /* ... (implementation unchanged) ... */ }

CFFBotManager::CFFBotManager() :
	m_PlayerFootstepEvent(this),
	// m_PlayerRadioEvent(this), // Removed radio event member initialization
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
// void CFFBotManager::OnPlayerRadio( IGameEvent *event ) { /* ... */ } // Removed PlayerRadio event handler
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
    // Ensure TEAM_ID_MIN_PLAYABLE and TEAM_ID_MAX_PLAYABLE are defined (e.g. TEAM_ID_BLUE and TEAM_ID_GREEN or TEAM_ID_RED)
    // For 2-team (Red vs Blue), Red=0, Blue=1 from bot_constants.h perspective for array indexing
    // Assuming teamID is passed as engine team ID (e.g. TEAM_SPECTATOR, TEAM_RED_TF, TEAM_BLUE_TF)
    // And these are mapped to 0,1 for m_teamCaptureZones array.
    // Example: if TEAM_RED_TF = 2, TEAM_BLUE_TF = 3, then index = teamID - TEAM_RED_TF;
    // For this example, using direct mapping if teamID is already 0 or 1.
    int teamIndex = -1;
    if (teamID == TEAM_ID_RED) teamIndex = 0; // Or map based on actual engine team values
    else if (teamID == TEAM_ID_BLUE) teamIndex = 1;
    // Add other teams if necessary

    if (teamIndex >= 0 && teamIndex < MAX_PLAYABLE_TEAMS_FF) {
         return m_teamCaptureZones[teamIndex].Get();
    }
    // DevWarning("GetTeamCaptureZone: Invalid or unhandled teamID %d requested.\n", teamID);
    return NULL;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::ExtractScenarioData(void)
{
    m_isMapDataLoaded = true; // Set early, then clear if nav mesh missing
    m_gameScenario = SCENARIO_ARENA; // Default
    m_zoneCount = 0;
    for(int i=0; i<MAX_ZONES; ++i) {
        m_zone[i].m_entity = NULL;
        m_zone[i].m_areaCount = 0;
        m_zone[i].m_iszEntityName = NULL_STRING;
        m_zone[i].m_team = TEAM_UNASSIGNED;
        m_zone[i].m_index = -1; // Initialize index to -1 (invalid/not set)
    }
    for(int i=0; i<MAX_PLAYABLE_TEAMS_FF; ++i) {
        m_teamCaptureZones[i] = NULL;
    }

    if ( !TheNavMesh || !TheNavMesh->IsLoaded() ) {
        DevWarning( "CFFBotManager::ExtractScenarioData: Nav mesh not loaded. Cannot determine objectives.\n" );
        m_isMapDataLoaded = false;
        return;
    }

    bool foundCTFObjectives = false;
    bool foundCPObjectives = false;
    bool foundVIPObjectives = false;

    CUtlVector<CPManagerZoneInitData> foundCPZoneData;
    int sequentialCPIDCounter = 0;

    // Iterate all entities to find objectives by botgoaltype or classname
    CBaseEntity *pEntity = NULL;
    for (CEntitySphereQuery sphere(vec3_origin, MAX_COORD_FLOAT, 0); (pEntity = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity())
    {
        if (!pEntity) continue;

        int goalType = GetEntityBotGoalTypeFromLua(pEntity); // From bot_util.h
        int teamNum = GetEntityTeamFromLua(pEntity); // TODO_FF: Implement this in bot_util.cpp

        if (goalType == BOT_GOAL_FLAG) {
            foundCTFObjectives = true;
            // Flags themselves are mainly tracked by FFGameState.
            // If their stands are separate entities without botgoaltype, they're found by name later.
            // AddZone(pEntity, teamNum); // Optionally add flags to generic zones if needed for pathing targets
            DevMsg("ExtractScenarioData: Found CTF Flag '%s' (botgoaltype %d, team %d)\n", STRING(pEntity->GetEntityName()), goalType, teamNum);
        }
        else if (goalType == BOT_GOAL_FLAG_CAP) {
            foundCTFObjectives = true;
            if (teamNum >= TEAM_ID_MIN_PLAYABLE && teamNum <= TEAM_ID_MAX_PLAYABLE_ENGINE) { // Use engine team IDs for check
                int teamIndex = teamNum - TEAM_ID_MIN_PLAYABLE; // Convert to 0-indexed for array
                 if (teamIndex >= 0 && teamIndex < MAX_PLAYABLE_TEAMS_FF) {
                    m_teamCaptureZones[teamIndex] = pEntity;
                    AddZone(pEntity, teamNum); // Add to generic zones
                    DevMsg("ExtractScenarioData: Found CTF Capture Zone '%s' for team %d (botgoaltype %d)\n", STRING(pEntity->GetEntityName()), teamNum, goalType);
                 } else {
                    DevWarning("ExtractScenarioData: CTF Capture Zone '%s' has out-of-bounds teamIndex %d from teamNum %d.\n", STRING(pEntity->GetEntityName()), teamIndex, teamNum);
                 }
            } else {
                DevWarning("ExtractScenarioData: CTF Capture Zone '%s' has invalid team %d from Lua.\n", STRING(pEntity->GetEntityName()), teamNum);
            }
        }
        else if (goalType == BOT_GOAL_HUNTED_ESCAPE) {
            foundVIPObjectives = true;
            AddZone(pEntity, TEAM_UNASSIGNED); // Escape zones usually neutral
            DevMsg("ExtractScenarioData: Found VIP Escape Zone '%s' (botgoaltype %d)\n", STRING(pEntity->GetEntityName()), goalType);
        }
        // Check for CPs by classname, as they might not have a botgoaltype or it might be misconfigured
        else if (FClassnameIs(pEntity, FF_CP_ENTITY_CLASSNAME_MGR_DEF)) {
            foundCPObjectives = true;
            CPManagerZoneInitData cpData;
            cpData.pEntity = pEntity;
            cpData.iszEntityName = pEntity->GetEntityName();
            cpData.tentativeID = -1;

            int lua_cp_number = GetEntityCpNumberFromLua(pEntity);
            if (lua_cp_number > 0) { // Lua is 1-indexed
                cpData.tentativeID = lua_cp_number - 1;
            } else {
                const char* name = STRING(cpData.iszEntityName);
                const char* num_str = Q_strrchr(name, '_');
                if (num_str && atoi(num_str + 1) > 0) {
                    cpData.tentativeID = atoi(num_str + 1) - 1; // Assume 1-indexed names
                }
                // If still -1, will be handled by sequential assignment after sorting
            }
            foundCPZoneData.AddToTail(cpData);
        }
    }

    // Fallback: Find CTF Flag Stands by name if not found by botgoaltype (or if stands are separate entities)
    const char* flagStandNames[] = { FF_FLAG_STAND_RED_NAME_MGR_DEF, FF_FLAG_STAND_BLUE_NAME_MGR_DEF };
    for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
        if (i >= (sizeof(flagStandNames)/sizeof(flagStandNames[0]))) break;
        CBaseEntity* pStandEntity = gEntList.FindEntityByName(NULL, flagStandNames[i]);
        if (pStandEntity) {
            bool alreadyAdded = false; // Check if already added (e.g. if flag entity itself is the stand and has botgoaltype)
            for(int j=0; j < m_zoneCount; ++j) { if(m_zone[j].m_entity == pStandEntity) { alreadyAdded = true; break; } }
            if (!alreadyAdded) {
                foundCTFObjectives = true;
                AddZone(pStandEntity, TEAM_ID_RED + i); // Assign team based on array order
                DevMsg("ExtractScenarioData: Found CTF Flag Stand by name: '%s' for team %d\n", STRING(pStandEntity->GetEntityName()), TEAM_ID_RED + i);
            }
        }
    }

    // Sort and add CP zones
    if (foundCPZoneData.Count() > 0) {
        foundCPZoneData.Sort();
        bool usedCPIDs[MAX_ZONES] = {false}; // Assuming MAX_ZONES is large enough for CP IDs

        for (int i=0; i < foundCPZoneData.Count(); ++i) {
            CPManagerZoneInitData& cpData = foundCPZoneData[i]; // Modifiable reference
            if (cpData.tentativeID == -1) { // Needs sequential assignment
                for (int j = 0; j < MAX_ZONES; ++j) {
                    if (!usedCPIDs[j]) {
                        cpData.tentativeID = j;
                        break;
                    }
                }
            }
            if (cpData.tentativeID != -1 && cpData.tentativeID < MAX_ZONES) {
                if (!usedCPIDs[cpData.tentativeID]) {
                    AddZone(cpData.pEntity, TEAM_UNASSIGNED, cpData.tentativeID);
                    usedCPIDs[cpData.tentativeID] = true;
                    DevMsg("ExtractScenarioData: Added CP Zone '%s' with index %d\n", STRING(cpData.iszEntityName), cpData.tentativeID);
                } else {
                    DevWarning("ExtractScenarioData: Duplicate CP index %d for '%s'\n", cpData.tentativeID, STRING(cpData.iszEntityName));
                }
            } else {
                 DevWarning("ExtractScenarioData: CP '%s' could not be assigned a valid index.\n", STRING(cpData.iszEntityName));
            }
        }
    }

    // Determine Game Scenario based on found objectives
    if (foundCTFObjectives) {
        m_gameScenario = SCENARIO_CAPTURETHEFLAG;
        DevMsg("ExtractScenarioData: Game scenario set to CTF.\n");
    } else if (foundCPObjectives) {
        m_gameScenario = SCENARIO_CONTROLPOINT;
        DevMsg("ExtractScenarioData: Game scenario set to Control Points.\n");
    } else if (foundVIPObjectives) {
        m_gameScenario = SCENARIO_VIP;
        DevMsg("ExtractScenarioData: Game scenario set to VIP.\n");
    } else {
        m_gameScenario = SCENARIO_ARENA; // Default if no specific objectives found
        DevMsg("ExtractScenarioData: No specific objectives found, defaulting to Arena.\n");
    }
    // Note: PopulateNavAreas was part of AddZone in this version
}
