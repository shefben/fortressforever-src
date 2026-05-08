//========= Fortress Forever Bot =============================================//
//
// FFBot console commands — validation, visualization, save/load, diagnose.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
#include "ff_bot_mapintel.h"
#include "ff_bot_persistence.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_info_script.h"
#include "ff_player.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "Path/NextBotPathFollow.h"
#include "shareddefs.h"
#include "entitylist.h"
#include "debugoverlay_shared.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// ff_nav_save — write the current FF semantic tags to <map>.ffnav so future
// loads can skip inference.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_save, "Save FF nav semantic tags to <map>.ffnav.", FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "Nav mesh not loaded.\n" );
		return;
	}
	if ( FFBotPersistence::Save( mesh ) )
		Msg( "Saved.\n" );
	else
		Msg( "Save failed.\n" );
}


//-----------------------------------------------------------------------------
// ff_nav_analyze — clear runtime tags + re-run inference + save. Useful
// after editing a map's spawns/flags without restarting the server.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_analyze, "Re-run FF map-intel inference and save .ffnav.", FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "Nav mesh not loaded.\n" );
		return;
	}

	// Clear inferred tags (preserve entity-derived ones).
	const int preservedMask =
		FF_NAV_SPAWN_ANY | FF_NAV_FLAG_ANY | FF_NAV_CAP_ANY |
		FF_NAV_RESUPPLY | FF_NAV_AMMO | FF_NAV_ARMOR | FF_NAV_HEALTH | FF_NAV_GRENADES |
		FF_NAV_VIP_GOAL | FF_NAV_NO_BUILD;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		const int keep = area->GetFFTags() & preservedMask;
		area->ClearFFTags();
		area->AddFFTag( keep );
		area->SetClassMask( 0 );
	}

	FFBotMapIntel::RunLevelInit( mesh );
	FFBotPersistence::Save( mesh );
	Msg( "FF nav re-analyzed.\n" );
}


