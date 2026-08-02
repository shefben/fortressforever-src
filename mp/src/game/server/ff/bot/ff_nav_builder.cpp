//========= Fortress Forever Bot =============================================//
//
// FFNavBuilder — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_builder.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_bot_tagger.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "player.h"
#include "filesystem.h"
#include "utlbuffer.h"
#include "debugoverlay_shared.h"
#include "shareddefs.h"
#include "eiface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Sidecar format. Bump the version when the record layout changes; the loader
// refuses anything it doesn't recognise rather than misreading it.
//-----------------------------------------------------------------------------
// v1: <type> <team> <x> <y> <z> <radius>
// v2: ... <yaw>          - facing, for markers that are about direction
// v1 files still load; their markers simply have yaw 0.
#define FFNAVPT_FILE_VERSION		2

// Marker with radius 0 tags exactly the nav area under it. This is how far we
// look for that area — generous, because in noclip you're often authoring from
// above a stairwell or a catwalk.
#define FFNAVPT_AREA_SEARCH_RANGE	256.0f

// Hard cap on marker count. Not a real limit for hand authoring; it exists so
// a corrupt or hand-mangled file can't make the level load take minutes.
#define FFNAVPT_MAX_POINTS			4096


ConVar ff_nav_builder_radius( "ff_nav_builder_radius", "0", FCVAR_CHEAT,
	"Radius applied to newly placed nav markers. 0 = tag only the area under "
	"the marker; >0 = tag every area whose centre is within this distance." );

ConVar ff_nav_builder_delete_radius( "ff_nav_builder_delete_radius", "96", FCVAR_CHEAT,
	"How close a marker has to be to you for ff_nav_delete to remove it." );

ConVar ff_nav_builder_spawn_height( "ff_nav_builder_spawn_height", "256", FCVAR_CHEAT,
	"Height above the highest spawn-room corner marker that still counts as "
	"inside the room. Raise it for multi-storey spawns." );

ConVar ff_nav_builder_link_oneway( "ff_nav_builder_link_oneway", "0", FCVAR_CHEAT,
	"How linkfrom/linkto pairs are applied. 0 = both directions, 1 = one way "
	"only, from the linkfrom end to the linkto end. Set it before the map "
	"loads, or run ff_nav_builder_reload after changing it — a drop off a ledge "
	"is genuinely one-way and wiring the return trip sends bots off it "
	"expecting to walk back up." );

ConVar ff_nav_builder_team( "ff_nav_builder_team", "0", FCVAR_CHEAT,
	"Team stamped on team-specific markers. 0 = whichever team you're on, "
	"2 = blue, 3 = red, 4 = yellow, 5 = green." );

ConVar ff_nav_builder_overlay( "ff_nav_builder_overlay", "1", FCVAR_CHEAT,
	"Draw placed nav markers while ff_manual_nav_builder is on." );

ConVar ff_nav_builder_keymap( "ff_nav_builder_keymap", "1", FCVAR_CHEAT,
	"Draw the numpad key map on screen while ff_manual_nav_builder is on. "
	"0 = off. The console still has ff_nav_builder_help." );

static void OnBuilderModeChanged( IConVar *var, const char *pOldValue, float flOldValue );

ConVar ff_manual_nav_builder( "ff_manual_nav_builder", "0", FCVAR_CHEAT,
	"Manual nav point authoring mode. Binds the numpad to marker placement and "
	"draws every marker on the map.",
	OnBuilderModeChanged );


//-----------------------------------------------------------------------------
// Type table. MUST stay in FFNavPointType order — ValidateTypeTable asserts it.
//-----------------------------------------------------------------------------
struct FFNavPointTypeInfo
{
	int         type;
	const char *name;
	bool        needsTeam;
	bool        usesYaw;
	int         r, g, b;
	const char *description;
};

// Colours are picked to be tellable apart AT A GLANCE, through a wall, at
// range, in a dark FF spawn corridor. That rules out subtle differences: every
// entry below is far from every other entry in at least one channel, and the
// type name is drawn beside the marker anyway for the pairs that are closest
// (dispenser/flag, waterexit/defend).
static const FFNavPointTypeInfo s_typeInfo[] =
{
	//  type                 name          team   yaw    r    g    b
	{ FFNAVPT_INVALID,      "invalid",    false, false,   0,   0,   0,
	  "not a marker" },

	// ---- Page 1 -----------------------------------------------------------
	{ FFNAVPT_SNIPER,       "sniper",     false, true,  255,   0, 255,	// magenta
	  "sniper holds this position, watching the way you're facing" },

	{ FFNAVPT_SENTRY,       "sentry",     false, true,  255, 128,   0,	// orange
	  "engineer should consider a sentry here, facing your way (a hint)" },

	{ FFNAVPT_DISPENSER,    "dispenser",  false, false, 255, 255,   0,	// yellow
	  "engineer should consider a dispenser here" },

	{ FFNAVPT_DETPACK,      "detpack",    false, false, 255,   0,   0,	// red
	  "breakable wall: blow it OPEN to make a route" },

	{ FFNAVPT_SPAWN_CORNER, "spawn",      true,  false,  64,  64, 255,	// blue
	  "spawn-room corner; place FOUR per room to bound it" },

	{ FFNAVPT_DOOR,         "door",       false, false, 255, 255, 255,	// white
	  "a door lives here: expect it shut, walk into it and use it" },

	{ FFNAVPT_FLAG,         "flag",       true,  false, 255, 224, 128,	// pale gold
	  "flag rest position" },

	{ FFNAVPT_CAP,          "cap",        true,  false, 160, 255,   0,	// lime
	  "capture point owned by a team" },

	{ FFNAVPT_HAZARD_GEAR,  "gassuit",    false, false,   0, 255,   0,	// green
	  "gas suit / hazard gear pickup (rock2)" },

	// ---- Page 2 -----------------------------------------------------------
	{ FFNAVPT_DETPACK_SEAL, "detpackseal",false, false, 200,  32,  96,	// dark red
	  "breakable wall: blow it SHUT to deny a route" },

	{ FFNAVPT_PIPETRAP,     "pipetrap",   false, true,  255,  96,  32,	// burnt orange
	  "demoman lays a pipe carpet here, covering the way you face" },

	{ FFNAVPT_AIM,          "aim",        false, true,  255, 255, 192,	// pale yellow
	  "look this way from here — facing, not position" },

	{ FFNAVPT_DEFEND,       "defend",     true,  true,    0, 160, 255,	// sky blue
	  "hold this ground on defense, watching the way you face" },

	{ FFNAVPT_DANGER,       "danger",     false, false, 255,  48, 112,	// crimson
	  "avoid: pit, crusher, known grinder" },

	{ FFNAVPT_JUMP,         "jump",       false, true,  192, 128, 255,	// violet
	  "conc / rocket-jump launch position, aimed the way you face" },

	{ FFNAVPT_WATER_EXIT,   "waterexit",  false, false,   0, 255, 255,	// cyan
	  "where to leave the water: ladder foot, ramp, surfacing point" },

	{ FFNAVPT_LIFT,         "lift",       false, false, 128, 160, 192,	// steel
	  "lift / elevator: expect to wait for it rather than walk on" },

	{ FFNAVPT_CAP_NEUTRAL,  "capneutral", false, false, 176, 176, 176,	// light grey
	  "capture point nobody owns (dustbowl-style push maps)" },

	// ---- Page 3 -----------------------------------------------------------
	{ FFNAVPT_RESUPPLY,     "resupply",   false, false,   0, 255, 160,	// spring green
	  "ammo / health / armor available here" },

	{ FFNAVPT_NO_SPAWNING,  "nospawn",    false, false,  96,  96,  96,	// dark grey
	  "never pick this area as a bot spawn point" },

	{ FFNAVPT_ESCAPE,       "escape",     false, false, 255, 128, 192,	// pink
	  "hunted-mode VIP escape destination" },

	{ FFNAVPT_LINK_FROM,    "linkfrom",   false, false, 255, 255,   0,	// bright yellow
	  "start of a nav connection the mesh is missing" },

	{ FFNAVPT_LINK_TO,      "linkto",     false, false, 255, 200,   0,	// amber
	  "...and where that connection comes out" },
};


//-----------------------------------------------------------------------------
// Numpad pages.
//
// Page 1 is exactly the nine keys the tool had before paging existed, in the
// same order, so anyone who already learned the layout keeps it.
//-----------------------------------------------------------------------------
struct FFNavBuilderPage
{
	const char *name;
	int         slot[ FFNAVPT_SLOTS_PER_PAGE ];
};

static const FFNavBuilderPage s_pages[ FFNAVPT_PAGE_COUNT ] =
{
	{ "objectives & build", {
		FFNAVPT_SNIPER, FFNAVPT_SENTRY, FFNAVPT_DISPENSER,
		FFNAVPT_DETPACK, FFNAVPT_SPAWN_CORNER, FFNAVPT_DOOR,
		FFNAVPT_FLAG, FFNAVPT_CAP, FFNAVPT_HAZARD_GEAR } },

	{ "tactical", {
		FFNAVPT_DETPACK_SEAL, FFNAVPT_PIPETRAP, FFNAVPT_AIM,
		FFNAVPT_DEFEND, FFNAVPT_DANGER, FFNAVPT_JUMP,
		FFNAVPT_WATER_EXIT, FFNAVPT_LIFT, FFNAVPT_CAP_NEUTRAL } },

	{ "occasional & links", {
		FFNAVPT_RESUPPLY, FFNAVPT_NO_SPAWNING, FFNAVPT_ESCAPE,
		FFNAVPT_LINK_FROM, FFNAVPT_LINK_TO, FFNAVPT_INVALID,
		FFNAVPT_INVALID, FFNAVPT_INVALID, FFNAVPT_INVALID } },
};

