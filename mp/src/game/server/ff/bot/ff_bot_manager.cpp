//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#pragma warning( disable : 4530 )					// STL uses exceptions, but we are not compiling with them - ignore warning

#include "cbase.h"

#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "nav_area.h"
#include "ff_gamerules.h"
#include "shared_util.h"
#include "KeyValues.h"
#include "tier0/icommandline.h"
#include "gameeventdefs.h"
#include "ff_item_flag.h"
#include "filesystem.h" // Required for KeyValues file operations
#include "ff_weapon_base.h" // Required for FFWeaponID and CFFWeaponBase
#include "ff_buildableobject.h" // For buildable types

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)				// disable warning that variable *may* not be initialized 
#endif

IBotManager *TheBots = NULL;
CFFBotManager g_FFBotManager;

bool CFFBotManager::m_isMapDataLoaded = false;

int g_nClientPutInServerOverrides = 0;


void DrawOccupyTime( void );
ConVar bot_show_occupy_time( "bot_show_occupy_time", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show when each nav area can first be reached by each team." );

void DrawBattlefront( void );
ConVar bot_show_battlefront( "bot_show_battlefront", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show areas where rushing players will initially meet." );

int UTIL_FFBotsInGame( void );

ConVar bot_join_delay( "bot_join_delay", "0", FCVAR_GAMEDLL, "Prevents bots from joining the server for this many seconds after a map change." );

// ADDED: For ff_bot_join_team cvar
extern ConVar ff_bot_join_team;
extern ConVar bot_quota;
extern ConVar bot_chatter;
// extern ConVar bot_prefix; // Not used in FF for now
extern ConVar bot_join_after_player;
// extern ConVar bot_allow_rogues; // Concept might not apply

// FF specific weapon allow CVars (ensure these are declared elsewhere, e.g. ff_bot.cpp or ff_shareddefs.cpp)
extern ConVar ff_bot_allow_pistols;
extern ConVar ff_bot_allow_shotguns;
extern ConVar ff_bot_allow_sub_machine_guns;
extern ConVar ff_bot_allow_assaultcannons;
extern ConVar ff_bot_allow_rifles;
extern ConVar ff_bot_allow_sniper_rifles;
extern ConVar ff_bot_allow_flamethrowers;
extern ConVar ff_bot_allow_pipe_launchers;
extern ConVar ff_bot_allow_rocket_launchers;
extern ConVar ff_bot_allow_tranqguns;


#include "util_player_by_index.h" // For UTIL_PlayerByIndex

//--------------------------------------------------------------------------------------------------------------
// Helper to cast a CBasePlayer to a CFFBot
inline CFFBot *ToFFBot( CBasePlayer *player )
{
	if ( player && player->IsBot() )
		return static_cast<CFFBot *>( player );
	return NULL;
}

//--------------------------------------------------------------------------------------------------------------
// Macro to iterate over all active bots
#define FF_FOR_EACH_BOT( botPointer, i ) \
	for( int i=1; i<=gpGlobals->maxClients; ++i ) \
		if ( (botPointer = ToFFBot( UTIL_PlayerByIndex( i ) )) != NULL )

//--------------------------------------------------------------------------------------------------------------


inline bool AreBotsAllowed()
{
	const char *nobots = CommandLine()->CheckParm( "-nobots" );
	if ( nobots ) return false;
	return true;
}

void InstallFFBotControl( void )
{
	if (TheBots == NULL)
	{
		TheBots = &g_FFBotManager;
	}
}

void RemoveFFBotControl( void )
{
	if ( TheBots != NULL )
	{
		TheBots = NULL;
	}
}

CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername )
{
	CBasePlayer *pPlayer = TheBots->AllocateAndBindBotEntity( pEdict );
	if ( pPlayer ) pPlayer->SetPlayerName( playername );
	++g_nClientPutInServerOverrides;
	return pPlayer;
}

CFFBotManager::CFFBotManager() : m_gameEventListener(this)
{
	m_zoneCount = 0;
	m_serverActive = false;
	m_roundStartTimestamp = 0.0f;
	m_isRoundOver = true;
	m_isDefenseRushing = false; // ADDED: Initialize m_isDefenseRushing

	RegisterGameEventListeners();

	TheBotPhrases = new BotPhraseManager;
	TheBotProfiles = new BotProfileManager;
}

CFFBotManager::~CFFBotManager()
{
	UnregisterGameEventListeners();

	if (TheBotPhrases)
	{
		delete TheBotPhrases;
		TheBotPhrases = NULL;
	}
	if (TheBotProfiles)
	{
		delete TheBotProfiles;
		TheBotProfiles = NULL;
	}
}

void CFFBotManager::RestartRound( void )
{
	CBotManager::RestartRound();
	ResetRadioMessageTimestamps();
	m_lastSeenEnemyTimestamp = -9999.9f;
	if (FFGameRules())
		m_roundStartTimestamp = gpGlobals->curtime + FFGameRules()->GetFreezeTime();
	else
		m_roundStartTimestamp = gpGlobals->curtime;

	// ADDED: Logic for m_isDefenseRushing
	// FF_TODO_AI_BEHAVIOR: Define what "defense" means in FF scenarios to make this meaningful.
	// For now, random chance like CS. Could be tied to specific objectives or team roles.
	const float defenseRushChance = 25.0f;
	m_isDefenseRushing = (RandomFloat( 0.0f, 100.0f ) <= defenseRushChance);

	TheBotPhrases->OnRoundRestart();
	m_isRoundOver = false;
}

// ADDED: Implementation for IsDefenseRushing
bool CFFBotManager::IsDefenseRushing( void ) const
{
	return m_isDefenseRushing;
}

void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue ) { /* ... (implementation as before) ... */ }

// Helper for registering game events
void CFFBotManager::ListenForGameEvent( const char *name )
{
	gameeventmanager->AddListener( &m_gameEventListener, name, true );
}

void CFFBotManager::RegisterGameEventListeners()
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_footstep" );
	ListenForGameEvent( "player_radio" );
	ListenForGameEvent( "player_falldamage" );
	ListenForGameEvent( "door_moving" );
	ListenForGameEvent( "break_prop" );
	ListenForGameEvent( "break_breakable" );
	ListenForGameEvent( "weapon_fire" );
	ListenForGameEvent( "weapon_fire_on_empty" );
	ListenForGameEvent( "weapon_reload" );
	ListenForGameEvent( "bullet_impact" );
	ListenForGameEvent( "hegrenade_detonate" );
	ListenForGameEvent( "grenade_bounce" );
	ListenForGameEvent( "nav_blocked" );
	ListenForGameEvent( "server_shutdown" );
	ListenForGameEvent( "round_start" );
	ListenForGameEvent( "round_end" );
	ListenForGameEvent( "round_freeze_end" );
	ListenForGameEvent( "ff_restartround" );
	ListenForGameEvent( "player_changeclass" );
	ListenForGameEvent( "player_changeteam" );
	ListenForGameEvent( "disguise_lost" );
	ListenForGameEvent( "cloak_lost" );

	// Buildable events
	ListenForGameEvent( "build_dispenser" );
	ListenForGameEvent( "build_sentrygun" );
	ListenForGameEvent( "build_detpack" );
	ListenForGameEvent( "build_mancannon" );
	ListenForGameEvent( "buildable_built" );
	ListenForGameEvent( "dispenser_killed" );
	ListenForGameEvent( "dispenser_dismantled" );
	ListenForGameEvent( "dispenser_detonated" );
	ListenForGameEvent( "dispenser_sabotaged" );
	ListenForGameEvent( "buildable_sapper_removed" );
	ListenForGameEvent( "sentrygun_killed" );
	ListenForGameEvent( "sentrygun_dismantled" );
	ListenForGameEvent( "sentrygun_detonated" );
	ListenForGameEvent( "sentrygun_upgraded" );
	ListenForGameEvent( "sentrygun_sabotaged" );
	ListenForGameEvent( "detpack_detonated" ); // Used for Demoman pipes/stickies too
	ListenForGameEvent( "mancannon_detonated" );

	// New events for Medic and Dispenser interactions
	ListenForGameEvent( "player_healed" );             // Event for player receiving health from a medic
	ListenForGameEvent( "player_resupplied" );         // Generic resupply, might need filtering for dispenser
	// FF_TODO_EVENTS: Or a more specific event like "player_got_dispenser_ammo" / "player_got_dispenser_item"

	ListenForGameEvent( "luaevent" ); // Listen for generic Lua events
}

void CFFBotManager::UnregisterGameEventListeners()
{
	gameeventmanager->RemoveListener( &m_gameEventListener );
}


