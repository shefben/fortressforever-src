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
#include "utlvector.h"
#include <algorithm>
#include "../../shared/ff/ff_shareddefs.h" // For CLASS_CIVILIAN (hopefully) and team IDs
#include "bot_util.h" // For GetEntityCpNumberFromLua

// Lua includes
extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Ensure team_id and class_id constants are defined.
// These might come from ff_shareddefs.h or bot_constants.h.
// Using values from ff_gamestate.h for now if not globally available.
#ifndef TEAM_ID_NONE
#define TEAM_ID_NONE -1
#endif
#ifndef TEAM_ID_RED
#define TEAM_ID_RED 0 // As per ff_gamestate.h example
#endif
#ifndef TEAM_ID_BLUE
#define TEAM_ID_BLUE 1 // As per ff_gamestate.h example
#endif

#ifndef VIP_TEAM // From ff_gamestate.h example
#define VIP_TEAM TEAM_ID_BLUE
#endif

#ifndef CLASS_CIVILIAN // Standard Source SDK, check if FF uses a different enum or ID
#define CLASS_CIVILIAN 10 // Matches ff_shareddefs.h convention usually, and ff_gamestate.h CLASS_CIVILIAN_FF placeholder
#endif


#ifndef FLAG_RETURN_TIME
#define FLAG_RETURN_TIME 30.0f
#endif
#define FF_FLAG_ITEM_RED_NAME "red_flag"
#define FF_FLAG_ITEM_BLUE_NAME "blue_flag"
#define FF_FLAG_STAND_RED_NAME "flag_red_stand"
#define FF_FLAG_STAND_BLUE_NAME "flag_blue_stand"
#define FF_CP_ENTITY_CLASSNAME "trigger_controlpoint"

static const float CP_POLL_INTERVAL = 0.75f;

// Helper struct for sorting
struct CPInitData {
    CBaseEntity* pEntity;
    string_t iszEntityName;
    int tentativeID; // 0-indexed after potential conversion from 1-indexed Lua cp_number
    bool operator<(const CPInitData& other) const { return tentativeID < other.tentativeID; }
};

