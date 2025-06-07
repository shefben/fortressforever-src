//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../../server/ff/ff_shareddefs.h" // Changed from cs_shareddefs.h, for FF_TEAM_* etc.
#include "engine/IEngineSound.h"
#include "KeyValues.h"

#include "bot.h" // Should pick up the ff_bot.h from the same directory if possible, or adjusted path
#include "bot_util.h"
#include "bot_profile.h"

#include "../../server/ff/bot/ff_bot.h" // Explicit path to ensure CFFBot is known
#include "../../server/ff/ff_player.h"   // Ensure CFFPlayer is known
#include "../../server/ff/bot/ff_bot_manager.h" // For FF_TEAM_* constants

#include <ctype.h>
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static int s_iBeamSprite = 0;

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if given name is already in use by another player
 */
bool UTIL_IsNameTaken( const char *name, bool ignoreHumans )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );

		if (player == NULL)
			continue;

		if (player->IsPlayer() && player->IsBot())
		{
			// bots can have prefixes so we need to check the name
			// against the profile name instead.
			CFFBot *bot = dynamic_cast<CFFBot *>(player); // Changed CCSBot to CFFBot
			if ( bot && bot->GetProfile() && bot->GetProfile()->GetName() && FStrEq(name, bot->GetProfile()->GetName())) // Added GetName() null check
			{
				return true;
			}
		}
		else
		{
			if (!ignoreHumans)
			{
				if (FStrEq( name, player->GetPlayerName() ))
					return true;
			}
		}
	}

	return false;
}


//--------------------------------------------------------------------------------------------------------------
int UTIL_ClientsInGame( void )
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *player = UTIL_PlayerByIndex( i );
		if (player == NULL) continue;
		count++;
	}
	return count;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return the number of non-bots on the given team
 */
int UTIL_HumansOnTeam( int teamID, bool isAlive ) // teamID will be FF_TEAM_*
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *entity = UTIL_PlayerByIndex( i );
		if ( entity == NULL ) continue;
		CBasePlayer *player = static_cast<CBasePlayer *>( entity );
		if (player->IsBot()) continue;
		if (player->GetTeamNumber() != teamID) continue; // Relies on GetTeamNumber() returning FF_TEAM_*
		if (isAlive && !player->IsAlive()) continue;
		count++;
	}
	return count;
}


//--------------------------------------------------------------------------------------------------------------
int UTIL_BotsInGame( void )
{
	int count = 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>(UTIL_PlayerByIndex( i ));
		if ( player == NULL || !player->IsBot() ) continue;
		count++;
	}
	return count;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Kick a bot from the given team. If no bot exists on the team, return false.
 */
bool UTIL_KickBotFromTeam( int kickTeam ) // kickTeam will be FF_TEAM_*
{
	int i;
	for ( i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (player == NULL || !player->IsBot() || !player->IsAlive() || player->GetTeamNumber() != kickTeam) continue;
		engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", player->GetPlayerName() ) );
		return true;
	}
	for ( i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (player == NULL || !player->IsBot() || player->GetTeamNumber() != kickTeam) continue;
		engine->ServerCommand( UTIL_VarArgs( "kick \"%s\"\n", player->GetPlayerName() ) );
		return true;
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if all of the members of the given team are bots
 */
bool UTIL_IsTeamAllBots( int team ) // team will be FF_TEAM_*
{
	int botCount = 0;
	for( int i=1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (player == NULL || player->GetTeamNumber() != team) continue;
		if (!player->IsBot()) return false;
		++botCount;
	}
	return (botCount) ? true : false;
}

//--------------------------------------------------------------------------------------------------------------
extern CBasePlayer *UTIL_GetClosestPlayer( const Vector &pos, float *distance )
{
	CBasePlayer *closePlayer = NULL; float closeDistSq = 999999999999.9f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (!IsEntityValid( player ) || !player->IsAlive()) continue;
		Vector playerOrigin = GetCentroid( player ); float distSq = (playerOrigin - pos).LengthSqr();
		if (distSq < closeDistSq) { closeDistSq = distSq; closePlayer = player; }
	}
	if (distance) *distance = (float)sqrt( closeDistSq );
	return closePlayer;
}

//--------------------------------------------------------------------------------------------------------------
extern CBasePlayer *UTIL_GetClosestPlayer( const Vector &pos, int team, float *distance ) // team will be FF_TEAM_*
{
	CBasePlayer *closePlayer = NULL; float closeDistSq = 999999999999.9f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (!IsEntityValid( player ) || !player->IsAlive() || player->GetTeamNumber() != team) continue;
		Vector playerOrigin = GetCentroid( player ); float distSq = (playerOrigin - pos).LengthSqr();
		if (distSq < closeDistSq) { closeDistSq = distSq; closePlayer = player; }
	}
	if (distance) *distance = (float)sqrt( closeDistSq );
	return closePlayer;
}