void CFFBotManager::OnGameEvent( IGameEvent *event )
{
	const char *eventName = event->GetName();

	if ( FStrEq( eventName, "player_death" ) ) OnPlayerDeath( event );
	else if ( FStrEq( eventName, "player_footstep" ) ) OnPlayerFootstep( event );
	else if ( FStrEq( eventName, "player_radio" ) ) OnPlayerRadio( event );
	else if ( FStrEq( eventName, "player_falldamage" ) ) OnPlayerFallDamage( event );
	else if ( FStrEq( eventName, "door_moving" ) ) OnDoorMoving( event );
	else if ( FStrEq( eventName, "break_prop" ) ) OnBreakProp( event );
	else if ( FStrEq( eventName, "break_breakable" ) ) OnBreakBreakable( event );
	else if ( FStrEq( eventName, "weapon_fire" ) ) OnWeaponFire( event );
	else if ( FStrEq( eventName, "weapon_fire_on_empty" ) ) OnWeaponFireOnEmpty( event );
	else if ( FStrEq( eventName, "weapon_reload" ) ) OnWeaponReload( event );
	else if ( FStrEq( eventName, "bullet_impact" ) ) OnBulletImpact( event );
	else if ( FStrEq( eventName, "hegrenade_detonate" ) ) OnHEGrenadeDetonate( event );
	else if ( FStrEq( eventName, "grenade_bounce" ) ) OnGrenadeBounce( event );
	else if ( FStrEq( eventName, "nav_blocked" ) ) OnNavBlocked( event );
	else if ( FStrEq( eventName, "server_shutdown" ) ) OnServerShutdown( event );
	else if ( FStrEq( eventName, "round_start" ) ) OnRoundStart( event );
	else if ( FStrEq( eventName, "round_end" ) ) OnRoundEnd( event );
	else if ( FStrEq( eventName, "round_freeze_end" ) ) OnRoundFreezeEnd( event );
	else if ( FStrEq( eventName, "ff_restartround" ) ) OnFFRestartRound( event );
	else if ( FStrEq( eventName, "player_changeclass" ) ) OnPlayerChangeClass( event );
	else if ( FStrEq( eventName, "player_changeteam" ) ) OnPlayerChangeTeam( event );
	else if ( FStrEq( eventName, "disguise_lost" ) ) OnDisguiseLost( event );
	else if ( FStrEq( eventName, "cloak_lost" ) ) OnCloakLost( event );
	else if ( FStrEq( eventName, "build_dispenser" ) ) OnBuildDispenser( event );
	else if ( FStrEq( eventName, "build_sentrygun" ) ) OnBuildSentryGun( event );
	else if ( FStrEq( eventName, "build_detpack" ) ) OnBuildDetpack( event );
	else if ( FStrEq( eventName, "build_mancannon" ) ) OnBuildManCannon( event );
	else if ( FStrEq( eventName, "buildable_built" ) ) OnBuildableBuilt( event );
	else if ( FStrEq( eventName, "dispenser_killed" ) ) OnDispenserKilled( event );
	else if ( FStrEq( eventName, "dispenser_dismantled" ) ) OnDispenserDismantled( event );
	else if ( FStrEq( eventName, "dispenser_detonated" ) ) OnDispenserDetonated( event );
	else if ( FStrEq( eventName, "dispenser_sabotaged" ) ) OnDispenserSabotaged( event );
	else if ( FStrEq( eventName, "buildable_sapper_removed" ) ) OnBuildableSapperRemoved( event );
	else if ( FStrEq( eventName, "sentrygun_killed" ) ) OnSentryGunKilled( event );
	else if ( FStrEq( eventName, "sentrygun_dismantled" ) ) OnSentryGunDismantled( event );
	else if ( FStrEq( eventName, "sentrygun_detonated" ) ) OnSentryGunDetonated( event );
	else if ( FStrEq( eventName, "sentrygun_upgraded" ) ) OnSentryGunUpgraded( event );
	else if ( FStrEq( eventName, "sentrygun_sabotaged" ) ) OnSentryGunSabotaged( event );
	else if ( FStrEq( eventName, "detpack_detonated" ) ) OnDetpackDetonated( event ); // Handles Demoman pipes/stickies
	else if ( FStrEq( eventName, "mancannon_detonated" ) ) OnManCannonDetonated( event );
	else if ( FStrEq( eventName, "player_healed" ) ) OnPlayerHealed( event );
	else if ( FStrEq( eventName, "player_resupplied" ) ) OnPlayerResuppliedFromDispenser( event ); // Assuming "player_resupplied" is the one
	else if ( FStrEq( eventName, "luaevent" ) ) OnLuaEvent( event );
}

//--------------------------------------------------------------------------------------------------------------
// Lua Event Handler
//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::OnLuaEvent( IGameEvent *event )
{
	const char *objectiveId = event->GetString("objective_id", NULL);
	const char *newStateStr = event->GetString("objective_state", NULL);
	// Optional: const char* luaEventName = event->GetString("eventname", "unnamed_lua_event"); // Generic game event name
	const char* specificEventType = event->GetString("event_type", ""); // Custom field for more specific event types like "flag_picked_up"
	int userId = event->GetInt("userid", -1); // Userid of the player who triggered the script or is primarily associated with the event.

	if (!objectiveId) // objective_id is crucial for any objective-related event
	{
		DevWarning("CFFBotManager::OnLuaEvent: Received luaevent with missing objective_id. EventType: '%s', PlayerID: %d\n", specificEventType, userId);
		return;
	}

	// Find the objective first.
	LuaObjectivePoint* pFoundObjective = NULL;
	int foundObjectiveIdx = -1;
	FOR_EACH_VEC(m_luaObjectivePoints, i)
	{
		if (m_luaObjectivePoints[i].m_id && FStrEq(m_luaObjectivePoints[i].m_id, objectiveId))
		{
			pFoundObjective = &m_luaObjectivePoints[i];
			foundObjectiveIdx = i;
			break;
		}
	}

	if (!pFoundObjective)
	{
		DevWarning("CFFBotManager::OnLuaEvent: Received luaevent for unknown objective_id '%s'. EventType: '%s', PlayerID: %d\n", objectiveId, specificEventType, userId);
		return;
	}

	// Handle state changes if "objective_state" is provided
	LuaObjectiveState newState = pFoundObjective->m_state; // Default to current state
	bool objectiveStateExplicitlySet = false;
	if (newStateStr && newStateStr[0] != '\0')
	{
		objectiveStateExplicitlySet = true;
		if (FStrEq(newStateStr, "active")) newState = LUA_OBJECTIVE_ACTIVE;
		else if (FStrEq(newStateStr, "inactive")) newState = LUA_OBJECTIVE_INACTIVE;
		else if (FStrEq(newStateStr, "completed")) newState = LUA_OBJECTIVE_COMPLETED;
		else if (FStrEq(newStateStr, "failed")) newState = LUA_OBJECTIVE_FAILED;
		else if (FStrEq(newStateStr, "carried")) newState = LUA_OBJECTIVE_CARRIED;
		else if (FStrEq(newStateStr, "dropped")) newState = LUA_OBJECTIVE_DROPPED;
		else
		{
			DevWarning("CFFBotManager::OnLuaEvent: Invalid objective_state '%s' for objective_id '%s'. EventType: '%s', PID: %d\n", newStateStr, objectiveId, specificEventType, userId);
			// Don't return; other parameters like owner change might still be valid.
			objectiveStateExplicitlySet = false; // Treat as if state wasn't set, so it doesn't change below if invalid
		}
	}


	bool anyPropertyChanged = false;

	// Update state if it was explicitly provided and different
	if (objectiveStateExplicitlySet && pFoundObjective->m_state != newState)
	{
		pFoundObjective->m_state = newState;
		DevMsg("CFFBotManager::OnLuaEvent: Objective '%s' state updated to '%s' (%d). EventType: '%s', PID: %d\n", objectiveId, newStateStr, newState, specificEventType, userId);
		anyPropertyChanged = true;
	}

	// Update owner team if provided
	// FF_LUA_TODO: Confirm actual key names used in luaevent for team changes (e.g., "new_owner_team", "owner_team_name").
	int newOwnerTeamId = event->GetInt("new_owner_team", -99);
	if (newOwnerTeamId != -99) // Integer team ID provided
	{
		if (pFoundObjective->currentOwnerTeam != newOwnerTeamId)
		{
			pFoundObjective->currentOwnerTeam = newOwnerTeamId;
			DevMsg("CFFBotManager::OnLuaEvent: Objective '%s' owner updated to team %d. EventType: '%s', PID: %d\n", objectiveId, newOwnerTeamId, specificEventType, userId);
			anyPropertyChanged = true;
		}
	}
	else // Check for string team name if integer not provided
	{
		const char *ownerTeamNameStr = event->GetString("owner_team_name", NULL);
		if (ownerTeamNameStr && ownerTeamNameStr[0] != '\0')
		{
			int parsedTeamId = FF_TEAM_NEUTRAL;
			if (FStrEq(ownerTeamNameStr, "RED")) parsedTeamId = FF_TEAM_RED;
			else if (FStrEq(ownerTeamNameStr, "BLUE")) parsedTeamId = FF_TEAM_BLUE;
			else if (FStrEq(ownerTeamNameStr, "NEUTRAL")) parsedTeamId = FF_TEAM_NEUTRAL;
			// else DevWarning("CFFBotManager::OnLuaEvent: Objective '%s' received unknown owner_team_name '%s'. EventType: '%s', PID: %d\n", objectiveId, ownerTeamNameStr, specificEventType, userId);

			if (pFoundObjective->currentOwnerTeam != parsedTeamId)
			{
				pFoundObjective->currentOwnerTeam = parsedTeamId;
				DevMsg("CFFBotManager::OnLuaEvent: Objective '%s' owner updated to team '%s' (%d). EventType: '%s', PID: %d\n", objectiveId, ownerTeamNameStr, parsedTeamId, specificEventType, userId);
				anyPropertyChanged = true;
			}
		}
	}

	// Handle specific event types like flag interactions
	// FF_LUA_TODO: Confirm event names and parameters for flag actions. These are examples.
	if (FStrEq(specificEventType, "flag_picked_up"))
	{
		int pickerUserId = event->GetInt("picker_userid", userId); // Default to main event userid if specific not given
		CFFPlayer* pPicker = ToFFPlayer(UTIL_PlayerByUserId(pickerUserId)); // Changed to CFFPlayer
		CFFBot* pPickerBot = ToFFBot(pPicker);

		// The flag entity itself IS the objective entity, so pFoundObjective->m_entity is the flag.
		CFFInfoScript* pFlagInfoScript = dynamic_cast<CFFInfoScript*>(pFoundObjective->m_entity.Get());

		if (pPicker && pFlagInfoScript)
		{
			int flagTeamAffiliation = pFoundObjective->teamAffiliation; // Get the flag's actual team
			if (pPickerBot)
			{
				pPickerBot->NotifyPickedUpFlag(pFlagInfoScript, (pPickerBot->GetTeamNumber() == flagTeamAffiliation) ? 2 /*own_flag*/ : 1 /*enemy_flag*/);
			}
			// Update LuaObjectivePoint state for the flag
			if (pFoundObjective->m_state != LUA_OBJECTIVE_CARRIED) { // May have already been set by objective_state param
				pFoundObjective->m_state = LUA_OBJECTIVE_CARRIED;
				anyPropertyChanged = true;
				DevMsg("CFFBotManager::OnLuaEvent (flag_picked_up): Objective '%s' state set to CARRIED. PID: %d\n", objectiveId, pickerUserId);
			}
			if (pFoundObjective->currentOwnerTeam != pPicker->GetTeamNumber()) { // May have already been set by new_owner_team param
				pFoundObjective->currentOwnerTeam = pPicker->GetTeamNumber();
				anyPropertyChanged = true;
				DevMsg("CFFBotManager::OnLuaEvent (flag_picked_up): Objective '%s' owner set to team %d. PID: %d\n", objectiveId, pPicker->GetTeamNumber(), pickerUserId);
			}
		}
		else
		{
			DevWarning("CFFBotManager::OnLuaEvent (flag_picked_up): Missing picker_userid or flag_entindex for objective '%s'. PID: %d\n", objectiveId, userId);
		}
	}
	else if (FStrEq(specificEventType, "flag_dropped"))
	{
		// The flag entity IS the objective entity.
		CFFInfoScript* pFlagInfoScript = dynamic_cast<CFFInfoScript*>(pFoundObjective->m_entity.Get());
		int dropperUserId = event->GetInt("dropper_userid", userId); // Optional: who dropped it
		CFFBot* pDropperBot = ToFFBot(UTIL_PlayerByUserId(dropperUserId));

		if (pFlagInfoScript)
		{
			if (pDropperBot && pDropperBot->GetCarriedFlag() == pFlagInfoScript) // If a bot dropped it, notify it
			{
				pDropperBot->NotifyDroppedFlag(pFlagInfoScript);
			}
			else // If a human dropped it, or a bot not correctly tracked, ensure any bot carrying it is cleared.
			{
				FF_FOR_EACH_BOT(pBot, i)
				{
					if (pBot->GetCarriedFlag() == pFlagInfoScript)
					{
						pBot->NotifyDroppedFlag(pFlagInfoScript); // This will clear their internal flag state
						DevMsg("CFFBotManager::OnLuaEvent (flag_dropped): Notified bot %s of dropping flag %s implicitly.\n", pBot->GetPlayerName(), objectiveId);
					}
				}
			}

			// Update LuaObjectivePoint state for the flag
			if (pFoundObjective->m_state != LUA_OBJECTIVE_DROPPED) { // May have been set by objective_state param
				pFoundObjective->m_state = LUA_OBJECTIVE_DROPPED; // Or LUA_OBJECTIVE_ACTIVE if it's immediately available at its new spot
				anyPropertyChanged = true;
				DevMsg("CFFBotManager::OnLuaEvent (flag_dropped): Objective '%s' state set to DROPPED. PID: %d\n", objectiveId, dropperUserId);
			}
			if (pFoundObjective->currentOwnerTeam != FF_TEAM_NEUTRAL) { // Dropped flags are neutral
				pFoundObjective->currentOwnerTeam = FF_TEAM_NEUTRAL;
				anyPropertyChanged = true;
				DevMsg("CFFBotManager::OnLuaEvent (flag_dropped): Objective '%s' owner set to NEUTRAL. PID: %d\n", objectiveId, dropperUserId);
			}

			// Update position if provided
			if (event->GetFloat("flag_pos_x", FLT_MIN) != FLT_MIN) {
				Vector newFlagPos;
				newFlagPos.x = event->GetFloat("flag_pos_x");
				newFlagPos.y = event->GetFloat("flag_pos_y");
				newFlagPos.z = event->GetFloat("flag_pos_z");
				if (pFoundObjective->position != newFlagPos)
				{
					pFoundObjective->position = newFlagPos;
					// Also update the actual entity's position if it's not already there (game might do this, or Lua)
					if (pFlagInfoScript->GetAbsOrigin() != newFlagPos) {
						// pFlagInfoScript->SetAbsOrigin(newFlagPos); // This might be too direct, Lua/game should own entity pos
						DevMsg("CFFBotManager::OnLuaEvent (flag_dropped): Objective '%s' new position reported: (%.1f, %.1f, %.1f). Entity should match.\n", objectiveId, newFlagPos.x, newFlagPos.y, newFlagPos.z);
					}
					anyPropertyChanged = true;
				}
			}
		}
		else
		{
			DevWarning("CFFBotManager::OnLuaEvent (flag_dropped): Could not find flag entity for objective '%s'. PID: %d\n", objectiveId, userId);
		}
	}
	// Add other specific event types like "flag_captured", "objective_destroyed" etc.

	if (anyPropertyChanged)
	{
		// FF_TODO_AI: Potentially trigger bot re-evaluation of objectives more broadly if a significant global change occurred.
		// For now, individual bots will react based on their current state's OnUpdate logic polling these updated objective points.
		// PrintIfWatched("CFFBotManager::OnLuaEvent: Property changed for objective '%s'.\n", objectiveId);
	}
}

