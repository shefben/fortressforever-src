//========= Fortress Forever Bot =============================================//
//
// FFBot console commands — validation, visualization, diagnose.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_helpers.h"
#include "ff_bot_intel.h"
#include "ff_bot_autotag.h"
#include "ff_bot_learned_links.h"
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
#include "eiface.h"		// for engine->ServerCommand

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Debug overlay toggles.
//
// These exist because every navigation failure in this subsystem used to be
// invisible from the outside: "bot has no path at all" and "bot has a path it
// can't follow" look identical when you're watching it walk into a wall.
//
// bot_show_path draws, per bot:
//   yellow line + cross  — current path goal segment
//   red box overhead     — NO PATH (the failure that used to look like "stuck")
//   cyan line + label    — the movement arbiter is driving, and which system
//                          published the override
//   "STUCK stage N"      — position in the recovery escalation ladder
//
// bot_show_threat draws:
//   magenta line         — current primary threat
//   orange cross         — last-known-position memory
//-----------------------------------------------------------------------------
static void SetBotDebugFlag( const CCommand &args, bool CFFBot::*flag, const char *name )
{
	// No argument = toggle for all bots. Argument = bot name substring.
	const char *filter = ( args.ArgC() > 1 ) ? args[ 1 ] : NULL;

	int affected = 0;
	bool newValue = true;
	bool haveNewValue = false;

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || !player->IsBot() )
			continue;

		CFFBot *bot = dynamic_cast< CFFBot * >( player );
		if ( !bot )
			continue;

		const char *botName = bot->GetPlayerName();
		if ( filter && ( !botName || !Q_stristr( botName, filter ) ) )
			continue;

		// Derive the toggle target from the first matching bot so a group of
		// bots ends up in a consistent state rather than alternating.
		if ( !haveNewValue )
		{
			newValue = !( bot->*flag );
			haveNewValue = true;
		}

		bot->*flag = newValue;
		++affected;
	}

	if ( affected == 0 )
	{
		Msg( "[%s] no matching bots.\n", name );
		return;
	}
	Msg( "[%s] %s for %d bot(s).\n", name, newValue ? "ON" : "OFF", affected );
}

CON_COMMAND_F( bot_show_path,
	"Toggle path / movement-arbiter overlays for bots. Optional arg: bot name substring.",
	FCVAR_CHEAT )
{
	SetBotDebugFlag( args, &CFFBot::m_debugShowPath, "bot_show_path" );
}

CON_COMMAND_F( bot_show_threat,
	"Toggle threat and last-known-position overlays for bots. Optional arg: bot name substring.",
	FCVAR_CHEAT )
{
	SetBotDebugFlag( args, &CFFBot::m_debugShowThreat, "bot_show_threat" );
}


//-----------------------------------------------------------------------------
// ff_bot_nav_report — the single most useful diagnostic in this subsystem.
//
// If a team reports exits=0, its spawn room is a disconnected nav island and
// the bots' problem is the mesh, not the AI. Regenerate with nav_generate
// (which now forces doors open first) and re-check.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_bot_nav_report,
	"Print per-team spawn / exit / threshold / reachability counts for the bot nav mesh.",
	FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh )
	{
		Msg( "[ff_bot_nav_report] No FF nav mesh.\n" );
		return;
	}
	mesh->PrintNavReport();
}


//-----------------------------------------------------------------------------
// Learned-link inspection (FIX 11).
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_bot_links_report,
	"List nav connections learned by watching players move on this map.",
	FCVAR_CHEAT )
{
	FFBotLearnedLinks::PrintReport();
}

CON_COMMAND_F( ff_bot_links_clear,
	"Discard all learned nav links for this map (use after the map is updated).",
	FCVAR_CHEAT )
{
	FFBotLearnedLinks::Clear();
}

CON_COMMAND_F( ff_bot_links_save,
	"Write learned nav links for this map to disk now.",
	FCVAR_CHEAT )
{
	FFBotLearnedLinks::Save();
}