//-----------------------------------------------------------------------------
// ff_nav_validate — check that the current map's nav covers all gameplay
// entities and has reasonable connectivity.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_validate, "Validate FF nav coverage and connectivity.", FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "[ff_nav_validate] Nav mesh not loaded — cannot validate.\n" );
		return;
	}

	int problems = 0;

	// Check each spawn entity has nav coverage within 256u.
	int spawns = 0, spawnMissing = 0;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "info_ff_teamspawn" ) ) != NULL )
	{
		++spawns;
		CNavArea *near = TheNavMesh->GetNearestNavArea( e->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY );
		if ( !near )
		{
			++spawnMissing;
			++problems;
			Msg( "  ! info_ff_teamspawn at (%.0f %.0f %.0f) has no nav coverage\n",
				e->GetAbsOrigin().x, e->GetAbsOrigin().y, e->GetAbsOrigin().z );
		}
	}

	// Check flag/cap/resupply entities have nav coverage.
	int infoScripts = 0, infoMissing = 0;
	e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		if ( s->GetBotGoalType() == Omnibot::kNone )
			continue;
		++infoScripts;
		CNavArea *near = TheNavMesh->GetNearestNavArea( e->GetAbsOrigin(), false, 384.0f, false, true, TEAM_ANY );
		if ( !near )
		{
			++infoMissing;
			++problems;
			Msg( "  ! info_ff_script (goalType %d) at (%.0f %.0f %.0f) has no nav coverage\n",
				s->GetBotGoalType(), e->GetAbsOrigin().x, e->GetAbsOrigin().y, e->GetAbsOrigin().z );
		}
	}

	// Connectivity: every spawn should be able to reach every flag of
	// other teams.
	struct GeoCost : public IPathCost {
		virtual float operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *, float length ) const OVERRIDE {
			if ( !fromArea ) return 0.0f;
			if ( ladder ) return ladder->m_length;
			if ( length > 0.0f ) return length;
			return ( area->GetCenter() - fromArea->GetCenter() ).Length();
		}
	};
	GeoCost cost;
	int connectivityFails = 0;
	for ( int sourceTeam = TEAM_BLUE; sourceTeam <= TEAM_GREEN; ++sourceTeam )
	{
		const CUtlVector< CFFNavArea * > *spawnList = mesh->GetSpawnRoomAreas( sourceTeam );
		if ( !spawnList || spawnList->Count() == 0 )
			continue;
		CFFNavArea *spawn = ( *spawnList )[ 0 ];

		for ( int targetTeam = TEAM_BLUE; targetTeam <= TEAM_GREEN; ++targetTeam )
		{
			if ( targetTeam == sourceTeam )
				continue;
			const CUtlVector< CFFNavArea * > *flagList = mesh->GetFlagAreas( targetTeam );
			if ( !flagList || flagList->Count() == 0 )
				continue;
			CFFNavArea *flag = ( *flagList )[ 0 ];

			CNavArea *closest = NULL;
			if ( !NavAreaBuildPath( spawn, flag, NULL, cost, &closest ) )
			{
				Msg( "  ! No path from team %d spawn to team %d flag\n", sourceTeam, targetTeam );
				++problems;
				++connectivityFails;
			}
		}
	}

	// Tag-counts summary.
	int tagCounts[ 32 ] = { 0 };
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area ) continue;
		const int t = area->GetFFTags();
		for ( int b = 0; b < 32; ++b )
			if ( t & ( 1 << b ) ) ++tagCounts[ b ];
	}

	Msg( "\n[ff_nav_validate] Coverage:\n" );
	Msg( "    teamspawn entities:   %d (missing nav: %d)\n", spawns, spawnMissing );
	Msg( "    info_ff_script goals: %d (missing nav: %d)\n", infoScripts, infoMissing );
	Msg( "    connectivity failures: %d\n", connectivityFails );
	Msg( "[ff_nav_validate] Tag counts:\n" );
	int knownBits[] = { 0,1,2,3, 4,5,6,7, 8,9,10,11, 12,13,14,15,16, 17, 18,19,20,21, 22,23,24, 25,26,27 };
	const char *bitNames[] = {
		"SPAWN_BLUE","SPAWN_RED","SPAWN_YELLOW","SPAWN_GREEN",
		"FLAG_BLUE","FLAG_RED","FLAG_YELLOW","FLAG_GREEN",
		"CAP_BLUE","CAP_RED","CAP_YELLOW","CAP_GREEN",
		"RESUPPLY","AMMO","ARMOR","HEALTH","GRENADES",
		"VIP_GOAL",
		"NO_BUILD","SENTRY_HINT","SNIPER_HINT","DETPACKABLE_DOOR",
		"WATER","BACKDOOR","MANCANNON",
		"CHOKE","NEAR_DOOR","INTERCEPT_LANE",
	};
	for ( int i = 0; i < (int)( sizeof( knownBits ) / sizeof( int ) ); ++i )
	{
		if ( tagCounts[ knownBits[ i ] ] > 0 )
			Msg( "    %-22s %d\n", bitNames[ i ], tagCounts[ knownBits[ i ] ] );
	}

	Msg( "[ff_nav_validate] %s (%d issue%s)\n",
		problems == 0 ? "OK" : "ISSUES FOUND",
		problems, problems == 1 ? "" : "s" );
}