static int s_currentPage = 0;


//-----------------------------------------------------------------------------
// State.
//-----------------------------------------------------------------------------
static CUtlVector< FFNavPoint > s_points;
static int   s_selectedType   = FFNAVPT_SNIPER;
static float s_nextOverlayTime = 0.0f;
static bool  s_loadedThisMap  = false;

// Full re-derivation of every tag a marker can set. Defined below, next to the
// deletion path that motivates it.
static void RebuildMeshTags( void );


//-----------------------------------------------------------------------------
static void ValidateTypeTable( void )
{
	COMPILE_TIME_ASSERT( (int)ARRAYSIZE( s_typeInfo ) == (int)FFNAVPT_COUNT );
	for ( int i = 0; i < (int)ARRAYSIZE( s_typeInfo ); ++i )
	{
		Assert( s_typeInfo[ i ].type == i );
	}
}


//-----------------------------------------------------------------------------
static bool IsValidType( int type )
{
	return ( type > FFNAVPT_INVALID && type < FFNAVPT_COUNT );
}

const char *FFNavBuilder::NameForType( int type )
{
	return IsValidType( type ) ? s_typeInfo[ type ].name : "invalid";
}

const char *FFNavBuilder::DescriptionForType( int type )
{
	return IsValidType( type ) ? s_typeInfo[ type ].description : "";
}

bool FFNavBuilder::TypeNeedsTeam( int type )
{
	return IsValidType( type ) ? s_typeInfo[ type ].needsTeam : false;
}

bool FFNavBuilder::TypeUsesYaw( int type )
{
	return IsValidType( type ) ? s_typeInfo[ type ].usesYaw : false;
}


//-----------------------------------------------------------------------------
// Paging.
//-----------------------------------------------------------------------------
int FFNavBuilder::GetPage( void )
{
	return s_currentPage;
}

void FFNavBuilder::SetPage( int page )
{
	if ( page >= 0 && page < FFNAVPT_PAGE_COUNT )
		s_currentPage = page;
}

void FFNavBuilder::CyclePage( int delta )
{
	s_currentPage = ( s_currentPage + delta ) % FFNAVPT_PAGE_COUNT;
	if ( s_currentPage < 0 )
		s_currentPage += FFNAVPT_PAGE_COUNT;
}

int FFNavBuilder::TypeForSlot( int page, int slot )
{
	if ( page < 0 || page >= FFNAVPT_PAGE_COUNT )
		return FFNAVPT_INVALID;
	if ( slot < 1 || slot > FFNAVPT_SLOTS_PER_PAGE )
		return FFNAVPT_INVALID;
	return s_pages[ page ].slot[ slot - 1 ];
}

const char *FFNavBuilder::PageName( int page )
{
	if ( page < 0 || page >= FFNAVPT_PAGE_COUNT )
		return "?";
	return s_pages[ page ].name;
}

void FFNavBuilder::PrintPage( void )
{
	Msg( "[ff_nav_builder] page %d/%d — %s\n",
		s_currentPage + 1, FFNAVPT_PAGE_COUNT, PageName( s_currentPage ) );

	for ( int slot = 1; slot <= FFNAVPT_SLOTS_PER_PAGE; ++slot )
	{
		const int type = TypeForSlot( s_currentPage, slot );
		if ( type == FFNAVPT_INVALID )
			continue;
		Msg( "    %d  %-12s %s%s\n", slot, NameForType( type ),
			TypeNeedsTeam( type ) ? "(team) " : "",
			TypeUsesYaw( type ) ? "(aims where you look)" : "" );
	}
}

void FFNavBuilder::ColorForType( int type, int *r, int *g, int *b )
{
	const FFNavPointTypeInfo &info = s_typeInfo[ IsValidType( type ) ? type : 0 ];
	if ( r ) *r = info.r;
	if ( g ) *g = info.g;
	if ( b ) *b = info.b;
}

int FFNavBuilder::TypeFromName( const char *name )
{
	if ( !name || !*name )
		return FFNAVPT_INVALID;

	for ( int i = 1; i < FFNAVPT_COUNT; ++i )
	{
		if ( !Q_stricmp( name, s_typeInfo[ i ].name ) )
			return i;
	}
	return FFNAVPT_INVALID;
}

int FFNavBuilder::GetSelectedType( void )
{
	return s_selectedType;
}

void FFNavBuilder::SetSelectedType( int type )
{
	if ( IsValidType( type ) )
		s_selectedType = type;
}

void FFNavBuilder::CycleSelectedType( int delta )
{
	// FFNAVPT_INVALID is index 0 and isn't selectable, so the cycle runs over
	// [1, FFNAVPT_COUNT).
	const int span = FFNAVPT_COUNT - 1;
	int next = ( s_selectedType - 1 + delta ) % span;
	if ( next < 0 )
		next += span;
	s_selectedType = next + 1;

	Msg( "[ff_nav_builder] selected: %s — %s\n",
		NameForType( s_selectedType ), DescriptionForType( s_selectedType ) );
}


//-----------------------------------------------------------------------------
int FFNavBuilder::ResolveAuthoringTeam( CBasePlayer *player )
{
	const int forced = ff_nav_builder_team.GetInt();
	if ( forced >= TEAM_BLUE && forced <= TEAM_GREEN )
		return forced;

	if ( player )
	{
		const int own = player->GetTeamNumber();
		if ( own >= TEAM_BLUE && own <= TEAM_GREEN )
			return own;
	}

	// Spectating with no forced team: blue is as good a default as any, and
	// the marker is trivially re-placed with ff_nav_builder_team set.
	return TEAM_BLUE;
}

static const char *TeamName( int team )
{
	switch ( team )
	{
	case TEAM_BLUE:		return "blue";
	case TEAM_RED:		return "red";
	case TEAM_YELLOW:	return "yellow";
	case TEAM_GREEN:	return "green";
	}
	return "none";
}


//-----------------------------------------------------------------------------
static void GetPointsFilename( char *out, int outSize )
{
	Q_snprintf( out, outSize, "maps/%s.ffnavpoints", STRING( gpGlobals->mapname ) );
}


//-----------------------------------------------------------------------------
// Which nav areas does this marker affect?
//
// radius 0 tags the single area under the marker. A non-zero radius is measured
// to area CENTRES, not to the nearest point on the area: measuring to the
// nearest point makes one huge open-field area swallow every small radius you
// place inside it, which is the opposite of what an author means by "50 units
// around here".
//-----------------------------------------------------------------------------
static void CollectAreasForPoint( const FFNavPoint &pt, CUtlVector< CFFNavArea * > &out )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return;

	if ( pt.radius <= 0.0f )
	{
		CNavArea *area = TheNavMesh->GetNearestNavArea(
			pt.pos, false, FFNAVPT_AREA_SEARCH_RANGE, false, true, TEAM_ANY );
		if ( area )
			out.AddToTail( static_cast< CFFNavArea * >( area ) );
		return;
	}

	const float radiusSq = pt.radius * pt.radius;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( ( area->GetCenter() - pt.pos ).LengthSqr() <= radiusSq )
			out.AddToTail( area );
	}
}