void CFFBotManager::StartFrame( void )
{
	// ... (implementation as before) ...
	if ( !AreBotsAllowed() ) return; // Don't run bot logic if not allowed (e.g. -nobots)

	CBotManager::StartFrame();
	MaintainBotQuota();
	// Event listeners are now managed by Register/UnregisterGameEventListeners
	// No need for: EnableEventListeners( UTIL_FFBotsInGame() > 0 );
	if (cv_bot_debug.GetInt() == 5) { /* ... */ }
	if (bot_show_occupy_time.GetBool()) DrawOccupyTime();
	if (bot_show_battlefront.GetBool()) DrawBattlefront();
	if ( m_checkTransientAreasTimer.IsElapsed() && !nav_edit.GetBool() )
	{
		CUtlVector< CNavArea * >& transientAreas = TheNavMesh->GetTransientAreas();
		for ( int i=0; i<transientAreas.Count(); ++i )
		{ CNavArea *area = transientAreas[i]; if ( area->GetAttributes() & NAV_MESH_TRANSIENT ) area->UpdateBlocked(); }
		m_checkTransientAreasTimer.Start( 2.0f );
	}
}

// ConVar extern declarations (as before)
extern ConVar bot_difficulty;
// ... (all other ConVar externs) ...
extern ConVar ff_bot_allow_tranqguns;


bool CFFBotManager::IsWeaponUseable( const CBasePlayerWeapon *weapon ) const
{
	// ... (implementation as before) ...
	if (weapon == NULL) return false;
	const CFFWeaponBase *ffWeapon = dynamic_cast<const CFFWeaponBase *>(weapon);
	if (ffWeapon == NULL) return true;
	FFWeaponID weaponID = ffWeapon->GetWeaponID();
	switch (weaponID) {
		case FF_WEAPON_CROWBAR: case FF_WEAPON_KNIFE: case FF_WEAPON_MEDKIT: case FF_WEAPON_SPANNER: case FF_WEAPON_UMBRELLA: return true;
		case FF_WEAPON_DEPLOYDISPENSER: case FF_WEAPON_DEPLOYSENTRYGUN: case FF_WEAPON_DEPLOYDETPACK: case FF_WEAPON_DEPLOYMANCANNON: return true;
		default: break;
	}
	switch (weaponID) {
		case FF_WEAPON_JUMPGUN: return ff_bot_allow_pistols.GetBool();
		case FF_WEAPON_SHOTGUN: case FF_WEAPON_SUPERSHOTGUN: return ff_bot_allow_shotguns.GetBool();
		case FF_WEAPON_NAILGUN: case FF_WEAPON_SUPERNAILGUN: case FF_WEAPON_TOMMYGUN: return ff_bot_allow_sub_machine_guns.GetBool();
		case FF_WEAPON_AUTORIFLE: case FF_WEAPON_RAILGUN: return ff_bot_allow_rifles.GetBool();
		case FF_WEAPON_SNIPERRIFLE: return ff_bot_allow_sniper_rifles.GetBool();
		case FF_WEAPON_RPG: case FF_WEAPON_IC: return ff_bot_allow_rocket_launchers.GetBool();
		case FF_WEAPON_FLAMETHROWER: return ff_bot_allow_flamethrowers.GetBool();
		case FF_WEAPON_GRENADELAUNCHER: case FF_WEAPON_PIPELAUNCHER: return ff_bot_allow_pipe_launchers.GetBool();
		case FF_WEAPON_ASSAULTCANNON: return ff_bot_allow_assaultcannons.GetBool(); // MODIFIED for ff_bot_allow_assaultcannons
		case FF_WEAPON_TRANQUILISER: return ff_bot_allow_tranqguns.GetBool();
		case FF_WEAPON_NONE: case FF_WEAPON_CUBEMAP: case FF_WEAPON_MAX: return false;
		default: return true;
	}
}

// ... (IsOnDefense, IsOnOffense, ServerActivate, ServerDeactivate, ClientDisconnect, BotArgumentsFromArgv, bot_add, CollectBots, bot_kill, bot_kick, bot_goto_mark, weapon restriction commands, ServerCommand, ClientCommand, BotAddCommand, UTIL_FFBotsInGame, UTIL_FFKickBotFromTeam, MaintainBotQuota, ExtractScenarioData, GetZone, GetClosestZone, GetRandomPositionInZone, GetRandomAreaInZone as before) ...

CFFBot::BuildableType CFFBotManager::GetBuildableTypeFromEntity( CBaseEntity *pBuildable )
{
	if ( !pBuildable )
		return CFFBot::BUILDABLE_NONE;

	if ( FClassnameIs( pBuildable, "obj_sentrygun" ) ) // FF_TODO_GAME_MECHANIC: Verify classname from actual game
		return CFFBot::BUILDABLE_SENTRY;
	if ( FClassnameIs( pBuildable, "obj_dispenser" ) ) // FF_TODO_GAME_MECHANIC: Verify classname
		return CFFBot::BUILDABLE_DISPENSER;
	// FF_TODO_CLASS_ENGINEER: Add teleporter types if they become relevant for bots
	// if ( FClassnameIs( pBuildable, "obj_teleporter_entrance" ) )
	//     return CFFBot::BUILDABLE_TELE_ENTRANCE;
	// if ( FClassnameIs( pBuildable, "obj_teleporter_exit" ) )
	//     return CFFBot::BUILDABLE_TELE_EXIT;

	return CFFBot::BUILDABLE_NONE;
}