//-----------------------------------------------------------------------------
// ff_nav_visualize <type> — draw debug overlays for a tag class.
// type: spawn, flag, cap, resupply, sniper, sentry, water, backdoor,
//       choke, near_door, danger, all
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_visualize, "Visualize FF nav tags. Args: spawn|flag|cap|resupply|sniper|sentry|water|backdoor|choke|near_door|danger|all", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_visualize <type>\n" );
		return;
	}
	const char *type = args.Arg( 1 );

	int tagFilter = 0;
	int color[3] = { 255, 255, 255 };
	bool dangerMode = false;

	if ( FStrEq( type, "spawn" ) )      { tagFilter = FF_NAV_SPAWN_ANY;       color[0]=128; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "flag" ) )  { tagFilter = FF_NAV_FLAG_ANY;        color[0]=255; color[1]=255; color[2]=64;  }
	else if ( FStrEq( type, "cap" ) )   { tagFilter = FF_NAV_CAP_ANY;         color[0]=255; color[1]=128; color[2]=64;  }
	else if ( FStrEq( type, "resupply" ) ) { tagFilter = FF_NAV_RESUPPLY;     color[0]=64;  color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "sniper" ) ){ tagFilter = FF_NAV_SNIPER_HINT;     color[0]=255; color[1]=64;  color[2]=255; }
	else if ( FStrEq( type, "sentry" ) ){ tagFilter = FF_NAV_SENTRY_HINT;     color[0]=255; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "water" ) ) { tagFilter = FF_NAV_WATER;           color[0]=64;  color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "backdoor" ) ){ tagFilter = FF_NAV_BACKDOOR;      color[0]=180; color[1]=64;  color[2]=180; }
	else if ( FStrEq( type, "choke" ) ) { tagFilter = FF_NAV_CHOKE;           color[0]=255; color[1]=128; color[2]=0;   }
	else if ( FStrEq( type, "near_door" ) ){ tagFilter = FF_NAV_NEAR_DOOR;    color[0]=128; color[1]=64;  color[2]=64;  }
	else if ( FStrEq( type, "danger" ) ) { dangerMode = true; }
	else if ( FStrEq( type, "all" ) )   { tagFilter = -1; }
	else { Msg( "Unknown type '%s'\n", type ); return; }

	int drawn = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;

		float intensity = 0.0f;
		if ( dangerMode )
		{
			const float d = area->GetDangerScore();
			if ( d <= 0.5f ) continue;
			intensity = MIN( 1.0f, d / 10.0f );
			color[0] = (int)( 255 * intensity );
			color[1] = 0;
			color[2] = 0;
		}
		else if ( tagFilter == -1 )
		{
			if ( area->GetFFTags() == 0 ) continue;
			intensity = 1.0f;
		}
		else
		{
			if ( !area->HasFFTag( tagFilter ) ) continue;
			intensity = 1.0f;
		}

		const Vector center = area->GetCenter();
		NDebugOverlay::Box( center, Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
			color[0], color[1], color[2], 64, 30.0f );
		++drawn;
	}
	Msg( "[ff_nav_visualize] Drew %d areas for '%s'.\n", drawn, type );
}


//-----------------------------------------------------------------------------
// ff_bot_diagnose [name] — print state for a bot. With no name, prints
// for every bot.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_bot_diagnose, "Print state for a bot. Usage: ff_bot_diagnose [name]", FCVAR_CHEAT )
{
	const char *filter = ( args.ArgC() >= 2 ) ? args.Arg( 1 ) : NULL;
	int found = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->IsBot() )
			continue;
		CFFBot *bot = dynamic_cast< CFFBot * >( base );
		if ( !bot )
			continue;
		if ( filter && !V_stristr( bot->GetPlayerName(), filter ) )
			continue;
		++found;

		Msg( "[ff_bot_diagnose] %s\n", bot->GetPlayerName() );
		Msg( "    team=%d class=%d hp=%d/%d alive=%d\n",
			bot->GetTeamNumber(), bot->GetClassSlot(),
			bot->GetHealth(), bot->GetMaxHealth(), bot->IsAlive() );
		Msg( "    pos=(%.0f %.0f %.0f) vel=%.0f\n",
			bot->GetAbsOrigin().x, bot->GetAbsOrigin().y, bot->GetAbsOrigin().z,
			bot->GetAbsVelocity().Length() );
		Msg( "    routeSeed=%u lastUnstuck=%.1fs ago\n",
			bot->m_routeSeed, gpGlobals->curtime - bot->m_lastUnstuckTime );

		// Active threat.
		IVision *vis = bot->GetVisionInterface();
		const CKnownEntity *threat = vis ? vis->GetPrimaryKnownThreat( false ) : NULL;
		if ( threat && threat->GetEntity() )
		{
			Msg( "    threat=%s dist=%.0f visible=%d\n",
				threat->GetEntity()->GetClassname(),
				( threat->GetEntity()->GetAbsOrigin() - bot->GetAbsOrigin() ).Length(),
				threat->IsVisibleInFOVNow() );
		}
		else
		{
			Msg( "    threat=(none)\n" );
		}

		// Status effects.
		Msg( "    cloaked=%d disguised=%d concussed=%d tranqed=%d infected=%d\n",
			bot->IsCloaked(), bot->IsDisguised(), bot->IsConcussed(),
			bot->IsTranqed(), bot->IsInfected() );

		// Building state.
		if ( bot->GetSentryGun() )
			Msg( "    sentry: hp=%d\n", bot->GetSentryGun()->GetHealth() );
		if ( bot->GetDispenser() )
			Msg( "    dispenser built\n" );
		if ( bot->GetDetpack() )
			Msg( "    detpack deployed\n" );
	}

	Msg( "[ff_bot_diagnose] Inspected %d bot%s.\n", found, found == 1 ? "" : "s" );
}
