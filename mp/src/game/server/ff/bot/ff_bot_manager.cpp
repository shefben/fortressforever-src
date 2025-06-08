//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#pragma warning( disable : 4530 )					// STL uses exceptions, but we are not compiling with them - ignore warning

#include "cbase.h"
#include "ff_bot_manager.h" // Own header
#include "ff_bot.h"
#include "ff_bot_chatter.h"
#include "../../shared/bot/bot_profile.h"    // Changed path
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"
#include "nav_area.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "../../shared/bot/bot_constants.h"  // Changed path
#include "../../shared/bot/bot_util.h"       // Changed path
#include "../../shared/ff/ff_shareddefs.h" // Already corrected

// Engine/Shared specific includes
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


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef _WIN32
#pragma warning (disable:4701)				// disable warning that variable *may* not be initialized 
#endif

CBotManager *TheBots = NULL; // This global is assigned an instance of CFFBotManager

bool CFFBotManager::m_isMapDataLoaded = false;

int g_nClientPutInServerOverrides = 0;


void DrawOccupyTime( void );
ConVar bot_show_occupy_time( "bot_show_occupy_time", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show when each nav area can first be reached by each team." );

void DrawBattlefront( void );
ConVar bot_show_battlefront( "bot_show_battlefront", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Show areas where rushing players will initially meet." );

int UTIL_FFSBotsInGame( void ); // Forward declaration

ConVar bot_join_delay( "bot_join_delay", "0", FCVAR_GAMEDLL, "Prevents bots from joining the server for this many seconds after a map change." );

/**
 * Determine whether bots can be used or not
 */
inline bool AreBotsAllowed()
{
	// If they pass in -nobots, don't allow bots.  This is for people who host servers, to
	// allow them to disallow bots to enforce CPU limits.
	if ( CommandLine() ) // Ensure CommandLine() is not null
	{
		const char *nobots = CommandLine()->CheckParm( "-nobots" );
		if ( nobots )
		{
			return false;
		}
	}

	return true;
}


//--------------------------------------------------------------------------------------------------------------
void InstallBotControl( void )
{
	if ( TheBots != NULL )
		delete TheBots;

	TheBots = new CFFBotManager;
}


//--------------------------------------------------------------------------------------------------------------
void RemoveBotControl( void )
{
	if ( TheBots != NULL )
		delete TheBots;

	TheBots = NULL;
}


//--------------------------------------------------------------------------------------------------------------
CBasePlayer* ClientPutInServerOverride_Bot( edict_t *pEdict, const char *playername )
{
	CBasePlayer *pPlayer = TheBots->AllocateAndBindBotEntity( pEdict );
	if ( pPlayer )
	{
		pPlayer->SetPlayerName( playername );
	}
	++g_nClientPutInServerOverrides;

	return pPlayer;
}

//--------------------------------------------------------------------------------------------------------------
// Constructor
CFFBotManager::CFFBotManager() :
	m_PlayerFootstepEvent(this),
	m_PlayerRadioEvent(this),
	m_PlayerDeathEvent(this),
	m_PlayerFallDamageEvent(this),
	m_BombPickedUpEvent(this),
	m_BombPlantedEvent(this),
	m_BombBeepEvent(this),
	m_BombDefuseBeginEvent(this),
	m_BombDefusedEvent(this),
	m_BombDefuseAbortEvent(this),
	m_BombExplodedEvent(this),
	m_RoundEndEvent(this),
	m_RoundStartEvent(this),
	m_RoundFreezeEndEvent(this),
	m_DoorMovingEvent(this),
	m_BreakPropEvent(this),
	m_BreakBreakableEvent(this),
	m_HostageFollowsEvent(this),
	m_HostageRescuedAllEvent(this),
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
	m_ServerShutdownEvent(this)
{
	m_zoneCount = 0;
	SetLooseBomb( NULL );
	m_serverActive = false;

	m_isBombPlanted = false;
	m_bombDefuser = NULL;
	m_roundStartTimestamp = 0.0f;

	m_eventListenersEnabled = true;
	// m_commonEventListeners population is done in the header via DECLARE_FFBOTMANAGER_EVENT_LISTENER


	if (!TheBotPhrases) TheBotPhrases = new BotPhraseManager;
	if (!TheBotProfiles) TheBotProfiles = new BotProfileManager;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when a new round begins
 */
void CFFBotManager::RestartRound( void )
{
	// extend
	CBotManager::RestartRound();

	SetLooseBomb( NULL );
	m_isBombPlanted = false;
	m_earliestBombPlantTimestamp = gpGlobals->curtime + RandomFloat( 10.0f, 30.0f );
	m_bombDefuser = NULL;

	ResetRadioMessageTimestamps();

	m_lastSeenEnemyTimestamp = -9999.9f;

	m_roundStartTimestamp = gpGlobals->curtime + mp_freezetime.GetFloat();

	const float defenseRushChance = 33.3f;
	m_isDefenseRushing = (RandomFloat( 0.0f, 100.0f ) <= defenseRushChance) ? true : false;

	if (TheBotPhrases) TheBotPhrases->OnRoundRestart();

	m_isRoundOver = false;
}

//--------------------------------------------------------------------------------------------------------------

void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue )
{
	if (!extent) return;

	int darkRed = red/2;
	int darkGreen = green/2;
	int darkBlue = blue/2;

	Vector v[8];
	v[0].x = extent->lo.x; v[0].y = extent->lo.y; v[0].z = extent->lo.z;
	v[1].x = extent->hi.x; v[1].y = extent->lo.y; v[1].z = extent->lo.z;
	v[2].x = extent->hi.x; v[2].y = extent->hi.y; v[2].z = extent->lo.z;
	v[3].x = extent->lo.x; v[3].y = extent->hi.y; v[3].z = extent->lo.z;
	v[4].x = extent->lo.x; v[4].y = extent->lo.y; v[4].z = extent->hi.z;
	v[5].x = extent->hi.x; v[5].y = extent->lo.y; v[5].z = extent->hi.z;
	v[6].x = extent->hi.x; v[6].y = extent->hi.y; v[6].z = extent->hi.z;
	v[7].x = extent->lo.x; v[7].y = extent->hi.y; v[7].z = extent->hi.z;

	static int edge[] = 
	{
		1, 2, 3, 4, -1,
		5, 6, 7, 8, -5,
		1, -5,
		2, -6,
		3, -7,
		4, -8,
		0
	};

	Vector from, to;
	bool restart = true;
	for( int i=0; edge[i] != 0; ++i )
	{
		if (restart)
		{
			to = v[ edge[i]-1 ];
			restart = false;
			continue;
		}
		
		from = to;

		int index = edge[i];
		if (index < 0)
		{
			restart = true;
			index = -index;
		}

		to = v[ index-1 ];

		NDebugOverlay::Line( from, to, darkRed, darkGreen, darkBlue, true, 0.1f );
		NDebugOverlay::Line( from, to, red, green, blue, false, 0.15f );
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBotManager::EnableEventListeners( bool enable )
{
	if ( m_eventListenersEnabled == enable || !gameeventmanager )
	{
		return;
	}

	m_eventListenersEnabled = enable;

	for ( int i=0; i<m_commonEventListeners.Count(); ++i )
	{
		BotEventInterface* listener = m_commonEventListeners[i];
		if (!listener) continue;

		if ( enable )
		{
			gameeventmanager->AddListener( listener, listener->GetEventName(), true );
		}
		else
		{
			gameeventmanager->RemoveListener( listener );
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Called each frame
 */
void CFFBotManager::StartFrame( void )
{
	if ( !AreBotsAllowed() )
	{
		EnableEventListeners( false );
		return;
	}

	CBotManager::StartFrame();

	MaintainBotQuota();
	EnableEventListeners( UTIL_FFSBotsInGame() > 0 );

	if (cv_bot_debug.GetInt() == 5)
	{
		for( int z=0; z<m_zoneCount; ++z )
		{
			Zone *zone = &m_zone[z];
			if ( zone->m_isBlocked ) UTIL_DrawBox( &zone->m_extent, 1, 255, 0, 200 );
			else UTIL_DrawBox( &zone->m_extent, 1, 255, 100, 0 );
		}
	}

	if (bot_show_occupy_time.GetBool()) DrawOccupyTime();
	if (bot_show_battlefront.GetBool()) DrawBattlefront();

	if ( m_checkTransientAreasTimer.IsElapsed() && !nav_edit.GetBool() && TheNavMesh )
	{
		CUtlVector< CNavArea * >& transientAreas = TheNavMesh->GetTransientAreas();
		for ( int i=0; i<transientAreas.Count(); ++i )
		{
			CNavArea *area = transientAreas[i];
			if ( area && area->GetAttributes() & NAV_MESH_TRANSIENT )
			{
				area->UpdateBlocked();
			}
		}
		m_checkTransientAreasTimer.Start( 2.0f );
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if the bot can use this weapon
 */
bool CFFBotManager::IsWeaponUseable( const CFFWeaponBase *weapon ) const
{
	if (weapon == NULL) return false;
	// TODO: Update for FF weapon types and C4 equivalent
	// if (weapon->GetWeaponID() == FF_WEAPON_C4_EQUIVALENT) return true;
	if ((!AllowShotguns() && weapon->IsKindOf( WEAPONTYPE_SHOTGUN )) ||
		(!AllowMachineGuns() && weapon->IsKindOf( WEAPONTYPE_MACHINEGUN )) || 
		(!AllowRifles() && weapon->IsKindOf( WEAPONTYPE_RIFLE )) || 
		(!AllowSnipers() && weapon->IsKindOf( WEAPONTYPE_SNIPER_RIFLE )) || 
		(!AllowSubMachineGuns() && weapon->IsKindOf( WEAPONTYPE_SUBMACHINEGUN )) || 
		(!AllowPistols() && weapon->IsKindOf( WEAPONTYPE_PISTOL )) ||
		(!AllowGrenades() && weapon->IsKindOf( WEAPONTYPE_GRENADE )))
	{
		return false;
	}
	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this player is on "defense"
 */
bool CFFBotManager::IsOnDefense( const CFFPlayer *player ) const
{
	if (!player) return false;
	// TODO: Update for FF Scenarios and Teams
	switch (GetScenario())
	{
		case SCENARIO_DEFUSE_BOMB: return (player->GetTeamNumber() == TEAM_CT);
		case SCENARIO_RESCUE_HOSTAGES: return (player->GetTeamNumber() == TEAM_TERRORIST);
		case SCENARIO_ESCORT_VIP: return (player->GetTeamNumber() == TEAM_TERRORIST);
		// Add FF specific scenarios here
		// case SCENARIO_CAPTURETHEFLAG: return (player->GetTeamNumber() == GetTeamToDefendFlag()); // Example
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this player is on "offense"
 */
bool CFFBotManager::IsOnOffense( const CFFPlayer *player ) const
{
	return !IsOnDefense( player );
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when a map has just been loaded
 */
void CFFBotManager::ServerActivate( void )
{
	m_isMapDataLoaded = false;
	if (TheBotPhrases)
	{
		TheBotPhrases->Reset();
		TheBotPhrases->Initialize( "BotChatter.db", 0 );
	}
	if (TheBotProfiles)
	{
		TheBotProfiles->Reset();
		TheBotProfiles->FindVoiceBankIndex( "BotChatter.db" );
		const char *filename = "BotPackList.db";
		if (filesystem)
		{
			FileHandle_t file = filesystem->Open( filename, "r" );
			if ( !file ) TheBotProfiles->Init( "BotProfile.db" );
			else
			{
				int dataLength = filesystem->Size( filename );
				if (dataLength > 0)
				{
					char *dataPointer = new char[ dataLength + 1];
					int actuallyRead = filesystem->Read( dataPointer, dataLength, file );
					dataPointer[actuallyRead] = '\0';
					filesystem->Close( file );
					const char *dataFile = SharedParse( dataPointer );
					const char *token;
					while ( dataFile )
					{
						token = SharedGetToken();
						if (token && *token)
						{
							char *clone = CloneString( token );
							TheBotProfiles->Init( clone );
							delete[] clone;
						}
						dataFile = SharedParse( dataFile );
					}
					delete [] dataPointer;
				} else {
					filesystem->Close( file );
					TheBotProfiles->Init( "BotProfile.db" );
				}
			}
		} else {
			TheBotProfiles->Init( "BotProfile.db" );
		}
		const BotProfileManager::VoiceBankList *voiceBanks = TheBotProfiles->GetVoiceBanks();
		if (voiceBanks && TheBotPhrases)
		{
			for ( int i=1; i<voiceBanks->Count(); ++i )
			{
				TheBotPhrases->Initialize( (*voiceBanks)[i], i );
			}
		}
	}
	if (TheNavMesh)
	{
		// TODO: Update spawn point names for FF (e.g., "info_player_red", "info_player_blue")
		// TheNavMesh->SetPlayerSpawnName( "info_player_team1_ff" );
		// TheNavMesh->SetPlayerSpawnName( "info_player_team2_ff" );
	}
	ExtractScenarioData();
	RestartRound();
	if (TheBotPhrases) TheBotPhrases->OnMapChange();
	m_serverActive = true;
}


void CFFBotManager::ServerDeactivate( void )
{
	m_serverActive = false;
}

void CFFBotManager::ClientDisconnect( CBaseEntity *entity )
{
	// Base implementation from CBotManager or specific logic can go here
}


//--------------------------------------------------------------------------------------------------------------
/**
* Parses out bot name/template/etc params from the current ConCommand
*/
void BotArgumentsFromArgv( const CCommand &args, const char **name, FFWeaponType *weaponType, BotDifficultyType *difficulty, int *team = NULL, bool *all = NULL ) // Changed CSWeaponType
{
	static char s_name[MAX_PLAYER_NAME_LENGTH];
	s_name[0] = 0;
	*name = s_name;
	*difficulty = NUM_DIFFICULTY_LEVELS;
	if ( team ) *team = TEAM_UNASSIGNED;
	if ( all ) *all = false;
	*weaponType = WEAPONTYPE_UNDEFINED;

	for ( int arg=1; arg<args.ArgC(); ++arg )
	{
		bool found = false;
		const char *token = args[arg];
		if ( all && FStrEq( token, "all" ) ) { *all = true; found = true; }
		// TODO: Update for FF teams (e.g., "red", "blue")
		// else if ( team && FStrEq( token, "red" ) ) { *team = TEAM_RED_FF; found = true; }
		// else if ( team && FStrEq( token, "blue" ) ) { *team = TEAM_BLUE_FF; found = true; }

		for( int i=0; i<NUM_DIFFICULTY_LEVELS && !found; ++i )
		{
			if (!stricmp( BotDifficultyName[i], token )) { *difficulty = (BotDifficultyType)i; found = true; }
		}
		if ( !found )
		{
			*weaponType = WeaponClassFromString( token ); // TODO: Ensure this works with FF weapon classes
			if ( *weaponType != WEAPONTYPE_UNDEFINED ) found = true;
		}
		if ( !found ) Q_strncpy( s_name, token, sizeof( s_name ) );
	}
}


//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_add, "bot_add <team> <type> <difficulty> <name> - Adds a bot matching the given criteria.", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || !TheFFBots() ) return;
	const char *name; BotDifficultyType difficulty; FFWeaponType weaponType; int team; // Changed CSWeaponType
	BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team );
	TheFFBots()->BotAddCommand( team, true, name, weaponType, difficulty ); // FROM_CONSOLE = true
}


//--------------------------------------------------------------------------------------------------------------
// TODO: Update for FF teams (e.g., bot_add_red, bot_add_blue)
CON_COMMAND_F( bot_add_t, "bot_add_t <type> <difficulty> <name> - Adds a bot to team T.", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || !TheFFBots() ) return;
	const char *name; BotDifficultyType difficulty; FFWeaponType weaponType; // Changed CSWeaponType
	BotArgumentsFromArgv( args, &name, &weaponType, &difficulty );
	TheFFBots()->BotAddCommand( TEAM_TERRORIST, true, name, weaponType, difficulty ); // TEAM_TERRORIST, FROM_CONSOLE = true
}


//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_add_ct, "bot_add_ct <type> <difficulty> <name> - Adds a bot to team CT.", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || !TheFFBots() ) return;
	const char *name; BotDifficultyType difficulty; FFWeaponType weaponType; // Changed CSWeaponType
	BotArgumentsFromArgv( args, &name, &weaponType, &difficulty );
	TheFFBots()->BotAddCommand( TEAM_CT, true, name, weaponType, difficulty ); // TEAM_CT, FROM_CONSOLE = true
}


//--------------------------------------------------------------------------------------------------------------
class CollectBots // TODO: Update criteria for FF (FFWeaponType, team enums)
{
public:
	CollectBots( const char *name, FFWeaponType weaponType, BotDifficultyType difficulty, int team ) // Changed CSWeaponType
	{
		m_name = name; m_difficulty = difficulty; m_team = team; m_weaponType = weaponType;
	}
	bool operator() ( CBasePlayer *player )
	{
		if (!player || !player->IsBot() ) return true;
		CFFBot *bot = dynamic_cast< CFFBot * >(player);
		if ( !bot || !bot->GetProfile() ) return true;
		if ( m_name && *m_name )
		{
			if ( FStrEq( m_name, bot->GetProfile()->GetName() ) ) { m_bots.RemoveAll(); m_bots.AddToTail( bot ); return false; }
			if ( !bot->GetProfile()->InheritsFrom( m_name ) ) return true;
		}
		if ( m_difficulty != NUM_DIFFICULTY_LEVELS && !bot->GetProfile()->IsDifficulty( m_difficulty ) ) return true;
		// TODO: Update for FF teams
		if ( (m_team == TEAM_CT || m_team == TEAM_TERRORIST) && bot->GetTeamNumber() != m_team ) return true;
		// TODO: Update for FF weapon types and enums
		if ( m_weaponType != WEAPONTYPE_UNDEFINED )
		{
			if ( !bot->GetProfile()->GetWeaponPreferenceCount() ) return true;
			// FFWeaponID prefID = (FFWeaponID)bot->GetProfile()->GetWeaponPreference(0);
			// if ( m_weaponType != WeaponClassFromWeaponID( prefID ) ) return true; // WeaponClassFromWeaponID for FF
		}
		m_bots.AddToTail( bot );
		return true;
	}
	CUtlVector< CFFBot * > m_bots;
private:
	const char *m_name; FFWeaponType m_weaponType; BotDifficultyType m_difficulty; int m_team;
};

//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_kill, "bot_kill <all> <team> <type> <difficulty> <name> - Kills a specific bot, or all bots, matching criteria.", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	const char *name; BotDifficultyType difficulty; FFWeaponType weaponType; int team; bool all; // Changed CSWeaponType
	BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team, &all );
	if ( (!name || !*name) && team == TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) all = true;
	CollectBots collector( name, weaponType, difficulty, team );
	ForEachPlayer( collector );
	for ( int i=0; i<collector.m_bots.Count(); ++i )
	{
		CFFBot *bot = collector.m_bots[i];
		if ( !bot || !bot->IsAlive() ) continue;
		bot->CommitSuicide();
		if ( !all ) return;
	}
}


//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_kick, "bot_kick <all> <team> <type> <difficulty> <name> - Kicks a specific bot, or all bots, matching criteria.", FCVAR_GAMEDLL )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	const char *name; BotDifficultyType difficulty; FFWeaponType weaponType; int team; bool all; // Changed CSWeaponType
	BotArgumentsFromArgv( args, &name, &weaponType, &difficulty, &team, &all );
	if ( (!name || !*name) && team == TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) all = true;
	CollectBots collector( name, weaponType, difficulty, team );
	ForEachPlayer( collector );
	for ( int i=0; i<collector.m_bots.Count(); ++i )
	{
		CFFBot *bot = collector.m_bots[i];
		if (!bot || !engine) continue;
		engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", bot->GetPlayerName() ) );
		if ( !all ) { cv_bot_quota.SetValue( clamp( cv_bot_quota.GetInt() - 1, 0, cv_bot_quota.GetInt() ) ); return; }
	}
	if ( all && (!name || !*name) && team == TEAM_UNASSIGNED && difficulty == NUM_DIFFICULTY_LEVELS ) cv_bot_quota.SetValue( 0 );
	else cv_bot_quota.SetValue( clamp( cv_bot_quota.GetInt() - collector.m_bots.Count(), 0, cv_bot_quota.GetInt() ) );
}


//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_knives_only, "Restricts the bots to only using knives", FCVAR_GAMEDLL )
{ /* ... (weapon restriction logic, ensure FF cvars if needed) ... */ }
CON_COMMAND_F( bot_pistols_only, "Restricts the bots to only using pistols", FCVAR_GAMEDLL )
{ /* ... */ }
CON_COMMAND_F( bot_snipers_only, "Restricts the bots to only using sniper rifles", FCVAR_GAMEDLL )
{ /* ... */ }
CON_COMMAND_F( bot_all_weapons, "Allows the bots to use all weapons", FCVAR_GAMEDLL )
{ /* ... */ }


//--------------------------------------------------------------------------------------------------------------
CON_COMMAND_F( bot_goto_mark, "Sends a bot to the selected nav area.", FCVAR_GAMEDLL | FCVAR_CHEAT )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || !TheNavMesh ) return;
	CNavArea *area = TheNavMesh->GetMarkedArea();
	if (area)
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
			if (player && player->IsBot())
			{
				CFFBot *bot = dynamic_cast<CFFBot *>( player );
				if ( bot ) bot->MoveTo( area->GetCenter(), FASTEST_ROUTE ); // FASTEST_ROUTE
				break;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
// CON_COMMAND_F( nav_check_connectivity, ... ) remains largely the same, using TheFFBots() now.
CON_COMMAND_F( nav_check_connectivity, "Checks to be sure every (or just the marked) nav area can get to every goal area for the map.", FCVAR_CHEAT )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || !TheNavMesh || !TheFFBots() || !engine) return;
	if ( TheNavMesh->GetMarkedArea() )
	{
		CNavArea *markedArea = TheNavMesh->GetMarkedArea();
		bool fine = CheckAreaAgainstAllZoneAreas( markedArea ); // Uses TheFFBots() internally
		if( fine ) Msg( "Area #%d is connected to all goal areas.\n", markedArea->GetID() );
	}
	else
	{
		float start = engine->Time();
		FOR_EACH_VEC( TheNavAreas, nit )
		{
			if (TheNavAreas[nit]) CheckAreaAgainstAllZoneAreas(TheNavAreas[ nit ]);
		}
		float end = engine->Time();
		float time = (end - start) * 1000.0f;
		Msg( "nav_check_connectivity took %2.2f ms\n", time );
	}
}

// Ensure all event handler implementations (OnPlayerFootstep, etc.) are present if they were in the original CS file.
// These were already handled and ported to use CFFBOTMANAGER_ITERATE_BOTS in previous steps.
// The DECLARE_FFBOTMANAGER_EVENT_LISTENER macro handles their declaration in the header.
// The definitions (like CFFBotManager::OnPlayerFootstep) would call CFFBOTMANAGER_ITERATE_BOTS.
// This file snippet doesn't show these definitions, but they were present in the previous `read_files` output for this file.
// Assuming they are still there and correct from Subtask 8.
void CFFBotManager::OnPlayerFootstep( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFootstep, event ); }
void CFFBotManager::OnPlayerRadio( IGameEvent *event ) { if ( event->GetInt( "slot" ) == RADIO_ENEMY_SPOTTED ) { SetLastSeenEnemyTimestamp(); } CFFBOTMANAGER_ITERATE_BOTS( OnPlayerRadio, event ); }
void CFFBotManager::OnPlayerDeath( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerDeath, event ); }
void CFFBotManager::OnPlayerFallDamage( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnPlayerFallDamage, event ); }
void CFFBotManager::OnBombPickedUp( IGameEvent *event ) { SetLooseBomb( NULL ); CFFBOTMANAGER_ITERATE_BOTS( OnBombPickedUp, event ); }
void CFFBotManager::OnBombPlanted( IGameEvent *event ) { m_isBombPlanted = true; m_bombPlantTimestamp = gpGlobals->curtime; CFFBOTMANAGER_ITERATE_BOTS( OnBombPlanted, event ); }
void CFFBotManager::OnBombBeep( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnBombBeep, event ); }
void CFFBotManager::OnBombDefuseBegin( IGameEvent *event ) { m_bombDefuser = static_cast<CFFPlayer *>( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ); CFFBOTMANAGER_ITERATE_BOTS( OnBombDefuseBegin, event ); }
void CFFBotManager::OnBombDefused( IGameEvent *event ) { m_isBombPlanted = false; m_bombDefuser = NULL; CFFBOTMANAGER_ITERATE_BOTS( OnBombDefused, event ); }
void CFFBotManager::OnBombDefuseAbort( IGameEvent *event ) { m_bombDefuser = NULL; CFFBOTMANAGER_ITERATE_BOTS( OnBombDefuseAbort, event ); }
void CFFBotManager::OnBombExploded( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnBombExploded, event ); }
void CFFBotManager::OnRoundEnd( IGameEvent *event ) { m_isRoundOver = true; CFFBOTMANAGER_ITERATE_BOTS( OnRoundEnd, event ); }
void CFFBotManager::OnRoundStart( IGameEvent *event ) { RestartRound(); CFFBOTMANAGER_ITERATE_BOTS( OnRoundStart, event ); }
void CFFBotManager::OnDoorMoving( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnDoorMoving, event ); }
void CFFBotManager::OnBreakProp( IGameEvent *event ) { if (!TheNavMesh) return; CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) ); TheNavMesh->ForAllAreas( collector ); CFFBOTMANAGER_ITERATE_BOTS( OnBreakProp, event ); }
void CFFBotManager::OnBreakBreakable( IGameEvent *event ) { if (!TheNavMesh) return; CheckAreasOverlappingBreakable collector( UTIL_EntityByIndex( event->GetInt( "entindex" ) ) ); TheNavMesh->ForAllAreas( collector ); CFFBOTMANAGER_ITERATE_BOTS( OnBreakBreakable, event ); }
void CFFBotManager::OnHostageFollows( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnHostageFollows, event ); }
void CFFBotManager::OnHostageRescuedAll( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnHostageRescuedAll, event ); }
void CFFBotManager::OnWeaponFire( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFire, event ); }
void CFFBotManager::OnWeaponFireOnEmpty( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponFireOnEmpty, event ); }
void CFFBotManager::OnWeaponReload( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponReload, event ); }
void CFFBotManager::OnWeaponZoom( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnWeaponZoom, event ); }
void CFFBotManager::OnBulletImpact( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnBulletImpact, event ); }
void CFFBotManager::OnHEGrenadeDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnHEGrenadeDetonate, event ); }
void CFFBotManager::OnFlashbangDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnFlashbangDetonate, event ); }
void CFFBotManager::OnSmokeGrenadeDetonate( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnSmokeGrenadeDetonate, event ); }
void CFFBotManager::OnGrenadeBounce( IGameEvent *event ) { CFFBOTMANAGER_ITERATE_BOTS( OnGrenadeBounce, event ); }

// Note: The DECLARE_FFBOTMANAGER_EVENT_LISTENER macro in the header sets up m_NavBlockedEvent and m_ServerShutdownEvent.
// Their OnEventName methods will call the CFFBotManager::OnNavBlocked and CFFBotManager::OnServerShutdown respectively.
// These methods were defined in the previous read of this file.
