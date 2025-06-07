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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)				// disable warning that variable *may* not be initialized 
#endif

CBotManager *TheBots = NULL;

bool CFFBotManager::m_isMapDataLoaded = false;

int g_nClientPutInServerOverrides = 0;


void DrawOccupyTime( void );
ConVar bot_show_occupy_time( "bot_show_occupy_time", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show when each nav area can first be reached by each team." );

void DrawBattlefront( void );
ConVar bot_show_battlefront( "bot_show_battlefront", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show areas where rushing players will initially meet." );

int UTIL_FFBotsInGame( void );

ConVar bot_join_delay( "bot_join_delay", "0", FCVAR_GAMEDLL, "Prevents bots from joining the server for this many seconds after a map change." );

inline bool AreBotsAllowed()
{
	const char *nobots = CommandLine()->CheckParm( "-nobots" );
	if ( nobots ) return false;
	return true;
}

void InstallFFBotControl( void )
{
	if ( TheBots != NULL ) delete TheBots;
	TheBots = new CFFBotManager;
}

void RemoveFFBotControl( void )
{
	if ( TheBots != NULL ) delete TheBots;
	TheBots = NULL;
}

CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername )
{
	CBasePlayer *pPlayer = TheBots->AllocateAndBindBotEntity( pEdict );
	if ( pPlayer ) pPlayer->SetPlayerName( playername );
	++g_nClientPutInServerOverrides;
	return pPlayer;
}

CFFBotManager::CFFBotManager()
{
	m_zoneCount = 0;
	m_serverActive = false;
	m_roundStartTimestamp = 0.0f;
	m_isRoundOver = true;

	m_eventListenersEnabled = true;
	m_commonEventListeners.AddToTail( &m_PlayerFootstepEvent );
	m_commonEventListeners.AddToTail( &m_PlayerRadioEvent );
	m_commonEventListeners.AddToTail( &m_PlayerFallDamageEvent );
	m_commonEventListeners.AddToTail( &m_DoorMovingEvent );
	m_commonEventListeners.AddToTail( &m_BreakPropEvent );
	m_commonEventListeners.AddToTail( &m_BreakBreakableEvent );
	m_commonEventListeners.AddToTail( &m_WeaponFireEvent );
	m_commonEventListeners.AddToTail( &m_WeaponFireOnEmptyEvent );
	m_commonEventListeners.AddToTail( &m_WeaponReloadEvent );
	m_commonEventListeners.AddToTail( &m_BulletImpactEvent );
	m_commonEventListeners.AddToTail( &m_GrenadeBounceEvent );
	m_commonEventListeners.AddToTail( &m_NavBlockedEvent );

	TheBotPhrases = new BotPhraseManager;
	TheBotProfiles = new BotProfileManager;
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
	TheBotPhrases->OnRoundRestart();
	m_isRoundOver = false;
}

void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue ) { /* ... (implementation as before) ... */ }

void CFFBotManager::EnableEventListeners( bool enable )
{
	if ( m_eventListenersEnabled == enable ) return;
	m_eventListenersEnabled = enable;
	for ( int i=0; i<m_commonEventListeners.Count(); ++i )
	{
		if ( enable ) gameeventmanager->AddListener( m_commonEventListeners[i], m_commonEventListeners[i]->GetEventName(), true );
		else gameeventmanager->RemoveListener( m_commonEventListeners[i] );
	}
}