//--------------------------------------------------------------------------------------------------------------
void UTIL_ConstructBotNetName( char *name, int nameLength, const BotProfile *profile )
{
	if (profile == NULL) { name[0] = 0; return; }
	if ((cv_bot_prefix.GetString() == NULL) || (strlen(cv_bot_prefix.GetString()) == 0))
	{ Q_strncpy( name, profile->GetName(), nameLength ); return; }

	const char *diffStr = BotDifficultyName[0];
	for ( int i=BOT_EXPERT; i>0; --i )
		if ( profile->IsDifficulty( (BotDifficultyType)i ) ) { diffStr = BotDifficultyName[i]; break; }

	// FF_TODO: The <weaponclass> token is CS-specific due to CCSWeaponInfo & related functions.
	// This part needs to be adapted or removed for FF. For now, commenting out weaponStr logic.
	const char *weaponStr = "";
	/*
	if ( profile->GetWeaponPreferenceCount() )
	{
		weaponStr = profile->GetWeaponPreferenceAsString( 0 );
		const char *translatedAlias = GetTranslatedWeaponAlias( weaponStr ); // CS Function
		char wpnName[128]; Q_snprintf( wpnName, sizeof( wpnName ), "weapon_%s", translatedAlias );
		WEAPON_FILE_INFO_HANDLE	hWpnInfo = LookupWeaponInfoSlot( wpnName ); // CS Function
		if ( hWpnInfo != GetInvalidWeaponInfoHandle() )
		{
			// CCSWeaponInfo *pWeaponInfo = dynamic_cast< CCSWeaponInfo* >( GetFileWeaponInfoFromHandle( hWpnInfo ) ); // CS Type
			// if ( pWeaponInfo )
			// {
			// 	CSWeaponType weaponType = pWeaponInfo->m_WeaponType; // CS Type
			// 	weaponStr = WeaponClassAsString( weaponType ); // CS Function
			// }
		}
	}
	if ( !weaponStr ) weaponStr = "";
	*/

	char skillStr[16]; Q_snprintf( skillStr, sizeof( skillStr ), "%.0f", profile->GetSkill()*100 );
	char temp[MAX_PLAYER_NAME_LENGTH*2]; char prefix[MAX_PLAYER_NAME_LENGTH*2];
	Q_strncpy( temp, cv_bot_prefix.GetString(), sizeof( temp ) );
	Q_StrSubst( temp, "<difficulty>", diffStr, prefix, sizeof( prefix ) );
	Q_StrSubst( prefix, "<weaponclass>", weaponStr, temp, sizeof( temp ) ); // weaponStr will be empty for now
	Q_StrSubst( temp, "<skill>", skillStr, prefix, sizeof( prefix ) );
	Q_snprintf( name, nameLength, "%s %s", prefix, profile->GetName() );
}

//--------------------------------------------------------------------------------------------------------------
bool UTIL_IsVisibleToTeam( const Vector &spot, int team ) // team will be FF_TEAM_*
{
	for( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );
		if (player == NULL || !player->IsAlive() || player->GetTeamNumber() != team) continue;
		trace_t result; UTIL_TraceLine( player->EyePosition(), spot, CONTENTS_SOLID, player, COLLISION_GROUP_NONE, &result );
		if (result.fraction == 1.0f) return true;
	}
	return false;
}