FFGameState::FFGameState( CFFBot *owner ) {
	m_owner = owner;
	m_nextCPPollTime = 0.0f;
	Reset();
}
void FFGameState::Reset( void ) {
	m_isRoundOver = false;
	m_nextCPPollTime = gpGlobals->curtime + CP_POLL_INTERVAL;
	InitializeFlagState(TEAM_ID_RED, FF_FLAG_ITEM_RED_NAME, FF_FLAG_STAND_RED_NAME);
	InitializeFlagState(TEAM_ID_BLUE, FF_FLAG_ITEM_BLUE_NAME, FF_FLAG_STAND_BLUE_NAME);
	InitializeControlPointStates(FF_CP_ENTITY_CLASSNAME);
	InitializeVIPState();
}
void FFGameState::InitializeFlagState(int teamID, const char* flagItemEntityName, const char* flagStandEntityName) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
// Initializes the state of all control points.
// It tries to get a 'cp_number' from Lua first, then falls back to name parsing.
//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeControlPointStates(const char* cpEntityClassName)
{
    // Clear previous state
    for (int i = 0; i < MAX_CONTROL_POINTS_FF; ++i) {
        this->m_ControlPoints[i].Reset();
        this->m_ControlPoints[i].pointID = -1;
    }
    this->m_numControlPoints = 0;

    if (!cpEntityClassName) {
        DevWarning("InitializeControlPointStates: cpEntityClassName is NULL.\n");
        return;
    }

    CUtlVector<CPInitData> foundCPs;
    CBaseEntity *pEntity = NULL;
    // int sequentialIDCounter = 0; // Replaced with better fallback logic

    // Iterate all entities of the given classname
    // Note: Using FindEntityByClassname in a loop is standard. CEntitySphereQuery might be an alternative if class iteration is slow.
    for (pEntity = gEntList.FindEntityByClassname(NULL, cpEntityClassName);
         pEntity != NULL;
         pEntity = gEntList.FindEntityByClassname(pEntity, cpEntityClassName))
    {
        if (foundCPs.Count() >= MAX_CONTROL_POINTS_FF) {
            DevWarning("FFGameState: Found more CP entities than MAX_CONTROL_POINTS_FF (%d). Some CPs may not be tracked.\n", MAX_CONTROL_POINTS_FF);
            break;
        }

        CPInitData cpData;
        cpData.pEntity = pEntity;
        cpData.iszEntityName = pEntity->GetEntityName();
        cpData.tentativeID = -1; // Initialize as not found

        // 1. Attempt to get cp_number from Lua using the new helper
        int lua_cp_number = GetEntityCpNumberFromLua(pEntity); // From bot_util.h

        if (lua_cp_number > 0) { // Lua cp_number is typically 1-indexed and positive
            cpData.tentativeID = lua_cp_number - 1; // Convert to 0-indexed for C++
            // DevMsg("FFGameState: CP '%s' (ent %d), Lua cp_number: %d -> tentativeID: %d\n", STRING(cpData.iszEntityName), pEntity->entindex(), lua_cp_number, cpData.tentativeID);
        } else {
            // 2. Fallback to name parsing (e.g., "control_point_1", "cp_alpha_3")
            const char* name = STRING(cpData.iszEntityName);
            const char* num_str = Q_strrchr(name, '_');
            if (num_str && *(num_str + 1) != '\0') { // Check there's something after '_'
                 char *endptr;
                 long val = strtol(num_str + 1, &endptr, 10);
                 if (*endptr == '\0' && val > 0) { // Successfully parsed a positive integer
                    cpData.tentativeID = val - 1; // Assuming names are 1-indexed
                    // DevMsg("FFGameState: CP '%s' (ent %d), Name-parsed ID: %d -> tentativeID: %d\n", name, pEntity->entindex(), (int)val, cpData.tentativeID);
                 }
            }
        }

        // If tentativeID is still -1, it means neither Lua nor name parsing yielded a valid ID.
        // It will be handled after sorting to pick the lowest available slot.
        if (cpData.tentativeID == -1) {
             DevMsg("FFGameState: CP '%s' (ent %d) could not determine ID from Lua or name yet. Will attempt to assign lowest available slot after sorting.\n", STRING(cpData.iszEntityName), pEntity->entindex());
        }
        foundCPs.AddToTail(cpData);
    }

    if (foundCPs.Count() > 1) {
        foundCPs.Sort();
    }

    bool usedIDs[MAX_CONTROL_POINTS_FF];
    for(int k=0; k<MAX_CONTROL_POINTS_FF; ++k) usedIDs[k] = false;

    for (int i = 0; i < foundCPs.Count(); ++i) {
        if (this->m_numControlPoints >= MAX_CONTROL_POINTS_FF) {
             DevWarning("FFGameState: Trying to populate m_ControlPoints exceeded MAX_CONTROL_POINTS_FF during sorted insertion.\n");
             break;
        }

        CPInitData& cpData = foundCPs[i];

        if (cpData.tentativeID == -1) {
            bool foundSlot = false;
            for (int j = 0; j < MAX_CONTROL_POINTS_FF; ++j) {
                if (!usedIDs[j]) {
                    cpData.tentativeID = j;
                    DevMsg("FFGameState: CP '%s' assigned fallback ID %d.\n", STRING(cpData.iszEntityName), j);
                    foundSlot = true;
                    break;
                }
            }
            if (!foundSlot) {
                 DevWarning("FFGameState: CP '%s' could not be assigned a fallback ID (no free slots up to MAX_CONTROL_POINTS_FF). Skipping.\n", STRING(cpData.iszEntityName));
                 continue;
            }
        }

        if (cpData.tentativeID < 0 || cpData.tentativeID >= MAX_CONTROL_POINTS_FF) {
            DevWarning("FFGameState: CP '%s' has invalid ID %d after processing. Skipping.\n", STRING(cpData.iszEntityName), cpData.tentativeID);
            continue;
        }
        if (usedIDs[cpData.tentativeID]) {
            DevWarning("FFGameState: Duplicate CP ID %d detected for '%s'. Original was '%s'. Skipping duplicate.\n",
                cpData.tentativeID, STRING(cpData.iszEntityName), STRING(this->m_ControlPoints[cpData.tentativeID].m_iszEntityName));
            continue;
        }

        usedIDs[cpData.tentativeID] = true;
        this->m_ControlPoints[cpData.tentativeID].entity = cpData.pEntity;
        this->m_ControlPoints[cpData.tentativeID].m_iszEntityName = cpData.iszEntityName;
        this->m_ControlPoints[cpData.tentativeID].pointID = cpData.tentativeID;
        // Default values for owningTeam, isLocked, captureProgress are set by FF_ControlPointState::Reset()

        this->m_numControlPoints++;
        // DevMsg("FFGameState: Initialized CP C++ ID %d: Name '%s', Lua-derived/parsed TentativeID %d, Entity %d\n",
        //        cpData.tentativeID, STRING(cpData.iszEntityName), cpData.tentativeID, cpData.pEntity ? cpData.pEntity->entindex() : -1);
    }

    DevMsg("FFGameState: Finished initializing %d control points.\n", this->m_numControlPoints);
}