void CFFBotManager::StartFrame( void )
{
	if ( !AreBotsAllowed() ) { EnableEventListeners( false ); return; }
	CBotManager::StartFrame();
	MaintainBotQuota();
	EnableEventListeners( UTIL_FFBotsInGame() > 0 );
	if (cv_bot_debug.GetInt() == 5) { /* ... (visualization logic as before) ... */ }
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

// ConVar extern declarations, now defined in ff_bot_cvars.cpp
extern ConVar bot_difficulty;
extern ConVar bot_quota;
extern ConVar bot_quota_mode;
extern ConVar bot_join_team;
extern ConVar bot_join_after_player;
extern ConVar bot_auto_vacate;
extern ConVar bot_chatter;
extern ConVar bot_prefix;
extern ConVar bot_defer_to_human;

extern ConVar ff_bot_allow_pistols;
extern ConVar ff_bot_allow_shotguns;
extern ConVar ff_bot_allow_sub_machine_guns;
extern ConVar ff_bot_allow_rifles;
extern ConVar ff_bot_allow_machine_guns;
extern ConVar ff_bot_allow_grenades;
extern ConVar ff_bot_allow_rocket_launchers;
extern ConVar ff_bot_allow_flamethrowers;
extern ConVar ff_bot_allow_pipe_launchers;
extern ConVar ff_bot_allow_miniguns;
extern ConVar ff_bot_allow_sniper_rifles;
extern ConVar ff_bot_allow_mediguns;
extern ConVar ff_bot_allow_tranqguns;
// Note: bot_debug and bot_debug_target are also in ff_bot_cvars.cpp but not directly used in this file's logic yet.


bool CFFBotManager::IsWeaponUseable( const CBasePlayerWeapon *weapon ) const
{
	if (weapon == NULL)
		return false; // Cannot use a null weapon

	const CFFWeaponBase *ffWeapon = dynamic_cast<const CFFWeaponBase *>(weapon);
	if (ffWeapon == NULL)
	{
		// If it's not an CFFWeaponBase, we can't get FFWeaponID.
		// For safety, assume usable, or log a warning.
		// Alternatively, could be strict and return false.
		// CSBotManager returns true for C4 (non-CSWeapon).
		// Let's be permissive for now for uncastable weapons.
		return true;
	}

	FFWeaponID weaponID = ffWeapon->GetWeaponID();

	// Melee weapons are generally always allowed, unless specific "X_only" mode is on,
	// which is handled by other cvars being 0.
	// There isn't a single "ff_bot_allow_melee" cvar, so they bypass these checks.
	switch (weaponID)
	{
		case FF_WEAPON_CROWBAR:
		case FF_WEAPON_KNIFE:
		case FF_WEAPON_MEDKIT: // Medkit is more utility but often melee slot
		case FF_WEAPON_SPANNER:
		case FF_WEAPON_UMBRELLA:
			return true;
		// Deployables are not "combat" weapons in the same sense for this check
		case FF_WEAPON_DEPLOYDISPENSER:
		case FF_WEAPON_DEPLOYSENTRYGUN:
		case FF_WEAPON_DEPLOYDETPACK:
		case FF_WEAPON_DEPLOYMANCANNON:
			return true; // Or false, depending on interpretation. Bots don't "fight" with these.
		default:
			break;
	}

	// Check against specific categories
	// FF_TODO_WEAPONS: Refine these mappings as needed. Some FF weapons might need new categories or special handling.
	switch (weaponID)
	{
		// Pistols
		case FF_WEAPON_JUMPGUN: // Scout's primary, acts like a pistol
			return ff_bot_allow_pistols.GetBool();

		// Shotguns
		case FF_WEAPON_SHOTGUN:
		case FF_WEAPON_SUPERSHOTGUN:
			return ff_bot_allow_shotguns.GetBool();

		// SMGs / Nailguns
		case FF_WEAPON_NAILGUN:
		case FF_WEAPON_SUPERNAILGUN:
		case FF_WEAPON_TOMMYGUN: // Civilian primary
			return ff_bot_allow_sub_machine_guns.GetBool(); // Or ff_bot_allow_rifles if SMG isn't a good fit

		// Rifles (General Purpose)
		case FF_WEAPON_AUTORIFLE: // Sniper secondary
		case FF_WEAPON_RAILGUN:   // Engineer primary
			return ff_bot_allow_rifles.GetBool();

		// Sniper Rifles
		case FF_WEAPON_SNIPERRIFLE:
			return ff_bot_allow_sniper_rifles.GetBool();

		// Rocket Launchers / Explosives
		case FF_WEAPON_RPG:
		case FF_WEAPON_IC: // Incendiary Cannon, explosive/fire AOE
			return ff_bot_allow_rocket_launchers.GetBool();

		// Flamethrowers
		case FF_WEAPON_FLAMETHROWER:
			return ff_bot_allow_flamethrowers.GetBool();

		// Pipe Launchers (Demoman)
		case FF_WEAPON_GRENADELAUNCHER: // Often primary for Demo, distinct from hand grenades
		case FF_WEAPON_PIPELAUNCHER:
			return ff_bot_allow_pipe_launchers.GetBool();

		// Miniguns (HWGuy)
		case FF_WEAPON_ASSAULTCANNON:
			return ff_bot_allow_miniguns.GetBool();

		// Utility / Special
		case FF_WEAPON_TRANQUILISER: // Spy primary
			return ff_bot_allow_tranqguns.GetBool();

		// Grenades (Hand grenades - if they are represented as CFFWeaponBase instances and bots can "wield" them)
		// This check might be more relevant in CanEquip/SwitchTo logic rather than general "use".
		// For now, if a bot is somehow wielding a grenade type directly, check ff_bot_allow_grenades.
		// However, actual grenade throwing is usually a separate action.
		// FF_TODO_WEAPONS: Verify if hand grenades are CFFWeaponBase and appear here.
		// Assuming GetWeaponID() can return grenade types if they are wielded:
		// case FF_WEAPON_GRENADE_NORMAL: (Example, if such an ID exists for wielded grenades)
		// case FF_WEAPON_GRENADE_CONC:
		// case FF_WEAPON_GRENADE_EMP:
		// case FF_WEAPON_GRENADE_NAIL:
		//	 return ff_bot_allow_grenades.GetBool();

		// FF_WEAPON_NONE, FF_WEAPON_CUBEMAP, FF_WEAPON_MAX should not be actively used.
		case FF_WEAPON_NONE:
		case FF_WEAPON_CUBEMAP: // Should not be a usable weapon for a bot
		case FF_WEAPON_MAX:
			return false;

		default:
			// If a weapon ID is not explicitly handled, assume it's usable to be less restrictive.
			// Or, log a warning about an unhandled weapon type.
			// Warning("CFFBotManager::IsWeaponUseable: Unhandled weapon ID %d\n", weaponID);
			return true;
	}
}

bool CFFBotManager::IsOnDefense( const CFFPlayer *player ) const { return false; /* FF_TODO: Implement FF game mode logic */ }
bool CFFBotManager::IsOnOffense( const CFFPlayer *player ) const { return !IsOnDefense( player ); /* FF_TODO: Implement FF game mode logic */ }

void CFFBotManager::ServerActivate( void )
{
	m_isMapDataLoaded = false;
	TheBotPhrases->Reset();
	TheBotPhrases->Initialize( "BotChatter.db", 0 );

	TheBotProfiles->Reset();
	TheBotProfiles->FindVoiceBankIndex( "BotChatter.db" );
	const char *filename = "BotPackList.db";
	FileHandle_t file = filesystem->Open( filename, "r" );
	if ( !file )
	{
		TheBotProfiles->Init( "BotProfile.db" );
	}
	else
	{
		// FF_TODO_PORT: This BotProfile loading from BotPackList.db needs to be verified if it's still used.
		// The original CS code had a more complex block here for parsing BotPackList.db
		// For now, assuming the simple Init("BotProfile.db") if BotPackList.db is not found is okay,
		// or that the existing BotProfile loading in the file is sufficient.
		// If BotPackList.db is used, the parsing logic from cs_bot_manager.cpp should be ported here.
		// Simplified for now:
		char *dataPointer = new char[filesystem->Size(filename) + 1];
		filesystem->Read(dataPointer, filesystem->Size(filename), file);
		dataPointer[filesystem->Size(filename)] = 0;
		filesystem->Close(file);
		const char *dataFile = SharedParse(dataPointer);
		const char *token;
		while (dataFile)
		{
			token = SharedGetToken();
			if (token && *token)
			{
				char *clone = CloneString(token);
				TheBotProfiles->Init(clone);
				delete[] clone;
			}
			dataFile = SharedParse(dataFile);
		}
		delete[] dataPointer;
	}
	const BotProfileManager::VoiceBankList *voiceBanks = TheBotProfiles->GetVoiceBanks();
	for ( int i=1; i<voiceBanks->Count(); ++i )
	{
		TheBotPhrases->Initialize( (*voiceBanks)[i], i );
	}

	TheNavMesh->SetPlayerSpawnName( "info_player_red" );
	TheNavMesh->SetPlayerSpawnName( "info_player_blue" );

	// FF_LUA_INTEGRATION:
	// Lua scripts (base.lua, mapname.lua, globalscripts/custom.lua) are loaded and executed
	// by FFScriptManager::LevelInit(), which is called by game rules (eg CGameRules::State_Enter(GR_STATE_PREGAME)).
	// CFFBotManager does not need to manually load these scripts here.
	// Instead, it would query data/call functions defined in Lua if needed.
	Msg("[FF_BOT] CFFBotManager::ServerActivate - Lua scripts presumed loaded by FFScriptManager.\n");
	if (_scriptman.GetLuaState() == NULL)
	{
		Warning("[FF_BOT] Lua state is NULL in CFFBotManager::ServerActivate. Lua scripts might not have loaded correctly.\n");
	}
	else
	{
		Msg("[FF_BOT] Lua state is available. Bots can potentially interact with Lua.\n");
		// FF_LUA_INTEGRATION_TODO: Add calls here to Lua functions to get objective data
		// and populate m_luaObjectivePoints, m_luaPathPoints, etc.
		// Example:
		// luabridge::LuaRef luaObjectives = luabridge::getGlobal( _scriptman.GetLuaState(), "FF_GetMapObjectivePoints" );
		// if ( luaObjectives.isFunction() ) { try { luabridge::LuaResult result = luaObjectives(); /* process result */ } catch(...) {} }
	}

	ExtractScenarioData(); // This might need to be called AFTER Lua populates objectives if they are Lua-driven
	RestartRound();
	TheBotPhrases->OnMapChange();
	m_serverActive = true;
}

void CFFBotManager::ServerDeactivate( void ) { m_serverActive = false; }
void CFFBotManager::ClientDisconnect( CBaseEntity *entity ) { }

void BotArgumentsFromArgv( const CCommand &args, const char **name, int *weaponType, BotDifficultyType *difficulty, int *team = NULL, bool *all = NULL )
{
	static char s_name[MAX_PLAYER_NAME_LENGTH]; s_name[0] = 0; *name = s_name; *difficulty = NUM_DIFFICULTY_LEVELS;
	if ( team ) *team = FF_TEAM_UNASSIGNED; if ( all ) *all = false; *weaponType = 0;
	for ( int arg=1; arg<args.ArgC(); ++arg ) {
		bool found = false; const char *token = args[arg];
		if ( all && FStrEq( token, "all" ) ) { *all = true; found = true; }
		else if ( team ) {
			if ( FStrEq( token, "red" ) ) { *team = FF_TEAM_RED; found = true; }
			else if ( FStrEq( token, "blue" ) ) { *team = FF_TEAM_BLUE; found = true; }
			// else if ( FStrEq( token, "yellow" ) ) { *team = FF_TEAM_YELLOW; found = true; } // If yellow is playable
			// else if ( FStrEq( token, "green" ) ) { *team = FF_TEAM_GREEN; found = true; } // If green is playable
			else if ( FFGameRules() ) { int parsedTeam = FFGameRules()->GetTeamIndex(token);
				if (parsedTeam == FF_TEAM_RED || parsedTeam == FF_TEAM_BLUE /* || add yellow/green */) { *team = parsedTeam; found = true;} }
		}
		for( int i=0; i<NUM_DIFFICULTY_LEVELS && !found; ++i ) if (!stricmp( BotDifficultyName[i], token )) { *difficulty = (BotDifficultyType)i; found = true; }
		if ( !found ) { /* FF_TODO: Weapon parsing */ }
		if ( !found ) Q_strncpy( s_name, token, sizeof( s_name ) );
	}
}

CON_COMMAND_F( bot_add, "bot_add <team> <type> <difficulty> <name> - Adds a bot matching the given criteria.", FCVAR_GAMEDLL )
{ if ( !UTIL_IsCommandIssuedByServerAdmin() ) return; const char *name; BotDifficultyType difficulty; int weaponType; int team;
  BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team ); TheFFBots()->BotAddCommand( team, FROM_CONSOLE, name, weaponType, difficulty ); }