//-----------------------------------------------------------------------------
// Stamp one marker onto one area.
//
// Markers whose meaning already has an FF_NAV_* bit reuse it, so every existing
// consumer picks them up for free — a hand-placed "sentry" marker is
// indistinguishable to CFFBotEngineerBuildSentrygun from a mapper-tagged one,
// which is the entire point. Genuinely new concepts go into the FF_NAV2_* word.
//
// Spawn corners are NOT handled here; four of them describe a volume, and a
// volume is not a per-marker property. See ApplySpawnRegions.
//-----------------------------------------------------------------------------
static void ApplyPointToArea( const FFNavPoint &pt, CFFNavArea *area, CFFNavMesh *mesh )
{
	area->SetAttributeFF2( FF_NAV2_MANUAL );

	switch ( pt.type )
	{
	case FFNAVPT_SNIPER:
		area->SetAttributeFF( FF_NAV_SNIPER_SPOT );
		break;

	case FFNAVPT_SENTRY:
		area->SetAttributeFF( FF_NAV_SENTRY_SPOT );
		break;

	case FFNAVPT_DISPENSER:
		area->SetAttributeFF2( FF_NAV2_DISPENSER_SPOT );
		break;

	case FFNAVPT_DETPACK:
		area->SetAttributeFF2( FF_NAV2_DETPACK_SPOT );
		break;

	case FFNAVPT_DETPACK_SEAL:
		area->SetAttributeFF2( FF_NAV2_DETPACK_SEAL );
		break;

	case FFNAVPT_PIPETRAP:
		area->SetAttributeFF2( FF_NAV2_PIPETRAP );
		break;

	case FFNAVPT_AIM:
		// The yaw goes onto the area, not just into the sidecar. CFFBotAnalyzer
		// derives aim hints too, and those have no marker to look up — the
		// consumer should not have to know which kind of hint it got.
		area->SetAttributeFF2( FF_NAV2_AIM_HINT );
		area->SetAimYaw( pt.yaw );
		break;

	case FFNAVPT_LIFT:
		// Same bit the auto-tagger sets from func_train / func_plat / a
		// vertically-travelling door. Manual placement covers the lifts that
		// aren't brush entities we recognise - Lua-driven platforms, and
		// trigger_push columns that behave like one.
		area->SetAttributeFF2( FF_NAV2_LIFT );
		break;

	case FFNAVPT_DOOR:
		// Same bit MarkDoorwayAreas sets. Manual placement matters where the
		// blocker isn't a brush entity we recognise — a Lua-driven gate, a
		// clip that only exists during setup, a door whose brush model doesn't
		// overlap the area its hinge blocks.
		area->SetAttributeFF( FF_NAV_DOORWAY );
		break;

	case FFNAVPT_FLAG:
		area->SetAttributeFF( CFFNavArea::FlagAttributeForTeam( pt.team ) );
		if ( mesh )
			mesh->AddFlagArea( pt.team, area );
		break;

	case FFNAVPT_CAP:
		area->SetAttributeFF( CFFNavArea::CapAttributeForTeam( pt.team ) );
		if ( mesh )
			mesh->AddCapArea( pt.team, area );
		break;

	case FFNAVPT_CAP_NEUTRAL:
		area->SetAttributeFF2( FF_NAV2_CAP_NEUTRAL );
		break;

	case FFNAVPT_HAZARD_GEAR:
		area->SetAttributeFF2( FF_NAV2_HAZARD_GEAR );
		break;

	case FFNAVPT_DEFEND:
		area->SetAttributeFF2( CFFNavArea::DefendAttributeForTeam( pt.team ) );
		break;

	case FFNAVPT_RESUPPLY:
		// A resupply cabinet gives everything, so tag everything. Bots looking
		// for one specific resource will still find this area.
		area->SetAttributeFF( FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH | FF_NAV_HAS_ARMOR );
		if ( mesh )
			mesh->AddResupplyArea( area );
		break;

	case FFNAVPT_WATER_EXIT:
		area->SetAttributeFF2( FF_NAV2_WATER_EXIT );
		break;

	case FFNAVPT_JUMP:
		area->SetAttributeFF2( FF_NAV2_JUMP_SPOT );
		break;

	case FFNAVPT_DANGER:
		area->SetAttributeFF2( FF_NAV2_DANGER );
		break;

	case FFNAVPT_NO_SPAWNING:
		area->SetAttributeFF( FF_NAV_NO_SPAWNING );
		break;

	case FFNAVPT_ESCAPE:
		area->SetAttributeFF( FF_NAV_HUNTED_ESCAPE );
		break;

	default:
		break;
	}
}


//-----------------------------------------------------------------------------
// Turn groups of four spawn-corner markers into spawn rooms.
//
// Corners are consumed per team in file order, four at a time: markers 1-4 are
// one room, 5-8 the next, and so on. Insertion order rather than clustering,
// because clustering guesses and guessing wrong here silently mislabels a whole
// room. A trailing partial group is reported, not silently absorbed.
//
// The volume is the XY bounding box of the four corners; vertically it runs
// from a little below the lowest corner to ff_nav_builder_spawn_height above
// the highest. You mark the floor, you get the room.
//-----------------------------------------------------------------------------
static int ApplySpawnRegions( CFFNavMesh *mesh )
{
	int roomsBuilt = 0;

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		CUtlVector< const FFNavPoint * > corners;
		for ( int i = 0; i < s_points.Count(); ++i )
		{
			const FFNavPoint &pt = s_points[ i ];
			if ( pt.type == FFNAVPT_SPAWN_CORNER && pt.team == team )
				corners.AddToTail( &pt );
		}

		if ( corners.Count() == 0 )
			continue;

		const int groups = corners.Count() / 4;
		const int leftover = corners.Count() % 4;

		for ( int g = 0; g < groups; ++g )
		{
			Vector mins( FLT_MAX, FLT_MAX, FLT_MAX );
			Vector maxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );

			for ( int c = 0; c < 4; ++c )
			{
				const Vector &p = corners[ g * 4 + c ]->pos;
				mins.x = MIN( mins.x, p.x );	mins.y = MIN( mins.y, p.y );	mins.z = MIN( mins.z, p.z );
				maxs.x = MAX( maxs.x, p.x );	maxs.y = MAX( maxs.y, p.y );	maxs.z = MAX( maxs.z, p.z );
			}

			// A corner is placed at the author's feet; the floor of the area
			// they were standing on is a little lower still.
			mins.z -= 64.0f;
			maxs.z += ff_nav_builder_spawn_height.GetFloat();

			const int spawnBit = CFFNavArea::SpawnRoomAttributeForTeam( team );
			int tagged = 0;

			for ( int i = 0; i < TheNavAreas.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
				if ( !area )
					continue;

				const Vector center = area->GetCenter();
				if ( center.x < mins.x || center.x > maxs.x ) continue;
				if ( center.y < mins.y || center.y > maxs.y ) continue;
				if ( center.z < mins.z || center.z > maxs.z ) continue;

				area->SetAttributeFF( spawnBit );
				area->SetAttributeFF2( FF_NAV2_MANUAL );
				if ( mesh )
					mesh->AddSpawnRoomArea( team, area );
				++tagged;
			}

			++roomsBuilt;
			Msg( "[ff_nav_builder] %s spawn room %d: %d area(s) inside "
				"(%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
				TeamName( team ), g + 1, tagged,
				mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z );

			if ( tagged == 0 )
			{
				Warning( "[ff_nav_builder] ...that box contains no nav areas. "
					"Corners misplaced, or the room has no nav mesh.\n" );
			}
		}

		if ( leftover != 0 )
		{
			Warning( "[ff_nav_builder] %s has %d spare spawn corner(s) — a room "
				"needs exactly 4. Place %d more, or delete the spares.\n",
				TeamName( team ), leftover, 4 - leftover );
		}
	}

	return roomsBuilt;
}


//-----------------------------------------------------------------------------
// Turn linkfrom / linkto pairs into real nav connections.
//
// Everything else in this file tags an area. This is the only thing that
// changes the GRAPH, and it is the one piece of authoring the builder was
// missing: an area that is adjacent in the world and unconnected in the mesh is
// invisible to A* no matter what attributes it carries, because there is no
// edge for the cost model to price. Underwater tunnels, one-way drops, gaps
// that need a run-up, and any doorway that happened to be shut when
// nav_generate ran all produce exactly that.
//
// Pairs are taken in file order — each FROM claims the next TO after it —
// matching how spawn corners are consumed, and for the same reason: proximity
// clustering guesses, and a wrong guess here silently wires two places together
// that aren't connected at all.
//
// Direction is derived from the two area centres because CNavArea's adjacency
// lists are bucketed by compass direction; pathfinding only ever walks those
// buckets, so any consistent choice works provided it matches how the geometry
// lies. Same derivation FFBotLearnedLinks uses.
//-----------------------------------------------------------------------------
static NavDirType DirectionBetween( const Vector &from, const Vector &to )
{
	const Vector delta = to - from;
	if ( fabsf( delta.x ) > fabsf( delta.y ) )
		return ( delta.x > 0.0f ) ? EAST : WEST;
	return ( delta.y > 0.0f ) ? SOUTH : NORTH;
}


static int ApplyLinkMarkers( void )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return 0;

	CUtlVector< const FFNavPoint * > froms;
	CUtlVector< const FFNavPoint * > tos;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( pt.type == FFNAVPT_LINK_FROM )
			froms.AddToTail( &pt );
		else if ( pt.type == FFNAVPT_LINK_TO )
			tos.AddToTail( &pt );
	}

	const int pairs = MIN( froms.Count(), tos.Count() );
	const bool oneWay = ff_nav_builder_link_oneway.GetBool();
	int made = 0;

	for ( int p = 0; p < pairs; ++p )
	{
		CNavArea *fromBase = TheNavMesh->GetNearestNavArea( froms[ p ]->pos, false,
			FFNAVPT_AREA_SEARCH_RANGE, false, true, TEAM_ANY );
		CNavArea *toBase = TheNavMesh->GetNearestNavArea( tos[ p ]->pos, false,
			FFNAVPT_AREA_SEARCH_RANGE, false, true, TEAM_ANY );

		if ( !fromBase || !toBase || fromBase == toBase )
		{
			Warning( "[ff_nav_builder] link pair %d does not resolve to two "
				"distinct nav areas — check both ends are on the mesh.\n", p + 1 );
			continue;
		}

		// Already connected. Not an error: the mesh may have been regenerated
		// since, and it having learned the connection on its own is the outcome
		// we wanted.
		bool already = false;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			if ( fromBase->IsConnected( toBase, (NavDirType)d ) )
			{
				already = true;
				break;
			}
		}

		if ( !already )
		{
			fromBase->ConnectTo( toBase, DirectionBetween( fromBase->GetCenter(), toBase->GetCenter() ) );
			++made;
		}

		if ( oneWay )
			continue;

		bool backAlready = false;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			if ( toBase->IsConnected( fromBase, (NavDirType)d ) )
			{
				backAlready = true;
				break;
			}
		}

		if ( !backAlready )
		{
			toBase->ConnectTo( fromBase, DirectionBetween( toBase->GetCenter(), fromBase->GetCenter() ) );
			++made;
		}
	}

	if ( froms.Count() != tos.Count() )
	{
		Warning( "[ff_nav_builder] %d linkfrom marker(s) and %d linkto — they come "
			"in pairs, and the odd one out does nothing.\n",
			froms.Count(), tos.Count() );
	}

	return made;
}