//--------------------------------------------------------------------------------------------------------------
bool FFGameState::IsPlayerVIP(CFFPlayer* pPlayer) const
{
    if (!pPlayer || !pPlayer->IsAlive()) return false;

    // Fortress Forever "Hunted" mode designates the player on TEAM_BLUE (the "Hunted" team)
    // as the VIP, and they are typically class-restricted (often to Civilian).
    // CLASS_CIVILIAN should be defined in ff_shareddefs.h or equivalent.
    // FF_HUNTED_TEAM is defined in bot_constants.h (hopefully matching game's actual VIP team).

    const CPlayerClassInfo *pClassInfo = pPlayer->GetPlayerClass(); // Assumes CFFPlayer::GetPlayerClass() returns CPlayerClassInfo*

    if (pClassInfo && pClassInfo->GetClassID() == CLASS_CIVILIAN && pPlayer->GetTeamNumber() == FF_HUNTED_TEAM)
    {
        // DevMsg("IsPlayerVIP: Player %s IS VIP (Class: %d, Team: %d)\n", pPlayer->GetPlayerName(), pClassInfo->GetClassID(), pPlayer->GetTeamNumber());
        return true;
    }

    // DevMsg("IsPlayerVIP: Player %s (Class: %d, Team: %d) is NOT VIP.\n", pPlayer->GetPlayerName(), pClassInfo ? pClassInfo->GetClassID() : -1, pPlayer->GetTeamNumber());
    return false;
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeVIPState( void )
{
    m_vipPlayer = NULL;
    m_isVIPAlive = false;
    m_isVIPEscaped = false;
    int foundVIPs = 0;

    for (int i = 1; i <= gpGlobals->maxClients; ++i)
    {
        CBasePlayer *pBasePlayer = UTIL_PlayerByIndex(i);
        if (pBasePlayer && pBasePlayer->IsPlayer() && pBasePlayer->IsConnected() && pBasePlayer->IsAlive()) // Only consider living players initially
        {
            CFFPlayer *pFFPlayer = ToFFPlayer(pBasePlayer);
            if (pFFPlayer && IsPlayerVIP(pFFPlayer))
            {
                m_vipPlayer = pFFPlayer;
                m_isVIPAlive = true;
                foundVIPs++;
                DevMsg("FFGameState::InitializeVIPState: Found VIP: %s (UserID: %d)\n", pFFPlayer->GetPlayerName(), pFFPlayer->GetUserID());
                // Typically, there's only one VIP. If multiple could exist by this logic, take the first.
                // Or, if IsPlayerVIP is precise, this loop should find only one.
            }
        }
    }
    if (foundVIPs == 0) {
        DevMsg("FFGameState::InitializeVIPState: No VIP player identified at round start/reset.\n");
    } else if (foundVIPs > 1) {
        DevWarning("FFGameState::InitializeVIPState: Multiple VIPs identified (%d). Using the last one found: %s.\n", foundVIPs, m_vipPlayer.Get() ? m_vipPlayer->GetPlayerName() : "UNKNOWN");
    }
}


void FFGameState::Update( void ) { /* ... (CP Polling logic + other updates unchanged) ... */
	if (IsRoundOver()) return;
	for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) { /* ... flag return ... */ }
    if (m_vipPlayer.IsValid() && m_isVIPAlive && !m_vipPlayer->IsAlive()) {
        DevMsg("FFGameState::Update: VIP %s no longer alive. Setting m_isVIPAlive = false.\n", m_vipPlayer->GetPlayerName());
        m_isVIPAlive = false;
    }
	if (gpGlobals->curtime >= m_nextCPPollTime && m_numControlPoints > 0) { /* ... CP polling logic ... */ }
}

