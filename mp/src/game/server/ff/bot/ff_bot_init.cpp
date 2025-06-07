//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_shareddefs.h"
#include "ff_gamerules.h"
#include "ff_bot_manager.h" // For FF_TEAM_* defines and TheFFBots()
#include "../../shared/ff/ff_playerclass_parse.h" // For MAX_PLAYERCLASS_SLOTS

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#pragma warning( disable : 4355 )			// warning 'this' used in base member initializer list - we're using it safely


//--------------------------------------------------------------------------------------------------------------
static void PrefixChanged( IConVar *c, const char *oldPrefix, float flOldValue )
{
	if ( TheFFBots() && TheFFBots()->IsServerActive() )
	{
		for( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
			if ( !player || !player->IsBot() || !player->edict() ) continue;

			CFFBot *bot = dynamic_cast< CFFBot * >( player );
			if ( !bot ) continue;

			char botName[MAX_PLAYER_NAME_LENGTH];
			UTIL_ConstructBotNetName( botName, MAX_PLAYER_NAME_LENGTH, bot->GetProfile() );
			engine->SetFakeClientConVarValue( bot->edict(), "name", botName );
		}
	}
}


ConVar cv_bot_traceview( "bot_traceview", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_stop( "bot_stop", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, immediately stops all bot processing." );
ConVar cv_bot_show_nav( "bot_show_nav", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_walk( "bot_walk", "0", FCVAR_REPLICATED, "If nonzero, bots can only walk, not run." );
ConVar cv_bot_difficulty( "bot_difficulty", "1", FCVAR_REPLICATED, "Defines the skill of bots joining the game.  Values are: 0=easy, 1=normal, 2=hard, 3=expert." );
ConVar cv_bot_debug( "bot_debug", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_debug_target( "bot_debug_target", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_quota( "bot_quota", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Determines the total number of bots in the game." );
ConVar cv_bot_quota_mode( "bot_quota_mode", "normal", FCVAR_REPLICATED, "Determines the type of quota.\nAllowed values: 'normal', 'fill', and 'match'.\nIf 'fill', the server will adjust bots to keep N players in the game, where N is bot_quota.\nIf 'match', the server will maintain a 1:N ratio of humans to bots, where N is bot_quota." );
ConVar cv_bot_prefix( "bot_prefix", "", FCVAR_REPLICATED, "This string is prefixed to the name of all bots that join the game.\n<difficulty> will be replaced with the bot's difficulty.\n<weaponclass> will be replaced with the bot's desired weapon class.\n<skill> will be replaced with a 0-100 representation of the bot's skill.", PrefixChanged );
ConVar cv_bot_allow_rogues( "bot_allow_rogues", "1", FCVAR_REPLICATED, "If nonzero, bots may occasionally go 'rogue'. Rogue bots do not obey radio commands, nor pursue scenario goals." );
// FF_TODO: Define FF Specific Weapon CVars based on FF weapon types/categories
ConVar cv_bot_join_team( "bot_join_team", "any", FCVAR_REPLICATED, "Determines the team bots will join into. Allowed values: 'any', 'red', 'blue'." );
ConVar cv_bot_join_after_player( "bot_join_after_player", "1", FCVAR_REPLICATED, "If nonzero, bots wait until a player joins before entering the game." );
ConVar cv_bot_auto_vacate( "bot_auto_vacate", "1", FCVAR_REPLICATED, "If nonzero, bots will automatically leave to make room for human players." );
ConVar cv_bot_zombie( "bot_zombie", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots will stay in idle mode and not attack." );
ConVar cv_bot_defer_to_human( "bot_defer_to_human", "0", FCVAR_REPLICATED, "If nonzero and there is a human on the team, the bots will not do the scenario tasks." );
ConVar cv_bot_chatter( "bot_chatter", "normal", FCVAR_REPLICATED, "Control how bots talk. Allowed values: 'off', 'radio', 'minimal', or 'normal'." );
ConVar cv_bot_profile_db( "bot_profile_db", "BotProfile.db", FCVAR_REPLICATED, "The filename from which bot profiles will be read." );
ConVar cv_bot_dont_shoot( "bot_dont_shoot", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots will not fire weapons (for debugging)." );
ConVar cv_bot_eco_limit( "bot_eco_limit", "2000", FCVAR_REPLICATED, "If nonzero, bots will not buy if their money falls below this amount." ); // FF_TODO: Review if FF has similar economy for bots
ConVar cv_bot_auto_follow( "bot_auto_follow", "0", FCVAR_REPLICATED, "If nonzero, bots with high co-op may automatically follow a nearby human player." );
ConVar cv_bot_flipout( "bot_flipout", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots use no CPU for AI. Instead, they run around randomly." );


extern void FinishClientPutInServer( CFFPlayer *pPlayer );


//--------------------------------------------------------------------------------------------------------------
void Bot_ServerCommand( void ) { /* FF specific server commands can be handled here if needed */ }

//--------------------------------------------------------------------------------------------------------------
CFFBot::CFFBot( void ) : m_chatter( this ), m_gameState( this ) { m_hasJoined = false; }

//--------------------------------------------------------------------------------------------------------------
CFFBot::~CFFBot() { }

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::Initialize( const BotProfile *profile, int team )
{
	BaseClass::Initialize( profile, team );
	m_diedLastRound = false;
	m_morale = POSITIVE;
	m_combatRange = RandomFloat( 325.0f, 425.0f );
	m_safeTime = 15.0f + 5.0f * GetProfile()->GetAggression();
	m_name[0] = '\000';
	ResetValues();
	m_desiredTeam = team;

	if (GetTeamNumber() == FF_TEAM_UNASSIGNED || GetTeamNumber() == FF_TEAM_SPECTATOR ) // Also handle if bot is on spectator
	{
		HandleCommand_JoinTeam( m_desiredTeam );
	}

	// FF Class Selection Logic
	int desiredClassID = profile->GetSkin(); // m_skin from profile is assumed to be the class slot ID

	// Validate desiredClassID (assuming class slots are 0 to MAX_PLAYERCLASS_SLOTS-1 or 1 to MAX_PLAYERCLASS_SLOTS)
	// MAX_PLAYERCLASS_SLOTS is 10 (from ff_playerclass_parse.cpp)
	// Assuming slots are 1-indexed for safety, similar to CS, until proven otherwise.
	// Or 0-indexed if that's the FF convention (g_bUsedPlayerClassSlots[m_iSlot] suggests 0-indexed).
	// Let's assume 0-indexed for now based on g_bUsedPlayerClassSlots.
	if (desiredClassID < 0 || desiredClassID >= MAX_PLAYERCLASS_SLOTS)
	{
		me->PrintIfWatched( "Profile skin/class ID %d is invalid. Attempting fallback.\n", desiredClassID );
		desiredClassID = 0; // Default to first class (e.g., Scout) or a random valid one.
		// FF_TODO: Implement more robust fallback (random valid class for team).
		// For now, try to join class 0 as a fallback.
	}

	// FF_TODO: Add check here if FFGameRules()->IsClassValidForTeam(desiredClassID, m_desiredTeam) exists.
	// If not valid, pick a default for the team.
	// Example fallback:
	// if (FFGameRules() && !FFGameRules()->IsClassValidForTeam(desiredClassID, m_desiredTeam))
	// {
	//		if (m_desiredTeam == FF_TEAM_RED) desiredClassID = <DEFAULT_RED_CLASS_SLOT_ID>;
	//		else if (m_desiredTeam == FF_TEAM_BLUE) desiredClassID = <DEFAULT_BLUE_CLASS_SLOT_ID>;
	//      else desiredClassID = 0; // Default fallback
	//		me->PrintIfWatched( "Profile class ID %d not valid for team %d. Switched to %d.\n", profile->GetSkin(), m_desiredTeam, desiredClassID );
	// }

	me->PrintIfWatched( "Attempting to join class ID: %d\n", desiredClassID );
	HandleCommand_JoinClass( desiredClassID ); // Assuming HandleCommand_JoinClass takes an int (slot ID)

	// Original CS class selection logic commented out:
	/*
	if (GetTeamNumber() == 0)
	{
		HandleCommand_JoinTeam( m_desiredTeam );
		int desiredClass = GetProfile()->GetSkin();
		if ( m_desiredTeam == TEAM_CT && desiredClass )
		{
			desiredClass = FIRST_CT_CLASS + desiredClass - 1;
		}
		else if ( m_desiredTeam == TEAM_TERRORIST && desiredClass )
		{
			desiredClass = FIRST_T_CLASS + desiredClass - 1;
		}
		HandleCommand_JoinClass( desiredClass );
	}
	*/

	return true;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::ResetValues( void )
{
	m_chatter.Reset();
	m_gameState.Reset();
	m_avoid = NULL; m_avoidTimestamp = 0.0f;
	m_hurryTimer.Invalidate(); m_alertTimer.Invalidate(); m_sneakTimer.Invalidate();
	m_noiseBendTimer.Invalidate(); m_bendNoisePositionValid = false;
	m_isStuck = false; m_stuckTimestamp = 0.0f;
	m_wiggleTimer.Invalidate(); m_stuckJumpTimer.Invalidate();
	m_pathLength = 0; m_pathIndex = 0; m_areaEnteredTimestamp = 0.0f;
	m_currentArea = NULL; m_lastKnownArea = NULL; m_isStopping = false;
	m_avoidFriendTimer.Invalidate(); m_isFriendInTheWay = false; m_isWaitingBehindFriend = false;
	m_isAvoidingGrenade.Invalidate();
	StopPanicking();
	m_disposition = ENGAGE_AND_INVESTIGATE;
	m_enemy = NULL;
	m_grenadeTossState = NOT_THROWING;
	m_initialEncounterArea = NULL;
	m_wasSafe = true;
	m_nearbyEnemyCount = 0; m_enemyPlace = 0;
	m_nearbyFriendCount = 0; m_closestVisibleFriend = NULL; m_closestVisibleHumanFriend = NULL;
	for( int w=0; w<MAX_PLAYERS; ++w ) { m_watchInfo[w].timestamp = 0.0f; m_watchInfo[w].isEnemy = false; m_playerTravelDistance[ w ] = -1.0f; }
	m_updateTravelDistanceTimer.Start( RandomFloat( 0.0f, 0.9f ) ); m_travelDistancePhase = 0;
	m_isEnemyVisible = false; m_visibleEnemyParts = NONE;
	m_lastSawEnemyTimestamp = -999.9f; m_firstSawEnemyTimestamp = 0.0f; m_currentEnemyAcquireTimestamp = 0.0f;
	m_isLastEnemyDead = true; m_attacker = NULL; m_attackedTimestamp = 0.0f;
	m_enemyDeathTimestamp = 0.0f; m_friendDeathTimestamp = 0.0f;
	m_lastVictimID = 0; m_isAimingAtEnemy = false; m_fireWeaponTimestamp = 0.0f;
	m_equipTimer.Invalidate(); m_zoomTimer.Invalidate();
	m_isFollowing = false; m_leader = NULL; m_followTimestamp = 0.0f; m_allowAutoFollowTime = 0.0f;
	m_enemyQueueIndex = 0; m_enemyQueueCount = 0; m_enemyQueueAttendIndex = 0;
	m_isEnemySniperVisible = false; m_sawEnemySniperTimer.Invalidate();
	m_lookAroundStateTimestamp = 0.0f; m_inhibitLookAroundTimestamp = 0.0f;
	m_lookPitch = 0.0f; m_lookPitchVel = 0.0f; m_lookYaw = 0.0f; m_lookYawVel = 0.0f;
	m_aimOffsetTimestamp = 0.0f; m_aimSpreadTimestamp = 0.0f; m_lookAtSpotState = NOT_LOOKING_AT_SPOT;
	for( int p=0; p<MAX_PLAYERS; ++p ) { m_partInfo[p].m_validFrame = 0; }
	m_spotEncounter = NULL; m_spotCheckTimestamp = 0.0f; m_peripheralTimestamp = 0.0f;
	m_avgVelIndex = 0; m_avgVelCount = 0;
	m_lastOrigin = GetCentroid( this );
	m_lastRadioCommand = RADIO_INVALID;
	m_lastRadioRecievedTimestamp = 0.0f; m_lastRadioSentTimestamp = 0.0f;
	m_radioSubject = NULL; m_voiceEndTimestamp = 0.0f;
	m_noisePosition = Vector( 0, 0, 0 ); m_noiseTimestamp = 0.0f;
	m_stateTimestamp = 0.0f; m_task = SEEK_AND_DESTROY;
	m_taskEntity = NULL;
	m_approachPointCount = 0; m_approachPointViewPosition.Init(99999999999.9f, 0.0f, 0.0f);
	m_checkedHidingSpotCount = 0;
	StandUp(); Run();
	m_mustRunTimer.Invalidate(); m_waitTimer.Invalidate(); m_pathLadder = NULL;
	m_repathTimer.Invalidate();
	m_huntState.ClearHuntArea();
	m_hasVisitedEnemySpawn = false;
	m_stillTimer.Invalidate();
	if (m_diedLastRound) DecreaseMorale();
	m_diedLastRound = false;
	m_isRogue = false;	
	m_surpriseTimer.Invalidate();
	m_goalEntity = NULL; m_avoid = NULL; m_enemy = NULL;
	for ( int i=0; i<MAX_ENEMY_QUEUE; ++i ) { m_enemyQueue[i].player = NULL; m_enemyQueue[i].isReloading = false; }
	m_isOpeningDoor = false; StopAttacking(); Idle();
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::Spawn( void )
{
	BaseClass::Spawn();
	ResetValues();
	V_strcpy_safe( m_name, GetPlayerName() );
	Buy();
}