//-----------------------------------------------------------------------------
void FFNavBuilder::ApplyToMesh( CFFNavMesh *mesh )
{
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return;
	if ( s_points.Count() == 0 )
		return;

	int stamped = 0;
	int orphaned = 0;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( pt.type == FFNAVPT_SPAWN_CORNER )
			continue;	// handled as a volume below
		if ( pt.type == FFNAVPT_LINK_FROM || pt.type == FFNAVPT_LINK_TO )
			continue;	// handled as a graph edge below

		CUtlVector< CFFNavArea * > areas;
		CollectAreasForPoint( pt, areas );

		if ( areas.Count() == 0 )
		{
			// The marker outlived the geometry under it. Usually means the nav
			// was regenerated with different coverage, occasionally that the
			// map changed. Loud, because a silently dead marker looks exactly
			// like a bot ignoring you.
			++orphaned;
			Warning( "[ff_nav_builder] marker '%s' at (%.0f %.0f %.0f) has no "
				"nav area within %.0fu — it does nothing.\n",
				NameForType( pt.type ), pt.pos.x, pt.pos.y, pt.pos.z,
				FFNAVPT_AREA_SEARCH_RANGE );
			continue;
		}

		for ( int a = 0; a < areas.Count(); ++a )
			ApplyPointToArea( pt, areas[ a ], mesh );

		++stamped;
	}

	const int rooms = ApplySpawnRegions( mesh );
	const int links = ApplyLinkMarkers();

	Msg( "[ff_nav_builder] applied %d marker(s), %d spawn room(s), "
	     "%d nav connection(s)%s.\n",
		stamped, rooms, links,
		orphaned > 0 ? " (see orphan warnings above)" : "" );
}


//-----------------------------------------------------------------------------
// Sidecar IO. Plain text, one marker per line:
//
//     <type> <team> <x> <y> <z> <radius>
//
// Positions rather than nav area IDs, so the file survives nav_generate. That
// matters more than it sounds: the whole reason this exists is that redoing map
// knowledge by hand after every mesh regeneration is what stopped people doing
// it in the first place.
//-----------------------------------------------------------------------------
void FFNavBuilder::Save( void )
{
	char filename[ MAX_PATH ];
	GetPointsFilename( filename, sizeof( filename ) );

	if ( s_points.Count() == 0 )
	{
		// Nothing left to describe: remove the file rather than leave a stale
		// one that a future load would resurrect.
		filesystem->RemoveFile( filename, "MOD" );
		Msg( "[ff_nav_builder] no markers left; removed %s\n", filename );
		return;
	}

	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );

	buf.Printf( "// Fortress Forever — manual bot nav markers\n" );
	buf.Printf( "// map: %s\n", STRING( gpGlobals->mapname ) );
	buf.Printf( "//\n" );
	buf.Printf( "// Written by ff_nav_place / ff_nav_delete. Hand-editable:\n" );
	buf.Printf( "//     <type> <team> <x> <y> <z> <radius> <yaw>\n" );
	buf.Printf( "// team  0 = none, 2 = blue, 3 = red, 4 = yellow, 5 = green\n" );
	buf.Printf( "// radius 0 = tag the one nav area under the marker\n" );
	buf.Printf( "// yaw    degrees. Only meaningful for sniper / sentry / aim /\n" );
	buf.Printf( "//        pipetrap / defend / jump; ignored by everything else.\n" );
	buf.Printf( "//\n" );
	buf.Printf( "// Four 'spawn' markers of the same team bound one spawn room,\n" );
	buf.Printf( "// consumed in the order they appear below.\n" );
	buf.Printf( "version %d\n", FFNAVPT_FILE_VERSION );

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		buf.Printf( "%s %d %.2f %.2f %.2f %.2f %.1f\n",
			NameForType( pt.type ), pt.team,
			pt.pos.x, pt.pos.y, pt.pos.z, pt.radius, pt.yaw );
	}

	if ( filesystem->WriteFile( filename, "MOD", buf ) )
	{
		Msg( "[ff_nav_builder] saved %d marker(s) to %s\n", s_points.Count(), filename );
	}
	else
	{
		Warning( "[ff_nav_builder] FAILED to write %s — markers are live but "
			"will not survive a map change.\n", filename );
	}
}


//-----------------------------------------------------------------------------
// Minimal whitespace tokenizer. Deliberately not sscanf: the CRT's scanf family
// is deprecation-warned under /W4 /WX in this tree, and the format is simple
// enough that hand-splitting is shorter than working around that.
//-----------------------------------------------------------------------------
static bool NextToken( const char *&p, char *out, int outSize )
{
	while ( *p == ' ' || *p == '\t' )
		++p;

	if ( *p == '\0' )
		return false;

	int n = 0;
	while ( *p != '\0' && *p != ' ' && *p != '\t' )
	{
		if ( n < outSize - 1 )
			out[ n++ ] = *p;
		++p;
	}
	out[ n ] = '\0';
	return ( n > 0 );
}


//-----------------------------------------------------------------------------
static void ParseLine( char *line, int lineNumber )
{
	// Strip a trailing CR (files edited on either platform must both work) and
	// anything after a comment marker.
	for ( char *c = line; *c; ++c )
	{
		if ( *c == '\r' )
		{
			*c = '\0';
			break;
		}
		if ( ( c[ 0 ] == '/' && c[ 1 ] == '/' ) || c[ 0 ] == '#' )
		{
			*c = '\0';
			break;
		}
	}

	const char *p = line;
	char token[ 64 ];

	if ( !NextToken( p, token, sizeof( token ) ) )
		return;		// blank or comment-only

	if ( !Q_stricmp( token, "version" ) )
	{
		char versionToken[ 32 ];
		if ( NextToken( p, versionToken, sizeof( versionToken ) ) )
		{
			const int version = Q_atoi( versionToken );
			if ( version != FFNAVPT_FILE_VERSION )
			{
				Warning( "[ff_nav_builder] file is version %d, this build reads "
					"version %d. Markers may be ignored.\n",
					version, FFNAVPT_FILE_VERSION );
			}
		}
		return;
	}

	const int type = FFNavBuilder::TypeFromName( token );
	if ( type == FFNAVPT_INVALID )
	{
		Warning( "[ff_nav_builder] line %d: unknown marker type '%s'.\n",
			lineNumber, token );
		return;
	}

	FFNavPoint pt;
	pt.type = type;
	pt.team = 0;
	pt.radius = 0.0f;
	pt.yaw = 0.0f;
	pt.pos.Init();

	char teamToken[ 32 ], xToken[ 32 ], yToken[ 32 ], zToken[ 32 ];
	if ( !NextToken( p, teamToken, sizeof( teamToken ) ) ||
	     !NextToken( p, xToken, sizeof( xToken ) ) ||
	     !NextToken( p, yToken, sizeof( yToken ) ) ||
	     !NextToken( p, zToken, sizeof( zToken ) ) )
	{
		Warning( "[ff_nav_builder] line %d: expected '<type> <team> <x> <y> <z> "
			"[radius]'.\n", lineNumber );
		return;
	}

	pt.team  = Q_atoi( teamToken );
	pt.pos.x = (float)Q_atof( xToken );
	pt.pos.y = (float)Q_atof( yToken );
	pt.pos.z = (float)Q_atof( zToken );

	char radiusToken[ 32 ];
	if ( NextToken( p, radiusToken, sizeof( radiusToken ) ) )
		pt.radius = (float)Q_atof( radiusToken );

	// Absent in v1 files, which is fine - a marker with no recorded facing
	// simply has none, and every type that ignores yaw is unaffected.
	char yawToken[ 32 ];
	if ( NextToken( p, yawToken, sizeof( yawToken ) ) )
		pt.yaw = (float)Q_atof( yawToken );

	if ( FFNavBuilder::TypeNeedsTeam( type ) &&
	     ( pt.team < TEAM_BLUE || pt.team > TEAM_GREEN ) )
	{
		Warning( "[ff_nav_builder] line %d: '%s' needs a team (2-5), got %d. "
			"Skipped.\n", lineNumber, token, pt.team );
		return;
	}

	if ( s_points.Count() >= FFNAVPT_MAX_POINTS )
	{
		Warning( "[ff_nav_builder] marker limit (%d) reached; ignoring the rest "
			"of the file.\n", FFNAVPT_MAX_POINTS );
		return;
	}

	s_points.AddToTail( pt );
}


//-----------------------------------------------------------------------------
static void LoadPointsFile( void )
{
	s_points.RemoveAll();

	char filename[ MAX_PATH ];
	GetPointsFilename( filename, sizeof( filename ) );

	FileHandle_t f = filesystem->Open( filename, "rb", "MOD" );
	if ( !f )
		return;		// no sidecar for this map yet — completely normal

	const int size = filesystem->Size( f );
	if ( size <= 0 )
	{
		filesystem->Close( f );
		return;
	}

	char *data = new char[ size + 1 ];
	const int read = filesystem->Read( data, size, f );
	filesystem->Close( f );
	data[ ( read > 0 && read <= size ) ? read : 0 ] = '\0';

	int lineNumber = 0;
	char *lineStart = data;
	while ( *lineStart )
	{
		char *lineEnd = lineStart;
		while ( *lineEnd && *lineEnd != '\n' )
			++lineEnd;

		const bool more = ( *lineEnd != '\0' );
		*lineEnd = '\0';

		++lineNumber;
		ParseLine( lineStart, lineNumber );

		if ( !more )
			break;
		lineStart = lineEnd + 1;
	}

	delete[] data;

	Msg( "[ff_nav_builder] %s: %d marker(s) loaded.\n", filename, s_points.Count() );
}


