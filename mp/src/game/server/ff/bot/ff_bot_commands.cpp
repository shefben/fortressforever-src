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
		{ FF_NAV2_CUTPOINT,          "CUTPOINT (derived)" },
		{ FF_NAV2_HIGH_TRAFFIC,      "HIGH_TRAFFIC (derived)" },
		{ FF_NAV2_OVERLOOK,          "OVERLOOK (derived)" },
		{ FF_NAV2_BREACHABLE,        "BREACHABLE (derived)" },
		{ FF_NAV2_TELEPORT,          "TELEPORT (derived)" },
		{ FF_NAV2_PUSH,              "PUSH (derived)" },
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
//-----------------------------------------------------------------------------
// Visualisation type table.
//
// One row per drawable attribute, replacing what used to be a thirty-branch
// if/else chain. Two reasons it's a table now:
//
//   * `all` needs to pick a colour PER AREA rather than per command, which
//     means walking the same set of types the individual modes use. Two copies
//     of that list would drift, and a legend that disagrees with the view it
//     describes is worse than no legend.
//   * The colours in the chain had already drifted — `autosniper` and `sentry`
//     were both (255,128,255), which is invisible in a combined view and
//     confusing in a single one.
//
// ORDER IS SIGNIFICANT. `all` assigns each area the colour of the FIRST row it
// matches, so the rows run most-specific-first: objectives, then authored
// intent, then derived hints, then terrain. An area that is both a flag room
// and a choke reads as "flag room", which is the more useful fact about it.
//-----------------------------------------------------------------------------
struct NavVizType
{
	const char  *name;
	int          filter;	// FF_NAV_*  (word 1); 0 if this row uses word 2
	int          filter2;	// FF_NAV2_* (word 2); 0 if this row uses word 1
	int          r, g, b;
	const char  *description;
};

