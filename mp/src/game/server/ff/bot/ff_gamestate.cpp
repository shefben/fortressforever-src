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
#include "../ff_player.h" // Required for CFFPlayer iteration and CHandle<CFFPlayer>


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define placeholder team IDs if not available from a global header
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
#define FF_CP_ENTITY_CLASSNAME "trigger_ff_control_point"

// VIP Game Mode Placeholders from ff_gamestate.h (ensure they are consistent)
#ifndef CLASS_CIVILIAN_FF
#define CLASS_CIVILIAN_FF 10
#endif
#ifndef VIP_TEAM
#define VIP_TEAM TEAM_ID_BLUE
#endif


//--------------------------------------------------------------------------------------------------------------
FFGameState::FFGameState( CFFBot *owner )
{
	m_owner = owner;
	Reset();
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::Reset( void )
{
	m_isRoundOver = false;

	InitializeFlagState(TEAM_ID_RED, "flag_red");
	InitializeFlagState(TEAM_ID_BLUE, "flag_blue");
	InitializeControlPointStates(FF_CP_ENTITY_CLASSNAME);
	InitializeVIPState();
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeFlagState(int teamID, const char* flagEntityName)
{
	if (teamID < 0 || teamID >= MAX_PLAYABLE_TEAMS_FF) return;
	m_Flags[teamID].Reset();
	m_Flags[teamID].teamAffiliation = teamID;
	CBaseEntity *pFlagEnt = gEntList.FindEntityByName(NULL, flagEntityName, NULL);
	if (pFlagEnt) {
		m_Flags[teamID].entity = pFlagEnt;
		m_Flags[teamID].entitySpawnLocation = pFlagEnt->GetAbsOrigin();
		m_Flags[teamID].dropLocation = pFlagEnt->GetAbsOrigin();
		m_Flags[teamID].currentState = FF_FLAG_STATE_HOME;
	} else {
		m_Flags[teamID].entity = NULL;
		m_Flags[teamID].currentState = FF_FLAG_STATE_HOME;
	}
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeControlPointStates(const char* cpEntityClassName)
{
	m_numControlPoints = 0;
	CBaseEntity *pCPEntity = NULL;
	for (int i = 0; (pCPEntity = gEntList.FindEntityByClassname(pCPEntity, cpEntityClassName)) != NULL; ++i) {
		if (m_numControlPoints >= MAX_CONTROL_POINTS_FF) break;
		m_ControlPoints[m_numControlPoints].Reset();
		m_ControlPoints[m_numControlPoints].entity = pCPEntity;
		m_ControlPoints[m_numControlPoints].pointID = m_numControlPoints;
		m_ControlPoints[m_numControlPoints].owningTeam = TEAM_ID_NONE;
		m_ControlPoints[m_numControlPoints].isLocked = false;
		m_numControlPoints++;
	}
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::InitializeVIPState( void )
{
    m_vipPlayer = NULL;
    m_isVIPAlive = false;
    m_isVIPEscaped = false;

    // Iterate through players to find the VIP
    // This assumes CFFBotManager or similar can provide a list of all CFFPlayers
    // or we iterate through MAXCLIENTS. Using UTIL_PlayerByIndex for simplicity.
    for (int i = 1; i <= gpGlobals->maxClients; ++i)
    {
        CBasePlayer *pBasePlayer = UTIL_PlayerByIndex(i);
        if (pBasePlayer && pBasePlayer->IsPlayer() && pBasePlayer->IsConnected())
        {
            CFFPlayer *pFFPlayer = ToFFPlayer(pBasePlayer); // Assumes ToFFPlayer exists
            if (pFFPlayer && IsPlayerVIP(pFFPlayer))
            {
                m_vipPlayer = pFFPlayer;
                m_isVIPAlive = pFFPlayer->IsAlive(); // Check if already alive
                // Warning("FFGameState: VIP Found - %s (Alive: %d)\n", pFFPlayer->GetPlayerName(), m_isVIPAlive);
                break;
            }
        }
    }
    if (!m_vipPlayer.IsValid())
    {
        // Warning("FFGameState::InitializeVIPState: No VIP player found on server.\n");
    }
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Helper to determine if a given player is the VIP.
 * This is a placeholder and needs to be adapted to actual FF VIP identification.
 */
bool FFGameState::IsPlayerVIP(CFFPlayer* pPlayer) const
{
    if (!pPlayer) return false;
    // TODO_FF: Replace with actual VIP identification logic.
    // Example: Check player class, a specific entity flag, or team.
    // This assumes VIP is on a specific team and has a specific class ID.
    // CFFPlayerClass *pClass = pPlayer->GetPlayerClass();
    // if (pClass && pClass->GetClassSlot() == CLASS_CIVILIAN_FF && pPlayer->GetTeamNumber() == VIP_TEAM)
    // {
    //     return true;
    // }
    // Placeholder: if a global VIP handle exists in game rules:
    // if (FFGameRules() && FFGameRules()->GetVIP() == pPlayer) return true;
    return false; // Default: No one is VIP until logic is implemented
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::Update( void )
{
	if (IsRoundOver()) return;
	for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
		if (m_Flags[i].currentState == FF_FLAG_STATE_DROPPED && m_Flags[i].entity.IsValid()) {
			if (m_Flags[i].returnTime > 0 && gpGlobals->curtime >= m_Flags[i].returnTime) {
				OnFlagReturned(m_Flags[i].entity.Get());
			}
		}
	}
    // Update VIP alive status if VIP is known
    if (m_vipPlayer.IsValid() && m_isVIPAlive && !m_vipPlayer->IsAlive()) {
        // Warning("FFGameState::Update: Detected VIP (%s) died without OnVIPKilled event.\n", m_vipPlayer->GetPlayerName());
        // OnVIPKilled(m_vipPlayer.Get(), NULL); // Pass NULL as killer if unknown
		m_isVIPAlive = false; // Directly update here
    }
}

//--------------------------------------------------------------------------------------------------------------
void FFGameState::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; }
void FFGameState::OnRoundStart( IGameEvent *event ) { Reset(); }
bool FFGameState::IsRoundOver( void ) const { return m_isRoundOver; }

//--------------------------------------------------------------------------------------------------------------
int FFGameState::GetFlagTeamFromEntity(CBaseEntity* pFlagEntity) const {
	if (!pFlagEntity) return TEAM_ID_NONE;
	for (int i = 0; i < MAX_PLAYABLE_TEAMS_FF; ++i) {
		if (m_Flags[i].entity.IsValid() && m_Flags[i].entity.Get() == pFlagEntity) return i;
	}
	return TEAM_ID_NONE;
}
int FFGameState::GetCPIDFromEntity(CBaseEntity* pCPEntity) const {
    if (!pCPEntity) return -1;
    for (int i = 0; i < m_numControlPoints; ++i) {
        if (m_ControlPoints[i].entity.IsValid() && m_ControlPoints[i].entity.Get() == pCPEntity) return m_ControlPoints[i].pointID;
    }
    return -1;
}

// --- CTF Event Handlers ---
void FFGameState::OnFlagPickedUp(CBaseEntity* pFlagEntity, CFFPlayer* pPlayer) {
	if (!pFlagEntity || !pPlayer) return;
	int flagTeam = GetFlagTeamFromEntity(pFlagEntity);
	if (flagTeam != TEAM_ID_NONE) {
		m_Flags[flagTeam].currentState = FF_FLAG_STATE_CARRIED;
		m_Flags[flagTeam].carrier = pPlayer;
		m_Flags[flagTeam].returnTime = -1.0f;
	}
}
void FFGameState::OnFlagDropped(CBaseEntity* pFlagEntity, const Vector& dropLocation) {
	if (!pFlagEntity) return;
	int flagTeam = GetFlagTeamFromEntity(pFlagEntity);
	if (flagTeam != TEAM_ID_NONE) {
		m_Flags[flagTeam].currentState = FF_FLAG_STATE_DROPPED;
		m_Flags[flagTeam].carrier = NULL;
		m_Flags[flagTeam].dropLocation = dropLocation;
		m_Flags[flagTeam].returnTime = gpGlobals->curtime + FLAG_RETURN_TIME;
	}
}
void FFGameState::OnFlagCaptured(CBaseEntity* pFlagEntity, CFFPlayer* pCapturer) {
	if (!pFlagEntity) return;
	int capturedFlagTeam = GetFlagTeamFromEntity(pFlagEntity);
	if (capturedFlagTeam != TEAM_ID_NONE) {
		m_Flags[capturedFlagTeam].currentState = FF_FLAG_STATE_HOME;
		m_Flags[capturedFlagTeam].carrier = NULL;
		m_Flags[capturedFlagTeam].dropLocation = m_Flags[capturedFlagTeam].entitySpawnLocation;
		m_Flags[capturedFlagTeam].returnTime = -1.0f;
	}
}
void FFGameState::OnFlagReturned(CBaseEntity* pFlagEntity) {
	if (!pFlagEntity) return;
	int flagTeam = GetFlagTeamFromEntity(pFlagEntity);
	if (flagTeam != TEAM_ID_NONE) {
		m_Flags[flagTeam].currentState = FF_FLAG_STATE_HOME;
		m_Flags[flagTeam].carrier = NULL;
		m_Flags[flagTeam].dropLocation = m_Flags[flagTeam].entitySpawnLocation;
		m_Flags[flagTeam].returnTime = -1.0f;
	}
}

// --- CTF Query Methods ---
const FFGameState::FF_FlagState* FFGameState::GetFlagInfo(int team) const {
	if (team >= 0 && team < MAX_PLAYABLE_TEAMS_FF) return &m_Flags[team];
	return NULL;
}
bool FFGameState::IsTeamFlagHome(int team) const {
	const FF_FlagState* pFlagState = GetFlagInfo(team);
	return pFlagState && pFlagState->currentState == FF_FLAG_STATE_HOME;
}
bool FFGameState::IsTeamFlagCarried(int team, CFFPlayer** pCarrier) const {
	const FF_FlagState* pFlagState = GetFlagInfo(team);
	if (pFlagState && pFlagState->currentState == FF_FLAG_STATE_CARRIED) {
		if (pCarrier) *pCarrier = pFlagState->carrier.Get();
		return true;
	}
	if (pCarrier) *pCarrier = NULL;
	return false;
}
bool FFGameState::IsTeamFlagDropped(int team, Vector* pDropLocation) const {
	const FF_FlagState* pFlagState = GetFlagInfo(team);
	if (pFlagState && pFlagState->currentState == FF_FLAG_STATE_DROPPED) {
		if (pDropLocation) *pDropLocation = pFlagState->dropLocation;
		return true;
	}
	if (pDropLocation) pDropLocation->Init();
	return false;
}
bool FFGameState::IsOtherTeamFlagAtOurBase(int myTeamID) const {
    if (myTeamID < 0 || myTeamID >= MAX_PLAYABLE_TEAMS_FF) return false;
    int otherTeamID = (myTeamID == TEAM_ID_RED) ? TEAM_ID_BLUE : (myTeamID == TEAM_ID_BLUE) ? TEAM_ID_RED : TEAM_ID_NONE;
    if (otherTeamID == TEAM_ID_NONE) return false;
    const FF_FlagState* enemyFlagState = GetFlagInfo(otherTeamID);
    const FF_FlagState* myFlagState = GetFlagInfo(myTeamID);
    if (enemyFlagState && myFlagState && myFlagState->entity.IsValid()) {
        Vector flagLocationToCheck = vec3_invalid;
        if (enemyFlagState->currentState == FF_FLAG_STATE_DROPPED) flagLocationToCheck = enemyFlagState->dropLocation;
        else if (enemyFlagState->currentState == FF_FLAG_STATE_CARRIED && enemyFlagState->carrier.IsValid()) flagLocationToCheck = enemyFlagState->carrier->GetAbsOrigin();
        if (flagLocationToCheck != vec3_invalid) {
            const float captureRadiusSq = Square(150.0f);
            return (flagLocationToCheck - myFlagState->entitySpawnLocation).LengthSqr() < captureRadiusSq;
        }
    }
    return false;
}

// --- Control Point (CP) Event Handlers ---
void FFGameState::OnControlPointCaptured(CBaseEntity* pCPEntity, int newOwnerTeam) {
    int cpID = GetCPIDFromEntity(pCPEntity);
    if (cpID != -1) {
        m_ControlPoints[cpID].owningTeam = newOwnerTeam;
        for (int i = 0; i < MAX_TEAMS_FF; ++i) {
            m_ControlPoints[cpID].captureProgress[i] = (i == newOwnerTeam) ? 1.0f : 0.0f;
        }
    }
}
void FFGameState::OnControlPointProgress(CBaseEntity* pCPEntity, int teamMakingProgress, float newProgress) {
    int cpID = GetCPIDFromEntity(pCPEntity);
    if (cpID != -1 && teamMakingProgress >= 0 && teamMakingProgress < MAX_TEAMS_FF) {
        if (m_ControlPoints[cpID].isLocked) return;
        m_ControlPoints[cpID].captureProgress[teamMakingProgress] = clamp(newProgress, 0.0f, 1.0f);
    }
}
void FFGameState::OnControlPointBlocked(CBaseEntity* pCPEntity, bool isBlocked) {
    int cpID = GetCPIDFromEntity(pCPEntity);
    if (cpID != -1) m_ControlPoints[cpID].isLocked = isBlocked;
}

// --- Control Point (CP) Query Methods ---
const FFGameState::FF_ControlPointState* FFGameState::GetControlPointInfo(int cpID) const {
    if (cpID >= 0 && cpID < m_numControlPoints) return &m_ControlPoints[cpID];
    return NULL;
}
int FFGameState::GetControlPointOwner(int cpID) const {
    const FF_ControlPointState* pCPState = GetControlPointInfo(cpID);
    return pCPState ? pCPState->owningTeam : TEAM_ID_NONE;
}
float FFGameState::GetControlPointCaptureProgress(int cpID, int team) const {
    const FF_ControlPointState* pCPState = GetControlPointInfo(cpID);
    if (pCPState && team >= 0 && team < MAX_TEAMS_FF) return pCPState->captureProgress[team];
    return 0.0f;
}
bool FFGameState::IsControlPointLocked(int cpID) const {
    const FF_ControlPointState* pCPState = GetControlPointInfo(cpID);
    return pCPState ? pCPState->isLocked : true;
}

// --- VIP Escort Event Handlers ---
void FFGameState::OnVIPKilled(CFFPlayer* pVIPVictim, CBaseEntity* pKiller)
{
    if (pVIPVictim && m_vipPlayer.IsValid() && pVIPVictim == m_vipPlayer.Get())
    {
        m_isVIPAlive = false;
        // Warning("FFGameState: VIP '%s' was killed.\n", pVIPVictim->GetPlayerName());
    }
}

void FFGameState::OnVIPEscaped(CFFPlayer* pVIP)
{
    if (pVIP && m_vipPlayer.IsValid() && pVIP == m_vipPlayer.Get())
    {
        m_isVIPEscaped = true;
        m_isVIPAlive = false; // Typically, an escaped VIP is considered "out of play" and safe
        // Warning("FFGameState: VIP '%s' escaped.\n", pVIP->GetPlayerName());
    }
}

void FFGameState::OnPlayerSpawn(CFFPlayer* pPlayer)
{
    if (pPlayer && IsPlayerVIP(pPlayer))
    {
        // If a new VIP spawns (e.g. game mode allows VIP to change, or initial VIP spawns late)
        if (!m_vipPlayer.IsValid() || m_vipPlayer.Get() != pPlayer)
        {
            // Warning("FFGameState: Player '%s' spawned and identified as VIP.\n", pPlayer->GetPlayerName());
            m_vipPlayer = pPlayer;
        }
        m_isVIPAlive = true; // Reset alive status on spawn
        m_isVIPEscaped = false; // Reset escaped status on spawn
    }
    else if (pPlayer && m_vipPlayer.IsValid() && pPlayer == m_vipPlayer.Get())
    {
        // Existing VIP respawned
        // Warning("FFGameState: Known VIP '%s' respawned.\n", pPlayer->GetPlayerName());
        m_isVIPAlive = true;
        m_isVIPEscaped = false;
    }
}

// --- VIP Escort Query Methods ---
CFFPlayer* FFGameState::GetVIP() const
{
    return m_vipPlayer.Get();
}

bool FFGameState::IsVIPAlive() const
{
    // Ensure consistency if the VIP entity somehow became invalid but m_isVIPAlive wasn't updated
    if (!m_vipPlayer.IsValid() && m_isVIPAlive)
    {
        // const_cast<FFGameState*>(this)->m_isVIPAlive = false; // VIP pointer is gone, so not alive
    }
    return m_isVIPAlive;
}

bool FFGameState::IsVIPEscaped() const
{
    return m_isVIPEscaped;
}

[end of mp/src/game/server/ff/bot/ff_gamestate.cpp]