//-----------------------------------------------------------------------------
void FFNavBuilder::OnMapLoad( void )
{
	ValidateTypeTable();

	s_nextOverlayTime = 0.0f;
	s_loadedThisMap = true;
	LoadPointsFile();
}


//-----------------------------------------------------------------------------
void FFNavBuilder::Reload( void )
{
	LoadPointsFile();

	// Not just ApplyToMesh: a hand edit can REMOVE markers as well as add them,
	// and stamping the survivors on top of the old tags would leave the deleted
	// ones live. RebuildMeshTags wipes and re-derives, which also refreshes
	// spawn exits and incursion distances — the things a moved spawn-room
	// corner silently invalidates.
	RebuildMeshTags();
}


//-----------------------------------------------------------------------------
bool FFNavBuilder::Place( CBasePlayer *player, int type, int team )
{
	if ( !player )
	{
		Msg( "[ff_nav_builder] must be issued by a player.\n" );
		return false;
	}
	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
	{
		Msg( "[ff_nav_builder] no nav mesh loaded — generate one first.\n" );
		return false;
	}
	if ( !IsValidType( type ) )
	{
		Msg( "[ff_nav_builder] unknown marker type.\n" );
		return false;
	}
	if ( s_points.Count() >= FFNAVPT_MAX_POINTS )
	{
		Msg( "[ff_nav_builder] marker limit (%d) reached.\n", FFNAVPT_MAX_POINTS );
		return false;
	}

	// Trace to the floor so noclip authoring lands the marker on the ground the
	// bots will actually stand on, not wherever the camera happened to float.
	const Vector originPos = player->GetAbsOrigin();
	trace_t tr;
	UTIL_TraceLine( originPos + Vector( 0.0f, 0.0f, 16.0f ),
		originPos - Vector( 0.0f, 0.0f, 4096.0f ),
		MASK_PLAYERSOLID_BRUSHONLY, player, COLLISION_GROUP_NONE, &tr );

	FFNavPoint pt;
	pt.type   = type;
	pt.pos    = ( tr.fraction < 1.0f ) ? tr.endpos : originPos;
	pt.radius = MAX( 0.0f, ff_nav_builder_radius.GetFloat() );
	pt.team   = TypeNeedsTeam( type )
		? ( ( team >= TEAM_BLUE && team <= TEAM_GREEN ) ? team : ResolveAuthoringTeam( player ) )
		: 0;

	// Facing comes from where you were looking, which is the only sane way to
	// author it: stand where the sniper stands, look where you want them to
	// look, press the key. Eye angles rather than body angles, because that's
	// what you were actually aiming with.
	pt.yaw = TypeUsesYaw( type ) ? AngleNormalize( player->EyeAngles().y ) : 0.0f;

	CNavArea *area = TheNavMesh->GetNearestNavArea(
		pt.pos, false, FFNAVPT_AREA_SEARCH_RANGE, false, true, TEAM_ANY );
	if ( !area )
	{
		Msg( "[ff_nav_builder] no nav area within %.0fu of you — a marker here "
			"would do nothing. Move onto walkable nav, or regenerate.\n",
			FFNAVPT_AREA_SEARCH_RANGE );
		return false;
	}

	s_points.AddToTail( pt );

	// Apply live so the effect is visible immediately rather than next map load.
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( pt.type == FFNAVPT_SPAWN_CORNER )
	{
		// A corner on its own means nothing; only a completed set of four
		// bounds a room. Recompute the lot when a set closes.
		int cornerCount = 0;
		for ( int i = 0; i < s_points.Count(); ++i )
		{
			if ( s_points[ i ].type == FFNAVPT_SPAWN_CORNER && s_points[ i ].team == pt.team )
				++cornerCount;
		}

		if ( ( cornerCount % 4 ) == 0 )
		{
			ApplySpawnRegions( mesh );
			if ( mesh )
			{
				mesh->CollectAndMarkSpawnRoomExits();
				mesh->ComputeIncursionDistances();
				mesh->ComputeInvasionAreas();
			}
		}
		else
		{
			Msg( "[ff_nav_builder] %s spawn corner %d of 4 — %d more to close "
				"this room.\n", TeamName( pt.team ),
				( cornerCount % 4 ), 4 - ( cornerCount % 4 ) );
		}
	}
	else if ( pt.type == FFNAVPT_LINK_FROM || pt.type == FFNAVPT_LINK_TO )
	{
		// A link needs both ends before it means anything, same as a spawn room
		// needs four corners. Re-run the whole pass when a pair closes, so the
		// connection is live in the graph immediately and you can walk it to
		// check you put it where you meant to.
		int fromCount = 0;
		int toCount = 0;
		for ( int i = 0; i < s_points.Count(); ++i )
		{
			if ( s_points[ i ].type == FFNAVPT_LINK_FROM )
				++fromCount;
			else if ( s_points[ i ].type == FFNAVPT_LINK_TO )
				++toCount;
		}

		if ( fromCount == toCount )
		{
			const int made = ApplyLinkMarkers();
			Msg( "[ff_nav_builder] link pair closed — %d connection(s) now live.\n", made );
		}
		else
		{
			Msg( "[ff_nav_builder] %d linkfrom / %d linkto — place the other end "
				"to close the pair.\n", fromCount, toCount );
		}
	}
	else
	{
		CUtlVector< CFFNavArea * > areas;
		CollectAreasForPoint( pt, areas );
		for ( int a = 0; a < areas.Count(); ++a )
			ApplyPointToArea( pt, areas[ a ], mesh );
	}

	Save();

	char hud[ 128 ];
	if ( TypeNeedsTeam( pt.type ) )
	{
		Q_snprintf( hud, sizeof( hud ), "+ %s (%s)  [%d markers]",
			NameForType( pt.type ), TeamName( pt.team ), s_points.Count() );
	}
	else
	{
		Q_snprintf( hud, sizeof( hud ), "+ %s  [%d markers]",
			NameForType( pt.type ), s_points.Count() );
	}
	ClientPrint( player, HUD_PRINTCENTER, hud );

	Msg( "[ff_nav_builder] placed %s%s%s at (%.0f %.0f %.0f) radius %.0f, "
		"area %d.\n",
		NameForType( pt.type ),
		TypeNeedsTeam( pt.type ) ? " for team " : "",
		TypeNeedsTeam( pt.type ) ? TeamName( pt.team ) : "",
		pt.pos.x, pt.pos.y, pt.pos.z, pt.radius, area->GetID() );

	return true;
}


//-----------------------------------------------------------------------------
// Removing a marker is harder than adding one.
//
// Attribute bits are additive and shared: a nav area tagged FF_NAV_SENTRY_SPOT
// can have got that bit from a manual marker, from a mapper's nav_edit pass in
// the .nav file, or from the auto-tagger. There is no way to subtract one
// marker's contribution without knowing which. So deletion wipes the bits every
// marker type can set and re-derives the whole lot from scratch.
//
// CFFBotTagger::TagAreasFromEntities is the full derivation pass — entity tags,
// then FFNavBuilder::ApplyToMesh, then spawn exits, incursion distances,
// invasion vectors and heuristics — so calling it is both necessary and
// sufficient here. At authoring scale it's instant.
//-----------------------------------------------------------------------------
static void RebuildMeshTags( void )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
		return;

	const unsigned int kAuthoredWord1 =
		FF_NAV_SNIPER_SPOT | FF_NAV_SENTRY_SPOT | FF_NAV_DOORWAY |
		FF_NAV_FLAG_ANY | FF_NAV_CAP_ANY | FF_NAV_SPAWN_ROOM_ANY |
		FF_NAV_SPAWN_ROOM_EXIT |
		FF_NAV_HAS_AMMO | FF_NAV_HAS_HEALTH | FF_NAV_HAS_ARMOR |
		FF_NAV_NO_SPAWNING | FF_NAV_HUNTED_ESCAPE;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		area->ClearAttributeFF( kAuthoredWord1 );
		area->ClearAllAttributesFF2();
	}

	mesh->ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( mesh );
	mesh->MarkDoorwayAreas();
}


//-----------------------------------------------------------------------------
int FFNavBuilder::DeleteNear( const Vector &pos, float radius )
{
	const float radiusSq = radius * radius;
	int removed = 0;

	// Back to front: removal is by index and shifts everything after it.
	bool removedALink = false;

	for ( int i = s_points.Count() - 1; i >= 0; --i )
	{
		if ( ( s_points[ i ].pos - pos ).LengthSqr() > radiusSq )
			continue;

		Msg( "[ff_nav_builder] deleted %s at (%.0f %.0f %.0f)\n",
			NameForType( s_points[ i ].type ),
			s_points[ i ].pos.x, s_points[ i ].pos.y, s_points[ i ].pos.z );

		if ( s_points[ i ].type == FFNAVPT_LINK_FROM || s_points[ i ].type == FFNAVPT_LINK_TO )
			removedALink = true;

		s_points.Remove( i );
		++removed;
	}

	if ( removed == 0 )
		return 0;

	Save();
	RebuildMeshTags();

	if ( removedALink )
	{
		// Attributes can be wiped and re-derived; a graph edge cannot. Calling
		// CNavArea::Disconnect on a live mesh while PathFollowers hold segments
		// through it is how you get a bot walking a path whose next waypoint no
		// longer exists. Same reasoning as FFBotLearnedLinks::Clear.
		//
		// The marker is gone from the file, so the connection is gone from the
		// next map load. Saying so is better than pretending it worked.
		Warning( "[ff_nav_builder] the connection those markers made is still live "
			"in the graph. It goes away on the next map load — removing a nav edge "
			"from under a bot that is walking it is not safe to do mid-round.\n" );
	}

	return removed;
}