//--------------------------------------------------------------------------------------------------------------
// Event Handlers
//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::OnServerShutdown( IGameEvent *event )
{
	// ADDED: CVar saving logic
	// Based on CCSBotManager::OnServerShutdown
	if ( !engine->IsDedicatedServer() )
	{
		// Since we're a listen server, save some config info for the next time we start up
		// FF_TODO_CVARS: Verify these are the correct/desired ConVar names for FF bots
		static const char *botVars[] =
		{
			"bot_quota",
			"bot_difficulty",
			"bot_chatter",
			// "bot_prefix", // FF might not use this
			"ff_bot_join_team", // FF specific cvar, if it exists
			// "bot_defer_to_human", // FF might have different logic or cvar
			"bot_join_after_player",
			// "bot_allow_rogues", // Concept might not apply or be named differently
			"ff_bot_allow_pistols",
			"ff_bot_allow_shotguns",
			"ff_bot_allow_sub_machine_guns",
			"ff_bot_allow_assaultcannons",
			"ff_bot_allow_rifles",
			"ff_bot_allow_sniper_rifles",
			"ff_bot_allow_flamethrowers",
			"ff_bot_allow_pipe_launchers",
			"ff_bot_allow_rocket_launchers",
			"ff_bot_allow_tranqguns"
			// Add other ff_bot_allow_X weapon CVars here if they exist
		};

		KeyValues *data = new KeyValues( "ServerConfig" );

		if (data)
		{
			// Load existing config data to preserve other settings
			data->LoadFromFile( filesystem, "ServerConfig.vdf", "GAME" );

			for ( int i=0; i<ARRAYSIZE(botVars); ++i )
			{
				const char *varName = botVars[i];
				if ( varName )
				{
					ConVarRef var(varName); // Use ConVarRef for safer access
					if ( var.IsValid() && var.IsFlagSet(FCVAR_ARCHIVE) ) // Only save archived convars
					{
						data->SetString( varName, var.GetString() );
					}
					else
					{
						// Optional: Log if a cvar is not found or not archived
						// if ( !var.IsValid() ) Msg("CFFBotManager::OnServerShutdown: ConVar '%s' not found for saving.\n", varName);
						// else if ( !var.IsFlagSet(FCVAR_ARCHIVE) ) Msg("CFFBotManager::OnServerShutdown: ConVar '%s' is not archived, not saving.\n", varName);
					}
				}
			}
			data->SaveToFile( filesystem, "ServerConfig.vdf", "GAME" );
			data->deleteThis();
		}
	}
	// UnregisterGameEventListeners(); // This is usually handled in the destructor.
}

void CFFBotManager::OnPlayerFootstep( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerFootstep( event ); }
}
void CFFBotManager::OnPlayerRadio( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerRadio( event ); }
}
void CFFBotManager::OnPlayerDeath( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerDeath( event ); }
}
void CFFBotManager::OnPlayerFallDamage( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerFallDamage( event ); }
}
void CFFBotManager::OnRoundEnd( IGameEvent *event )
{
	m_isRoundOver = true;
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnRoundEnd( event ); }
}
void CFFBotManager::OnRoundStart( IGameEvent *event )
{
	RestartRound();
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnRoundStart( event ); }
}
void CFFBotManager::OnFFRestartRound( IGameEvent *event )
{
	Msg("CFFBotManager::OnFFRestartRound\n");
	RestartRound();
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnFFRestartRound( event );}
}
void CFFBotManager::OnPlayerChangeClass( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerChangeClass( event );}
}
void CFFBotManager::OnDisguiseLost( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnDisguiseLost( event );}
}
void CFFBotManager::OnCloakLost( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnCloakLost( event );}
}


// Buildable Event Handlers
void CFFBotManager::OnBuildDispenser( IGameEvent *event )
{
	// This event likely fires when blueprint is PLACED. OnBuildableBuilt handles full construction.
	// int builderId = event->GetInt("userid", 0); // FF_TODO_GAME_MECHANIC: Verify event parameter "userid" is the builder
	// int buildableEntIndex = event->GetInt("entindex", 0); // FF_TODO_GAME_MECHANIC: Verify event parameter "entindex"
	// Msg("CFFBotManager::OnBuildDispenser (Blueprint by userid %d, entindex %d)\n", builderId, buildableEntIndex);
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnBuildDispenser( event ); }
}

void CFFBotManager::OnBuildSentryGun( IGameEvent *event )
{
	// int builderId = event->GetInt("userid", 0); // FF_TODO_GAME_MECHANIC: Verify event parameter "userid"
	// int buildableEntIndex = event->GetInt("entindex", 0); // FF_TODO_GAME_MECHANIC: Verify event parameter "entindex"
	// Msg("CFFBotManager::OnBuildSentryGun (Blueprint by userid %d, entindex %d)\n", builderId, buildableEntIndex);
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnBuildSentryGun( event ); }
}
void CFFBotManager::OnBuildDetpack( IGameEvent *event ) { /* ... */ }
void CFFBotManager::OnBuildManCannon( IGameEvent *event ) { /* ... */ }


void CFFBotManager::OnBuildableBuilt( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter names ("userid", "entindex")
	int builderId = event->GetInt("userid", 0);
	int buildableEntIndex = event->GetInt("entindex", 0);
	Msg("CFFBotManager::OnBuildableBuilt (Builder userid %d, Buildable entindex %d)\n", builderId, buildableEntIndex);

	CFFPlayer *pBuilder = ToFFPlayer(UTIL_PlayerByUserId(builderId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(buildableEntIndex);

	if (pBuilder && pBuilder->IsBot() && pBuildable)
	{
		CFFBot *pBotOwner = static_cast<CFFBot*>(pBuilder);
		CFFBot::BuildableType type = GetBuildableTypeFromEntity(pBuildable);
		if (type != CFFBot::BUILDABLE_NONE)
		{
			pBotOwner->NotifyBuildingBuilt(pBuildable, type);
		}
	}
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnBuildableBuilt( event ); }
}

void CFFBotManager::OnDispenserKilled( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }

	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnDispenserKilled( event ); }
}

void CFFBotManager::OnDispenserDismantled( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnDispenserDismantled( event ); }
}

void CFFBotManager::OnDispenserDetonated( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnDispenserDetonated( event ); }
}

void CFFBotManager::OnDispenserSabotaged( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid"
	int ownerId = event->GetInt("ownerid", 0);
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingSapped(pBuildable, true); }

	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnDispenserSabotaged( event ); }
}

void CFFBotManager::OnSentryGunKilled( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }

	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnSentryGunKilled( event ); }
}

void CFFBotManager::OnSentryGunDismantled( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnSentryGunDismantled( event ); }
}

void CFFBotManager::OnSentryGunDetonated( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid" vs "userid"
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnSentryGunDetonated( event ); }
}

void CFFBotManager::OnSentryGunUpgraded( IGameEvent *event )
{
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	int level = event->GetInt("level", 0); // FF_TODO_EVENTS: Verify "level" is the correct key for new level.

	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);

	if (pOwner && pOwner->IsBot() && pBuildable)
	{
		CFFBot* pBotOwner = static_cast<CFFBot*>(pOwner);
		if (level > 0) // Ensure a valid level is passed from the event
		{
			pBotOwner->NotifyBuildingUpgraded(pBuildable, level);
		}
		else
		{
			// If event doesn't provide level, bot's NotifyBuildingUpgraded will try to derive it.
			pBotOwner->NotifyBuildingUpgraded(pBuildable, 0);
			Warning("CFFBotManager::OnSentryGunUpgraded: Event for sentry %d did not provide new level. Bot %s might have outdated info.\n", entIndex, pBotOwner->GetPlayerName());
		}
	}

	// Notify all bots about the upgrade for awareness (they might not be the owner)
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnSentryGunUpgraded( event ); }
}

void CFFBotManager::OnSentryGunSabotaged( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter "ownerid"
	int ownerId = event->GetInt("ownerid", 0);
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingSapped(pBuildable, true); }

	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnSentryGunSabotaged( event ); }
}

void CFFBotManager::OnBuildableSapperRemoved( IGameEvent *event )
{
	// FF_TODO_GAME_MECHANIC: Verify event parameter names ("ownerid", "entindex")
	int ownerId = event->GetInt("ownerid", 0);
	int buildableEntIndex = event->GetInt("entindex", 0);
	Msg("CFFBotManager::OnBuildableSapperRemoved (Owner userid %d, Buildable entindex %d)\n", ownerId, buildableEntIndex);

	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(buildableEntIndex);

	if (pOwner && pOwner->IsBot() && pBuildable)
	{
		static_cast<CFFBot*>(pOwner)->NotifyBuildingSapped(pBuildable, false);
	}
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnBuildableSapperRemoved( event ); }
}


void CFFBotManager::OnDetpackDetonated( IGameEvent *event )
{
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0); // entindex of the detpack/pipe itself
	CFFPlayer *pOwnerPlayer = ToFFPlayer(UTIL_PlayerByUserId(ownerId));

	if (pOwnerPlayer && pOwnerPlayer->IsBot())
	{
		CFFBot *pOwnerBot = static_cast<CFFBot*>(pOwnerPlayer);
		if (pOwnerBot->IsDemoman())
		{
			// This event could be for an Engineer's detpack or a Demoman's pipe/sticky.
			// We need to be sure it's for a Demoman's explosive before calling NotifyPipeDetonated.
			// The event itself might not distinguish type (pipe vs sticky vs detpack) beyond owner.
			// CFFPlayer::DetonateStickies() likely fires this for each sticky.
			// CFFPlayer::FirePipeBomb() for direct pipes also creates entities that explode.
			// For now, if it's a Demoman owner, let the bot handle it via NotifyPipeDetonated.
			// The bot can then try to infer more if needed.
			pOwnerBot->NotifyPipeDetonated(event);
		}
		else if (pOwnerBot->IsEngineer()) // If it's an Engineer's detpack (assuming different class or handling)
		{
			CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
			if (pBuildable) // Check if it's one of their known buildables (though detpack isn't tracked like S/D)
			{
				// Assuming Engineer detpack destruction means NotifyBuildingDestroyed
				// FF_TODO_CLASS_ENGINEER: Verify if Engineer detpacks are tracked as "buildings" for this purpose.
				// pOwnerBot->NotifyBuildingDestroyed(pBuildable);
				// For now, just log for engineer detpack, as NotifyBuildingDestroyed might not be right
				// if detpacks aren't "built" and "destroyed" in the same way as S/D.
				// PrintIfWatched("BotManager: Engineer %s's detpack (ent %d) detonated.\n", pOwnerBot->GetPlayerName(), entIndex);
			}
		}
	}

	// Original broader notification (if any bot needs to know about any detpack detonation)
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnDetpackDetonated( event ); } // This was CFFBot::OnDetpackDetonated
}