//-----------------------------------------------------------------------------
// Manual sniper-spot authoring.
//
// The auto-discovery heuristics get a map's real sniping positions right most
// of the time, but "most of the time" isn't good enough for a signature spot
// like a 2fort battlement. FF_NAV_SNIPER_SPOT is in FF_NAV_PERSISTENT_
// ATTRIBUTES, so anything marked here survives in the .nav file once you run
// nav_save, and tier 1 of CFFBotSniperLurk::FindNewHome prefers these over
// anything it works out for itself.
//
// Workflow:
//   1. walk to the exact spot you want snipers to hold
//   2. ff_nav_mark_sniper
//   3. repeat for each position
//   4. nav_save
//-----------------------------------------------------------------------------
static CFFNavArea *NavAreaUnderCommandClient( void )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player )
	{
		Msg( "Must be issued by a player.\n" );
		return NULL;
	}
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
	{
		Msg( "No nav mesh loaded.\n" );
		return NULL;
	}

	CNavArea *area = TheNavMesh->GetNearestNavArea( player->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY );
	if ( !area )
	{
		Msg( "No nav area within 256u of you.\n" );
		return NULL;
	}
	return static_cast< CFFNavArea * >( area );
}

CON_COMMAND_F( ff_nav_mark_sniper,
	"Tag the nav area you're standing in as a sniper position. Run nav_save to persist.",
	FCVAR_CHEAT )
{
	CFFNavArea *area = NavAreaUnderCommandClient();
	if ( !area )
		return;

	area->SetAttributeFF( FF_NAV_SNIPER_SPOT );
	Msg( "[ff_nav_mark_sniper] area %d tagged FF_NAV_SNIPER_SPOT. "
	     "Run nav_save to write it to the .nav file.\n", area->GetID() );
}

CON_COMMAND_F( ff_nav_unmark_sniper,
	"Remove the sniper-position tag from the nav area you're standing in.",
	FCVAR_CHEAT )
{
	CFFNavArea *area = NavAreaUnderCommandClient();
	if ( !area )
		return;

	area->ClearAttributeFF( FF_NAV_SNIPER_SPOT | FF_NAV_AUTO_SNIPER_SPOT );
	Msg( "[ff_nav_unmark_sniper] area %d cleared. Run nav_save to persist.\n", area->GetID() );
}