// Event Handlers & Query Methods (implementations mostly unchanged from previous steps)
void FFGameState::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; }
void FFGameState::OnRoundStart( IGameEvent *event ) { Reset(); }
bool FFGameState::IsRoundOver( void ) const { return m_isRoundOver; }
int FFGameState::GetFlagTeamFromEntity(CBaseEntity* pFlagEntity) const { /* ... */ }
int FFGameState::GetCPIDFromEntity(const CBaseEntity* pCPEntity) const { /* ... */ }
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

void FFGameState::OnVIPKilled(CFFPlayer* pVIPVictim, CBaseEntity* pKiller) {
    if (pVIPVictim && m_vipPlayer.IsValid() && pVIPVictim == m_vipPlayer.Get()) {
        DevMsg("FFGameState::OnVIPKilled: Tracked VIP %s was killed.\n", pVIPVictim->GetPlayerName());
        m_isVIPAlive = false;
    }
}
void FFGameState::OnVIPEscaped(CFFPlayer* pVIP) {
    if (pVIP && m_vipPlayer.IsValid() && pVIP == m_vipPlayer.Get()) {
        DevMsg("FFGameState::OnVIPEscaped: Tracked VIP %s escaped.\n", pVIP->GetPlayerName());
        m_isVIPEscaped = true;
        m_isVIPAlive = false;
    }
}
void FFGameState::OnPlayerSpawn(CFFPlayer* pPlayer) {
    if (!pPlayer) return;
    if (IsPlayerVIP(pPlayer)) {
        if (!m_vipPlayer.IsValid() || m_vipPlayer.Get() != pPlayer) {
            DevMsg("FFGameState::OnPlayerSpawn: Player %s identified as VIP.\n", pPlayer->GetPlayerName());
            m_vipPlayer = pPlayer;
        } else {
            DevMsg("FFGameState::OnPlayerSpawn: Known VIP %s respawned.\n", pPlayer->GetPlayerName());
        }
        m_isVIPAlive = true;
        m_isVIPEscaped = false;
    } else if (m_vipPlayer.IsValid() && pPlayer == m_vipPlayer.Get()) {
        // This case means a player who WAS the VIP respawned as NOT a VIP.
        DevMsg("FFGameState::OnPlayerSpawn: Former VIP %s respawned as non-VIP. Clearing VIP status.\n", pPlayer->GetPlayerName());
        m_vipPlayer = NULL;
        m_isVIPAlive = false;
        m_isVIPEscaped = false;
    }
}
CFFPlayer* FFGameState::GetVIP() const { return m_vipPlayer.Get(); }
bool FFGameState::IsVIPAlive() const { return m_isVIPAlive && m_vipPlayer.IsValid() && m_vipPlayer->IsAlive(); } // More robust check
bool FFGameState::IsVIPEscaped() const { return m_isVIPEscaped; }