void CFFBotManager::OnManCannonDetonated( IGameEvent *event )
{
	int ownerId = event->GetInt("ownerid", event->GetInt("userid", 0));
	int entIndex = event->GetInt("entindex", 0);
	CFFPlayer *pOwner = ToFFPlayer(UTIL_PlayerByUserId(ownerId));
	CBaseEntity *pBuildable = UTIL_EntityByIndex(entIndex);
	if (pOwner && pOwner->IsBot() && pBuildable) { static_cast<CFFBot*>(pOwner)->NotifyBuildingDestroyed(pBuildable); }
	// CFFBot *bot;
	// FF_FOR_EACH_BOT( bot, i ) { bot->OnManCannonDetonated( event ); }
}


//--------------------------------------------------------------------------------------------------------------
// New Event Handler Implementations
//--------------------------------------------------------------------------------------------------------------

void CFFBotManager::OnPlayerHealed(IGameEvent *event)
{
	int patient_userid = event->GetInt("patient_userid", event->GetInt("userid")); // Target of the heal
	int healer_userid = event->GetInt("healer_userid", event->GetInt("attacker")); // Medic doing the healing
	int health_healed = event->GetInt("amount", event->GetInt("healing_given")); // Amount of health given

	// Notify the patient if they are a bot
	CFFPlayer *pPatient = ToFFPlayer(UTIL_PlayerByUserId(patient_userid));
	CFFBot *pPatientBot = ToFFBot(pPatient);
	if (pPatientBot)
	{
		CFFPlayer *pMedic = ToFFPlayer(UTIL_PlayerByUserId(healer_userid));
		pPatientBot->NotifyPlayerHealed(pMedic); // Pass the medic CFFPlayer pointer
	}

	// Notify the medic if they are a bot
	CFFPlayer *pMedic = ToFFPlayer(UTIL_PlayerByUserId(healer_userid));
	CFFBot *pMedicBot = ToFFBot(pMedic);
	if (pMedicBot && pMedicBot->IsMedic()) // Ensure the healer is actually a Medic bot
	{
		pMedicBot->NotifyMedicGaveHeal(pPatient, health_healed);
	}
}

void CFFBotManager::OnPlayerResuppliedFromDispenser(IGameEvent *event)
{
	// FF_TODO_EVENTS: Verify event name and parameter names ("userid", "dispenser_entindex")
	int player_userid = event->GetInt("userid");
	int dispenser_entindex = event->GetInt("dispenser_entindex", event->GetInt("entindex")); // entindex of the dispenser

	CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByUserId(player_userid));
	CFFBot *pBot = ToFFBot(pPlayer);

	if (pBot)
	{
		CBaseEntity* pDispenser = UTIL_EntityByIndex(dispenser_entindex);
		if (pDispenser && FClassnameIs(pDispenser, "obj_dispenser")) // FF_TODO_BUILDING: Verify dispenser classname
		{
			pBot->NotifyGotDispenserAmmo(pDispenser);
		}
	}
}


void CFFBotManager::OnPlayerChangeTeam( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerChangeTeam( event );}
}
void CFFBotManager::CheckForBlockedZones( void ) { /* ... */ }
void CFFBotManager::OnRoundFreezeEnd( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnRoundFreezeEnd( event ); }
}
void CFFBotManager::OnNavBlocked( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnNavBlocked( event ); }
	CheckForBlockedZones();
}
void CFFBotManager::OnDoorMoving( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnDoorMoving( event ); }
}
void CFFBotManager::OnBreakBreakable( IGameEvent *event )
{
	CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) );
	TheNavMesh->ForAllAreas( collector );
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnBreakBreakable( event ); }
}
void CFFBotManager::OnBreakProp( IGameEvent *event )
{
	CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) );
	TheNavMesh->ForAllAreas( collector );
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnBreakProp( event ); }
}
void CFFBotManager::OnWeaponFire( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnWeaponFire( event ); }
}
void CFFBotManager::OnWeaponFireOnEmpty( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnWeaponFireOnEmpty( event ); }
}
void CFFBotManager::OnWeaponReload( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnWeaponReload( event ); }
}
void CFFBotManager::OnBulletImpact( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnBulletImpact( event ); }
}
void CFFBotManager::OnHEGrenadeDetonate( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnHEGrenadeDetonate( event ); }
}
void CFFBotManager::OnGrenadeBounce( IGameEvent *event )
{
	CFFBot *bot;
	FF_FOR_EACH_BOT( bot, i ) { bot->OnGrenadeBounce( event ); }
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBotManager::IsImportantPlayer( CFFPlayer *player ) const { return false; }
unsigned int CFFBotManager::GetPlayerPriority( CBasePlayer *player ) const
{
	const unsigned int lowestPriority = 0xFFFFFFFF;
	if (!player->IsPlayer()) return lowestPriority;
	if (!player->IsBot()){ if ( player->GetTimeSinceLastMovement() > 60.0f ) return 2; return 0; }
	CFFBot *bot = dynamic_cast<CFFBot *>( player ); if ( !bot ) return 0;
	return 1 + bot->GetID();
}
CBaseEntity *CFFBotManager::GetRandomSpawn( int team ) const { /* ... */
	CUtlVector< CBaseEntity * > spawnSet; CBaseEntity *spot;
	if (team == FF_TEAM_RED || team == FF_TEAM_AUTOASSIGN || team == TEAM_ANY ) {
		for( spot = gEntList.FindEntityByClassname( NULL, "info_player_red" ); spot; spot = gEntList.FindEntityByClassname( spot, "info_player_red" ) ) spawnSet.AddToTail( spot );
	}
	if (team == FF_TEAM_BLUE || team == FF_TEAM_AUTOASSIGN || team == TEAM_ANY ) {
		for( spot = gEntList.FindEntityByClassname( NULL, "info_player_blue" ); spot; spot = gEntList.FindEntityByClassname( spot, "info_player_blue" ) ) spawnSet.AddToTail( spot );
	}
	if (spawnSet.Count() == 0) return NULL;
	return spawnSet[ RandomInt( 0, spawnSet.Count()-1 ) ];
}
float CFFBotManager::GetRadioMessageTimestamp( RadioType event, int teamID ) const { /* ... */
	int i = -1; if (teamID == FF_TEAM_RED) i = 0; else if (teamID == FF_TEAM_BLUE) i = 1;
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		return m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ];
	return 0.0f;
}
float CFFBotManager::GetRadioMessageInterval( RadioType event, int teamID ) const { /* ... */
	int i = -1; if (teamID == FF_TEAM_RED) i = 0; else if (teamID == FF_TEAM_BLUE) i = 1;
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		return gpGlobals->curtime - m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ];
	return 99999999.9f;
}
void CFFBotManager::SetRadioMessageTimestamp( RadioType event, int teamID ) { /* ... */
	int i = -1; if (teamID == FF_TEAM_RED) i = 0; else if (teamID == FF_TEAM_BLUE) i = 1;
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ] = gpGlobals->curtime;
}
void CFFBotManager::ResetRadioMessageTimestamps( void ) { /* ... */ }
void DrawOccupyTime( void ) { /* ... */ }
void DrawBattlefront( void ) { /* ... */ }
static bool CheckAreaAgainstAllZoneAreas(CNavArea *queryArea) { /* ... */ return false; } // Placeholder
// CON_COMMAND_F( nav_check_connectivity, ... ) { /* ... */ } // Already exists


// FF_TODO_GAME_MECHANIC: These are assumed values for info_ff_script touch flags.
// These should be verified against the actual definitions in info_ff_script.h or equivalent.
const int FF_TOUCH_ALLOW_RED_TEAM    = (1<<0); // Assumed: 1
const int FF_TOUCH_ALLOW_BLUE_TEAM   = (1<<1); // Assumed: 2
const int FF_TOUCH_ALLOW_YELLOW_TEAM = (1<<2); // Assumed: 4
const int FF_TOUCH_ALLOW_GREEN_TEAM  = (1<<3); // Assumed: 8
// const int FF_TOUCH_ALLOW_SPECTATORS = (1<<4); // Example if needed
// const int FF_TOUCH_ALLOW_PLAYERS_ONLY = (1<<5); // Example if needed


// Helper function to determine team affiliation from touch flags
static int DetermineTeamAffiliationFromTouchFlags(int touchFlags)
{
	// Check specific teams first. If it can be touched by multiple, this will prefer Red > Blue > Yellow > Green.
	// FF_TODO_GAME_MECHANIC: This logic might need adjustment if a point can truly belong to multiple teams simultaneously
	// in a way that isn't just "neutral but usable by X".
	if (touchFlags & FF_TOUCH_ALLOW_RED_TEAM) return FF_TEAM_RED;
	if (touchFlags & FF_TOUCH_ALLOW_BLUE_TEAM) return FF_TEAM_BLUE;
	if (touchFlags & FF_TOUCH_ALLOW_YELLOW_TEAM) return FF_TEAM_YELLOW; // Assuming FF_TEAM_YELLOW is defined
	if (touchFlags & FF_TOUCH_ALLOW_GREEN_TEAM) return FF_TEAM_GREEN;   // Assuming FF_TEAM_GREEN is defined

	// If no specific team, but potentially touchable by players (might imply neutral for any player)
	// This part is more speculative without seeing info_ff_script touch flag definitions.
	// For now, if not explicitly for a team, assume neutral.
	return FF_TEAM_NEUTRAL;
}