//-----------------------------------------------------------------------------
// ff_bot_sniper_report — why did the snipers pick what they picked?
//
// Prints every candidate the picker considers, with the signals it scores on,
// so a wrong choice can be diagnosed against the actual map instead of guessed
// at. Marks the area you're standing in so you can compare "the spot I think
// they should use" against what the heuristics see.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_bot_sniper_report,
	"List sniper-spot candidates with their scoring signals.",
	FCVAR_CHEAT )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
	{
		Msg( "No nav mesh loaded.\n" );
		return;
	}

	CBasePlayer *player = UTIL_GetCommandClient();
	CNavArea *hereArea = player ? TheNavMesh->GetNearestNavArea(
		player->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY ) : NULL;

	int tagged = 0, autoTagged = 0, highGround = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		const unsigned int a = area->GetAttributesFF();
		if ( a & FF_NAV_SNIPER_SPOT )       ++tagged;
		if ( a & FF_NAV_AUTO_SNIPER_SPOT )  ++autoTagged;
		if ( a & FF_NAV_HIGH_GROUND )       ++highGround;
	}

	Msg( "==== FF sniper spots ====\n" );
	Msg( "  hand-tagged=%d  auto-tagged=%d  high-ground=%d  (of %d areas)\n",
		tagged, autoTagged, highGround, TheNavAreas.Count() );

	if ( tagged == 0 && autoTagged == 0 )
	{
		Msg( "  NO SNIPER SPOTS AT ALL — bots fall through to the high-ground\n"
		     "  and cap-area tiers. Use ff_nav_mark_sniper to author them.\n" );
	}

	if ( hereArea )
	{
		CFFNavArea *here = static_cast< CFFNavArea * >( hereArea );
		const unsigned int a = here->GetAttributesFF();
		Msg( "  you are in area %d: %s%s%s%s\n",
			here->GetID(),
			( a & FF_NAV_SNIPER_SPOT )      ? "SNIPER_SPOT "      : "",
			( a & FF_NAV_AUTO_SNIPER_SPOT ) ? "AUTO_SNIPER_SPOT " : "",
			( a & FF_NAV_HIGH_GROUND )      ? "HIGH_GROUND "      : "",
			( a & FF_NAV_SPAWN_ROOM_ANY )   ? "SPAWN_ROOM "       : "" );
		Msg( "    If the spot you want is missing HIGH_GROUND / SNIPER_SPOT,\n"
		     "    that is why the bots don't consider it.\n" );
	}
	Msg( "=========================\n" );
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

	// Check flag/cap/resupply entities have nav coverage. For "missing"
	// entries, dig deeper: trace down from the entity to find ground, and
	// look for the closest nav area within a wider 2048u sphere. This
	// distinguishes "entity floating intentionally on a tall pillar
	// (nav exists at the base, just not at the top)" from "entity in a
	// dead spot the bot can't reach at all."
	static const char * const kGoalNames[] = {
		"None", "BackPack_Ammo", "BackPack_Armor", "BackPack_Health",
		"BackPack_Grenades", "Flag", "FlagCap", "HuntedEscape",
	};
	int infoScripts = 0, infoMissing = 0;
	e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *s = static_cast< CFFInfoScript * >( e );
		const int goalType = s->GetBotGoalType();
		if ( goalType == Omnibot::kNone )
			continue;
		++infoScripts;
		const Vector entPos = e->GetAbsOrigin();
		CNavArea *nearTight = TheNavMesh->GetNearestNavArea( entPos, false, 384.0f, false, true, TEAM_ANY );
		if ( nearTight )
			continue;
		++infoMissing;
		++problems;

		// Trace straight down to find ground. If we hit it within 2048u,
		// report the height — useful for the user to know "this flag is
		// 500u up, mappers want it reached via a tower/pad."
		trace_t tr;
		UTIL_TraceLine( entPos, entPos - Vector( 0, 0, 4096.0f ),
			MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		const float groundHeight = ( tr.fraction < 1.0f )
			? ( entPos.z - tr.endpos.z )
			: -1.0f;

		// Closest nav within 2048u (wider sphere for reachability hints).
		CNavArea *nearWide = TheNavMesh->GetNearestNavArea( entPos, false, 2048.0f, false, true, TEAM_ANY );
		const float wideDist = nearWide
			? ( nearWide->GetCenter() - entPos ).Length()
			: -1.0f;

		const char *goalName =
			( goalType >= 0 && goalType < ARRAYSIZE( kGoalNames ) )
			? kGoalNames[ goalType ]
			: "Unknown";

		Msg( "  ! info_ff_script (%s, goalType %d) at (%.0f %.0f %.0f): "
			"no nav within 384u\n",
			goalName, goalType, entPos.x, entPos.y, entPos.z );
		if ( groundHeight >= 0.0f )
			Msg( "      ground %.0fu below entity (likely on a raised "
				"platform/tower)\n", groundHeight );
		else
			Msg( "      no ground beneath entity in 4096u (truly floating "
				"in skybox/void)\n" );
		if ( wideDist >= 0.0f )
			Msg( "      closest nav area %.0fu away — reachable nav exists "
				"nearby; mark walkable + regenerate to extend coverage\n",
				wideDist );
		else
			Msg( "      no nav within 2048u — entity is in an isolated "
				"region the sampler never reached\n" );
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
		{ FF_NAV_WATER,              "WATER" },
		{ FF_NAV_UNDERWATER,         "UNDERWATER" },
		{ FF_NAV_CHOKE,              "CHOKE" },
		{ FF_NAV_HIGH_GROUND,        "HIGH_GROUND" },
		{ FF_NAV_AUTO_SNIPER_SPOT,   "AUTO_SNIPER_SPOT" },
		{ FF_NAV_AUTO_SENTRY_SPOT,   "AUTO_SENTRY_SPOT" },
		{ FF_NAV_NEAR_LADDER,        "NEAR_LADDER" },
	};
	// Second attribute word — hand-authored markers (FFNavBuilder). Reported
	// separately because "the author told us" and "we worked it out" are very
	// different things to see zero of.
	struct { int bit; const char *name; } known2[] = {
		{ FF_NAV2_MANUAL,            "MANUAL (any marker)" },
		{ FF_NAV2_DISPENSER_SPOT,    "DISPENSER_SPOT" },
		{ FF_NAV2_DETPACK_SPOT,      "DETPACK_SPOT" },
		{ FF_NAV2_CAP_NEUTRAL,       "CAP_NEUTRAL" },
		{ FF_NAV2_HAZARD_GEAR,       "HAZARD_GEAR" },
		{ FF_NAV2_DANGER,            "DANGER" },
		{ FF_NAV2_JUMP_SPOT,         "JUMP_SPOT" },
		{ FF_NAV2_WATER_EXIT,        "WATER_EXIT" },
		{ FF_NAV2_DEFEND_BLUE,       "DEFEND_BLUE" },
		{ FF_NAV2_DEFEND_RED,        "DEFEND_RED" },
		{ FF_NAV2_DEFEND_YELLOW,     "DEFEND_YELLOW" },
		{ FF_NAV2_DEFEND_GREEN,      "DEFEND_GREEN" },
		{ FF_NAV2_DETPACK_SEAL,      "DETPACK_SEAL" },
		{ FF_NAV2_PIPETRAP,          "PIPETRAP" },
		{ FF_NAV2_AIM_HINT,          "AIM_HINT" },
		{ FF_NAV2_LIFT,              "LIFT (auto+manual)" },
		{ FF_NAV2_HAZARD_ZONE,       "HAZARD_ZONE (auto)" },
	};

	const int kKnown = (int)( sizeof( known ) / sizeof( known[ 0 ] ) );
	const int kKnown2 = (int)( sizeof( known2 ) / sizeof( known2[ 0 ] ) );
	int counts[ 32 ] = { 0 };
	int counts2[ 32 ] = { 0 };
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area ) continue;
		const unsigned int t = area->GetAttributesFF();
		for ( int k = 0; k < kKnown; ++k )
		{
			if ( t & known[ k ].bit ) ++counts[ k ];
		}
		const unsigned int t2 = area->GetAttributesFF2();
		for ( int k = 0; k < kKnown2; ++k )
		{
			if ( t2 & known2[ k ].bit ) ++counts2[ k ];
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

	Msg( "[ff_nav_validate] Hand-authored markers (maps/<map>.ffnavpoints):\n" );
	if ( counts2[ 0 ] == 0 )
	{
		Msg( "    none — ff_manual_nav_builder 1 to author some.\n" );
	}
	else
	{
		for ( int k = 0; k < kKnown2; ++k )
		{
			if ( counts2[ k ] > 0 )
				Msg( "    %-22s %d\n", known2[ k ].name, counts2[ k ] );
		}
	}

	Msg( "[ff_nav_validate] %s (%d issue%s)\n",
		problems == 0 ? "OK" : "ISSUES FOUND",
		problems, problems == 1 ? "" : "s" );
}