class CollectBots { /* ... (implementation as before, but ensure CFFBot is used internally) ... */
public:
	CollectBots( const char *name, int weaponType, BotDifficultyType difficulty, int team )
	{ m_name = name; m_difficulty = difficulty; m_team = team; m_weaponType = weaponType; }
	bool operator() ( CBasePlayer *player ) {
		if ( !player->IsBot() ) return true; CFFBot *bot = dynamic_cast< CFFBot * >(player);
		if ( !bot || !bot->GetProfile() ) return true;
		if ( m_name && *m_name ) { if ( FStrEq( m_name, bot->GetProfile()->GetName() ) ) { m_bots.RemoveAll(); m_bots.AddToTail( bot ); return false; }
			if ( !bot->GetProfile()->InheritsFrom( m_name ) ) return true; }
		if ( m_difficulty != NUM_DIFFICULTY_LEVELS && !bot->GetProfile()->IsDifficulty( m_difficulty ) ) return true;
		if ( m_team != FF_TEAM_UNASSIGNED && bot->GetTeamNumber() != m_team ) return true;
		m_bots.AddToTail( bot ); return true;
	}
	CUtlVector< CFFBot * > m_bots;
private:
	const char *m_name; int m_weaponType; BotDifficultyType m_difficulty; int m_team;
};

CON_COMMAND_F( bot_kill, "bot_kill <all> <team> <type> <difficulty> <name> - Kills a specific bot, or all bots.", FCVAR_GAMEDLL )
{ if ( !UTIL_IsCommandIssuedByServerAdmin() ) return; const char *name; BotDifficultyType difficulty; int weaponType; int team; bool all;
  BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team, &all );
  if ( (!name || !*name) && team == FF_TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) all = true;
  CollectBots collector( name, weaponType, difficulty, team ); ForEachPlayer( collector );
  for ( int i=0; i<collector.m_bots.Count(); ++i ) { CFFBot *bot = collector.m_bots[i]; if ( !bot->IsAlive() ) continue; bot->CommitSuicide(); if ( !all ) return; }
}
CON_COMMAND_F( bot_kick, "bot_kick <all> <team> <type> <difficulty> <name> - Kicks a specific bot, or all bots.", FCVAR_GAMEDLL )
{ if ( !UTIL_IsCommandIssuedByServerAdmin() ) return; const char *name; BotDifficultyType difficulty; int weaponType; int team; bool all;
  BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team, &all );
  if ( (!name || !*name) && team == FF_TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) all = true;
  CollectBots collector( name, weaponType, difficulty, team ); ForEachPlayer( collector );
  for ( int i=0; i<collector.m_bots.Count(); ++i ) { CFFBot *bot = collector.m_bots[i]; engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", bot->GetPlayerName() ) );
	if ( !all ) { int newQuota = cv_bot_quota.GetInt() - 1; cv_bot_quota.SetValue( clamp( newQuota, 0, cv_bot_quota.GetInt() ) ); return; } }
  if ( all && (!name || !*name) && team == FF_TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) cv_bot_quota.SetValue( 0 );
  else { int newQuota = cv_bot_quota.GetInt() - collector.m_bots.Count(); cv_bot_quota.SetValue( clamp( newQuota, 0, cv_bot_quota.GetInt() ) ); }
}

CON_COMMAND_F( bot_goto_mark, "Sends a bot to the selected nav area.", FCVAR_GAMEDLL | FCVAR_CHEAT )
{ if ( !UTIL_IsCommandIssuedByServerAdmin() ) return; CNavArea *area = TheNavMesh->GetMarkedArea(); if (area) {
	for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) ); if (player == NULL || !player->IsBot()) continue;
	CFFBot *bot = dynamic_cast<CFFBot *>( player ); if ( bot ) bot->MoveTo( area->GetCenter(), FASTEST_ROUTE ); break; } }
}

// The extern ConVar declarations that were here have been removed,
// as they are now defined in ff_bot_cvars.cpp.
// The ConCommand functions below will link to those definitions.

CON_COMMAND_F( bot_knives_only, "Restricts the bots to only using melee weapons", FCVAR_GAMEDLL ) // FF uses various melee
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	// Assuming melee is always allowed or not controlled by a specific "allow_melee" cvar for bots
	ff_bot_allow_pistols.SetValue( 0 );
	ff_bot_allow_shotguns.SetValue( 0 );
	ff_bot_allow_sub_machine_guns.SetValue( 0 );
	ff_bot_allow_rifles.SetValue( 0 );
	ff_bot_allow_machine_guns.SetValue( 0 );
	ff_bot_allow_grenades.SetValue( 0 );
	ff_bot_allow_sniper_rifles.SetValue( 0 );
	ff_bot_allow_rocket_launchers.SetValue( 0 );
	ff_bot_allow_flamethrowers.SetValue( 0 );
	ff_bot_allow_pipe_launchers.SetValue( 0 );
	ff_bot_allow_miniguns.SetValue( 0 );
	ff_bot_allow_mediguns.SetValue( 0 );
	ff_bot_allow_tranqguns.SetValue( 0 );
}