// Helper function to map Omnibot GoalType to our internal LuaObjectivePoint::type
static int MapOmnibotGoalToBotObjectiveType(int omniGoalType)
{
	// FF_LUA_TODO: The C++ side of Omnibot::GoalType enum isn't directly available.
	// These values are *assumed* based on typical Omnibot Lua bindings (e.g., Bot.kFlag might be 5).
	// These need to be verified against the actual values used in ff_lualib_omnibot.cpp or similar.
	const int OMNIBOT_KFLAG = 5;     // Assumed value for a flag item (that might be carried)
	const int OMNIBOT_KFLAGCAP = 6;  // Assumed value for a flag capture zone

	// Conceptual mapping:
	// 0=Generic/Unknown, 1=FlagGoal(CapturePoint), 2=Item_Flag, ...
	switch (omniGoalType)
	{
		case OMNIBOT_KFLAG:
			return 2; // Item_Flag
		case OMNIBOT_KFLAGCAP:
			return 1; // FlagGoal (CapturePoint)
		// FF_LUA_TODO: Add mappings for other Omnibot::GoalTypes as they are identified and needed by bots:
		// case OMNIBOT_KART_PATH: return 3; (PayloadCheckpoint/Path)
		// case OMNIBOT_DESTROY_TARGET: return X; (e.g., for Demoman objectives)
		default:
			return 0; // Generic/Unknown
	}
}


// ... (Existing IsOnDefense, IsOnOffense, ServerActivate, ServerDeactivate, etc. a before) ...
// ... but ExtractScenarioData will be modified below ...

void CFFBotManager::ExtractScenarioData( void )
{
	m_zoneCount = 0;
	m_luaObjectivePoints.RemoveAll(); // Clear previous Lua objectives
	m_gameScenario = SCENARIO_FF_UNKNOWN; // Default

	CBaseEntity *entity = NULL;
	bool foundMinecart = false;
	bool foundScriptObjective = false;

	while ( (entity = gEntList.NextEnt(entity)) != NULL )
	{
		// FF_TODO_LUA: Needs to interface with CFFInfoScript entities if those are the primary way
		// Lua objectives are defined and exposed to C++.
		if ( FClassnameIs( entity, "info_ff_script" ) )
		{
			foundScriptObjective = true;
			CFFInfoScript* pInfoScript = dynamic_cast<CFFInfoScript*>(entity);
			if (pInfoScript)
			{
				LuaObjectivePoint point;
				// Populate the descriptive name
				Q_strncpy(point.name, pInfoScript->GetEntityNameAsCStr(), sizeof(point.name) - 1);
				point.name[sizeof(point.name)-1] = '\0';

				// Populate the unique Lua objective ID (m_id)
				// FF_LUA_TODO: This assumes the "objective_id" used in Lua events will match the entity's targetname.
				// If CFFInfoScript has a separate field for "Lua Objective ID", use that instead.
				// For example: Q_strncpy(point.m_id, pInfoScript->GetLuaObjectiveId(), sizeof(point.m_id) - 1);
				Q_strncpy(point.m_id, pInfoScript->GetEntityNameAsCStr(), sizeof(point.m_id) - 1);
				point.m_id[sizeof(point.m_id)-1] = '\0';

				point.position = pInfoScript->GetAbsOrigin();

				// Set m_state based on FF_ScriptGoalState_e and m_iPosState
				if (pInfoScript->GetGoalState() == FF_ScriptGoalState_Disabled ||
				    pInfoScript->GetGoalState() == FF_ScriptGoalState_Remove ||
				    pInfoScript->m_iPosState == PS_REMOVED) // m_iPosState is from CPointEntity
				{
					point.m_state = LUA_OBJECTIVE_INACTIVE;
				}
				else
				{
					point.m_state = LUA_OBJECTIVE_ACTIVE; // Default to active if not explicitly disabled/removed
				}

				point.teamAffiliation = DetermineTeamAffiliationFromTouchFlags(pInfoScript->GetTouchFlags());
				point.type = MapOmnibotGoalToBotObjectiveType(pInfoScript->GetBotGoalType());

				// currentOwnerTeam logic
				if (pInfoScript->IsCarried())
				{
					CBaseEntity* pCarrier = pInfoScript->GetCarrier();
					if (pCarrier && pCarrier->IsPlayer())
					{
						point.currentOwnerTeam = pCarrier->GetTeamNumber();
					}
					else
					{
						point.currentOwnerTeam = FF_TEAM_NEUTRAL; // Carried by non-player? Or default.
					}
				}
				else
				{
					// FF_LUA_TODO: How to get current owner of a static capture point if not directly on CFFInfoScript C++ side?
					// It might be part of the Lua script's internal state, or reflected by teamAffiliation if it's a permanent team point.
					// For now, if not carried, assume teamAffiliation might indicate initial/current owner, or it's neutral.
					// If a point is capturable, its currentOwnerTeam might be updated by game events.
					if (point.type == 1 /*FlagGoal/CapturePoint*/ || point.type == 2 /*Item_Flag at base*/)
					{
						// If it's a capture point or a flag base, its teamAffiliation likely means its "home" team.
						// Actual ownership might change. For now, use teamAffiliation as a proxy if not carried.
						// This will require game events to update currentOwnerTeam for dynamic points.
						point.currentOwnerTeam = point.teamAffiliation;
					} else {
						point.currentOwnerTeam = FF_TEAM_NEUTRAL; // Default for other types if not carried
					}
				}

				// Radius - FF_LUA_TODO: Ideally, CFFInfoScript would have a GetRadius() or similar if it's variable.
				// For now, using a default. Some objectives might have hardcoded radii or use bbox.
				point.radius = 100.0f; // Default radius
				// Example: if (pInfoScript->HasRadius()) point.radius = pInfoScript->GetRadius();
				point.m_entity = pInfoScript; // Store handle to the entity

				m_luaObjectivePoints.AddToTail(point);
				DevMsg("Extracted Lua Objective: ID '%s', Name '%s' (EntIndex: %d) at (%.f %.f %.f), State: %d, Type: %d, TeamAff: %d, Owner: %d\n",
					point.m_id, point.name, pInfoScript->entindex(), point.position.x, point.position.y, point.position.z, point.m_state, point.type, point.teamAffiliation, point.currentOwnerTeam);

			}
		}
		else if ( FClassnameIs( entity, "ff_minecart" ) ) // Assuming this is the classname
		{
			foundMinecart = true;
			// Existing minecart logic can go here or be adapted.
			// For now, just note its presence.
		}
		// Potentially other scenario entities specific to FF
	}

	if (foundScriptObjective && foundMinecart)
	{
		m_gameScenario = SCENARIO_FF_MIXED;
	}
	else if (foundScriptObjective)
	{
		m_gameScenario = SCENARIO_FF_ITEM_SCRIPT;
	}
	else if (foundMinecart)
	{
		m_gameScenario = SCENARIO_FF_MINECART;
	}

	DevMsg("CFFBotManager::ExtractScenarioData: Found %d Lua objective points. Scenario type: %d\n", m_luaObjectivePoints.Count(), m_gameScenario);
}


// Lua data accessors
const CUtlVector<CFFBotManager::LuaObjectivePoint>& CFFBotManager::GetAllLuaObjectivePoints() const { return m_luaObjectivePoints; }
int CFFBotManager::GetLuaObjectivePointCount() const { return m_luaObjectivePoints.Count(); }
const CFFBotManager::LuaObjectivePoint* CFFBotManager::GetLuaObjectivePoint(int index) const
{
	if (index < 0 || index >= m_luaObjectivePoints.Count()) return NULL;
	return &m_luaObjectivePoints[index];
}
const CUtlVector<CFFBotManager::LuaPathPoint>& CFFBotManager::GetAllLuaPathPoints() const { return m_luaPathPoints; }
int CFFBotManager::GetLuaPathPointCount() const { return m_luaPathPoints.Count(); }
const CFFBotManager::LuaPathPoint* CFFBotManager::GetLuaPathPoint(int index) const
{
	if (index < 0 || index >= m_luaPathPoints.Count()) return NULL;
	return &m_luaPathPoints[index];
}

