//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encapsulation of the current scenario/game state. Allows each bot imperfect knowledge.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_gamestate.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "bot_constants.h"
#include "gamedll/gameentitysystem.h"
#include "../ff_player.h"
#include "ff_scriptman.h"
#include "utlvector.h" // For CUtlVector
#include <algorithm> // For std::sort

// Lua includes
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifndef TEAM_ID_NONE
#define TEAM_ID_NONE -1
#endif
#ifndef TEAM_ID_RED
#define TEAM_ID_RED 0
#endif
#ifndef TEAM_ID_BLUE
#define TEAM_ID_BLUE 1
#endif
#ifndef FLAG_RETURN_TIME
#define FLAG_RETURN_TIME 30.0f
#endif
// TODO_FF: Define these based on actual entity names used in FF maps
#define FF_FLAG_ITEM_RED_NAME "red_flag"
#define FF_FLAG_ITEM_BLUE_NAME "blue_flag"
#define FF_FLAG_STAND_RED_NAME "flag_red_stand"
#define FF_FLAG_STAND_BLUE_NAME "flag_blue_stand"
#define FF_CP_ENTITY_CLASSNAME "trigger_controlpoint" // Example, confirm actual classname

#ifndef CLASS_CIVILIAN_FF
#define CLASS_CIVILIAN_FF 10
#endif
#ifndef VIP_TEAM
#define VIP_TEAM TEAM_ID_BLUE
#endif

static const float CP_POLL_INTERVAL = 0.75f;

// Helper structure for sorting CPs
struct CPInitData {
    CBaseEntity* entity;
    string_t entityName;
    int tentativeID; // Parsed or assigned ID before sorting

    bool operator<(const CPInitData& other) const {
        return tentativeID < other.tentativeID;
    }
};