//------------------------------------------------------------------------------------------------------------
void UTIL_DrawBeamFromEnt( int i, Vector vecEnd, int iLifetime, byte bRed, byte bGreen, byte bBlue ) { /* ... (BOTPORT message removed) ... */ }
//------------------------------------------------------------------------------------------------------------
void UTIL_DrawBeamPoints( Vector vecStart, Vector vecEnd, int iLifetime, byte bRed, byte bGreen, byte bBlue )
{ NDebugOverlay::Line( vecStart, vecEnd, bRed, bGreen, bBlue, true, (float)iLifetime * 0.1f ); } // Use iLifetime for duration
//------------------------------------------------------------------------------------------------------------
void CONSOLE_ECHO( const char * pszMsg, ... )
{ va_list argptr; static char szStr[1024]; va_start( argptr, pszMsg ); Q_vsnprintf( szStr, sizeof(szStr), pszMsg, argptr ); va_end( argptr ); Msg( "%s", szStr ); }
//------------------------------------------------------------------------------------------------------------
void BotPrecache( void ) { s_iBeamSprite = CBaseEntity::PrecacheModel( "sprites/smoke.spr" ); } // This sprite might be generic or CS-specific
//------------------------------------------------------------------------------------------------------------
#define COS_TABLE_SIZE 256
static float cosTable[ COS_TABLE_SIZE ];
void InitBotTrig( void ) { for( int i=0; i<COS_TABLE_SIZE; ++i ) { float angle = (float)(2.0f * M_PI * i / (float)(COS_TABLE_SIZE-1)); cosTable[i] = (float)cos( angle ); } }
float BotCOS( float angle ) { angle = AngleNormalizePositive( angle ); int i = (int)( angle * (COS_TABLE_SIZE-1) / 360.0f ); return cosTable[i]; }
float BotSIN( float angle ) { angle = AngleNormalizePositive( angle - 90 ); int i = (int)( angle * (COS_TABLE_SIZE-1) / 360.0f ); return cosTable[i]; }
//--------------------------------------------------------------------------------------------------------------
void HintMessageToAllPlayers( const char *message )
{ hudtextparms_t textParms; textParms.x = -1.0f; textParms.y = -1.0f; textParms.fadeinTime = 1.0f; textParms.fadeoutTime = 5.0f;
  textParms.holdTime = 5.0f; textParms.fxTime = 0.0f; textParms.r1 = 100; textParms.g1 = 255; textParms.b1 = 100;
  textParms.r2 = 255; textParms.g2 = 255; textParms.b2 = 255; textParms.effect = 0; textParms.channel = 0;
  UTIL_HudMessageAll( textParms, message ); }
//--------------------------------------------------------------------------------------------------------------------
bool IsCrossingLineOfFire( const Vector &start, const Vector &finish, CBaseEntity *ignore, int ignoreTeam  ) // ignoreTeam will be FF_TEAM_*
{
	for ( int p=1; p <= gpGlobals->maxClients; ++p )
	{
		CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( p ) );
		if (!IsEntityValid( player ) || player == ignore || !player->IsAlive() || (ignoreTeam && player->GetTeamNumber() == ignoreTeam)) continue;
		Vector viewForward; AngleVectors( player->EyeAngles() + player->GetPunchAngle(), &viewForward );
		const float longRange = 5000.0f; Vector playerOrigin = GetCentroid( player ); Vector playerTarget = playerOrigin + longRange * viewForward;
		Vector result( 0, 0, 0 );
		if (IsIntersecting2D( start, finish, playerOrigin, playerTarget, &result ))
		{
			float loZ, hiZ; if (start.z < finish.z) { loZ = start.z; hiZ = finish.z; } else { loZ = finish.z; hiZ = start.z; }
			if (result.z >= loZ && result.z <= hiZ + HumanHeight) return true;
		}
	}
	return false;
}
//--------------------------------------------------------------------------------------------------------------
bool WildcardMatch( const char *query, const char *test )
{ if ( !query || !test ) return false; while ( *test && *query ) { char nameChar = *test; char queryChar = *query;
    if ( tolower(nameChar) != tolower(queryChar) ) break; ++test; ++query; }
  if ( *query == 0 && *test == 0 ) return true; if ( *query == '*' ) return true; return false; }