// Word 1 and word 2 are separate bit spaces. A row sets exactly one of the two
// filters — ORing an FF_NAV2_ constant into the word-1 filter would silently
// match the wrong attribute, which is why they were never merged.
static const NavVizType s_navVizTypes[] =
{
	// ---- Objectives: what the map is about --------------------------------
	{ "flag",        FF_NAV_FLAG_ANY,        0,                       255, 255,  64, "flag rest position (per team)" },
	{ "cap",         FF_NAV_CAP_ANY,         0,                       255, 128,  64, "capture point (per team)" },
	{ "capneutral",  0,                      FF_NAV2_CAP_NEUTRAL,     176, 176, 176, "capture point nobody owns" },
	{ "escape",      FF_NAV_HUNTED_ESCAPE,   0,                       255, 128, 192, "hunted VIP escape destination" },

	// ---- Spawns -----------------------------------------------------------
	{ "exit",        FF_NAV_SPAWN_ROOM_EXIT, 0,                       128, 255, 128, "spawn-room threshold (the door out)" },
	{ "spawn",       FF_NAV_SPAWN_ROOM_ANY,  0,                       128, 128, 255, "inside a spawn room (per team)" },
	{ "nospawn",     FF_NAV_NO_SPAWNING,     0,                        96,  96,  96, "never place a bot here" },

	// ---- Authored intent --------------------------------------------------
	{ "sniper",      FF_NAV_SNIPER_SPOT,     0,                       255,  64, 255, "hand-tagged sniper position" },
	{ "sentry",      FF_NAV_SENTRY_SPOT,     0,                       255, 128,  64, "hand-tagged sentry position" },
	{ "dispenser",   0,                      FF_NAV2_DISPENSER_SPOT,  255, 255,   0, "build a dispenser here" },
	{ "detpack",     0,                      FF_NAV2_DETPACK_SPOT,    255,   0,   0, "breakable wall - blow it OPEN" },
	{ "detpackseal", 0,                      FF_NAV2_DETPACK_SEAL,    200,  32,  96, "breakable wall - blow it SHUT" },
	{ "pipetrap",    0,                      FF_NAV2_PIPETRAP,        255,  96,  32, "demoman pipe-carpet position" },
	{ "defend",      0,                      FF_NAV2_DEFEND_ANY,        0, 160, 255, "hold this ground on defense" },
	{ "aim",         0,                      FF_NAV2_AIM_HINT,        255, 255, 192, "look this way from here" },
	{ "jump",        0,                      FF_NAV2_JUMP_SPOT,       192, 128, 255, "jump launch position" },
	{ "waterexit",   0,                      FF_NAV2_WATER_EXIT,        0, 255, 255, "the marked way out of the water" },
	{ "danger",      0,                      FF_NAV2_DANGER,          255,  48, 112, "author says stay out" },

	// ---- Entity-derived ---------------------------------------------------
	{ "gassuit",     0,                      FF_NAV2_HAZARD_GEAR,       0, 255,   0, "protective equipment is here" },
	{ "hazardzone",  0,                      FF_NAV2_HAZARD_ZONE,     255,   0,  64, "overlaps a trigger_hurt" },
	{ "lift",        0,                      FF_NAV2_LIFT,            128, 160, 192, "rides a moving platform" },
	{ "breach",      0,                      FF_NAV2_BREACHABLE,      255,  64,   0, "breakable wall that opens a real shortcut" },
	{ "teleport",    0,                      FF_NAV2_TELEPORT,        224,  64, 255, "trigger_teleport mouth or exit" },
	{ "push",        0,                      FF_NAV2_PUSH,            160, 255,  64, "trigger_push volume" },

	// ---- Derived structure (CFFBotAnalyzer) -------------------------------
	// Above the shape heuristics below because these are measurements and
	// those are guesses at the same questions.
	{ "cutpoint",    0,                      FF_NAV2_CUTPOINT,        255,  32,  32, "removing this would split the nav graph" },
	{ "overlook",    0,                      FF_NAV2_OVERLOOK,        255, 160, 255, "sees a lot of the ground people walk on" },
	{ "traffic",     0,                      FF_NAV2_HIGH_TRAFFIC,    255, 224,  96, "on a large share of spawn-to-objective routes" },
	{ "resupply",    FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH |
	                 FF_NAV_HAS_ARMOR | FF_NAV_HAS_GRENADES, 0,        64, 255, 128, "ammo / health / armor available" },
	{ "doorway",     FF_NAV_DOORWAY,         0,                       255, 255, 255, "an openable blocker overlaps this" },

	// ---- Heuristics -------------------------------------------------------
	{ "autosniper",  FF_NAV_AUTO_SNIPER_SPOT, 0,                      160,  32, 160, "heuristic sniper perch" },
	{ "autosentry",  FF_NAV_AUTO_SENTRY_SPOT, 0,                      200, 200,  64, "heuristic sentry choke" },
	{ "choke",       FF_NAV_CHOKE,           0,                       255, 200,  64, "narrow corridor / doorway" },
	{ "highground",  FF_NAV_HIGH_GROUND,     0,                       128, 255, 255, "elevated relative to neighbours" },
	{ "ladder",      FF_NAV_NEAR_LADDER,     0,                       255, 255, 128, "adjacent to a ladder" },

	// ---- Terrain: last, because almost everything can also be one of these -
	{ "underwater",  FF_NAV_UNDERWATER,      0,                        32,  64, 200, "must swim" },
	{ "water",       FF_NAV_WATER,           0,                        64, 128, 255, "feet-level water (wading)" },

	// ---- Meta -------------------------------------------------------------
	// Dead last on purpose: MANUAL is set alongside whatever a marker actually
	// meant, so matching it first would paint every authored area the same
	// colour and hide the thing you placed.
	{ "manual",      0,                      FF_NAV2_MANUAL,          255, 128,   0, "some manual marker touched this" },
};


static const NavVizType *FindNavVizType( const char *name )
{
	for ( int i = 0; i < (int)ARRAYSIZE( s_navVizTypes ); ++i )
	{
		if ( FStrEq( name, s_navVizTypes[ i ].name ) )
			return &s_navVizTypes[ i ];
	}
	return NULL;
}


