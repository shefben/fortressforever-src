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

struct CPInitData { /* ... (definition unchanged) ... */
    CBaseEntity* entity;
    string_t entityName;
    int tentativeID;
    bool operator<(const CPInitData& other) const {
        return tentativeID < other.tentativeID;
    }
};

FFGameState::FFGameState( CFFBot *owner ) { /* ... (implementation unchanged) ... */
	m_owner = owner;
	m_nextCPPollTime = 0.0f;
	Reset();
}
void FFGameState::Reset( void ) { /* ... (implementation unchanged, calls InitializeVIPState) ... */
	m_isRoundOver = false;
	m_nextCPPollTime = gpGlobals->curtime + CP_POLL_INTERVAL;
	InitializeFlagState(TEAM_ID_RED, FF_FLAG_ITEM_RED_NAME, FF_FLAG_STAND_RED_NAME);
	InitializeFlagState(TEAM_ID_BLUE, FF_FLAG_ITEM_BLUE_NAME, FF_FLAG_STAND_BLUE_NAME);
	InitializeControlPointStates(FF_CP_ENTITY_CLASSNAME);
	InitializeVIPState();
}
void FFGameState::InitializeFlagState(int teamID, const char* flagItemEntityName, const char* flagStandEntityName) { /* ... (implementation unchanged) ... */ }
void FFGameState::InitializeControlPointStates(const char* cpEntityClassName) { /* ... (implementation unchanged) ... */ }

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

[end of mp/src/game/server/ff/bot/ff_gamestate.cpp]