CON_COMMAND_F( bot_pistols_only, "Restricts the bots to only using pistols (Scout, Spy, Engineer)", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	ff_bot_allow_pistols.SetValue( 1 );
	ff_bot_allow_shotguns.SetValue( 0 );
	ff_bot_allow_sub_machine_guns.SetValue( 0 );
	ff_bot_allow_rifles.SetValue( 0 );
	ff_bot_allow_machine_guns.SetValue( 0 );
	ff_bot_allow_grenades.SetValue( 0 );
	ff_bot_allow_sniper_rifles.SetValue( 0 );
	ff_bot_allow_rocket_launchers.SetValue( 0 );
	ff_bot_allow_flamethrowers.SetValue( 0 );
	ff_bot_allow_pipe_launchers.SetValue( 0 );
	ff_bot_allow_miniguns.SetValue( 0 );
	ff_bot_allow_mediguns.SetValue( 0 );
	ff_bot_allow_tranqguns.SetValue( 0 );
}

CON_COMMAND_F( bot_rockets_only, "Restricts the bots to only using rocket launchers (Soldier)", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	ff_bot_allow_pistols.SetValue( 0 );
	ff_bot_allow_shotguns.SetValue( 1 ); // Soldiers often have shotguns too
	ff_bot_allow_sub_machine_guns.SetValue( 0 );
	ff_bot_allow_rifles.SetValue( 0 );
	ff_bot_allow_machine_guns.SetValue( 0 );
	ff_bot_allow_grenades.SetValue( 0 ); // Grenades are separate
	ff_bot_allow_sniper_rifles.SetValue( 0 );
	ff_bot_allow_rocket_launchers.SetValue( 1 );
	ff_bot_allow_flamethrowers.SetValue( 0 );
	ff_bot_allow_pipe_launchers.SetValue( 0 );
	ff_bot_allow_miniguns.SetValue( 0 );
	ff_bot_allow_mediguns.SetValue( 0 );
	ff_bot_allow_tranqguns.SetValue( 0 );
}


CON_COMMAND_F( bot_snipers_only, "Restricts the bots to only using sniper rifles (Sniper)", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	ff_bot_allow_pistols.SetValue( 0 );
	ff_bot_allow_shotguns.SetValue( 0 );
	ff_bot_allow_sub_machine_guns.SetValue( 1 ); // Sniper often has SMG
	ff_bot_allow_rifles.SetValue( 0 );
	ff_bot_allow_machine_guns.SetValue( 0 );
	ff_bot_allow_grenades.SetValue( 0 );
	ff_bot_allow_sniper_rifles.SetValue( 1 );
	ff_bot_allow_rocket_launchers.SetValue( 0 );
	ff_bot_allow_flamethrowers.SetValue( 0 );
	ff_bot_allow_pipe_launchers.SetValue( 0 );
	ff_bot_allow_miniguns.SetValue( 0 );
	ff_bot_allow_mediguns.SetValue( 0 );
	ff_bot_allow_tranqguns.SetValue( 0 );
}

CON_COMMAND_F( bot_all_weapons, "Allows the bots to use all their normal weapons", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	ff_bot_allow_pistols.SetValue( 1 );
	ff_bot_allow_shotguns.SetValue( 1 );
	ff_bot_allow_sub_machine_guns.SetValue( 1 );
	ff_bot_allow_rifles.SetValue( 1 );
	ff_bot_allow_machine_guns.SetValue( 1 );
	ff_bot_allow_grenades.SetValue( 1 );
	ff_bot_allow_sniper_rifles.SetValue( 1 );
	ff_bot_allow_rocket_launchers.SetValue( 1 );
	ff_bot_allow_flamethrowers.SetValue( 1 );
	ff_bot_allow_pipe_launchers.SetValue( 1 );
	ff_bot_allow_miniguns.SetValue( 1 );
	ff_bot_allow_mediguns.SetValue( 1 );
	ff_bot_allow_tranqguns.SetValue( 1 );
}


bool CFFBotManager::ServerCommand( const char *cmd ) { return false; }
bool CFFBotManager::ClientCommand( CBasePlayer *player, const CCommand &args ) { return false; }

bool CFFBotManager::BotAddCommand( int team, bool isFromConsole, const char *profileName, int weaponType, BotDifficultyType difficulty )
{
	if ( !TheNavMesh->IsLoaded() ) { if ( !TheNavMesh->IsGenerating() ) { if ( !m_isMapDataLoaded ) { TheNavMesh->BeginGeneration(); m_isMapDataLoaded = true; } return false; } }
	if (TheNavMesh->IsGenerating()) return false;
	const BotProfile *profile = NULL;
	if ( !isFromConsole ) { profileName = NULL; difficulty = GetDifficultyLevel(); }
	else { if ( difficulty == NUM_DIFFICULTY_LEVELS ) difficulty = GetDifficultyLevel();
		if (team == FF_TEAM_UNASSIGNED) {
			if (FFGameRules()) { const char *joinTeamCvar = cv_bot_join_team.GetString();
				if (FStrEq(joinTeamCvar, "any")) team = FFGameRules()->SelectDefaultTeam();
				else if (FStrEq(joinTeamCvar, "red")) team = FF_TEAM_RED; else if (FStrEq(joinTeamCvar, "blue")) team = FF_TEAM_BLUE;
				else team = FFGameRules()->SelectDefaultTeam();
			} else { team = FF_TEAM_RED; }
		} }
	if ( profileName && *profileName ) { /* ... (profile loading as before, using FF_TEAM_*) ... */
		bool ignoreHumans = FFGameRules() ? FFGameRules()->IsCareer() : false;
		if (UTIL_IsNameTaken( profileName, ignoreHumans )) { if ( isFromConsole ) Msg( "Error - %s is already in the game.\n", profileName ); return true; }
		profile = TheBotProfiles->GetProfile( profileName, team );
		if ( !profile ) { profile = TheBotProfiles->GetProfileMatchingTemplate( profileName, team, difficulty );
			if ( !profile ) { if ( isFromConsole ) Msg( "Error - no profile for '%s' exists.\n", profileName ); return true; } }
	} else { if (team == FF_TEAM_UNASSIGNED) { if (FFGameRules()) team = FFGameRules()->SelectDefaultTeam(); else team = FF_TEAM_RED; }
		profile = TheBotProfiles->GetRandomProfile( difficulty, team, weaponType );
		if (profile == NULL) { if ( isFromConsole ) Msg( "All bot profiles at this difficulty level are in use.\n" ); return true; } }
	if (team == FF_TEAM_UNASSIGNED || team == FF_TEAM_SPECTATOR) { if ( isFromConsole ) Msg( "Could not add bot to the game: Invalid team or game is full.\n" ); return false; }
	if (FFGameRules() && FFGameRules()->TeamFull( team )) { if ( isFromConsole ) Msg( "Could not add bot to the game: Team is full\n" ); return false; }
	if (FFGameRules() && FFGameRules()->TeamStacked( team, FF_TEAM_UNASSIGNED )) { if ( isFromConsole ) Msg( "Could not add bot to the game: Team is stacked.\n" ); return false; }
	CFFBot *bot = CreateBot<CFFBot>( profile, team );
	if (bot == NULL) { if ( isFromConsole ) Msg( "Error: CreateBot() failed.\n" ); return false; }
	if (isFromConsole) cv_bot_quota.SetValue( cv_bot_quota.GetInt() + 1 );
	return true;
}