//-----------------------------------------------------------------------------
// ff_nav_visualize <type> — draw debug overlays for an attribute class.
// type: spawn, exit, flag, cap, resupply, sniper, sentry, combat, all
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_visualize, "Visualize FF nav attributes. Args: spawn|exit|flag|cap|resupply|sniper|sentry|combat|water|underwater|choke|highground|autosniper|autosentry|ladder|doorway|dispenser|detpack|detpackseal|pipetrap|aim|lift|hazardzone|capneutral|gassuit|defend|danger|jump|waterexit|manual|all", FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_visualize <type>\n" );
		return;
	}
	const char *type = args.Arg( 1 );

	int filter = 0;
	// Second attribute word (FF_NAV2_*, hand-authored via FFNavBuilder). A
	// separate filter because the two words are separate bit spaces — ORing an
	// FF_NAV2_ constant into 'filter' would silently match the wrong attribute.
	int filter2 = 0;
	int color[ 3 ] = { 255, 255, 255 };
	bool combatMode = false;

	if ( FStrEq( type, "spawn" ) )         { filter = FF_NAV_SPAWN_ROOM_ANY; color[0]=128; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "exit" ) )     { filter = FF_NAV_SPAWN_ROOM_EXIT; color[0]=128; color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "flag" ) )     { filter = FF_NAV_FLAG_ANY;       color[0]=255; color[1]=255; color[2]=64;  }
	else if ( FStrEq( type, "cap" ) )      { filter = FF_NAV_CAP_ANY;        color[0]=255; color[1]=128; color[2]=64;  }
	else if ( FStrEq( type, "resupply" ) ) { filter = FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH | FF_NAV_HAS_ARMOR | FF_NAV_HAS_GRENADES; color[0]=64; color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "sniper" ) )      { filter = FF_NAV_SNIPER_SPOT | FF_NAV_AUTO_SNIPER_SPOT; color[0]=255; color[1]=64;  color[2]=255; }
	else if ( FStrEq( type, "sentry" ) )      { filter = FF_NAV_SENTRY_SPOT | FF_NAV_AUTO_SENTRY_SPOT; color[0]=255; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "autosniper" ) )  { filter = FF_NAV_AUTO_SNIPER_SPOT;  color[0]=255; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "autosentry" ) )  { filter = FF_NAV_AUTO_SENTRY_SPOT;  color[0]=200; color[1]=200; color[2]=64;  }
	else if ( FStrEq( type, "water" ) )       { filter = FF_NAV_WATER;             color[0]=64;  color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "underwater" ) )  { filter = FF_NAV_UNDERWATER;        color[0]=32;  color[1]=64;  color[2]=200; }
	else if ( FStrEq( type, "choke" ) )       { filter = FF_NAV_CHOKE;             color[0]=255; color[1]=200; color[2]=64;  }
	else if ( FStrEq( type, "highground" ) )  { filter = FF_NAV_HIGH_GROUND;       color[0]=128; color[1]=255; color[2]=255; }
	else if ( FStrEq( type, "ladder" ) )      { filter = FF_NAV_NEAR_LADDER;       color[0]=255; color[1]=255; color[2]=128; }
	else if ( FStrEq( type, "doorway" ) )     { filter = FF_NAV_DOORWAY;           color[0]=255; color[1]=255; color[2]=255; }
	// Hand-authored (FFNavBuilder). Colours match the authoring overlay so the
	// two views agree.
	else if ( FStrEq( type, "dispenser" ) )   { filter2 = FF_NAV2_DISPENSER_SPOT;  color[0]=255; color[1]=255; color[2]=0;   }
	else if ( FStrEq( type, "detpack" ) )     { filter2 = FF_NAV2_DETPACK_SPOT;    color[0]=255; color[1]=0;   color[2]=0;   }
	else if ( FStrEq( type, "capneutral" ) )  { filter2 = FF_NAV2_CAP_NEUTRAL;     color[0]=176; color[1]=176; color[2]=176; }
	else if ( FStrEq( type, "gassuit" ) )     { filter2 = FF_NAV2_HAZARD_GEAR;     color[0]=0;   color[1]=255; color[2]=0;   }
	else if ( FStrEq( type, "defend" ) )      { filter2 = FF_NAV2_DEFEND_ANY;      color[0]=0;   color[1]=160; color[2]=255; }
	else if ( FStrEq( type, "danger" ) )      { filter2 = FF_NAV2_DANGER;          color[0]=255; color[1]=48;  color[2]=112; }
	else if ( FStrEq( type, "jump" ) )        { filter2 = FF_NAV2_JUMP_SPOT;       color[0]=192; color[1]=128; color[2]=255; }
	else if ( FStrEq( type, "waterexit" ) )   { filter2 = FF_NAV2_WATER_EXIT;      color[0]=0;   color[1]=255; color[2]=255; }
	else if ( FStrEq( type, "manual" ) )      { filter2 = FF_NAV2_MANUAL;          color[0]=255; color[1]=128; color[2]=0;   }
	else if ( FStrEq( type, "detpackseal" ) ) { filter2 = FF_NAV2_DETPACK_SEAL;    color[0]=200; color[1]=32;  color[2]=96;  }
	else if ( FStrEq( type, "pipetrap" ) )    { filter2 = FF_NAV2_PIPETRAP;        color[0]=255; color[1]=96;  color[2]=32;  }
	else if ( FStrEq( type, "aim" ) )         { filter2 = FF_NAV2_AIM_HINT;        color[0]=255; color[1]=255; color[2]=192; }
	else if ( FStrEq( type, "lift" ) )        { filter2 = FF_NAV2_LIFT;            color[0]=128; color[1]=160; color[2]=192; }
	else if ( FStrEq( type, "hazardzone" ) )  { filter2 = FF_NAV2_HAZARD_ZONE;     color[0]=255; color[1]=0;   color[2]=64;  }
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
			if ( area->GetAttributesFF() == 0 && area->GetAttributesFF2() == 0 ) continue;
		}
		else if ( filter2 != 0 )
		{
			if ( !area->HasAttributeFF2( filter2 ) ) continue;
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
// ff_nav_autotag — re-run the heuristic auto-tagger on the current nav.
// Useful after editing nav (e.g. running nav_mark_walkable + nav_generate
// fragments) to refresh water / sniper / sentry / choke / high-ground tags
// without a full level reload.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_autotag, "Re-run heuristic auto-tagging on the current nav.", FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "[ff_nav_autotag] Nav mesh not loaded.\n" );
		return;
	}

	// Clear the heuristic bits before re-stamping so removed criteria
	// don't leave stale tags. Entity-derived bits (spawn rooms, flags,
	// caps, resupplies) are NOT cleared — those need a full reload.
	const unsigned int kHeuristicBits =
		FF_NAV_WATER | FF_NAV_UNDERWATER | FF_NAV_CHOKE |
		FF_NAV_HIGH_GROUND | FF_NAV_AUTO_SNIPER_SPOT |
		FF_NAV_AUTO_SENTRY_SPOT | FF_NAV_NEAR_LADDER;

	// Word 2: only the bits the auto-tagger owns outright. FF_NAV2_LIFT and
	// FF_NAV2_HAZARD_GEAR are shared with the manual builder, so clearing them
	// here would silently drop hand-authored markers that the sidecar isn't
	// about to re-apply — ff_nav_autotag doesn't re-run FFNavBuilder.
	// FF_NAV2_HAZARD_ZONE is purely derived from trigger_hurt entities and is
	// safe to wipe and rebuild.
	const unsigned int kAutoBits2 = FF_NAV2_HAZARD_ZONE;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		area->ClearAttributeFF( kHeuristicBits );
		area->ClearAttributeFF2( kAutoBits2 );
	}

	CFFBotAutoTagger::TagAllAreas( mesh );
	Msg( "[ff_nav_autotag] Auto-tagging complete.\n" );
}


//-----------------------------------------------------------------------------
// ff_nav_generate_full — fire-and-forget nav generation pipeline. Equivalent
// to running, in order:
//
//   sv_cheats 1
//   nav_generate           (Source's built-in walker; reloads the map)
//   ff_nav_autotag         (auto-fires from CFFBotTagger on level init)
//   ff_nav_validate        (coverage / connectivity report)
//
// Since `nav_generate` triggers a map reload, the chained autotag + validate
// happen automatically the next time we're in OnServerActivate — this is
// just a convenience wrapper that issues nav_generate and prints a reminder
// of what'll fire afterward.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_generate_full, "Run nav_generate and (on map reload) auto-tag + validate.", FCVAR_CHEAT )
{
	Msg( "[ff_nav_generate_full] Issuing nav_generate. After the map reloads:\n" );
	Msg( "    - CFFBotTagger will stamp entity-derived tags (spawn/flag/cap/resupply)\n" );
	Msg( "    - CFFBotAutoTagger will stamp heuristic tags (water/sniper/sentry/choke)\n" );
	Msg( "    - run 'ff_nav_validate' to confirm coverage\n" );
	engine->ServerCommand( "nav_generate\n" );
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