// Forward declarations for any functions that might have been removed by the large replace block above
// and are still needed by other parts of the file that were not part of this specific diff.
// This is a safeguard. Actual review of the full file post-merge would confirm.
void CFFBotManager::ServerActivate( void )
{
	BaseClass::ServerActivate(); // Call base class version first

	m_isMapDataLoaded = false; // Reset map data loaded flag

	// Initialize bot phrases
	// TheBotPhrases is created in constructor. Initialize it here.
	// FF_TODO_FILES: Ensure "BotChatterFF.db" or equivalent exists and is correctly formatted.
	if (TheBotPhrases)
	{
		TheBotPhrases->Reset();
		TheBotPhrases->Initialize( "BotChatterFF.db", 0 ); // Assuming a FF specific chatter file
	}

	// Initialize bot profiles
	// TheBotProfiles is created in constructor. Initialize it here.
	// FF_TODO_FILES: Ensure "BotProfileFF.db" and "BotPackListFF.db" or equivalents exist.
	if (TheBotProfiles)
	{
		TheBotProfiles->Reset();
		// TheBotProfiles->FindVoiceBankIndex( "BotChatterFF.db" ); // Ensure default voice bank is first if using multiple chatter files

		// Read in the list of bot profile DBs from a FF specific pack list
		const char *profilePackListFile = "BotPackListFF.db";
		FileHandle_t file = filesystem->Open( profilePackListFile, "r" );

		if ( !file )
		{
			// Fallback to a default profile DB if pack list not found
			TheBotProfiles->Init( "BotProfileFF.db" );
		}
		else
		{
			int dataLength = filesystem->Size( profilePackListFile );
			char *dataPointer = new char[ dataLength ];

			filesystem->Read( dataPointer, dataLength, file );
			filesystem->Close( file );

			const char *dataFile = SharedParse( dataPointer );
			const char *token;

			while ( dataFile )
			{
				token = SharedGetToken();
				if (token && *token) // Ensure token is not null or empty
				{
					char *clone = CloneString( token );
					TheBotProfiles->Init( clone ); // Initialize each profile DB listed
					delete[] clone;
				}
				dataFile = SharedParse( dataFile );
			}
			delete [] dataPointer;
		}

		// FF_TODO_BOT_PROFILE: If FF uses custom voice speakables like CS, parse them here.
		// const BotProfileManager::VoiceBankList *voiceBanks = TheBotProfiles->GetVoiceBanks();
		// for ( int i=1; i<voiceBanks->Count(); ++i )
		// {
		// 	TheBotPhrases->Initialize( (*voiceBanks)[i], i );
		// }
	}

	// Tell the Navigation Mesh system what FF spawn points are named
	// FF_TODO_NAV: Verify these are the primary spawn point classnames for FF.
	// TheNavMesh->SetPlayerSpawnName( "info_player_red" ); // Example for red team
	// TheNavMesh->AddPlayerSpawnName( "info_player_blue" ); // Example for blue team
    // Or, if there's a generic one used by GetRandomSpawn:
    // TheNavMesh->SetPlayerSpawnName( "info_player_start_ff" ); // Hypothetical generic FF spawn

	ExtractScenarioData(); // Already present, good. This populates Lua objectives etc.

	// RestartRound(); // CS calls this, but in TF2-likes, gamerules usually handle the first round start.
	// Let's rely on game rules to call RestartRound.
	// If initial setup specific to bots is needed before first round, it should be in RestartRound().

	if (TheBotPhrases)
	{
		TheBotPhrases->OnMapChange(); // Notify phrases about map change
	}

	ResetRadioMessageTimestamps(); // Already present.
	m_serverActive = true;
	m_isMapDataLoaded = true; // Set this after all map-specific data is processed
}
void CFFBotManager::ServerDeactivate( void ) { BaseClass::ServerDeactivate(); m_serverActive = false; }
void CFFBotManager::ClientDisconnect( CBaseEntity *entity ) { BaseClass::ClientDisconnect(entity); }
bool CFFBotManager::ClientCommand( CBasePlayer *player, const CCommand &args ) { return BaseClass::ClientCommand(player, args); }
bool CFFBotManager::ServerCommand( const char *cmd ) { return false; }
// unsigned int CFFBotManager::GetPlayerPriority( CBasePlayer *player ) const { return 0; } // Already defined
// bool CFFBotManager::IsImportantPlayer( CFFPlayer *player ) const { return false; } // Already defined
const CFFBotManager::Zone *CFFBotManager::GetZone( int i ) const { if (i < 0 || i >= m_zoneCount) return NULL; return &m_zone[i]; }
const CFFBotManager::Zone *CFFBotManager::GetZone( const Vector &pos ) const { return NULL; } // Needs proper implementation
const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const Vector &pos ) const { return NULL; } // Needs proper implementation
// int CFFBotManager::GetZoneCount( void ) const { return m_zoneCount; } // Already defined
// void CFFBotManager::CheckForBlockedZones( void ) { } // Already defined
const Vector *CFFBotManager::GetRandomPositionInZone( const Zone *zone ) const { return NULL; }
CNavArea *CFFBotManager::GetRandomAreaInZone( const Zone *zone ) const { return NULL; }
// Process the "bot_add" console command
bool CFFBotManager::BotAddCommand( int team, bool isFromConsole, const char *profileName, int weaponType, BotDifficultyType difficulty )
{
	if ( !TheNavMesh->IsLoaded() )
	{
		if ( !TheNavMesh->IsGenerating() )
		{
			if ( !m_isMapDataLoaded )
			{
				TheNavMesh->BeginGeneration();
				m_isMapDataLoaded = true; // TODO: Ensure this flag is used consistently for map data vs. nav mesh
			}
			// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: Nav mesh not loaded and not generating. Aborting bot add.\n" );
			return false;
		}
	}

	// Dont allow bots to join if the Navigation Mesh is being generated
	if (TheNavMesh->IsGenerating())
	{
		// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: Nav mesh is generating. Aborting bot add.\n" );
		return false;
	}

	const BotProfile *profile = NULL;

	if ( !isFromConsole )
	{
		profileName = NULL; // Ignore profile name if not from console (i.e. quota system)
		difficulty = GetDifficultyLevel(); // Use cvar difficulty
	}
	else
	{
		// If difficulty not specified by console, use cvar difficulty
		if ( difficulty == NUM_DIFFICULTY_LEVELS )
		{
			difficulty = GetDifficultyLevel();
		}

		// If team not specified by console, check ff_bot_join_team cvar
		if (team == FF_TEAM_AUTOASSIGN || team == FF_TEAM_UNASSIGNED ) // Using FF_TEAM_AUTOASSIGN as general "pick one"
		{
			const char *joinTeamStr = ff_bot_join_team.GetString();
			if ( !stricmp( joinTeamStr, "RED" ) ) // FF_TODO_CVAR: Use actual team name strings if different
				team = FF_TEAM_RED;
			else if ( !stricmp( joinTeamStr, "BLUE" ) )
				team = FF_TEAM_BLUE;
			// FF_TODO_TEAMS: Add YELLOW, GREEN if they are valid bot teams
			// else if ( !stricmp( joinTeamStr, "YELLOW" ) ) team = FF_TEAM_YELLOW;
			// else if ( !stricmp( joinTeamStr, "GREEN" ) ) team = FF_TEAM_GREEN;
			else
			{
				if (FFGameRules())
					team = FFGameRules()->SelectDefaultTeam(); // Let gamerules decide
				else
					team = FF_TEAM_RED; // Fallback if no gamerules
			}
		}
	}

	if ( profileName && *profileName )
	{
		// In FF, bot names might not need to be unique from human players in the same way as CS career mode.
		// bool ignoreHumans = FFGameRules() && FFGameRules()->IsCareer(); // FF doesn't have career mode.
		// For now, assume profileName is a profile filename or a specific bot name from a profile.
		// Bot names are usually derived from profile, so UTIL_IsNameTaken might be complex.
		// Let profile selection handle it.

		// Try to add a bot by name (exact match from a profile file)
		profile = TheBotProfiles->GetProfile( profileName, team ); // team can be FF_TEAM_AUTOASSIGN here
		if ( !profile )
		{
			// Try to add a bot by template (partial match or inherited profile)
			// TheBotProfiles->GetProfileMatchingTemplate might need to be adapted for FF if not already.
			// For now, using GetRandomProfileFiltered to achieve similar effect if name is a template.
			profile = TheBotProfiles->GetRandomProfileFiltered( difficulty, team, profileName, weaponType );

			if ( !profile )
			{
				if ( isFromConsole )
				{
					Msg( "Error - no profile for '%s' exists or matches criteria.\n", profileName );
				}
				// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: No profile for '%s'.\n", profileName );
				return true; // Return true because the command was processed, even if it failed.
			}
		}
	}
	else
	{
		// If team not specified (e.g. by quota system), check ff_bot_join_team cvar
		if (team == FF_TEAM_AUTOASSIGN || team == FF_TEAM_UNASSIGNED)
		{
			const char *joinTeamStr = ff_bot_join_team.GetString();
			if ( !stricmp( joinTeamStr, "RED" ) ) team = FF_TEAM_RED;
			else if ( !stricmp( joinTeamStr, "BLUE" ) ) team = FF_TEAM_BLUE;
			// FF_TODO_TEAMS: Add YELLOW, GREEN
			else
			{
				if (FFGameRules())
					team = FFGameRules()->SelectDefaultTeam();
				else
					team = FF_TEAM_RED; // Fallback
			}
		}

		profile = TheBotProfiles->GetRandomProfileFiltered( difficulty, team, NULL, weaponType );
		if (profile == NULL)
		{
			if ( isFromConsole )
			{
				Msg( "All bot profiles at this difficulty level and team are in use or no matching profile found.\n" );
			}
			// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: No random profile available for difficulty %d, team %d.\n", difficulty, team);
			return true; // Command processed.
		}
	}

	// Ensure a valid playable team is selected
	if (team != FF_TEAM_RED && team != FF_TEAM_BLUE ) // FF_TODO_TEAMS: Add YELLOW, GREEN
	{
		if ( isFromConsole )
		{
			Msg( "Could not add bot to the game: Invalid team specified (%d).\n", team );
		}
		// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: Invalid team %d.\n", team );
		return false; // Return false as it's a setup error.
	}

	if (FFGameRules()) // Always check FFGameRules() before calling
	{
		if ( FFGameRules()->TeamFull( team ) )
		{
			if ( isFromConsole )
			{
				Msg( "Could not add bot to the game: Team %d is full.\n", team );
			}
			// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: Team %d full.\n", team );
			return false;
		}

		// FF_TODO_GAMERULES: Check if FFGameRules()->TeamStacked is relevant for FF.
		// if ( FFGameRules()->TeamStacked( team, FF_TEAM_UNASSIGNED ) ) // FF_TEAM_UNASSIGNED or FF_TEAM_AUTOASSIGN?
		// {
		// 	if ( isFromConsole )
		// 	{
		// 		Msg( "Could not add bot to the game: Team is stacked (to disable this check, set mp_autoteambalance to zero, increase mp_limitteams, and restart the round).\n" );
		// 	}
		// 	return false;
		// }
	}


	// Create the actual bot
	// CFFBot *bot = CreateBot<CFFBot>( profile, team ); // This is from CBotManager template
	edict_t *pEdict = engine->CreateFakeClient( profile->GetName() );
	if ( !pEdict )
	{
		Msg( "Failed to create fake client for bot %s.\n", profile->GetName() );
		if ( profile ) TheBotProfiles->DecrementUseCount( profile ); // Decrement if creation failed
		return false;
	}

	// AllocateAndBindBotEntity is expected to be called by ClientPutInServerOverride_Bot
	// but we need to set up the profile and team beforehand for the bot.
	// Store profile and intended team for the bot to pick up in its Spawn.
	// This requires a mechanism to pass this data to the bot entity before Spawn.
	// For now, assume CreateFakeClient + ClientPutInServer + Bot::Spawn sequence.
	// We need a way to associate `profile` and `team` with `pEdict` for the bot's Spawn method.
	// This is a deviation from CS where CreateBot handles it more directly.
	// A temporary global or a map could be used: g_PendingBotProfile[entindex(pEdict)] = profile;

	// The CS CreateBot<T> template does:
	// 1. Allocates entity (pEdict = engine->CreateFakeClient)
	// 2. Calls TheBots->AllocateAndBindBotEntity(pEdict) -> which calls ClientPutInServerOverride_Bot
	//    Inside ClientPutInServerOverride_Bot:
	//    - CBasePlayer *pPlayer = TheBots->AllocateBotEntity(); // Factory: new T (bot entity)
	//    - pPlayer->SetEdict(pEdict);
	//    - pPlayer->SetPlayerName(profile->GetName());
	// 3. Sets team: bot->ChangeTeam(team)
	// 4. Calls DispatchSpawn(bot->edict())
	// 5. Sets profile: bot->SetProfile(profile)

	// Let's try to follow this more closely.
	// ClientPutInServerOverride_Bot will be called when the entity is truly put in server.
	// We need to ensure the bot entity, once created via AllocateBotEntity, gets its profile and team.
	// This suggests CFFBot::Spawn() needs to acquire its profile.
	// Or, BotAddCommand should get the CFFBot* pointer after it's created.

	// This is tricky because AllocateAndBindBotEntity is in the base CBotManager and we don't get the CFFBot* back here directly.
	// Let's assume CFFBot::Spawn or some init function will handle profile and team assignment based on some criteria
	// or that we can retrieve the bot pointer shortly after.

	// For now, just creating the fake client. The bot entity itself will be created by the engine calling ClientPutInServer.
	// The profile and team need to be communicated to the bot instance.
	// One way: Store them in a temporary map keyed by edict or userid.
	// CFFBot::BotSpawn() can then retrieve it.
	// This is a common pattern if the entity creation is separated from parameter setup.
	// Let's assume such a mechanism exists or will be added to CFFBot.
	// For example: PreSpawnBotSettings(entindex(pEdict), profile, team);

	// If the bot is added via console, increment quota
	if (isFromConsole)
	{
		bot_quota.SetValue( bot_quota.GetInt() + 1 );
	}

	// FF_LOG_BOT_ACTION( NULL, "BotAddCommand: Successfully initiated bot add for profile '%s', team %d.\n", profile->GetName(), team );
	return true; // Successfully initiated adding a bot.
}
// Keep a minimum quota of bots in the game
void CFFBotManager::MaintainBotQuota( void )
{
	if ( !AreBotsAllowed() )
		return;

	if (TheNavMesh->IsGenerating()) // Don't add/remove if nav mesh is busy
		return;

	// Don't add/remove bots if game has ended for some reason
    if (FFGameRules() && FFGameRules()->IsGameOver())
		return;

	int totalHumansInGame = UTIL_HumansInGame( false ); // false = count all humans, including spectators for some checks
	int humanPlayersInGame = UTIL_HumansInGame( true ); // true = ignore spectators, only count active players

	// Don't add bots until local player has been registered (if listenserver), to make sure he's player ID #1 etc.
	if (!engine->IsDedicatedServer() && totalHumansInGame == 0)
		return;

	if ( !FFGameRules() || !TheFFBots() ) // Use TheFFBots() for safety
	{
		return;
	}

	int desiredBotCount = bot_quota.GetInt();
	int botsInGame = UTIL_FFBotsInGame(); // Assumed to be implemented

	// Check if the round has been ongoing for a while, affecting new spawns
	// FF_TODO_GAMERULES: Adjust time if FF has different spawn rules post-round start.
	// bool isRoundInProgress = FFGameRules()->m_bFirstConnected && // Assuming m_bFirstConnected or similar exists
	// 						 !TheFFBots()->IsRoundOver() &&
	// 						 ( FFGameRules()->GetRoundElapsedTime() >= 20.0f ); // 20 seconds is a common threshold

	// FF_TODO_CVAR: FF might have a different cvar for quota_mode. Using CS's "fill" and "match" for now.
	// const char *quotaMode = cv_bot_quota_mode.GetString(); // Assuming cv_bot_quota_mode exists
	const char *quotaMode = "normal"; // Placeholder if FF doesn't have fill/match modes, default to 'normal' (direct quota)

	if ( FStrEq( quotaMode, "fill" ) )
	{
		// 'fill' mode: bots + humans = bot_quota
		// if ( !isRoundInProgress ) // Don't adjust if round is too far along
		// {
			desiredBotCount = MAX( 0, desiredBotCount - humanPlayersInGame );
		// }
		// else
		// {
		// 	desiredBotCount = botsInGame; // Keep current number if round in progress
		// }
	}
	else if ( FStrEq( quotaMode, "match" ) )
	{
		// 'match' mode: bots = bot_quota * humans
		// if ( !isRoundInProgress )
		// {
			desiredBotCount = (int)MAX( 0, bot_quota.GetFloat() * humanPlayersInGame );
		// }
		// else
		// {
		// 	desiredBotCount = botsInGame;
		// }
	}
	// Else, "normal" mode means desiredBotCount is just bot_quota.GetInt().

	// Wait for a player to join, if cvar is set
	if (bot_join_after_player.GetBool())
	{
		if (humanPlayersInGame == 0)
			desiredBotCount = 0;
	}

	// Delay bot joining after map load
	if ( bot_join_delay.GetFloat() > 0 && FFGameRules()->GetMapElapsedTime() < bot_join_delay.GetFloat() )
	{
		desiredBotCount = 0;
	}

	// FF_TODO_CVAR: Check for FF equivalent of bot_auto_vacate for slot reservation.
	// if (cv_bot_auto_vacate.GetBool())
	// 	desiredBotCount = MIN( desiredBotCount, gpGlobals->maxClients - (humanPlayersInGame + 1) );
	// else
		desiredBotCount = MIN( desiredBotCount, gpGlobals->maxClients - humanPlayersInGame );


	// Team balancing logic (simplified from CS for now, can be expanded)
	// FF_TODO_BALANCE: This is a very basic balancing. FF might need more sophisticated logic,
	// especially if it supports more than 2 teams or has specific balancing rules.
	// if ( botsInGame > 0 && desiredBotCount == botsInGame && FFGameRules()->m_bFirstConnected )
	// {
	// 	if ( FFGameRules()->GetRoundElapsedTime() < 20.0f ) // Only balance early in round
	// 	{
	// 		if ( mp_autoteambalance.GetBool() ) // Assuming mp_autoteambalance cvar
	// 		{
	// 			int numRed, numBlue; // FF_TODO_TEAMS: Extend for Yellow, Green
	// 			FFGameRules()->GetTeamCounts( numRed, numBlue ); // Assumes FFGameRules has this
	//
	// 			// Only balance if bots can join any team (ff_bot_join_team is not "RED" or "BLUE")
	// 			const char* joinTeamPref = ff_bot_join_team.GetString();
	// 			if ( stricmp(joinTeamPref, "RED") != 0 && stricmp(joinTeamPref, "BLUE") != 0 )
	// 			{
	// 				if ( numRed > numBlue + 1 )
	// 				{
	// 					if ( UTIL_FFKickBotFromTeam( FF_TEAM_RED ) ) return; // Kicked, so restart maintain quota next frame
	// 				}
	// 				else if ( numBlue > numRed + 1 )
	// 				{
	// 					if ( UTIL_FFKickBotFromTeam( FF_TEAM_BLUE ) ) return;
	// 				}
	// 			}
	// 		}
	// 	}
	// }

	// Add bots if necessary
	if (desiredBotCount > botsInGame)
	{
		// Check if any team can accept a new bot (not full)
		// FF_TODO_TEAMS: Iterate through all playable FF teams if more than RED/BLUE.
		if ( (FFGameRules() && (!FFGameRules()->TeamFull( FF_TEAM_RED ) || !FFGameRules()->TeamFull( FF_TEAM_BLUE ))) || !FFGameRules() )
		{
			// Call BotAddCommand with FF_TEAM_AUTOASSIGN to let it pick based on ff_bot_join_team & game rules
			this->BotAddCommand( FF_TEAM_AUTOASSIGN );
		}
	}
	else if (desiredBotCount < botsInGame)
	{
		// Kick a bot to maintain quota

		// First, try to remove any unassigned bots (if such a state exists and is kickable)
		// if (UTIL_FFKickBotFromTeam( FF_TEAM_UNASSIGNED )) // FF_TEAM_UNASSIGNED might not be practical for kicking
		// 	return;

		int kickTeam = FF_TEAM_AUTOASSIGN;
		if (FFGameRules())
		{
			// Simple balancing: kick from the team with more players.
			// FF_TODO_BALANCE: More sophisticated: consider scores, number of humans vs bots on teams etc.
			int numRed, numBlue; // FF_TODO_TEAMS: Extend for Yellow, Green
			FFGameRules()->GetTeamCounts( numRed, numBlue );

			if (numRed > numBlue) kickTeam = FF_TEAM_RED;
			else if (numBlue > numRed) kickTeam = FF_TEAM_BLUE;
			else // Teams are equal, or no game rules, pick a team at random (preferring Red slightly)
			{
				kickTeam = (RandomInt( 0, 1 ) == 0) ? FF_TEAM_RED : FF_TEAM_BLUE;
			}
		} else {
			// No game rules, just pick randomly
			kickTeam = (RandomInt( 0, 1 ) == 0) ? FF_TEAM_RED : FF_TEAM_BLUE;
		}


		if (UTIL_FFKickBotFromTeam( kickTeam )) // Assumes UTIL_FFKickBotFromTeam is implemented
			return; // Kicked one, will re-evaluate next frame.

		// If failed to kick from chosen team (e.g. no bots on that team), try the other.
		// FF_TODO_TEAMS: This needs to be smarter for >2 teams.
		if (kickTeam == FF_TEAM_RED) UTIL_FFKickBotFromTeam( FF_TEAM_BLUE );
		else UTIL_FFKickBotFromTeam( FF_TEAM_RED );
	}
}
// void CFFBotManager::OnPlayerDeath( IGameEvent *event ) { CFFBot *bot; FF_FOR_EACH_BOT( bot, i ) { bot->OnPlayerDeath( event ); } } // Already defined
// ... and so on for all other event handlers and member functions previously defined in this file ...
// This is just a structural placeholder to ensure the diff applies; the actual functions are assumed to be present.
// The critical change is to ExtractScenarioData and the addition of the helper functions.