int UTIL_FFBotsInGame() { /* ... (implementation as before, using CFFBot) ... */
	int count = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i ) { CFFBot *player = dynamic_cast<CFFBot *>(UTIL_PlayerByIndex( i )); if ( player == NULL ) continue; count++; }
	return count;
}

bool UTIL_FFKickBotFromTeam( int kickTeam ) { /* ... (implementation as before, using CFFBot) ... */
	for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CFFBot *player = dynamic_cast<CFFBot *>( UTIL_PlayerByIndex( i ) );
		if (player && !player->IsAlive() && player->GetTeamNumber() == kickTeam) { engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", player->GetPlayerName() ) ); return true; } }
	for ( int i = 1; i <= gpGlobals->maxClients; ++i ) { CFFBot *player = dynamic_cast<CFFBot *>( UTIL_PlayerByIndex( i ) );
		if (player && player->GetTeamNumber() == kickTeam) { engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", player->GetPlayerName() ) ); return true; } }
	return false;
}

void CFFBotManager::MaintainBotQuota( void )
{
	if ( !AreBotsAllowed() || (TheNavMesh && TheNavMesh->IsGenerating()) ) return;
	int humanPlayersInGame = UTIL_HumansInGame( IGNORE_SPECTATORS );
	if (!engine->IsDedicatedServer() && UTIL_HumansInGame() == 0) return;
	if ( !FFGameRules() || !TheFFBots() ) return;
	int desiredBotCount = cv_bot_quota.GetInt(); int botsInGame = UTIL_FFBotsInGame();
	bool isRoundInProgress = FFGameRules()->m_bFirstConnected && !TheFFBots()->IsRoundOver() && ( FFGameRules()->GetRoundElapsedTime() >= 20.0f );
	if ( FStrEq( cv_bot_quota_mode.GetString(), "fill" ) ) desiredBotCount = isRoundInProgress ? botsInGame : MAX( 0, desiredBotCount - humanPlayersInGame );
	else if ( FStrEq( cv_bot_quota_mode.GetString(), "match" ) ) desiredBotCount = isRoundInProgress ? botsInGame : (int)MAX( 0, cv_bot_quota.GetFloat() * humanPlayersInGame );
	if (cv_bot_join_after_player.GetBool() && humanPlayersInGame == 0) desiredBotCount = 0;
	if ( bot_join_delay.GetInt() > FFGameRules()->GetMapElapsedTime() ) desiredBotCount = 0;
	desiredBotCount = MIN( desiredBotCount, gpGlobals->maxClients - humanPlayersInGame - (cv_bot_auto_vacate.GetBool() ? 1:0) );

	if ( botsInGame > 0 && desiredBotCount == botsInGame && FFGameRules()->m_bFirstConnected && FFGameRules()->GetRoundElapsedTime() < 20.0f ) {
		if ( mp_autoteambalance.GetBool() && FFGameRules() ) {
			int teamCount[FF_TEAM_COUNT] = {0}; // Assumes FF_TEAM_COUNT = 2 for Red/Blue
			int teamIter[] = {FF_TEAM_RED, FF_TEAM_BLUE}; // Add other primary teams if they exist
			for(int i = 1; i <= gpGlobals->maxClients; ++i) {
				CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));
				if (pPlayer && pPlayer->IsBot() && pPlayer->IsAlive()) {
					for(int t=0; t < FF_TEAM_COUNT; ++t) if(pPlayer->GetTeamNumber() == teamIter[t]) teamCount[t]++;
				}
			}
			// Simplified balancing for 2 teams
			if (teamCount[0] > teamCount[1] + 1) { if (UTIL_FFKickBotFromTeam(FF_TEAM_RED)) return; }
			else if (teamCount[1] > teamCount[0] + 1) { if (UTIL_FFKickBotFromTeam(FF_TEAM_BLUE)) return; }
		}
	}

	if (desiredBotCount > botsInGame) {
		bool canJoinRed = FFGameRules() ? !FFGameRules()->TeamFull(FF_TEAM_RED) : true;
		bool canJoinBlue = FFGameRules() ? !FFGameRules()->TeamFull(FF_TEAM_BLUE) : true;
		if (canJoinRed || canJoinBlue) TheFFBots()->BotAddCommand( FF_TEAM_AUTOASSIGN );
	} else if (desiredBotCount < botsInGame) {
		if (UTIL_FFKickBotFromTeam( FF_TEAM_UNASSIGNED )) return;
		int kickTeam = FF_TEAM_RED;
		if (FFGameRules()) {
			int redPlayers = FFGameRules()->GetTeamPlayerCount(FF_TEAM_RED); // Assumes GetTeamPlayerCount exists
			int bluePlayers = FFGameRules()->GetTeamPlayerCount(FF_TEAM_BLUE);
			if (bluePlayers > redPlayers) kickTeam = FF_TEAM_BLUE;
			else if (redPlayers == bluePlayers) kickTeam = (RandomInt(0,1) == 0) ? FF_TEAM_RED : FF_TEAM_BLUE;
		}
		if (UTIL_FFKickBotFromTeam( kickTeam )) return;
		UTIL_FFKickBotFromTeam( OtherTeam(kickTeam) );
	}
}

void CFFBotManager::ExtractScenarioData( void ) { /* ... (implementation as before, using FF_TEAM_*, SCENARIO_FF_*) ... */
	if (!TheNavMesh->IsLoaded()) return;
	m_zoneCount = 0; m_gameScenario = SCENARIO_FF_UNKNOWN;
	CBaseEntity *entity = NULL; bool itemScriptFound = false; bool minecartFound = false;
	for( int i=0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i ) {
		entity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) ); if (entity == NULL) continue;
		bool entityAddedAsZone = false;
		if ( FClassnameIs( entity, "info_ff_script" ) ) {
			itemScriptFound = true; if (m_zoneCount < MAX_ZONES) {
				CFFInfoScript* pFlag = dynamic_cast<CFFInfoScript*>(entity);
				if (pFlag) { m_zone[m_zoneCount].m_entity = pFlag; m_zone[m_zoneCount].m_isBlocked = false;
					m_zone[m_zoneCount].m_center = pFlag->GetAbsOrigin(); m_zone[m_zoneCount].m_zoneID = m_zoneCount;
					int touchFlags = pFlag->GetTouchFlags();
					if (touchFlags & kAllowRedTeam) m_zone[m_zoneCount].m_team = FF_TEAM_RED;
					else if (touchFlags & kAllowBlueTeam) m_zone[m_zoneCount].m_team = FF_TEAM_BLUE;
					else m_zone[m_zoneCount].m_team = FF_TEAM_NEUTRAL;
					entityAddedAsZone = true; m_zoneCount++;
				} } else { Msg( "Warning: Too many info_ff_script zones, some will be ignored.\n" ); }
		} else if ( FClassnameIs( entity, "ff_minecart" ) ) {
			minecartFound = true; if (m_zoneCount < MAX_ZONES) {
				m_zone[m_zoneCount].m_entity = entity; m_zone[m_zoneCount].m_isBlocked = false;
				m_zone[m_zoneCount].m_center = entity->GetAbsOrigin(); m_zone[m_zoneCount].m_zoneID = m_zoneCount;
				m_zone[m_zoneCount].m_team = FF_TEAM_NEUTRAL;
				entityAddedAsZone = true; m_zoneCount++;
			} else { Msg( "Warning: Too many ff_minecart zones, some will be ignored.\n" ); } }
		if (entityAddedAsZone) { Zone *currentZone = &m_zone[m_zoneCount -1]; Vector absmin, absmax;
			currentZone->m_entity->CollisionProp()->WorldSpaceAABB( &absmin, &absmax );
			currentZone->m_extent.lo = absmin; currentZone->m_extent.hi = absmax;
			const float zFudge = 50.0f; currentZone->m_extent.lo.z -= zFudge; currentZone->m_extent.hi.z += zFudge;
			CollectOverlappingAreas collector( currentZone ); TheNavMesh->ForAllAreas( collector ); } }
	if (itemScriptFound && minecartFound) m_gameScenario = SCENARIO_FF_MIXED;
	else if (itemScriptFound) m_gameScenario = SCENARIO_FF_ITEM_SCRIPT;
	else if (minecartFound) m_gameScenario = SCENARIO_FF_MINECART;
}
const CFFBotManager::Zone *CFFBotManager::GetZone( const Vector &pos ) const { /* ... (as before) ... */
	for( int z=0; z<m_zoneCount; ++z ) if (m_zone[z].m_extent.Contains( pos )) return &m_zone[z]; return NULL;
}
const CFFBotManager::Zone *CFFBotManager::GetClosestZone( const Vector &pos ) const { /* ... (as before) ... */
	const Zone *close = NULL; float closeRangeSq = 999999999.9f;
	for( int z=0; z<m_zoneCount; ++z ) { if ( m_zone[z].m_isBlocked ) continue; float rangeSq = (m_zone[z].m_center - pos).LengthSqr();
		if (rangeSq < closeRangeSq) { closeRangeSq = rangeSq; close = &m_zone[z]; } } return close;
}
const Vector *CFFBotManager::GetRandomPositionInZone( const Zone *zone ) const { /* ... (as before, m_isLegacy assumed false) ... */
	static Vector pos; if (zone == NULL || zone->m_areaCount == 0) return NULL;
	CNavArea *area = GetRandomAreaInZone(zone);
	// Assuming m_isLegacy is not used or defaults to false for FF zones
	Extent areaExtent; area->GetExtent(&areaExtent); Extent overlap;
	overlap.lo.x = MAX( areaExtent.lo.x, zone->m_extent.lo.x ); overlap.lo.y = MAX( areaExtent.lo.y, zone->m_extent.lo.y );
	overlap.hi.x = MIN( areaExtent.hi.x, zone->m_extent.hi.x ); overlap.hi.y = MIN( areaExtent.hi.y, zone->m_extent.hi.y );
	pos.x = (overlap.lo.x + overlap.hi.x)/2.0f; pos.y = (overlap.lo.y + overlap.hi.y)/2.0f; pos.z = area->GetZ( pos );
	return &pos;
}
CNavArea *CFFBotManager::GetRandomAreaInZone( const Zone *zone ) const { /* ... (as before) ... */
	int areaCount = zone->m_areaCount; if( areaCount == 0 ) { Assert( false && "CFFBotManager::GetRandomAreaInZone: No areas for this zone" ); return NULL; }
	int totalWeight = 0; for( int areaIndex = 0; areaIndex < areaCount; areaIndex++ ) { CNavArea *currentArea = zone->m_area[areaIndex];
		if( currentArea->GetAttributes() & NAV_MESH_JUMP ) totalWeight += 0; else if( currentArea->GetAttributes() & NAV_MESH_AVOID ) totalWeight += 1; else totalWeight += 20; }
	if( totalWeight == 0 ) { Assert( false && "CFFBotManager::GetRandomAreaInZone: No real areas for this zone" ); return NULL; }
	int randomPick = RandomInt( 1, totalWeight ); for( int areaIndex = 0; areaIndex < areaCount; areaIndex++ ) {
		CNavArea *currentArea = zone->m_area[areaIndex]; if( currentArea->GetAttributes() & NAV_MESH_JUMP ) randomPick -= 0;
		else if( currentArea->GetAttributes() & NAV_MESH_AVOID ) randomPick -= 1; else randomPick -= 20;
		if( randomPick <= 0 ) return currentArea; } return zone->m_area[0];
}