static bool NavVizMatches( const NavVizType &type, CFFNavArea *area )
{
	if ( type.filter2 != 0 )
		return area->HasAttributeFF2( type.filter2 );
	return area->HasAttributeFF( type.filter );
}


static void PrintNavVizTypes( void )
{
	Msg( "Usage: ff_nav_visualize <type>\n" );
	Msg( "  all     every tagged area, coloured by what it is (see legend)\n" );
	Msg( "  combat  recent combat intensity, red by weight\n" );
	Msg( "  list    this list\n\n" );

	for ( int i = 0; i < (int)ARRAYSIZE( s_navVizTypes ); ++i )
	{
		const NavVizType &t = s_navVizTypes[ i ];
		Msg( "  %-12s rgb(%3d,%3d,%3d)  %s\n",
			t.name, t.r, t.g, t.b, t.description );
	}
}


//-----------------------------------------------------------------------------
// Persistent visualisation state.
//
// Overlays used to be issued once with a 30-second lifetime, which meant the
// view you were working against silently evaporated partway through whatever
// you were doing. It now redraws from FFBotManager_Tick until switched off,
// which is what a debug view is for.
//-----------------------------------------------------------------------------
ConVar ff_nav_visualize_persist( "ff_nav_visualize_persist", "1", FCVAR_CHEAT,
	"Keep the last ff_nav_visualize view drawn until it is switched off. "
	"0 = draw once and let it expire (the old behaviour)." );

// Index into s_navVizTypes, or one of the sentinels below.
#define FFVIZ_NONE		-1
#define FFVIZ_ALL		-2
#define FFVIZ_COMBAT	-3

static int   s_vizActive = FFVIZ_NONE;
static float s_vizNextDraw = 0.0f;

// How often the persistent view refreshes, and how long each batch lives.
// Lifetime is comfortably longer than the interval so nothing blinks.
#define FFVIZ_REDRAW_INTERVAL	0.25f
#define FFVIZ_DRAW_LIFETIME		( FFVIZ_REDRAW_INTERVAL * 3.0f )


