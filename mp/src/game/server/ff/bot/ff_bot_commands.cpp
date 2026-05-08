//========= Fortress Forever Bot =============================================//
//
// FFBot console commands — validation, visualization, diagnose.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
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

	// Connectivity: every spawn should reach every other team's flag.
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

	// Attribute-counts summary — only the bits we still set.
	struct { int bit; const char *name; } known[] = {
		{ FF_NAV_BLOCKED,            "BLOCKED" },
		{ FF_NAV_SPAWN_ROOM_BLUE,    "SPAWN_ROOM_BLUE" },
		{ FF_NAV_SPAWN_ROOM_RED,     "SPAWN_ROOM_RED" },
		{ FF_NAV_SPAWN_ROOM_YELLOW,  "SPAWN_ROOM_YELLOW" },
		{ FF_NAV_SPAWN_ROOM_GREEN,   "SPAWN_ROOM_GREEN" },
		{ FF_NAV_SPAWN_ROOM_EXIT,    "SPAWN_ROOM_EXIT" },
		{ FF_NAV_HAS_AMMO,           "HAS_AMMO" },
		{ FF_NAV_HAS_HEALTH,         "HAS_HEALTH" },
		{ FF_NAV_HAS_ARMOR,          "HAS_ARMOR" },
		{ FF_NAV_HAS_GRENADES,       "HAS_GRENADES" },
		{ FF_NAV_FLAG_BLUE,          "FLAG_BLUE" },
		{ FF_NAV_FLAG_RED,           "FLAG_RED" },
		{ FF_NAV_FLAG_YELLOW,        "FLAG_YELLOW" },
		{ FF_NAV_FLAG_GREEN,         "FLAG_GREEN" },
		{ FF_NAV_CAP_BLUE,           "CAP_BLUE" },
		{ FF_NAV_CAP_RED,            "CAP_RED" },
		{ FF_NAV_CAP_YELLOW,         "CAP_YELLOW" },
		{ FF_NAV_CAP_GREEN,          "CAP_GREEN" },
		{ FF_NAV_SNIPER_SPOT,        "SNIPER_SPOT" },
		{ FF_NAV_SENTRY_SPOT,        "SENTRY_SPOT" },
		{ FF_NAV_HUNTED_ESCAPE,      "HUNTED_ESCAPE" },
		{ FF_NAV_NO_SPAWNING,        "NO_SPAWNING" },
		{ FF_NAV_UNBLOCKABLE,        "UNBLOCKABLE" },
	};
	const int kKnown = (int)( sizeof( known ) / sizeof( known[ 0 ] ) );
	int counts[ 32 ] = { 0 };
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area ) continue;
		const unsigned int t = area->GetAttributesFF();
		for ( int k = 0; k < kKnown; ++k )
		{
			if ( t & known[ k ].bit ) ++counts[ k ];
		}
	}

	Msg( "\n[ff_nav_validate] Coverage:\n" );
	Msg( "    teamspawn entities:   %d (missing nav: %d)\n", spawns, spawnMissing );
	Msg( "    info_ff_script goals: %d (missing nav: %d)\n", infoScripts, infoMissing );
	Msg( "    connectivity failures: %d\n", connectivityFails );
	Msg( "[ff_nav_validate] Attribute counts:\n" );
	for ( int k = 0; k < kKnown; ++k )
	{
		if ( counts[ k ] > 0 )
			Msg( "    %-22s %d\n", known[ k ].name, counts[ k ] );
	}

	Msg( "[ff_nav_validate] %s (%d issue%s)\n",
		problems == 0 ? "OK" : "ISSUES FOUND",
		problems, problems == 1 ? "" : "s" );
}


//-----------------------------------------------------------------------------
// ff_nav_visualize <type> — draw debug overlays for an attribute class.
// type: spawn, exit, flag, cap, resupply, sniper, sentry, combat, all
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_visualize, "Visualize FF nav attributes. Args: spawn|exit|flag|cap|resupply|sniper|sentry|combat|all", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_visualize <type>\n" );
		return;
	}
	const char *type = args.Arg( 1 );

	int filter = 0;
	int color[ 3 ] = { 255, 255, 255 };
	bool combatMode = false;

	if ( FStrEq( type, "spawn" ) )         { filter = FF_NAV_SPAWN_ROOM_ANY; color[0]=128; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "exit" ) )     { filter = FF_NAV_SPAWN_ROOM_EXIT; color[0]=128; color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "flag" ) )     { filter = FF_NAV_FLAG_ANY;       color[0]=255; color[1]=255; color[2]=64;  }
	else if ( FStrEq( type, "cap" ) )      { filter = FF_NAV_CAP_ANY;        color[0]=255; color[1]=128; color[2]=64;  }
	else if ( FStrEq( type, "resupply" ) ) { filter = FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH | FF_NAV_HAS_ARMOR | FF_NAV_HAS_GRENADES; color[0]=64; color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "sniper" ) )   { filter = FF_NAV_SNIPER_SPOT;    color[0]=255; color[1]=64;  color[2]=255; }
	else if ( FStrEq( type, "sentry" ) )   { filter = FF_NAV_SENTRY_SPOT;    color[0]=255; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "combat" ) )   { combatMode = true; }
	else if ( FStrEq( type, "all" ) )      { filter = -1; }
	else { Msg( "Unknown type '%s'\n", type ); return; }

	int drawn = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;

		if ( combatMode )
		{
			const float intensity = area->GetCombatIntensity();
			if ( intensity <= 0.05f ) continue;
			color[ 0 ] = (int)( 255 * intensity );
			color[ 1 ] = 0;
			color[ 2 ] = 0;
		}
		else if ( filter == -1 )
		{
			if ( area->GetAttributesFF() == 0 ) continue;
		}
		else
		{
			if ( !area->HasAttributeFF( filter ) ) continue;
		}

		const Vector center = area->GetCenter();
		NDebugOverlay::Box( center, Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
			color[ 0 ], color[ 1 ], color[ 2 ], 64, 30.0f );
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