//-----------------------------------------------------------------------------
void FFNavBuilder::Clear( void )
{
	const int had = s_points.Count();
	s_points.RemoveAll();

	char filename[ MAX_PATH ];
	GetPointsFilename( filename, sizeof( filename ) );
	filesystem->RemoveFile( filename, "MOD" );

	RebuildMeshTags();

	Msg( "[ff_nav_builder] cleared %d marker(s) and removed %s.\n", had, filename );
}


//-----------------------------------------------------------------------------
int FFNavBuilder::TotalCount( void )
{
	return s_points.Count();
}


//-----------------------------------------------------------------------------
void FFNavBuilder::PrintReport( void )
{
	Msg( "==== FF manual nav markers ====\n" );
	Msg( "  mode=%s  selected=%s  team=%s  radius=%.0f  (%d marker%s)\n",
		ff_manual_nav_builder.GetBool() ? "ON" : "off",
		NameForType( s_selectedType ),
		ff_nav_builder_team.GetInt() == 0 ? "auto" : TeamName( ff_nav_builder_team.GetInt() ),
		ff_nav_builder_radius.GetFloat(),
		s_points.Count(), s_points.Count() == 1 ? "" : "s" );

	if ( !s_loadedThisMap )
		Msg( "  (sidecar not loaded — no nav mesh on this map?)\n" );

	int perType[ FFNAVPT_COUNT ] = { 0 };
	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( IsValidType( pt.type ) )
			++perType[ pt.type ];

		Msg( "  %-11s %-7s (%7.0f %7.0f %7.0f) r=%.0f\n",
			NameForType( pt.type ),
			TypeNeedsTeam( pt.type ) ? TeamName( pt.team ) : "-",
			pt.pos.x, pt.pos.y, pt.pos.z, pt.radius );
	}

	// Spawn rooms are the one type where a partial set is a silent no-op, so
	// call it out explicitly rather than making the author count lines.
	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		int corners = 0;
		for ( int i = 0; i < s_points.Count(); ++i )
		{
			if ( s_points[ i ].type == FFNAVPT_SPAWN_CORNER && s_points[ i ].team == team )
				++corners;
		}
		if ( corners == 0 )
			continue;
		Msg( "  %s spawn: %d corner(s) = %d complete room(s)%s\n",
			TeamName( team ), corners, corners / 4,
			( corners % 4 ) ? ", INCOMPLETE SET" : "" );
	}

	Msg( "===============================\n" );
}


//-----------------------------------------------------------------------------
// Queries.
//-----------------------------------------------------------------------------
bool FFNavBuilder::FindNearestPoint( int type, int team, const Vector &from, Vector *out )
{
	float bestDistSq = FLT_MAX;
	const FFNavPoint *best = NULL;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( pt.type != type )
			continue;
		if ( team >= 0 && TypeNeedsTeam( type ) && pt.team != team )
			continue;

		const float distSq = ( pt.pos - from ).LengthSqr();
		if ( distSq < bestDistSq )
		{
			bestDistSq = distSq;
			best = &pt;
		}
	}

	if ( !best )
		return false;
	if ( out )
		*out = best->pos;
	return true;
}

//-----------------------------------------------------------------------------
// Same search, but hands back the facing as well.
//
// Separate from FindNearestPoint rather than an extra out-param on it because
// most callers legitimately don't care: for a sentry hint the position is the
// instruction and the yaw is a nicety, while for an aim hint the yaw IS the
// instruction and the position only says where it applies. A caller that asks
// for the yaw is saying which kind of marker it is reading.
//-----------------------------------------------------------------------------
bool FFNavBuilder::FindNearestPointWithYaw( int type, int team, const Vector &from,
                                            Vector *outPos, float *outYaw )
{
	float bestDistSq = FLT_MAX;
	const FFNavPoint *best = NULL;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( pt.type != type )
			continue;
		if ( team >= 0 && TypeNeedsTeam( type ) && pt.team != team )
			continue;

		const float distSq = ( pt.pos - from ).LengthSqr();
		if ( distSq < bestDistSq )
		{
			bestDistSq = distSq;
			best = &pt;
		}
	}

	if ( !best )
		return false;
	if ( outPos )
		*outPos = best->pos;
	if ( outYaw )
		*outYaw = best->yaw;
	return true;
}

void FFNavBuilder::CollectPoints( int type, int team, CUtlVector< Vector > *outVector )
{
	if ( !outVector )
		return;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];
		if ( pt.type != type )
			continue;
		if ( team >= 0 && TypeNeedsTeam( type ) && pt.team != team )
			continue;
		outVector->AddToTail( pt.pos );
	}
}

int FFNavBuilder::CountPoints( int type )
{
	int count = 0;
	for ( int i = 0; i < s_points.Count(); ++i )
	{
		if ( s_points[ i ].type == type )
			++count;
	}
	return count;
}


//-----------------------------------------------------------------------------
// Authoring overlay. Only runs while the mode is on — this is a live server
// tick, and drawing a few hundred primitives every frame for nobody's benefit
// is not something to leave switched on by accident.
//
// Depth-tested: markers are occluded by world geometry like anything else. It
// keeps the view readable in a room full of them, and you can always noclip
// through the wall to look.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// On-screen key map.
//
// The console prints the layout once when the mode turns on, which is exactly
// when you scroll past it, and `ff_nav_builder_help` reprints it into a console
// you have to close again to place anything. Authoring means looking at the
// world, so the layout belongs on the world.
//
// Screen coordinates are normalised 0..1 from the top-left. The block sits down
// the left edge, clear of the crosshair and of FF's own HUD elements.
//-----------------------------------------------------------------------------
#define FFNAVPT_HUD_X			0.012f
#define FFNAVPT_HUD_Y			0.30f
#define FFNAVPT_HUD_LINE		0.021f

static void DrawKeyMapHud( float duration )
{
	float y = FFNAVPT_HUD_Y;
	char line[ 160 ];

	// Header: which page you're on, which type is selected, which team you're
	// authoring for. All three change what a keypress does, so all three have
	// to be visible without opening the console.
	Q_snprintf( line, sizeof( line ), "NAV BUILDER   page %d/%d  -  %s",
		s_currentPage + 1, FFNAVPT_PAGE_COUNT,
		FFNavBuilder::PageName( s_currentPage ) );
	NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y, line, 255, 255, 255, 255, duration );
	y += FFNAVPT_HUD_LINE;

	Q_snprintf( line, sizeof( line ), "selected: %s      team: %s      markers: %d",
		FFNavBuilder::NameForType( s_selectedType ),
		ff_nav_builder_team.GetInt() == 0 ? "auto (yours)" : TeamName( ff_nav_builder_team.GetInt() ),
		s_points.Count() );
	NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y, line, 200, 200, 200, 255, duration );
	y += FFNAVPT_HUD_LINE * 1.5f;

	// The nine slots of the current page, each in its own type's colour so the
	// list and the markers in the world agree at a glance.
	for ( int slot = 1; slot <= FFNAVPT_SLOTS_PER_PAGE; ++slot )
	{
		const int type = FFNavBuilder::TypeForSlot( s_currentPage, slot );

		if ( type == FFNAVPT_INVALID )
		{
			Q_snprintf( line, sizeof( line ), "  [%d]  -", slot );
			NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y, line, 90, 90, 90, 255, duration );
			y += FFNAVPT_HUD_LINE;
			continue;
		}

		int r, g, b;
		FFNavBuilder::ColorForType( type, &r, &g, &b );

		Q_snprintf( line, sizeof( line ), "  [%d]  %-12s %s%s", slot,
			FFNavBuilder::NameForType( type ),
			FFNavBuilder::TypeNeedsTeam( type ) ? "(team) " : "",
			FFNavBuilder::TypeUsesYaw( type ) ? "(faces where you look)" : "" );

		// The selected type is brightened rather than marked, so the colour
		// still reads.
		const bool isSelected = ( type == s_selectedType );
		NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y, line,
			r, g, b, isSelected ? 255 : 170, duration );
		y += FFNAVPT_HUD_LINE;
	}

	y += FFNAVPT_HUD_LINE * 0.5f;

	static const char * const kControls[] = {
		"  [/]  next page          [*]  next team",
		"  [0]  place selected     [.]  delete here",
		"  [+]/[-]  cycle type     [enter]  list all",
	};

	for ( int i = 0; i < (int)ARRAYSIZE( kControls ); ++i )
	{
		NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y, kControls[ i ],
			170, 210, 255, 255, duration );
		y += FFNAVPT_HUD_LINE;
	}

	// Only worth saying while it's true.
	if ( !s_loadedThisMap )
	{
		y += FFNAVPT_HUD_LINE * 0.5f;
		NDebugOverlay::ScreenText( FFNAVPT_HUD_X, y,
			"  sidecar not loaded - is there a nav mesh on this map?",
			255, 96, 96, 255, duration );
	}
}