//-----------------------------------------------------------------------------
// Draw one batch. `verbose` prints counts and the legend — true for the console
// command, false for the per-frame refresh, which must stay silent.
//-----------------------------------------------------------------------------
static void DrawNavVisualization( int which, bool verbose, float lifetime )
{
	if ( which == FFVIZ_NONE )
		return;

	// ---- combat: a continuous quantity, not an attribute ------------------
	if ( which == FFVIZ_COMBAT )
	{
		int drawn = 0;
		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area )
				continue;

			const float intensity = area->GetCombatIntensity();
			if ( intensity <= 0.05f )
				continue;

			NDebugOverlay::Box( area->GetCenter(), Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
				(int)( 255 * intensity ), 0, 0, 64, lifetime );
			++drawn;
		}
		if ( verbose )
			Msg( "[ff_nav_visualize] Drew %d areas for 'combat'.\n", drawn );
		return;
	}

	// ---- all: colour each area by the first category it matches -----------
	//
	// This used to draw everything white, which answered "is anything tagged
	// at all" and nothing else. Per-area colouring makes it the view you
	// actually want after a nav_generate: one look tells you whether the
	// spawns, flags and caps landed where they should have.
	if ( which == FFVIZ_ALL )
	{
		int perType[ ARRAYSIZE( s_navVizTypes ) ] = { 0 };
		int drawn = 0;
		int untagged = 0;
		int unnamed = 0;

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area )
				continue;

			if ( area->GetAttributesFF() == 0 && area->GetAttributesFF2() == 0 )
			{
				++untagged;
				continue;
			}

			// First match wins; the table is ordered most-specific-first.
			const NavVizType *match = NULL;
			int matchIndex = -1;
			for ( int t = 0; t < (int)ARRAYSIZE( s_navVizTypes ); ++t )
			{
				if ( NavVizMatches( s_navVizTypes[ t ], area ) )
				{
					match = &s_navVizTypes[ t ];
					matchIndex = t;
					break;
				}
			}

			// Tagged with something the table doesn't name — an attribute
			// added to the enum and not to this list. Grey, and counted, so it
			// shows up as a gap rather than silently vanishing.
			if ( !match )
			{
				NDebugOverlay::Box( area->GetCenter(), Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
					110, 110, 110, 64, lifetime );
				++drawn;
				++unnamed;
				continue;
			}

			NDebugOverlay::Box( area->GetCenter(), Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
				match->r, match->g, match->b, 64, lifetime );
			++perType[ matchIndex ];
			++drawn;
		}

		if ( !verbose )
			return;

		Msg( "[ff_nav_visualize] Drew %d of %d areas. %d carry no attribute at all.\n",
			drawn, TheNavAreas.Count(), untagged );
		Msg( "  Each area is coloured by the FIRST category it matches, most\n"
		     "  specific first — a flag room that is also a choke reads as a flag room.\n" );
		Msg( "  legend (only categories actually present):\n" );

		for ( int t = 0; t < (int)ARRAYSIZE( s_navVizTypes ); ++t )
		{
			if ( perType[ t ] == 0 )
				continue;
			const NavVizType &vt = s_navVizTypes[ t ];
			Msg( "    %-12s rgb(%3d,%3d,%3d)  %4d area%s  %s\n",
				vt.name, vt.r, vt.g, vt.b, perType[ t ],
				perType[ t ] == 1 ? " " : "s", vt.description );
		}

		if ( unnamed > 0 )
		{
			Msg( "    %-12s rgb(110,110,110)  %4d areas  tagged with an attribute this table doesn't name\n",
				"(unnamed)", unnamed );
		}
		return;
	}

	// ---- a single named type ----------------------------------------------
	if ( which < 0 || which >= (int)ARRAYSIZE( s_navVizTypes ) )
		return;

	const NavVizType &vizType = s_navVizTypes[ which ];

	int drawn = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area || !NavVizMatches( vizType, area ) )
			continue;

		NDebugOverlay::Box( area->GetCenter(), Vector( -32, -32, 0 ), Vector( 32, 32, 32 ),
			vizType.r, vizType.g, vizType.b, 64, lifetime );
		++drawn;
	}

	if ( !verbose )
		return;

	Msg( "[ff_nav_visualize] Drew %d areas for '%s' — rgb(%d,%d,%d), %s.\n",
		drawn, vizType.name, vizType.r, vizType.g, vizType.b, vizType.description );

	if ( drawn == 0 )
	{
		Msg( "  Nothing carries that attribute. Entity-derived tags (spawn, flag,\n"
		     "  cap, resupply) come from live entities at map load, NOT from\n"
		     "  nav_generate — check ff_bot_lua_report and ff_nav_validate.\n" );
	}
}


//-----------------------------------------------------------------------------
// Per-frame refresh, called from FFBotManager_Tick.
//-----------------------------------------------------------------------------
void FFBotCommands_TickVisualization( void )
{
	if ( s_vizActive == FFVIZ_NONE )
		return;

	if ( !ff_nav_visualize_persist.GetBool() )
	{
		// Switched off mid-view. Stop redrawing; what's on screen expires.
		s_vizActive = FFVIZ_NONE;
		return;
	}

	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
	{
		s_vizActive = FFVIZ_NONE;
		return;
	}

	if ( gpGlobals->curtime < s_vizNextDraw )
		return;
	s_vizNextDraw = gpGlobals->curtime + FFVIZ_REDRAW_INTERVAL;

	DrawNavVisualization( s_vizActive, false, FFVIZ_DRAW_LIFETIME );
}