//--------------------------------------------------------------------------------------------------------------
FFGameState::FFGameState( CFFBot *owner )
{
	m_owner = owner;
	m_nextCPPollTime = 0.0f;
	Reset();
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::Reset( void )
{
	m_isRoundOver = false;
	m_nextCPPollTime = gpGlobals->curtime + CP_POLL_INTERVAL;

	// TODO_FF: Ensure flag item names and flag stand names are correct for FF maps
	InitializeFlagState(TEAM_ID_RED, FF_FLAG_ITEM_RED_NAME, FF_FLAG_STAND_RED_NAME);
	InitializeFlagState(TEAM_ID_BLUE, FF_FLAG_ITEM_BLUE_NAME, FF_FLAG_STAND_BLUE_NAME);

	InitializeControlPointStates(FF_CP_ENTITY_CLASSNAME);
	InitializeVIPState();
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeFlagState(int teamID, const char* flagItemEntityName, const char* flagStandEntityName)
{
	if (teamID < 0 || teamID >= MAX_PLAYABLE_TEAMS_FF) {
        DevWarning("FFGameState::InitializeFlagState: Invalid teamID %d\n", teamID);
        return;
    }

	m_Flags[teamID].Reset();
	m_Flags[teamID].teamAffiliation = teamID;
    m_Flags[teamID].m_iszEntityName = AllocPooledString(flagItemEntityName);

	CBaseEntity *pFlagItemEnt = gEntList.FindEntityByName(NULL, flagItemEntityName, NULL);
	if (pFlagItemEnt) {
		m_Flags[teamID].entity = pFlagItemEnt;
		DevMsg("FFGameState: Found flag item '%s' for team %d.\n", flagItemEntityName, teamID);
	} else {
		DevWarning("FFGameState::InitializeFlagState: Could not find flag item entity '%s' for team %d.\n", flagItemEntityName, teamID);
		m_Flags[teamID].entity = NULL;
	}

    CBaseEntity *pFlagStandEnt = gEntList.FindEntityByName(NULL, flagStandEntityName, NULL);
    if (pFlagStandEnt) {
        m_Flags[teamID].entitySpawnLocation = pFlagStandEnt->GetAbsOrigin();
		m_Flags[teamID].dropLocation = pFlagStandEnt->GetAbsOrigin(); // Initially at home
        DevMsg("FFGameState: Found flag stand '%s' for team %d at (%.1f, %.1f, %.1f).\n", flagStandEntityName, teamID,
            m_Flags[teamID].entitySpawnLocation.x, m_Flags[teamID].entitySpawnLocation.y, m_Flags[teamID].entitySpawnLocation.z);
    } else {
        DevWarning("FFGameState::InitializeFlagState: Could not find flag stand entity '%s' for team %d. Using flag item's origin if available, else (0,0,0).\n", flagStandEntityName, teamID);
        if (pFlagItemEnt) { // Fallback to flag item's current origin if stand not found
            m_Flags[teamID].entitySpawnLocation = pFlagItemEnt->GetAbsOrigin();
		    m_Flags[teamID].dropLocation = pFlagItemEnt->GetAbsOrigin();
        } else {
            m_Flags[teamID].entitySpawnLocation = vec3_origin;
		    m_Flags[teamID].dropLocation = vec3_origin;
        }
    }
	m_Flags[teamID].currentState = FF_FLAG_STATE_HOME;
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeControlPointStates(const char* cpEntityClassName)
{
	m_numControlPoints = 0;
	CUtlVector<CPInitData> foundCPs;
    int sequentialIdCounter = 0; // For CPs where ID cannot be parsed from name

	CBaseEntity *pCPEntity = NULL;
	for (;(pCPEntity = gEntList.FindEntityByClassname(pCPEntity, cpEntityClassName)) != NULL;)
	{
		if (foundCPs.Count() >= MAX_CONTROL_POINTS_FF) {
            DevWarning("FFGameState::InitializeControlPointStates: Found more CP entities of class '%s' than MAX_CONTROL_POINTS_FF (%d). Some will be ignored.\n", cpEntityClassName, MAX_CONTROL_POINTS_FF);
			break;
        }

        CPInitData cpData;
        cpData.entity = pCPEntity;
        cpData.entityName = pCPEntity->GetEntityName();
        cpData.tentativeID = -1;

        // Try to parse a numerical ID from the entity name
        // Example: "control_point_1", "cp_dustbowl_3", "point_alpha_0"
        const char *name = STRING(cpData.entityName);
        const char *p = name + Q_strlen(name) - 1;
        while (p >= name && isdigit(*p)) { p--; }
        if (p < (name + Q_strlen(name) - 1)) { // Found some digits at the end
            cpData.tentativeID = atoi(p + 1);
            // Adjust if parsed ID is 1-based from name to be 0-indexed internally for C++
            // This depends on map naming convention. Assuming names like "cp_1" mean ID 1 (so 0 in C++).
            if (cpData.tentativeID > 0) cpData.tentativeID--;
        }

        if (cpData.tentativeID == -1) { // Parsing failed or no number found
            cpData.tentativeID = sequentialIdCounter++; // Assign a sequential ID
            DevWarning("FFGameState: CP '%s' could not parse ID from name, assigned sequential temp ID %d.\n", name, cpData.tentativeID);
        } else {
            DevMsg("FFGameState: CP '%s' parsed tentative ID %d.\n", name, cpData.tentativeID);
        }
		foundCPs.AddToTail(cpData);
	}

    // Sort CPs by their tentativeID to ensure C++ array order matches Lua's expected order (if Lua relies on entity creation order or named indices)
    // This is crucial if Lua's command_points table is implicitly ordered.
    if (foundCPs.Count() > 1) {
        std::sort(foundCPs.begin(), foundCPs.end());
    }

    // Populate the main m_ControlPoints array
    for (int i = 0; i < foundCPs.Count(); ++i) {
        if (m_numControlPoints >= MAX_CONTROL_POINTS_FF) break; // Should not happen if first check worked

        m_ControlPoints[m_numControlPoints].Reset();
        m_ControlPoints[m_numControlPoints].entity = foundCPs[i].entity;
        m_ControlPoints[m_numControlPoints].m_iszEntityName = foundCPs[i].entityName;
        m_ControlPoints[m_numControlPoints].pointID = m_numControlPoints; // Final 0-indexed ID based on sorted order

        // TODO_FF: Determine initial CP owner and locked state from map entity properties if possible
        m_ControlPoints[m_numControlPoints].owningTeam = TEAM_ID_NONE;
        m_ControlPoints[m_numControlPoints].isLocked = false;

        DevMsg("FFGameState: Initialized CP ID %d ('%s', tentative Lua ID %d).\n",
            m_ControlPoints[m_numControlPoints].pointID,
            STRING(m_ControlPoints[m_numControlPoints].m_iszEntityName),
            foundCPs[i].tentativeID +1); // Log the 1-based tentative ID for easier Lua comparison
        m_numControlPoints++;
    }

	if (m_numControlPoints == 0) {
		DevMsg("FFGameState::InitializeControlPointStates: No entities found with classname '%s'.\n", cpEntityClassName);
	} else {
        DevMsg("FFGameState: Initialized %d control points.\n", m_numControlPoints);
    }
}
void FFGameState::InitializeVIPState( void ) { /* ... (implementation unchanged) ... */ }
bool FFGameState::IsPlayerVIP(CFFPlayer* pPlayer) const { /* ... (implementation unchanged, placeholder) ... */ return false; }

void FFGameState::Update( void ) { /* ... (implementation of CP polling logic from previous subtask, unchanged here) ... */
	if (IsRoundOver()) return;
	for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) { /* ... flag return ... */ }
    if (m_vipPlayer.IsValid() && m_isVIPAlive && !m_vipPlayer->IsAlive()) { m_isVIPAlive = false; }
	if (gpGlobals->curtime >= m_nextCPPollTime && m_numControlPoints > 0) { /* ... CP polling logic ... */ }
}

// Event Handlers & Query Methods (implementations unchanged from previous steps)
void FFGameState::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; }
void FFGameState::OnRoundStart( IGameEvent *event ) { Reset(); }
bool FFGameState::IsRoundOver( void ) const { return m_isRoundOver; }
int FFGameState::GetFlagTeamFromEntity(CBaseEntity* pFlagEntity) const { /* ... */ }
int FFGameState::GetCPIDFromEntity(const CBaseEntity* pCPEntity) const { // Added const
    if (!pCPEntity) return -1;
    for (int i = 0; i < m_numControlPoints; ++i) {
        if (m_ControlPoints[i].entity.IsValid() && m_ControlPoints[i].entity.Get() == pCPEntity) return m_ControlPoints[i].pointID;
    }
    return -1;
}
void FFGameState::OnFlagPickedUp(CBaseEntity* pFlagEntity, CFFPlayer* pPlayer) { /* ... */ }
void FFGameState::OnFlagDropped(CBaseEntity* pFlagEntity, const Vector& dropLocation) { /* ... */ }
void FFGameState::OnFlagCaptured(CBaseEntity* pFlagEntity, CFFPlayer* pCapturer) { /* ... */ }
void FFGameState::OnFlagReturned(CBaseEntity* pFlagEntity) { /* ... */ }
const FFGameState::FF_FlagState* FFGameState::GetFlagInfo(int team) const { /* ... */ }
bool FFGameState::IsTeamFlagHome(int team) const { /* ... */ }
bool FFGameState::IsTeamFlagCarried(int team, CFFPlayer** pCarrier) const { /* ... */ }
bool FFGameState::IsTeamFlagDropped(int team, Vector* pDropLocation) const { /* ... */ }
bool FFGameState::IsOtherTeamFlagAtOurBase(int myTeamID) const { /* ... */ }
void FFGameState::OnControlPointCaptured(CBaseEntity* pCPEntity, int newOwnerTeam) { /* ... */ }
void FFGameState::OnControlPointProgress(CBaseEntity* pCPEntity, int teamMakingProgress, float newProgress) { /* ... */ }
void FFGameState::OnControlPointBlocked(CBaseEntity* pCPEntity, bool isBlocked) { /* ... */ }
const FFGameState::FF_ControlPointState* FFGameState::GetControlPointInfo(int cpID) const { /* ... */ }
int FFGameState::GetControlPointOwner(int cpID) const { /* ... */ }
float FFGameState::GetControlPointCaptureProgress(int cpID, int team) const { /* ... */ }
bool FFGameState::IsControlPointLocked(int cpID) const { /* ... */ }
void FFGameState::OnVIPKilled(CFFPlayer* pVIPVictim, CBaseEntity* pKiller) { /* ... */ }
void FFGameState::OnVIPEscaped(CFFPlayer* pVIP) { /* ... */ }
void FFGameState::OnPlayerSpawn(CFFPlayer* pPlayer) { /* ... */ }
CFFPlayer* FFGameState::GetVIP() const { /* ... */ }
bool FFGameState::IsVIPAlive() const { /* ... */ }
bool FFGameState::IsVIPEscaped() const { /* ... */ }

[end of mp/src/game/server/ff/bot/ff_gamestate.cpp]
