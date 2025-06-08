//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BOT_UTIL_H
#define BOT_UTIL_H


#include "convar.h"
#include "util.h"
// It's better to include ff_bot_manager.h where FF_TEAM_* are defined,
// or a central ff_defs.h if that contains them.
// For now, assuming these will be picked up transitively or defined globally for FF.

//--------------------------------------------------------------------------------------------------------------
enum PriorityType
{
	PRIORITY_LOW, PRIORITY_MEDIUM, PRIORITY_HIGH, PRIORITY_UNINTERRUPTABLE
};


extern ConVar cv_bot_traceview;
extern ConVar cv_bot_stop;
extern ConVar cv_bot_show_nav;
extern ConVar cv_bot_walk;
extern ConVar cv_bot_difficulty;
extern ConVar cv_bot_debug;
extern ConVar cv_bot_debug_target;
extern ConVar cv_bot_quota;
extern ConVar cv_bot_quota_mode;
extern ConVar cv_bot_prefix;
extern ConVar cv_bot_allow_rogues;
// FF_TODO: Replace these CS-specific weapon allowance ConVars with FF equivalents if needed
// extern ConVar cv_bot_allow_pistols;
// extern ConVar cv_bot_allow_shotguns;
// extern ConVar cv_bot_allow_sub_machine_guns;
// extern ConVar cv_bot_allow_rifles;
// extern ConVar cv_bot_allow_machine_guns;
// extern ConVar cv_bot_allow_grenades;
// extern ConVar cv_bot_allow_snipers;
// extern ConVar cv_bot_allow_shield; // CS specific
extern ConVar cv_bot_join_team; // Description should reflect FF team names ("red", "blue", "any")
extern ConVar cv_bot_join_after_player;
extern ConVar cv_bot_auto_vacate;
extern ConVar cv_bot_zombie;
extern ConVar cv_bot_defer_to_human;
extern ConVar cv_bot_chatter;
extern ConVar cv_bot_profile_db; // FF might use a different profile DB name
extern ConVar cv_bot_dont_shoot;
extern ConVar cv_bot_eco_limit; // FF_TODO: Review if FF has similar economy for bots
extern ConVar cv_bot_auto_follow;
extern ConVar cv_bot_flipout;

#define RAD_TO_DEG( deg ) ((deg) * 180.0 / M_PI)
#define DEG_TO_RAD( rad ) ((rad) * M_PI / 180.0)

#define SIGN( num )	      (((num) < 0) ? -1 : 1)
#define ABS( num )        (SIGN(num) * (num))


#define CREATE_FAKE_CLIENT		( *g_engfuncs.pfnCreateFakeClient )
#define GET_USERINFO			( *g_engfuncs.pfnGetInfoKeyBuffer )
#define SET_KEY_VALUE			( *g_engfuncs.pfnSetKeyValue )
#define SET_CLIENT_KEY_VALUE	( *g_engfuncs.pfnSetClientKeyValue )

class BotProfile;
class CFFPlayer; // Changed from forward declaration of CCSPlayer if it existed, or ensure CBasePlayer is used if functions are generic

extern void   BotPrecache( void );
extern int		UTIL_ClientsInGame( void );

extern bool UTIL_IsNameTaken( const char *name, bool ignoreHumans = false );

#define IS_ALIVE true
// Takes FF_TEAM_* as teamID
extern int UTIL_HumansOnTeam( int teamID, bool isAlive = false );

extern int		UTIL_BotsInGame( void );
// Takes FF_TEAM_* as team
extern bool		UTIL_IsTeamAllBots( int team );
extern void		UTIL_DrawBeamFromEnt( int iIndex, Vector vecEnd, int iLifetime, byte bRed, byte bGreen, byte bBlue );
extern void		UTIL_DrawBeamPoints( Vector vecStart, Vector vecEnd, int iLifetime, byte bRed, byte bGreen, byte bBlue );
extern CBasePlayer *UTIL_GetClosestPlayer( const Vector &pos, float *distance = NULL );
// Takes FF_TEAM_* as team
extern CBasePlayer *UTIL_GetClosestPlayer( const Vector &pos, int team, float *distance = NULL );
// Takes FF_TEAM_* as kickTeam
extern bool UTIL_KickBotFromTeam( int kickTeam );

// Takes FF_TEAM_* as team
extern bool UTIL_IsVisibleToTeam( const Vector &spot, int team );

// Takes FF_TEAM_* as ignoreTeam
extern bool IsCrossingLineOfFire( const Vector &start, const Vector &finish, CBaseEntity *ignore = NULL, int ignoreTeam = 0 );

extern void UTIL_ConstructBotNetName(char *name, int nameLength, const BotProfile *bot);

extern void CONSOLE_ECHO( PRINTF_FORMAT_STRING const char * pszMsg, ... );

extern void InitBotTrig( void );
extern float BotCOS( float angle );
extern float BotSIN( float angle );

extern void HintMessageToAllPlayers( const char *message );

bool WildcardMatch( const char *query, const char *test );

//--------------------------------------------------------------------------------------------------------------
inline bool IsEntityValid( CBaseEntity *entity )
{
	if (entity == NULL) return false;
	if (FNullEnt( entity->edict() )) return false; // edict() might be engine specific, ensure it's fine
	return true;
}

//--------------------------------------------------------------------------------------------------------------
inline bool IsIntersecting2D( const Vector &startA, const Vector &endA, 
															const Vector &startB, const Vector &endB, 
															Vector *result = NULL )
{
	float denom = (endA.x - startA.x) * (endB.y - startB.y) - (endA.y - startA.y) * (endB.x - startB.x);
	if (denom == 0.0f) return false; // parallel
	float numS = (startA.y - startB.y) * (endB.x - startB.x) - (startA.x - startB.x) * (endB.y - startB.y);
	if (numS == 0.0f) return true; // coincident
	float numT = (startA.y - startB.y) * (endA.x - startA.x) - (startA.x - startB.x) * (endA.y - startA.y);
	float s = numS / denom;
	if (s < 0.0f || s > 1.0f) return false;
	float t = numT / denom;
	if (t < 0.0f || t > 1.0f) return false;
	if (result) *result = startA + s * (endA - startA);
	return true;
}

#endif