void FFNavBuilder::Tick( void )
{
	if ( !ff_manual_nav_builder.GetBool() )
		return;
	if ( gpGlobals->curtime < s_nextOverlayTime )
		return;

	const float kRedrawInterval = 0.25f;
	s_nextOverlayTime = gpGlobals->curtime + kRedrawInterval;

	// Comfortably longer than the redraw interval: overlays expire on a
	// different clock to the one that schedules them, and a marker that blinks
	// out for a frame every quarter second is unusable to work against.
	const float duration = kRedrawInterval * 2.0f;

	if ( ff_nav_builder_keymap.GetBool() )
		DrawKeyMapHud( duration );

	if ( !ff_nav_builder_overlay.GetBool() )
		return;

	for ( int i = 0; i < s_points.Count(); ++i )
	{
		const FFNavPoint &pt = s_points[ i ];

		int r, g, b;
		ColorForType( pt.type, &r, &g, &b );

		// Solid body, in the type's colour — the marker itself.
		NDebugOverlay::Box( pt.pos, Vector( -12.0f, -12.0f, 0.0f ),
			Vector( 12.0f, 12.0f, 56.0f ), r, g, b, 96, duration );

		// A stalk, so a marker on the floor below still catches the eye from a
		// catwalk — which is exactly where you author from.
		NDebugOverlay::Line( pt.pos, pt.pos + Vector( 0.0f, 0.0f, 96.0f ),
			r, g, b, false, duration );

		// Ground cross: pins the exact position, which the box only implies.
		NDebugOverlay::Cross3D( pt.pos, 16.0f, r, g, b, false, duration );

		if ( pt.radius > 0.0f )
		{
			NDebugOverlay::Circle( pt.pos + Vector( 0.0f, 0.0f, 2.0f ),
				pt.radius, r, g, b, 64, false, duration );
		}

		// Facing arrow for the types where direction is the point. Without it
		// a sniper marker and an aim marker are indistinguishable boxes, and
		// the one piece of information they carry is invisible.
		if ( TypeUsesYaw( pt.type ) )
		{
			NDebugOverlay::YawArrow( pt.pos + Vector( 0.0f, 0.0f, 28.0f ),
				pt.yaw, 72.0f, 10.0f, r, g, b, 192, false, duration );
		}

		char label[ 96 ];
		if ( TypeNeedsTeam( pt.type ) && TypeUsesYaw( pt.type ) )
		{
			Q_snprintf( label, sizeof( label ), "%s (%s) %.0f deg",
				NameForType( pt.type ), TeamName( pt.team ), pt.yaw );
		}
		else if ( TypeNeedsTeam( pt.type ) )
		{
			Q_snprintf( label, sizeof( label ), "%s (%s)",
				NameForType( pt.type ), TeamName( pt.team ) );
		}
		else if ( TypeUsesYaw( pt.type ) )
		{
			Q_snprintf( label, sizeof( label ), "%s %.0f deg",
				NameForType( pt.type ), pt.yaw );
		}
		else
		{
			Q_strncpy( label, NameForType( pt.type ), sizeof( label ) );
		}

		NDebugOverlay::EntityTextAtPosition( pt.pos + Vector( 0.0f, 0.0f, 104.0f ),
			0, label, duration, r, g, b, 255 );
	}
}


//=============================================================================
// Console commands.
//=============================================================================

//-----------------------------------------------------------------------------
// Key bindings.
//
// A server cannot rebind a client's keys — cl_restrict_server_commands blocks
// exactly that, and rightly so. What it can do is write a cfg and, on a listen
// server (where the authoring actually happens), exec it through the shared
// console.
//
// The binds are safe to leave in place forever: every command they invoke is a
// no-op while ff_manual_nav_builder is 0, so there is no "restore my binds"
// step to get wrong. That's also why the numpad is the target — it's the one
// block of keys FF leaves alone by default.
//-----------------------------------------------------------------------------
static const char * const kBuilderCfgName = "cfg/ff_navbuilder.cfg";

static const char * const kBuilderCfgBody =
	"// Fortress Forever - manual bot nav builder key binds\n"
	"//\n"
	"// Generated by ff_nav_builder_writecfg. Safe to keep bound permanently:\n"
	"// every command below refuses to act while ff_manual_nav_builder is 0.\n"
	"//\n"
	"// Digits 1-9 place from the CURRENT PAGE. KP_SLASH changes page. Page 1\n"
	"// is the original nine-key layout, so nothing you already learned moved.\n"
	"//\n"
	"bind \"KP_END\"        \"ff_nav_place_slot 1\"\n"
	"bind \"KP_DOWNARROW\"  \"ff_nav_place_slot 2\"\n"
	"bind \"KP_PGDN\"       \"ff_nav_place_slot 3\"\n"
	"bind \"KP_LEFTARROW\"  \"ff_nav_place_slot 4\"\n"
	"bind \"KP_5\"          \"ff_nav_place_slot 5\"\n"
	"bind \"KP_RIGHTARROW\" \"ff_nav_place_slot 6\"\n"
	"bind \"KP_HOME\"       \"ff_nav_place_slot 7\"\n"
	"bind \"KP_UPARROW\"    \"ff_nav_place_slot 8\"\n"
	"bind \"KP_PGUP\"       \"ff_nav_place_slot 9\"\n"
	"//\n"
	"bind \"KP_INS\"        \"ff_nav_builder_place\"\n"
	"bind \"KP_DEL\"        \"ff_nav_delete\"\n"
	"bind \"KP_SLASH\"      \"ff_nav_builder_page_cycle\"\n"
	"bind \"KP_MULTIPLY\"   \"ff_nav_builder_team_cycle\"\n"
	"bind \"KP_MINUS\"      \"ff_nav_builder_prev\"\n"
	"bind \"KP_PLUS\"       \"ff_nav_builder_next\"\n"
	"bind \"KP_ENTER\"      \"ff_nav_builder_list\"\n"
	"echo \"[ff_nav_builder] numpad bound. ff_nav_builder_help for the map.\"\n";

static bool WriteBuilderCfg( void )
{
	CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
	buf.PutString( kBuilderCfgBody );

	if ( !filesystem->WriteFile( kBuilderCfgName, "MOD", buf ) )
	{
		Warning( "[ff_nav_builder] could not write %s.\n", kBuilderCfgName );
		return false;
	}
	return true;
}

static void PrintBuilderHelp( void )
{
	Msg( "==== ff_manual_nav_builder ====\n" );
	Msg( "  Numpad (after 'exec ff_navbuilder'):\n" );
	Msg( "    1-9        place from the current page      0 / Ins  place SELECTED type\n" );
	Msg( "    . / Del    delete markers where you stand\n" );
	Msg( "    /  change page    *  change team    +/-  cycle type    Enter  list\n" );
	Msg( "\n" );

	for ( int page = 0; page < FFNAVPT_PAGE_COUNT; ++page )
	{
		Msg( "  Page %d — %s%s\n", page + 1, FFNavBuilder::PageName( page ),
			page == FFNavBuilder::GetPage() ? "   <-- current" : "" );
		for ( int slot = 1; slot <= FFNAVPT_SLOTS_PER_PAGE; ++slot )
		{
			const int type = FFNavBuilder::TypeForSlot( page, slot );
			if ( type == FFNAVPT_INVALID )
				continue;
			Msg( "      %d  %-12s %s\n", slot, FFNavBuilder::NameForType( type ),
				FFNavBuilder::DescriptionForType( type ) );
		}
	}

	Msg( "\n" );
	Msg( "  Or by name, any type, any time:  ff_nav_place <type> [team]\n" );
	Msg( "  Full type list:\n" );
	for ( int i = 1; i < FFNAVPT_COUNT; ++i )
	{
		Msg( "    %-12s %-6s %-5s %s\n",
			s_typeInfo[ i ].name,
			s_typeInfo[ i ].needsTeam ? "(team)" : "",
			s_typeInfo[ i ].usesYaw ? "(yaw)" : "",
			s_typeInfo[ i ].description );
	}
	Msg( "\n" );
	Msg( "  (team) markers use ff_nav_builder_team: 0 = your team, 2 blue,\n" );
	Msg( "         3 red, 4 yellow, 5 green.\n" );
	Msg( "  (yaw)  markers record the direction you were LOOKING when placed —\n" );
	Msg( "         stand where they stand, look where they should look.\n" );
	Msg( "\n" );
	Msg( "  Markers save to maps/<map>.ffnavpoints on every placement.\n" );
	Msg( "  Spawn rooms need FOUR corner markers of the same team.\n" );
	Msg( "  ff_nav_builder_radius >0 tags every area within that distance.\n" );
	Msg( "===============================\n" );
}

static void OnBuilderModeChanged( IConVar *var, const char *pOldValue, float flOldValue )
{
	// Signature is fixed by FnChangeCallback_t; we only care about the new value.
	NOTE_UNUSED( var );
	NOTE_UNUSED( pOldValue );
	NOTE_UNUSED( flOldValue );

	if ( !ff_manual_nav_builder.GetBool() )
	{
		Msg( "[ff_nav_builder] mode OFF. Binds stay put and stay harmless.\n" );
		return;
	}

	if ( !WriteBuilderCfg() )
		return;

	PrintBuilderHelp();
	FFNavBuilder::PrintReport();

	if ( engine && !engine->IsDedicatedServer() )
	{
		// Listen server: server and client share a console, so this actually
		// reaches the client's bind table. On a dedicated server it would just
		// error out on 'bind', which is why it's guarded.
		engine->ServerCommand( "exec ff_navbuilder\n" );
	}
	else
	{
		Msg( "[ff_nav_builder] dedicated server: run 'exec ff_navbuilder' on the "
			"client to get the binds.\n" );
	}
}