CON_COMMAND_F( ff_nav_visualize,
	"Visualize FF nav attributes, and keep it drawn until switched off. "
	"'all' colours every tagged area by category; 'list' prints the types and "
	"their colours; 'combat' shows combat intensity; 'off' clears the view.",
	FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		PrintNavVizTypes();
		return;
	}

	const char *type = args.Arg( 1 );

	if ( FStrEq( type, "list" ) || FStrEq( type, "help" ) )
	{
		PrintNavVizTypes();
		return;
	}

	if ( FStrEq( type, "off" ) || FStrEq( type, "none" ) || FStrEq( type, "0" ) )
	{
		s_vizActive = FFVIZ_NONE;
		Msg( "[ff_nav_visualize] Off. What's already drawn expires within a second.\n" );
		return;
	}

	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "[ff_nav_visualize] Nav mesh not loaded. Run nav_generate.\n" );
		return;
	}

	int which = FFVIZ_NONE;
	if ( FStrEq( type, "all" ) )
	{
		which = FFVIZ_ALL;
	}
	else if ( FStrEq( type, "combat" ) )
	{
		which = FFVIZ_COMBAT;
	}
	else
	{
		const NavVizType *found = FindNavVizType( type );
		if ( !found )
		{
			Msg( "Unknown type '%s'.\n", type );
			PrintNavVizTypes();
			return;
		}
		which = (int)( found - s_navVizTypes );
	}

	const bool persist = ff_nav_visualize_persist.GetBool();

	// One immediate batch with a lifetime long enough to bridge to the first
	// refresh, then the tick keeps it alive. With persistence off this is the
	// only batch, so give it the old 30-second life.
	DrawNavVisualization( which, true, persist ? FFVIZ_DRAW_LIFETIME : 30.0f );

	if ( persist )
	{
		s_vizActive = which;
		s_vizNextDraw = gpGlobals->curtime + FFVIZ_REDRAW_INTERVAL;
		Msg( "  Staying on screen until 'ff_nav_visualize off' "
		     "(or ff_nav_visualize_persist 0).\n" );
	}
	else
	{
		s_vizActive = FFVIZ_NONE;
	}
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
// ff_nav_generate_full — the whole waypointing pipeline, from an empty map to
// bots that know what the map means.
//
// `nav_generate` reloads the level when it finishes, which takes the command
// buffer with it, so nothing can be chained onto it here. It doesn't need to
// be: every stage after generation is wired into CFFNavMesh::OnServerActivate
// and fires on the reload by itself. This command issues the generation and
// tells you what is about to happen, because otherwise a two-minute silence
// followed by a map reload looks like a crash.
//-----------------------------------------------------------------------------
CON_COMMAND_F( ff_nav_generate_full,
	"Generate the nav mesh and derive all gameplay knowledge from it. "
	"Reloads the map when generation finishes.",
	FCVAR_CHEAT )
{
	Msg( "[ff_nav_generate_full] Generating. This takes a while on a large map,\n"
	     "and the level reloads when it finishes. On the reload, automatically:\n" );
	Msg( "\n" );
	Msg( "  GENERATION (writes the .nav)\n" );
	Msg( "    walkable-space sampling, seeded from every spawn point\n" );
	Msg( "    doors forced open so shut ones don't wall off half the map\n" );
	Msg( "    ladders built from brush contents (stock Source builds none)\n" );
	Msg( "    underwater areas connected vertically\n" );
	Msg( "    per-area visibility sets computed and saved\n" );
	Msg( "\n" );
	Msg( "  TAGGING (every map load, from live entities)\n" );
	Msg( "    spawn rooms, flags, capture points, resupplies\n" );
	Msg( "    hand-authored markers from maps/<map>.ffnavpoints\n" );
	Msg( "    spawn exits, per-team incursion distances, invasion vectors\n" );
	Msg( "    heuristics: water, high ground, chokes, ladders\n" );
	Msg( "    entity passes: hazard gear, trigger_hurt volumes, lifts\n" );
	Msg( "\n" );
	Msg( "  ANALYSIS (every map load, derived)\n" );
	Msg( "    structural chokepoints, from graph articulation points\n" );
	Msg( "    main routes, from spawn-to-objective traffic\n" );
	Msg( "    overlooks and sniper perches, from the visibility sets\n" );
	Msg( "    defensive posts, sentry ground and aim directions\n" );
	Msg( "    breakable walls that open shortcuts; teleport connections\n" );
	Msg( "\n" );
	Msg( "  Afterwards: ff_bot_nav_report, ff_nav_analyze_report,\n"
	     "              ff_nav_validate, ff_nav_visualize all\n" );

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