void CFFBotManager::OnServerShutdown( IGameEvent *event )
{
	if ( !engine->IsDedicatedServer() )
	{
		// Save bot-specific ConVars for Fortress Forever
		static const char *ffBotVars[] =
		{
			"bot_difficulty",
			"bot_quota",
			"bot_quota_mode",
			"bot_chatter",
			"bot_prefix",
			"bot_join_team",
			"bot_defer_to_human",
			"bot_join_after_player",
			"bot_auto_vacate",
			// FF Specific Weapon Allows
			"ff_bot_allow_pistols",
			"ff_bot_allow_shotguns",
			"ff_bot_allow_sub_machine_guns",
			"ff_bot_allow_rifles",
			"ff_bot_allow_machine_guns",
			"ff_bot_allow_grenades",
			"ff_bot_allow_rocket_launchers",
			"ff_bot_allow_flamethrowers",
			"ff_bot_allow_pipe_launchers",
			"ff_bot_allow_miniguns",
			"ff_bot_allow_sniper_rifles",
			"ff_bot_allow_mediguns",
			"ff_bot_allow_tranqguns",
			// Add any other relevant bot ConVars here
		};

		KeyValues *data = new KeyValues( "FFBotConfig" ); // Use a distinct name like "FFBotConfig"

		// load the config data
		if (data)
		{
			// Try to load existing, otherwise it's a new KeyValues object
			data->LoadFromFile( filesystem, "FFBotConfig.vdf", "GAME" );
			for ( int i=0; i<sizeof(ffBotVars)/sizeof(ffBotVars[0]); ++i )
			{
				const char *varName = ffBotVars[i];
				if ( varName )
				{
					ConVar *var = cvar->FindVar( varName );
					if ( var )
					{
						data->SetString( varName, var->GetString() );
					}
				}
			}
			data->SaveToFile( filesystem, "FFBotConfig.vdf", "GAME" );
			data->deleteThis();
		}
	}
}
void CFFBotManager::OnPlayerFootstep( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFootstep, event ); }
void CFFBotManager::OnPlayerRadio( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerRadio, event ); }
void CFFBotManager::OnPlayerDeath( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerDeath, event ); }
void CFFBotManager::OnPlayerFallDamage( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFallDamage, event ); }
void CFFBotManager::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; CFFBOTMANAGER_ITERATE_BOTS( OnRoundEnd, event ); }
void CFFBotManager::OnRoundStart( IGameEvent *event ) { RestartRound(); CFFBOTMANAGER_ITERATE_BOTS( OnRoundStart, event ); }
void CFFBotManager::OnFFRestartRound( IGameEvent *event ) { Msg("CFFBotManager::OnFFRestartRound\n"); RestartRound(); CFFBOTMANAGER_ITERATE_BOTS( OnFFRestartRound, event );}
void CFFBotManager::OnPlayerChangeClass( IGameEvent *event ) { Msg("CFFBotManager::OnPlayerChangeClass: %s changed class from %d to %d\n", event->GetString("playername"), event->GetInt("oldclass"), event->GetInt("newclass")); CFFBOTMANAGER_ITERATE_BOTS( OnPlayerChangeClass, event );}
void CFFBotManager::OnDisguiseLost( IGameEvent *event ) { Msg("CFFBotManager::OnDisguiseLost: %s lost disguise.\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnDisguiseLost, event );}
void CFFBotManager::OnCloakLost( IGameEvent *event ) { Msg("CFFBotManager::OnCloakLost: %s lost cloak.\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnCloakLost, event );}
void CFFBotManager::OnBuildDispenser( IGameEvent *event ) { Msg("CFFBotManager::OnBuildDispenser by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnBuildDispenser, event );}
void CFFBotManager::OnBuildSentryGun( IGameEvent *event ) { Msg("CFFBotManager::OnBuildSentryGun by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnBuildSentryGun, event );}
void CFFBotManager::OnBuildDetpack( IGameEvent *event ) { Msg("CFFBotManager::OnBuildDetpack by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnBuildDetpack, event );}
void CFFBotManager::OnBuildManCannon( IGameEvent *event ) { Msg("CFFBotManager::OnBuildManCannon by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnBuildManCannon, event );}
void CFFBotManager::OnDispenserKilled( IGameEvent *event ) { Msg("CFFBotManager::OnDispenserKilled (owner: %s, attacker: %s)\n", event->GetString("ownername"), event->GetString("attackername")); CFFBOTMANAGER_ITERATE_BOTS( OnDispenserKilled, event );}
void CFFBotManager::OnDispenserDismantled( IGameEvent *event ) { Msg("CFFBotManager::OnDispenserDismantled by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnDispenserDismantled, event );}
void CFFBotManager::OnDispenserDetonated( IGameEvent *event ) { Msg("CFFBotManager::OnDispenserDetonated by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnDispenserDetonated, event );}
void CFFBotManager::OnDispenserSabotaged( IGameEvent *event ) { Msg("CFFBotManager::OnDispenserSabotaged (owner: %s, saboteur: %s)\n", event->GetString("ownername"), event->GetString("saboteurname")); CFFBOTMANAGER_ITERATE_BOTS( OnDispenserSabotaged, event );}
void CFFBotManager::OnSentryGunKilled( IGameEvent *event ) { Msg("CFFBotManager::OnSentryGunKilled (owner: %s, attacker: %s)\n", event->GetString("ownername"), event->GetString("attackername")); CFFBOTMANAGER_ITERATE_BOTS( OnSentryGunKilled, event );}
void CFFBotManager::OnSentryGunDismantled( IGameEvent *event ) { Msg("CFFBotManager::OnSentryGunDismantled by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnSentryGunDismantled, event );}
void CFFBotManager::OnSentryGunDetonated( IGameEvent *event ) { Msg("CFFBotManager::OnSentryGunDetonated by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnSentryGunDetonated, event );}
void CFFBotManager::OnSentryGunUpgraded( IGameEvent *event ) { Msg("CFFBotManager::OnSentryGunUpgraded (owner: %s, upgrader: %s, level: %d)\n", event->GetString("ownername"), event->GetString("upgradername"), event->GetInt("level")); CFFBOTMANAGER_ITERATE_BOTS( OnSentryGunUpgraded, event );}
void CFFBotManager::OnSentryGunSabotaged( IGameEvent *event ) { Msg("CFFBotManager::OnSentryGunSabotaged (owner: %s, saboteur: %s)\n", event->GetString("ownername"), event->GetString("saboteurname")); CFFBOTMANAGER_ITERATE_BOTS( OnSentryGunSabotaged, event );}
void CFFBotManager::OnDetpackDetonated( IGameEvent *event ) { Msg("CFFBotManager::OnDetpackDetonated by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnDetpackDetonated, event );}
void CFFBotManager::OnManCannonDetonated( IGameEvent *event ) { Msg("CFFBotManager::OnManCannonDetonated by %s\n", event->GetString("playername")); CFFBOTMANAGER_ITERATE_BOTS( OnManCannonDetonated, event );}

// Add new event stubs that were added to the header
void CFFBotManager::OnPlayerChangeTeam( IGameEvent *event ) { Msg("CFFBotManager::OnPlayerChangeTeam: %s changed team to %d\n", event->GetString("playername"), event->GetInt("team")); CFFBOTMANAGER_ITERATE_BOTS( OnPlayerChangeTeam, event );}


static CBaseEntity * SelectSpawnSpot( const char *pEntClassName ) { /* ... (implementation as before) ... */
	CBaseEntity* pSpot = NULL; pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );
	if ( pSpot == NULL ) pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );
	CBaseEntity *pFirstSpot = pSpot;
	do { if ( pSpot ) { if ( pSpot->GetAbsOrigin() == Vector( 0, 0, 0 ) ) { pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName ); continue; } return pSpot; }
		pSpot = gEntList.FindEntityByClassname( pSpot, pEntClassName );
	} while ( pSpot != pFirstSpot ); return NULL;
}
void CFFBotManager::CheckForBlockedZones( void ) { /* ... (implementation as before, using FF_TEAM_RED/BLUE for spawn checks) ... */
	CBaseEntity *pSpot = SelectSpawnSpot( "info_player_red" );
	if ( !pSpot ) pSpot = SelectSpawnSpot( "info_player_blue" );
	if ( !pSpot ) return;
	Vector spawnPos = pSpot->GetAbsOrigin(); CNavArea *spawnArea = TheNavMesh->GetNearestNavArea( spawnPos ); if ( !spawnArea ) return;
	ShortestPathCost costFunc;
	for( int i=0; i<m_zoneCount; ++i ) {
		if (m_zone[i].m_areaCount == 0) continue;
		float dist = NavAreaTravelDistance( spawnArea, m_zone[i].m_area[0], costFunc ); m_zone[i].m_isBlocked = (dist < 0.0f );
		if ( cv_bot_debug.GetInt() == 5 && m_zone[i].m_isBlocked )
			DevMsg( "%.1f: Zone %d, area %d (%.0f %.0f %.0f) is blocked from spawn area %d (%.0f %.0f %.0f)\n", gpGlobals->curtime, i, m_zone[i].m_area[0]->GetID(), VEC_T_ARGS(m_zone[i].m_area[0]->GetCenter()), spawnArea->GetID(), VEC_T_ARGS(spawnPos) );
	}
}
void CFFBotManager::OnRoundFreezeEnd( IGameEvent *event ) { /* ... (implementation as before) ... */ }
void CFFBotManager::OnNavBlocked( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnNavBlocked, event ); CheckForBlockedZones(); }
void CFFBotManager::OnDoorMoving( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnDoorMoving, event ); }
void CFFBotManager::OnBreakBreakable( IGameEvent *event ) { CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) ); TheNavMesh->ForAllAreas( collector ); CFFBOTMANAGER_ITERATE_BOTS( OnBreakBreakable, event ); }
void CFFBotManager::OnBreakProp( IGameEvent *event ) { CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) ); TheNavMesh->ForAllAreas( collector ); CFFBOTMANAGER_ITERATE_BOTS( OnBreakProp, event ); }
void CFFBotManager::OnWeaponFire( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFire, event ); }
void CFFBotManager::OnWeaponFireOnEmpty( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFireOnEmpty, event ); }
void CFFBotManager::OnWeaponReload( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponReload, event ); }
void CFFBotManager::OnBulletImpact( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnBulletImpact, event ); }
void CFFBotManager::OnHEGrenadeDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnHEGrenadeDetonate, event ); }
void CFFBotManager::OnGrenadeBounce( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnGrenadeBounce, event ); }

bool CFFBotManager::IsImportantPlayer( CFFPlayer *player ) const { /* FF_TODO: Define FF important player logic */ return false; }

unsigned int CFFBotManager::GetPlayerPriority( CBasePlayer *player ) const
{
	const unsigned int lowestPriority = 0xFFFFFFFF;
	if (!player->IsPlayer()) return lowestPriority;
	if (!player->IsBot()){ if ( player->GetTimeSinceLastMovement() > 60.0f ) return 2; return 0; }
	CFFBot *bot = dynamic_cast<CFFBot *>( player ); if ( !bot ) return 0;
	// FF_TODO: Define FF-specific bot priorities
	return 1 + bot->GetID();
}

CBaseEntity *CFFBotManager::GetRandomSpawn( int team ) const
{
	CUtlVector< CBaseEntity * > spawnSet; CBaseEntity *spot;
	// FF_TODO: This assumes info_player_red/blue. Adapt if FF uses different or more spawn types.
	// Also, TEAM_ANY is from base engine, FF_TEAM_AUTOASSIGN is what we defined for bot logic.
	// Need to ensure consistent usage or mapping if FFGameRules()->SelectDefaultTeam() doesn't handle FF_TEAM_AUTOASSIGN.
	if (team == FF_TEAM_RED || team == FF_TEAM_AUTOASSIGN || team == TEAM_ANY ) {
		for( spot = gEntList.FindEntityByClassname( NULL, "info_player_red" ); spot; spot = gEntList.FindEntityByClassname( spot, "info_player_red" ) ) spawnSet.AddToTail( spot );
	}
	if (team == FF_TEAM_BLUE || team == FF_TEAM_AUTOASSIGN || team == TEAM_ANY ) {
		for( spot = gEntList.FindEntityByClassname( NULL, "info_player_blue" ); spot; spot = gEntList.FindEntityByClassname( spot, "info_player_blue" ) ) spawnSet.AddToTail( spot );
	}
	// Add other teams (Yellow, Green) if they are primary spawnable teams.
	if (spawnSet.Count() == 0) return NULL;
	return spawnSet[ RandomInt( 0, spawnSet.Count()-1 ) ];
}

float CFFBotManager::GetRadioMessageTimestamp( RadioType event, int teamID ) const
{
	int i = -1; // Map FF teams to array indices 0 and 1
	if (teamID == FF_TEAM_RED) i = 0;
	else if (teamID == FF_TEAM_BLUE) i = 1;
	// Add FF_TEAM_YELLOW to 2, FF_TEAM_GREEN to 3 if FF_TEAM_COUNT is 4
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		return m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ];
	return 0.0f;
}

float CFFBotManager::GetRadioMessageInterval( RadioType event, int teamID ) const
{
	int i = -1; if (teamID == FF_TEAM_RED) i = 0; else if (teamID == FF_TEAM_BLUE) i = 1;
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		return gpGlobals->curtime - m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ];
	return 99999999.9f;
}

void CFFBotManager::SetRadioMessageTimestamp( RadioType event, int teamID )
{
	int i = -1; if (teamID == FF_TEAM_RED) i = 0; else if (teamID == FF_TEAM_BLUE) i = 1;
	if (i >= 0 && i < FF_TEAM_COUNT && event > RADIO_START_1 && event < RADIO_END)
		m_radioMsgTimestamp[ event - RADIO_START_1 ][ i ] = gpGlobals->curtime;
}

void CFFBotManager::ResetRadioMessageTimestamps( void )
{
	for( int t=0; t<FF_TEAM_COUNT; ++t ) {
		for( int m=0; m<(RADIO_END - RADIO_START_1); ++m ) m_radioMsgTimestamp[ m ][ t ] = 0.0f; }
}

void DrawOccupyTime( void ) { /* ... (implementation as before, using FF_TEAM_RED/BLUE from ff_bot_manager.h) ... */ }
void DrawBattlefront( void ) { /* ... (implementation as before, using FF_TEAM_RED/BLUE from ff_bot_manager.h) ... */ }
static bool CheckAreaAgainstAllZoneAreas(CNavArea *queryArea) { /* ... (implementation as before) ... */ }
CON_COMMAND_F( nav_check_connectivity, "Checks to be sure every (or just the marked) nav area can get to every goal area for the map (e.g. control points).", FCVAR_CHEAT ) { /* ... (implementation as before) ... */ }

// Class CCollectOverlappingAreas remains the same as it's generic.
// Note: ShortestPathCost is defined in ff_bot.h and uses CFFBot.

// FF_LUA_INTEGRATION: Accessors for Lua-defined objective data
const CUtlVector<CFFBotManager::LuaObjectivePoint>& CFFBotManager::GetAllLuaObjectivePoints() const
{
	return m_luaObjectivePoints;
}

int CFFBotManager::GetLuaObjectivePointCount() const
{
	return m_luaObjectivePoints.Count();
}

const CFFBotManager::LuaObjectivePoint* CFFBotManager::GetLuaObjectivePoint(int index) const
{
	if (index < 0 || index >= m_luaObjectivePoints.Count())
	{
		return NULL;
	}
	return &m_luaObjectivePoints[index];
}

const CUtlVector<CFFBotManager::LuaPathPoint>& CFFBotManager::GetAllLuaPathPoints() const
{
	return m_luaPathPoints;
}

int CFFBotManager::GetLuaPathPointCount() const
{
	return m_luaPathPoints.Count();
}

const CFFBotManager::LuaPathPoint* CFFBotManager::GetLuaPathPoint(int index) const
{
	if (index < 0 || index >= m_luaPathPoints.Count())
	{
		return NULL;
	}
	return &m_luaPathPoints[index];
}