//-----------------------------------------------------------------------------
static bool BuilderModeGuard( void )
{
	if ( ff_manual_nav_builder.GetBool() )
		return true;

	Msg( "[ff_nav_builder] ff_manual_nav_builder is 0 — set it to 1 first.\n" );
	return false;
}


CON_COMMAND_F( ff_nav_place,
	"Place a manual bot nav marker at your feet. Usage: ff_nav_place <type> [team]",
	FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;

	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_place <type> [team]\n" );
		PrintBuilderHelp();
		return;
	}

	const int type = FFNavBuilder::TypeFromName( args.Arg( 1 ) );
	if ( type == FFNAVPT_INVALID )
	{
		Msg( "[ff_nav_place] unknown type '%s'. ff_nav_builder_help lists them.\n",
			args.Arg( 1 ) );
		return;
	}

	const int team = ( args.ArgC() >= 3 ) ? Q_atoi( args.Arg( 2 ) ) : -1;
	FFNavBuilder::Place( UTIL_GetCommandClient(), type, team );
}


CON_COMMAND_F( ff_nav_builder_place,
	"Place the currently selected manual nav marker at your feet.",
	FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;
	FFNavBuilder::Place( UTIL_GetCommandClient(), FFNavBuilder::GetSelectedType(), -1 );
}


CON_COMMAND_F( ff_nav_place_slot,
	"Place the marker bound to a numpad slot on the current page. "
	"Usage: ff_nav_place_slot <1-9>",
	FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;

	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_place_slot <1-%d>\n", FFNAVPT_SLOTS_PER_PAGE );
		FFNavBuilder::PrintPage();
		return;
	}

	const int slot = Q_atoi( args.Arg( 1 ) );
	const int type = FFNavBuilder::TypeForSlot( FFNavBuilder::GetPage(), slot );

	if ( type == FFNAVPT_INVALID )
	{
		Msg( "[ff_nav_place_slot] slot %d is empty on page %d (%s).\n",
			slot, FFNavBuilder::GetPage() + 1,
			FFNavBuilder::PageName( FFNavBuilder::GetPage() ) );
		return;
	}

	// Placing from a slot also selects that type, so KP_INS repeats it without
	// having to think about which page you're on.
	FFNavBuilder::SetSelectedType( type );
	FFNavBuilder::Place( UTIL_GetCommandClient(), type, -1 );
}


CON_COMMAND_F( ff_nav_builder_page_cycle,
	"Switch the numpad to the next page of marker types.",
	FCVAR_CHEAT )
{
	FFNavBuilder::CyclePage( 1 );
	FFNavBuilder::PrintPage();

	CBasePlayer *player = UTIL_GetCommandClient();
	if ( player )
	{
		char hud[ 128 ];
		Q_snprintf( hud, sizeof( hud ), "page %d/%d: %s",
			FFNavBuilder::GetPage() + 1, FFNAVPT_PAGE_COUNT,
			FFNavBuilder::PageName( FFNavBuilder::GetPage() ) );
		ClientPrint( player, HUD_PRINTCENTER, hud );
	}
}


CON_COMMAND_F( ff_nav_builder_page,
	"Show the current numpad page, or switch to one. "
	"Usage: ff_nav_builder_page [1-3]",
	FCVAR_CHEAT )
{
	if ( args.ArgC() >= 2 )
	{
		const int page = Q_atoi( args.Arg( 1 ) ) - 1;
		if ( page < 0 || page >= FFNAVPT_PAGE_COUNT )
		{
			Msg( "[ff_nav_builder_page] page must be 1-%d.\n", FFNAVPT_PAGE_COUNT );
			return;
		}
		FFNavBuilder::SetPage( page );
	}
	FFNavBuilder::PrintPage();
}


CON_COMMAND_F( ff_nav_delete,
	"Delete manual nav markers near you. Optional arg: radius (default "
	"ff_nav_builder_delete_radius).",
	FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;

	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player )
	{
		Msg( "[ff_nav_delete] must be issued by a player.\n" );
		return;
	}

	const float radius = ( args.ArgC() >= 2 )
		? (float)Q_atof( args.Arg( 1 ) )
		: ff_nav_builder_delete_radius.GetFloat();

	// Markers sit on the floor; the player's origin is their feet, so a plain
	// radial test is right without any vertical fudging.
	const int removed = FFNavBuilder::DeleteNear( player->GetAbsOrigin(), radius );

	char hud[ 96 ];
	if ( removed > 0 )
		Q_snprintf( hud, sizeof( hud ), "- deleted %d marker(s)", removed );
	else
		Q_snprintf( hud, sizeof( hud ), "no markers within %.0fu", radius );
	ClientPrint( player, HUD_PRINTCENTER, hud );

	Msg( "[ff_nav_delete] removed %d marker(s) within %.0fu.\n", removed, radius );
}


CON_COMMAND_F( ff_nav_builder_next, "Select the next manual nav marker type.", FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;
	FFNavBuilder::CycleSelectedType( 1 );

	CBasePlayer *player = UTIL_GetCommandClient();
	if ( player )
		ClientPrint( player, HUD_PRINTCENTER, FFNavBuilder::NameForType( FFNavBuilder::GetSelectedType() ) );
}


CON_COMMAND_F( ff_nav_builder_prev, "Select the previous manual nav marker type.", FCVAR_CHEAT )
{
	if ( !BuilderModeGuard() )
		return;
	FFNavBuilder::CycleSelectedType( -1 );

	CBasePlayer *player = UTIL_GetCommandClient();
	if ( player )
		ClientPrint( player, HUD_PRINTCENTER, FFNavBuilder::NameForType( FFNavBuilder::GetSelectedType() ) );
}


CON_COMMAND_F( ff_nav_builder_select,
	"Select a manual nav marker type by name. Usage: ff_nav_builder_select <type>",
	FCVAR_CHEAT )
{
	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: ff_nav_builder_select <type>\n" );
		return;
	}

	const int type = FFNavBuilder::TypeFromName( args.Arg( 1 ) );
	if ( type == FFNAVPT_INVALID )
	{
		Msg( "[ff_nav_builder_select] unknown type '%s'.\n", args.Arg( 1 ) );
		return;
	}

	FFNavBuilder::SetSelectedType( type );
	Msg( "[ff_nav_builder] selected: %s — %s\n",
		FFNavBuilder::NameForType( type ), FFNavBuilder::DescriptionForType( type ) );
}


CON_COMMAND_F( ff_nav_builder_team_cycle,
	"Cycle the team stamped on team-specific markers: auto -> blue -> red -> "
	"yellow -> green.",
	FCVAR_CHEAT )
{
	int team = ff_nav_builder_team.GetInt();
	team = ( team < TEAM_BLUE || team >= TEAM_GREEN ) ? ( team == 0 ? TEAM_BLUE : 0 ) : team + 1;
	if ( team > TEAM_GREEN )
		team = 0;
	ff_nav_builder_team.SetValue( team );

	const char *label = ( team == 0 ) ? "auto (your team)" : TeamName( team );
	Msg( "[ff_nav_builder] authoring team: %s\n", label );

	CBasePlayer *player = UTIL_GetCommandClient();
	if ( player )
	{
		char hud[ 64 ];
		Q_snprintf( hud, sizeof( hud ), "team: %s", label );
		ClientPrint( player, HUD_PRINTCENTER, hud );
	}
}


CON_COMMAND_F( ff_nav_builder_list, "List every manual nav marker on this map.", FCVAR_CHEAT )
{
	FFNavBuilder::PrintReport();
}


CON_COMMAND_F( ff_nav_builder_save, "Write manual nav markers to disk now.", FCVAR_CHEAT )
{
	FFNavBuilder::Save();
}


CON_COMMAND_F( ff_nav_builder_clear,
	"Delete EVERY manual nav marker on this map and remove the sidecar file.",
	FCVAR_CHEAT )
{
	// Destructive, unrecoverable, and one letter away from _save in autocomplete,
	// so it takes an explicit confirmation argument.
	if ( args.ArgC() < 2 || Q_stricmp( args.Arg( 1 ), "confirm" ) != 0 )
	{
		Msg( "[ff_nav_builder_clear] this permanently deletes all %d marker(s) "
			"for %s and removes maps/%s.ffnavpoints.\n",
			FFNavBuilder::TotalCount(), STRING( gpGlobals->mapname ),
			STRING( gpGlobals->mapname ) );
		Msg( "  Run 'ff_nav_builder_clear confirm' if that's what you want.\n" );
		return;
	}

	FFNavBuilder::Clear();
}


CON_COMMAND_F( ff_nav_builder_reload,
	"Re-read maps/<map>.ffnavpoints and re-apply it. Use after hand-editing.",
	FCVAR_CHEAT )
{
	FFNavBuilder::Reload();
}


CON_COMMAND_F( ff_nav_builder_writecfg,
	"(Re)write cfg/ff_navbuilder.cfg with the numpad key binds.",
	FCVAR_CHEAT )
{
	if ( WriteBuilderCfg() )
	{
		Msg( "[ff_nav_builder] wrote %s. Run 'exec ff_navbuilder' to bind.\n",
			kBuilderCfgName );
	}
}


CON_COMMAND_F( ff_nav_builder_help, "Show the manual nav builder key map and marker types.", FCVAR_CHEAT )
{
	PrintBuilderHelp();
}
