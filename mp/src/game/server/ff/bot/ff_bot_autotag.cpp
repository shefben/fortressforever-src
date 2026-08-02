//========= Fortress Forever Bot =============================================//
//
// CFFBotAutoTagger — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_autotag.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "nav_mesh.h"
#include "nav_ladder.h"
#include "shareddefs.h"
#include "bspflags.h"		// SURF_SKY
#include "engine/IEngineTrace.h"
#include "entitylist.h"
#include "ff_bot_lua_objectives.h"
#include "ff_bot_ride_lift.h"	// FFBotLift::IsLiftEntity — one definition, shared with the rider

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Tunables — radius of the local neighborhood the high-ground / cover
// heuristics consider. 1500u covers ~one large room or one open mid-area
// of a typical FF map.
#define FFBOT_AUTOTAG_NEIGHBORHOOD_RADIUS	1500.0f

// An area must be at least this many units above the median Z of its
// horizontal neighborhood to count as "high ground".
//
// SNIPER FIX 5 — this was 32u, which is barely more than StepHeight (18u).
// At that threshold practically every raised floor, doorstep and ramp landing
// in the map got tagged FF_NAV_HIGH_GROUND, so the tag carried almost no
// information and the sniper's HIGH_GROUND fallback tier was choosing between
// hundreds of mediocre candidates — one of the reasons it kept picking poor
// perches. 64u is a crouch-jump: a genuine floor-level change rather than a
// kerb, which is what "high ground" is supposed to mean.
#define FFBOT_AUTOTAG_HIGHGROUND_DELTA		64.0f

// Min/max widths a "choke" area can have. Below the min it's likely
// unwalkable detail; above the max it's a regular room.
#define FFBOT_AUTOTAG_CHOKE_MIN_WIDTH		32.0f
#define FFBOT_AUTOTAG_CHOKE_MAX_WIDTH		96.0f

// An area is considered "in the flag pocket" for sentry detection if
// it's within this distance of our flag.
#define FFBOT_AUTOTAG_SENTRY_FLAG_RADIUS	1200.0f

// How far a backward trace must hit a wall for an area to count as
// having "cover behind it" for sentry placement.
#define FFBOT_AUTOTAG_COVER_TRACE_RANGE		200.0f


//-----------------------------------------------------------------------------
// Water detection. UTIL_PointContents reads engine brush volume contents.
//
// We probe two heights:
//   - feet (+1u above area floor): if in CONTENTS_WATER → wading depth
//   - midbody (+36u): if also in water → bot has to swim through here
//-----------------------------------------------------------------------------
static void TagWater( CFFNavArea *area )
{
	const Vector center = area->GetCenter();
	const int feetContents = UTIL_PointContents( center + Vector( 0, 0, 1.0f ) );
	const bool feetInWater = ( feetContents & ( CONTENTS_WATER | CONTENTS_SLIME ) ) != 0;

	if ( !feetInWater )
		return;

	// Feet wet — check midbody.
	const int midContents = UTIL_PointContents( center + Vector( 0, 0, 36.0f ) );
	const bool midInWater = ( midContents & ( CONTENTS_WATER | CONTENTS_SLIME ) ) != 0;

	if ( midInWater )
		area->SetAttributeFF( FF_NAV_UNDERWATER );
	else
		area->SetAttributeFF( FF_NAV_WATER );
}


//-----------------------------------------------------------------------------
// Choke detection. A choke is a narrow nav area with few connections, sitting
// between larger areas. The heuristic:
//   - min(SizeX, SizeY) is in [32, 96] — narrower is unwalkable, wider is a
//     room
//   - 2D nav-area connection count <= 2 (one in, one out at most)
//
// Crude but effective on FF maps where corridors and doorways drive the
// gameplay flow. False positives (a small isolated area) don't hurt much
// — those just get a slight cost penalty in pathing.
//-----------------------------------------------------------------------------
static void TagChoke( CFFNavArea *area )
{
	const float width = MIN( area->GetSizeX(), area->GetSizeY() );
	if ( width < FFBOT_AUTOTAG_CHOKE_MIN_WIDTH ||
	     width > FFBOT_AUTOTAG_CHOKE_MAX_WIDTH )
		return;

	// Count distinct 2D-edge connections. NavDirType is 4-way; sum across.
	int totalConnections = 0;
	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		totalConnections += area->GetAdjacentCount( (NavDirType)dir );
	}

	if ( totalConnections <= 3 )
		area->SetAttributeFF( FF_NAV_CHOKE );
}


//-----------------------------------------------------------------------------
// High-ground detection. For each area, compare its Z to the *median* Z of
// areas within FFBOT_AUTOTAG_NEIGHBORHOOD_RADIUS. Median (not mean) so a few
// extreme outliers (e.g., one rooftop area in the neighborhood) don't pull
// the threshold up and falsely demote actual high-ground spots.
//
// Computing per-area median is O(N²) over nav areas in the worst case, but
// FF maps typically have 1-3k nav areas — well under a second total at level
// init.
//-----------------------------------------------------------------------------
static void TagHighGround( const CUtlVector< CFFNavArea * > &allAreas )
{
	const float radSq = FFBOT_AUTOTAG_NEIGHBORHOOD_RADIUS *
	                     FFBOT_AUTOTAG_NEIGHBORHOOD_RADIUS;

	CUtlVector< float > localZ;
	localZ.EnsureCapacity( 256 );

	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		CFFNavArea *area = allAreas[ i ];
		const Vector myCenter = area->GetCenter();

		localZ.RemoveAll();
		for ( int j = 0; j < allAreas.Count(); ++j )
		{
			if ( i == j )
				continue;
			const Vector otherCenter = allAreas[ j ]->GetCenter();
			const float dx = otherCenter.x - myCenter.x;
			const float dy = otherCenter.y - myCenter.y;
			if ( dx * dx + dy * dy <= radSq )
				localZ.AddToTail( otherCenter.z );
		}
		if ( localZ.Count() < 4 )
			continue;	// not enough neighbors to judge

		// Find median via partial sort (insertion sort is fine for ≤200 elem).
		const int n = localZ.Count();
		for ( int p = 1; p < n; ++p )
		{
			const float key = localZ[ p ];
			int q = p - 1;
			while ( q >= 0 && localZ[ q ] > key )
			{
				localZ[ q + 1 ] = localZ[ q ];
				--q;
			}
			localZ[ q + 1 ] = key;
		}
		const float median = localZ[ n / 2 ];

		if ( myCenter.z > median + FFBOT_AUTOTAG_HIGHGROUND_DELTA )
			area->SetAttributeFF( FF_NAV_HIGH_GROUND );
	}
}


//-----------------------------------------------------------------------------
// Auto sniper spot. An area qualifies if:
//   - tagged FF_NAV_HIGH_GROUND (already passed elevation test)
//   - NOT in any spawn room (don't snipe from inside spawn)
//   - has line-of-sight to at least one ingress target (enemy spawn-room
//     exit, our flag area, our cap area, or any flag/cap area)
//   - is not underwater
//
// We feed a single eye-position list of "places enemies travel through" and
// trace from this area's eye to each one, taking the first hit as proof of
// LOS. One hit is sufficient — we're not requiring the perfect overlook,
// just a usable angle.
//-----------------------------------------------------------------------------
// Returns true if the area "sees the outside" — either there's open sky
// directly above it (rooftops, exposed balconies) OR it has line-of-sight
// to at least one ingress eye through opaque-only geometry (window booths,
// shooting slits, doorways with a view of the courtyard). Pure interior
// rooms with neither qualify.
static bool AreaCanSnipeOutdoors( const Vector &spotPos,
                                   const CUtlVector< Vector > &ingressEyes )
{
	// Open-sky test. Trace upward from chest height. Accept either no hit
	// at all (very tall outdoor) or a hit on a sky brush (typical map
	// boundary). A hit on a regular brush means there's a ceiling above
	// = covered.
	{
		const Vector probeFrom = spotPos + Vector( 0, 0, 40.0f );
		const Vector probeTo   = spotPos + Vector( 0, 0, 1024.0f );
		trace_t tr;
		UTIL_TraceLine( probeFrom, probeTo, MASK_SOLID,
			NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.99f )
			return true;
		if ( tr.surface.flags & SURF_SKY )
			return true;
	}

	// Window/booth test. Even if there's a ceiling, a sniper booth has a
	// shooting slit with LOS to the courtyard. If we can see any ingress
	// eye via traceline through MASK_OPAQUE (= sees through glass /
	// non-collision props but NOT through walls), this is a viable booth.
	const Vector eyeFrom = spotPos + Vector( 0, 0, 64.0f );
	for ( int i = 0; i < ingressEyes.Count(); ++i )
	{
		trace_t tr;
		UTIL_TraceLine( eyeFrom, ingressEyes[ i ],
			MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.99f )
			return true;
	}

	return false;
}


static int TagAutoSniperSpots( const CUtlVector< CFFNavArea * > &allAreas,
                               const CUtlVector< Vector > &ingressEyes,
                               CFFNavMesh *mesh )
{
	int rejNoIngress = 0, rejNotHigh = 0, rejSpawn = 0, rejUnderwater = 0,
	    rejBlocked = 0, rejInward = 0, rejNoView = 0, tagged = 0;

	if ( ingressEyes.Count() == 0 )
	{
		Msg( "[CFFBotAutoTagger][sniper] No ingress eyes — skipping pass.\n" );
		return 0;
	}

	// Build per-team forwardness reference.
	struct PerTeam
	{
		Vector ourExitCentroid;
		Vector outwardDir;
		bool valid;
	};
	PerTeam perTeam[ FF_NAV_TEAM_COUNT ];
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
		perTeam[ i ].valid = false;

	Vector ingressCentroid = vec3_origin;
	for ( int i = 0; i < ingressEyes.Count(); ++i )
		ingressCentroid += ingressEyes[ i ];
	ingressCentroid *= ( 1.0f / (float)ingressEyes.Count() );

	int validTeamCount = 0;
	if ( mesh )
	{
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			const CUtlVector< CFFNavArea * > *exits = mesh->GetSpawnRoomExitAreas( t );
			if ( !exits || exits->Count() == 0 )
			{
				Msg( "[CFFBotAutoTagger][sniper] team %d: no spawn-exit areas.\n", t );
				continue;
			}
			Vector sum = vec3_origin;
			for ( int j = 0; j < exits->Count(); ++j )
				sum += ( *exits )[ j ]->GetCenter();
			Vector exitCentroid = sum * ( 1.0f / (float)exits->Count() );
			Vector outward = ingressCentroid - exitCentroid;
			outward.z = 0.0f;
			if ( outward.NormalizeInPlace() < 1.0f )
			{
				Msg( "[CFFBotAutoTagger][sniper] team %d: degenerate outward "
					"(exitCentroid=ingressCentroid).\n", t );
				continue;
			}
			perTeam[ t - TEAM_BLUE ].ourExitCentroid = exitCentroid;
			perTeam[ t - TEAM_BLUE ].outwardDir = outward;
			perTeam[ t - TEAM_BLUE ].valid = true;
			++validTeamCount;
			Msg( "[CFFBotAutoTagger][sniper] team %d: exitCentroid=(%.0f %.0f %.0f) "
				"outward=(%.2f %.2f %.2f)\n", t,
				exitCentroid.x, exitCentroid.y, exitCentroid.z,
				outward.x, outward.y, outward.z );
		}
	}
	Msg( "[CFFBotAutoTagger][sniper] ingressEyes=%d ingressCentroid=(%.0f %.0f %.0f) "
		"validTeams=%d\n", ingressEyes.Count(),
		ingressCentroid.x, ingressCentroid.y, ingressCentroid.z, validTeamCount );

	bool anyTeamValid = ( validTeamCount > 0 );

	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		CFFNavArea *area = allAreas[ i ];
		const unsigned int attrs = area->GetAttributesFF();

		if ( !( attrs & FF_NAV_HIGH_GROUND ) ) { ++rejNotHigh; continue; }
		if ( attrs & FF_NAV_SPAWN_ROOM_ANY )   { ++rejSpawn; continue; }
		if ( attrs & FF_NAV_UNDERWATER )       { ++rejUnderwater; continue; }
		if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
		{
			++rejBlocked;
			continue;
		}

		const Vector spotPos = area->GetCenter();

		if ( anyTeamValid )
		{
			bool forwardOfSomeTeam = false;
			for ( int t = 0; t < FF_NAV_TEAM_COUNT; ++t )
			{
				if ( !perTeam[ t ].valid )
					continue;
				const float forwardness = ( spotPos - perTeam[ t ].ourExitCentroid )
					.Dot( perTeam[ t ].outwardDir );
				if ( forwardness > 0.0f )
				{
					forwardOfSomeTeam = true;
					break;
				}
			}
			if ( !forwardOfSomeTeam )
			{
				++rejInward;
				continue;
			}
		}

		// Open-sky / window-LOS gate. Without this, every elevated area
		// in front of the base — including the ones under a roof
		// overhang — gets tagged. We want only ROOFTOPS (open sky
		// directly above) and SNIPER BOOTHS (enclosed but with a
		// shooting slit / window LOS to the enemy approach).
		if ( !AreaCanSnipeOutdoors( spotPos, ingressEyes ) )
		{
			++rejNoView;
			continue;
		}

		area->SetAttributeFF( FF_NAV_AUTO_SNIPER_SPOT );
		++tagged;
		if ( tagged <= 5 )
		{
			Msg( "[CFFBotAutoTagger][sniper] tagged area #%d at (%.0f %.0f %.0f)\n",
				area->GetID(), spotPos.x, spotPos.y, spotPos.z );
		}
	}
	(void)rejNoIngress;

	Msg( "[CFFBotAutoTagger][sniper] tagged=%d  rej: notHigh=%d spawn=%d "
		"underwater=%d blocked=%d inward=%d noView=%d\n",
		tagged, rejNotHigh, rejSpawn, rejUnderwater, rejBlocked, rejInward, rejNoView );

	return tagged;
}


//-----------------------------------------------------------------------------
// Auto sentry spot. An area qualifies if:
//   - within FFBOT_AUTOTAG_SENTRY_FLAG_RADIUS of any flag area (our defensive
//     pocket OR an enemy's, since "battle engineer" wants to push forward)
//   - NOT inside any spawn room
//   - NOT underwater (you can't build there)
//   - has cover behind: at least one of the 4 cardinal back-traces hits a
//     wall within FFBOT_AUTOTAG_COVER_TRACE_RANGE — gives the sentry a
//     blind side that spies can't backstab from
//   - has LOS to ≥1 ingress eye (so the SG can fire at attackers)
//
// Heuristic captures "near a flag, sheltered, watching the lane" without
// needing per-map hand-tagging.
//-----------------------------------------------------------------------------
static void TagAutoSentrySpots( const CUtlVector< CFFNavArea * > &allAreas,
                                const CUtlVector< Vector > &flagAreaCenters,
                                const CUtlVector< Vector > &ingressEyes )
{
	if ( flagAreaCenters.Count() == 0 || ingressEyes.Count() == 0 )
		return;

	const float flagRadSq = FFBOT_AUTOTAG_SENTRY_FLAG_RADIUS *
	                         FFBOT_AUTOTAG_SENTRY_FLAG_RADIUS;

	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		CFFNavArea *area = allAreas[ i ];
		const unsigned int attrs = area->GetAttributesFF();

		if ( attrs & FF_NAV_SPAWN_ROOM_ANY )
			continue;
		if ( attrs & FF_NAV_UNDERWATER )
			continue;
		if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
			continue;

		const Vector center = area->GetCenter();

		// Near any flag area?
		bool nearFlag = false;
		for ( int j = 0; j < flagAreaCenters.Count(); ++j )
		{
			if ( ( flagAreaCenters[ j ] - center ).LengthSqr() < flagRadSq )
			{
				nearFlag = true;
				break;
			}
		}
		if ( !nearFlag )
			continue;

		// Cover behind? Trace 4 cardinal directions; need at least one
		// nearby wall.
		bool hasCover = false;
		static const Vector kDirs[ 4 ] = {
			Vector(  1.0f,  0.0f, 0.0f ),
			Vector( -1.0f,  0.0f, 0.0f ),
			Vector(  0.0f,  1.0f, 0.0f ),
			Vector(  0.0f, -1.0f, 0.0f ),
		};
		const Vector probeFrom = center + Vector( 0, 0, 32.0f );
		for ( int d = 0; d < 4; ++d )
		{
			trace_t tr;
			UTIL_TraceLine( probeFrom,
				probeFrom + kDirs[ d ] * FFBOT_AUTOTAG_COVER_TRACE_RANGE,
				MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction < 0.6f )
			{
				hasCover = true;
				break;
			}
		}
		if ( !hasCover )
			continue;

		// LOS to ingress?
		const Vector eyeFrom = center + Vector( 0, 0, 64.0f );
		bool hasLOS = false;
		for ( int j = 0; j < ingressEyes.Count(); ++j )
		{
			trace_t tr;
			UTIL_TraceLine( eyeFrom, ingressEyes[ j ],
				MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 0.99f )
			{
				hasLOS = true;
				break;
			}
		}
		if ( hasLOS )
			area->SetAttributeFF( FF_NAV_AUTO_SENTRY_SPOT );
	}
}


//-----------------------------------------------------------------------------
// Ladder tags. Walks every ladder in the mesh and tags all of its connected
// areas with FF_NAV_NEAR_LADDER (which the path cost reads) and FF_NAV2_LADDER
// (which makes it visible and authorable).
//
// Both, because they answer different questions and the second one was
// missing. FF_NAV_NEAR_LADDER had no entry in ff_nav_visualize and no marker
// type, so "2fort's ladder has nothing on it" could equally have meant the
// ladder was never generated, was generated but connected to nothing, or was
// tagged perfectly and simply never drawn. Those want different fixes.
//
// Returns the number of ladders that contributed at least one tagged area, so
// a mesh whose ladders exist but connect to nothing can be reported as such
// rather than silently counted as working.
//-----------------------------------------------------------------------------
static int TagLadderAdjacent( int *outConnectedLadders )
{
	int tagged = 0;
	int connected = 0;

	if ( outConnectedLadders )
		*outConnectedLadders = 0;

	if ( !TheNavMesh )
		return 0;

	const NavLadderVector &ladders = TheNavMesh->GetLadders();

	for ( int i = 0; i < ladders.Count(); ++i )
	{
		CNavLadder *ladder = ladders[ i ];
		if ( !ladder )
			continue;

		CNavArea * const ends[] = {
			ladder->m_topForwardArea, ladder->m_topLeftArea,
			ladder->m_topRightArea, ladder->m_topBehindArea,
			ladder->m_bottomArea,
		};

		bool any = false;

		for ( int e = 0; e < (int)ARRAYSIZE( ends ); ++e )
		{
			if ( !ends[ e ] )
				continue;

			CFFNavArea *area = static_cast< CFFNavArea * >( ends[ e ] );
			if ( !area->HasAttributeFF2( FF_NAV2_LADDER ) )
				++tagged;

			area->SetAttributeFF( FF_NAV_NEAR_LADDER );
			area->SetAttributeFF2( FF_NAV2_LADDER );
			any = true;
		}

		if ( any )
			++connected;
	}

	if ( ladders.Count() > 0 && connected < ladders.Count() )
	{
		Warning( "[CFFBotAutoTagger] %d of %d ladder(s) connect to no nav area. "
			"Bots cannot use those — regenerate with ff_nav_generate_full, or "
			"place 'ladder' markers at both ends by hand.\n",
			ladders.Count() - connected, ladders.Count() );
	}

	if ( outConnectedLadders )
		*outConnectedLadders = connected;

	return tagged;
}


//-----------------------------------------------------------------------------
// Team doors and respawn gates.
//
// FF_NAV_DOORWAY already said "an openable brush overlaps this area", which is
// enough to stop the path cost treating a shut door as an impassable wall. It
// is not enough for the two doors that behave differently from every other:
//
//   * A respawn gate opens for one team, from one side. Costing it as merely
//     expensive sends bots to stand in a doorway that will never open for
//     them, forever, because the path exists and the path is wrong.
//
//   * A team-restricted door in the middle of a map is the same problem
//     without the spawn room.
//
// Neither can be read off the entity directly: FF drives door permissions from
// Lua, and the brush is a plain func_door. What CAN be read is the shape of
// the situation - a door whose areas straddle exactly one team's spawn room
// and the world outside it is a respawn gate, whatever the script calls it.
// Naming is used as a secondary signal only, because it is a convention rather
// than data.
//-----------------------------------------------------------------------------
#define FFBOT_AUTOTAG_DOOR_MARGIN	32.0f

static int TagTeamDoors( int *outGates )
{
	int tagged = 0;
	int gates = 0;

	if ( outGates )
		*outGates = 0;

	if ( !TheNavMesh || !TheNavMesh->IsLoaded() )
		return 0;

	static const char * const kDoorClasses[] = {
		"func_door", "func_door_rotating", "prop_door_rotating",
		"func_movelinear", "func_wall_toggle",
	};

	for ( int c = 0; c < (int)ARRAYSIZE( kDoorClasses ); ++c )
	{
		CBaseEntity *door = NULL;
		while ( ( door = gEntList.FindEntityByClassname( door, kDoorClasses[ c ] ) ) != NULL )
		{
			// A vertically-travelling door is a lift wearing a door's
			// classname, and TagLifts has already claimed it. Tagging it as a
			// gate as well would make bots refuse to ride other teams' lifts.
			if ( FFBotLift::IsLiftEntity( door ) )
				continue;

			Vector mins, maxs;
			door->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			mins -= Vector( FFBOT_AUTOTAG_DOOR_MARGIN, FFBOT_AUTOTAG_DOOR_MARGIN,
			                FFBOT_AUTOTAG_DOOR_MARGIN );
			maxs += Vector( FFBOT_AUTOTAG_DOOR_MARGIN, FFBOT_AUTOTAG_DOOR_MARGIN,
			                FFBOT_AUTOTAG_DOOR_MARGIN );

			CUtlVector< CFFNavArea * > touching;
			int spawnTeamsSeen = 0;
			int spawnTeam = TEAM_UNASSIGNED;
			bool sawNonSpawn = false;

			for ( int i = 0; i < TheNavAreas.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
				if ( !area )
					continue;

				const Vector p = area->GetCenter();
				if ( p.x < mins.x || p.x > maxs.x ) continue;
				if ( p.y < mins.y || p.y > maxs.y ) continue;
				if ( p.z < mins.z || p.z > maxs.z ) continue;

				touching.AddToTail( area );

				bool inSomeSpawn = false;
				for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
				{
					if ( !area->HasAttributeFF( CFFNavArea::SpawnRoomAttributeForTeam( t ) ) )
						continue;

					inSomeSpawn = true;
					if ( spawnTeam != t )
					{
						if ( spawnTeam == TEAM_UNASSIGNED )
							spawnTeam = t;
						++spawnTeamsSeen;
					}
				}

				if ( !inSomeSpawn )
					sawNonSpawn = true;
			}

			if ( touching.Count() == 0 )
				continue;

			// A door between exactly one team's spawn room and the rest of the
			// map is that team's respawn gate. Two teams' spawn rooms on
			// either side of one door is not a gate, it is a shared corridor
			// that happens to be tagged twice, and guessing an owner there
			// would be worse than declining to.
			// Only a respawn gate is auto-detected, and only from geometry.
			//
			// The targetname was a fallback here at first and has been removed.
			// Q_stristr on arbitrary door names matches far too much - "red"
			// is a substring of a great many things that are not red team's
			// door - and the consequence of a false positive is not a cosmetic
			// mistake, it is CFFBotPathCost refusing that area to three of the
			// four teams. A mid-map team door is rare, and the doorteam marker
			// exists to say so explicitly.
			if ( spawnTeamsSeen != 1 || !sawNonSpawn )
				continue;

			const int team = spawnTeam;
			const int doorBit = CFFNavArea::DoorAttributeForTeam( team );
			const int spawnBit = CFFNavArea::SpawnRoomAttributeForTeam( team );

			for ( int i = 0; i < touching.Count(); ++i )
			{
				CFFNavArea *area = touching[ i ];

				// The doorway tag goes on both sides, as before: it is what
				// stops a shut door deleting the route, and that has to apply
				// to whoever is walking up to it.
				area->SetAttributeFF( FF_NAV_DOORWAY );

				// The TEAM bits go on the spawn side only.
				//
				// This is the whole correction. Tagging every area the door
				// touches marks the corridor OUTSIDE the gate as belonging to
				// that team, and the path cost then refuses it to everyone
				// else - which on a map like 2fort severs the main route to
				// the flag, because the ground outside blue's respawn gate is
				// exactly the ground red has to cross. The areas inside the
				// spawn room are already refused to enemies by the spawn-room
				// rule; what this adds is the threshold areas, which are the
				// ones that were falling through the gap.
				if ( !area->HasAttributeFF( spawnBit ) )
					continue;

				if ( area->HasAttributeFF2( doorBit ) )
					continue;

				area->SetAttributeFF2( doorBit | FF_NAV2_DOOR_ONEWAY );
				++tagged;
			}

			++gates;
		}
	}

	if ( outGates )
		*outGates = gates;

	return tagged;
}


//=============================================================================
// Entity-derived auto-tagging.
//
// The passes below don't infer anything from geometry — they read live world
// entities and stamp what those entities mean. They live in the auto-tagger
// rather than CFFBotTagger so that ff_nav_autotag can refresh them without a
// level reload, which matters when you're iterating on a map.
//=============================================================================

// How far from a hazard-gear pickup an area still counts as "the gear is here".
#define FFBOT_AUTOTAG_GEAR_RANGE			192.0f

// Padding around a trigger_hurt's bounds. A nav area whose centre is just
// outside the brush is still somewhere you get hurt standing, because the
// player hull is 32u wide and the area is wider than its centre point.
#define FFBOT_AUTOTAG_HAZARD_PADDING		48.0f

// Padding around a lift brush, for the same reason plus the fact that a lift's
// bounds are recorded wherever it happens to be sitting at level init.
#define FFBOT_AUTOTAG_LIFT_PADDING			32.0f


//-----------------------------------------------------------------------------
// Protective equipment — rock2's gas suit and anything like it.
//
// Source is FFBotLuaObjectives, which classifies these by model
// (models/barneyhelmet_faceplate.mdl) because Omnibot's nine goal types have
// no way to say "protective equipment" and no map script can declare it.
//
// This is the piece FoxBot never had: TFC's suit was an undifferentiated
// item_tfgoal, so its bots walked into rock2's gas and died. FF gives the suit
// its own model, so it's identifiable.
//-----------------------------------------------------------------------------
static int TagHazardGear( void )
{
	CUtlVector< CBaseEntity * > gear;
	FFBotLuaObjectives::CollectOfClass( FFGOALCLASS_HAZARD_GEAR, -1, &gear );

	int tagged = 0;
	for ( int g = 0; g < gear.Count(); ++g )
	{
		CBaseEntity *ent = gear[ g ];
		if ( !ent )
			continue;

		const Vector pos = ent->GetAbsOrigin();
		const float rangeSq = FFBOT_AUTOTAG_GEAR_RANGE * FFBOT_AUTOTAG_GEAR_RANGE;

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area )
				continue;
			if ( ( area->GetCenter() - pos ).LengthSqr() > rangeSq )
				continue;
			area->SetAttributeFF2( FF_NAV2_HAZARD_GEAR );
			++tagged;
		}
	}
	return tagged;
}


//-----------------------------------------------------------------------------
// Hazard volumes — every trigger_hurt in the map.
//
// This is the generic answer to rock2's gas, and to lava pits, crushers,
// electrified floors and every other environmental killer. The damage may be
// switched on and off by Lua on a schedule we can't see, but the VOLUME is a
// real entity present from level load, so where the danger is is knowable even
// when when it's dangerous isn't.
//
// Note what this deliberately does NOT do: it doesn't make the area
// impassable, and it doesn't even make it expensive on its own. Plenty of maps
// have a trigger_hurt covering a bottomless pit that the nav mesh never
// touches, and plenty more have one that's inert for the whole round. Marking
// the geometry is cheap and always correct; deciding what to do about it is a
// separate question with a separate answer.
//-----------------------------------------------------------------------------
static int TagHazardZones( void )
{
	int tagged = 0;

	CBaseEntity *ent = NULL;
	while ( ( ent = gEntList.FindEntityByClassname( ent, "trigger_hurt" ) ) != NULL )
	{
		Vector mins, maxs;
		ent->CollisionProp()->WorldSpaceAABB( &mins, &maxs );

		mins -= Vector( FFBOT_AUTOTAG_HAZARD_PADDING, FFBOT_AUTOTAG_HAZARD_PADDING,
		                FFBOT_AUTOTAG_HAZARD_PADDING );
		maxs += Vector( FFBOT_AUTOTAG_HAZARD_PADDING, FFBOT_AUTOTAG_HAZARD_PADDING,
		                FFBOT_AUTOTAG_HAZARD_PADDING );

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area )
				continue;

			const Vector c = area->GetCenter();
			if ( c.x < mins.x || c.x > maxs.x ) continue;
			if ( c.y < mins.y || c.y > maxs.y ) continue;
			if ( c.z < mins.z || c.z > maxs.z ) continue;

			area->SetAttributeFF2( FF_NAV2_HAZARD_ZONE );
			++tagged;
		}
	}

	return tagged;
}


//-----------------------------------------------------------------------------
// Moving platforms.
//
// A lift breaks two assumptions the path follower makes: that an area's
// position is fixed, and that arriving somewhere is purely a matter of moving
// rather than waiting. FoxBot carried a per-waypoint W_FL_LIFT flag for
// exactly this, with a tightened arrival tolerance — its source still carries
// the comment "some lifts are small (e.g. rock2's lifts)".
//
// func_door is included only when it travels mostly vertically. A horizontal
// door is a door; a vertical one is an elevator wearing a door's classname,
// and FF maps use both spellings.
//-----------------------------------------------------------------------------
// The test itself lives in FFBotLift, next to the behaviour that acts on the
// tag. One definition: a lift the tagger recognises and the rider doesn't (or
// the reverse) is a bot that walks onto a platform it will never wait for.
static int TagLifts( void )
{
	int tagged = 0;

	static const char * const kCandidateClasses[] = {
		"func_train", "func_plat", "func_platrot", "func_tracktrain",
		"func_elevator", "func_door", "func_movelinear",
	};

	for ( int c = 0; c < (int)ARRAYSIZE( kCandidateClasses ); ++c )
	{
		CBaseEntity *ent = NULL;
		while ( ( ent = gEntList.FindEntityByClassname( ent, kCandidateClasses[ c ] ) ) != NULL )
		{
			if ( !FFBotLift::IsLiftEntity( ent ) )
				continue;

			Vector mins, maxs;
			ent->CollisionProp()->WorldSpaceAABB( &mins, &maxs );

			mins -= Vector( FFBOT_AUTOTAG_LIFT_PADDING, FFBOT_AUTOTAG_LIFT_PADDING, 0.0f );
			maxs += Vector( FFBOT_AUTOTAG_LIFT_PADDING, FFBOT_AUTOTAG_LIFT_PADDING,
			                FFBOT_AUTOTAG_LIFT_PADDING + 64.0f );

			for ( int i = 0; i < TheNavAreas.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
				if ( !area )
					continue;

				const Vector p = area->GetCenter();
				if ( p.x < mins.x || p.x > maxs.x ) continue;
				if ( p.y < mins.y || p.y > maxs.y ) continue;
				if ( p.z < mins.z || p.z > maxs.z ) continue;

				area->SetAttributeFF2( FF_NAV2_LIFT );
				++tagged;
			}
		}
	}

	return tagged;
}


//-----------------------------------------------------------------------------
void CFFBotAutoTagger::TagAllAreas( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
		return;

	// Snapshot all CFFNavArea*.
	CUtlVector< CFFNavArea * > allAreas;
	allAreas.EnsureCapacity( TheNavAreas.Count() );
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
		allAreas.AddToTail( static_cast< CFFNavArea * >( TheNavAreas[ i ] ) );

	int waterTagged = 0, underwaterTagged = 0, chokeTagged = 0,
	    highGroundTagged = 0, autoSniperTagged = 0, autoSentryTagged = 0,
	    ladderTagged = 0;

	// Pass 1 — water + choke + ladder. Independent, single-area heuristics.
	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		CFFNavArea *area = allAreas[ i ];
		const unsigned int before = area->GetAttributesFF();

		TagWater( area );
		TagChoke( area );

		const unsigned int after = area->GetAttributesFF();
		const unsigned int added = after & ~before;
		if ( added & FF_NAV_WATER )      ++waterTagged;
		if ( added & FF_NAV_UNDERWATER ) ++underwaterTagged;
		if ( added & FF_NAV_CHOKE )      ++chokeTagged;
	}
	int connectedLadders = 0;
	TagLadderAdjacent( &connectedLadders );

	// Pass 2 — high-ground (needs all areas first).
	{
		const unsigned int prevTotal = 0;	// don't count, we re-walk below
		(void)prevTotal;
	}
	TagHighGround( allAreas );
	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		if ( allAreas[ i ]->HasAttributeFF( FF_NAV_HIGH_GROUND ) )
			++highGroundTagged;
		if ( allAreas[ i ]->HasAttributeFF( FF_NAV_NEAR_LADDER ) )
			++ladderTagged;
	}

	// Pass 3 — collect ingress eye-positions used by sniper/sentry checks.
	// Ingress = "places enemies travel through". For a CTF map, that's
	// (a) every enemy spawn-room exit, (b) every flag/cap area regardless
	// of team (the flag is the most-traveled-through point on the map).
	CUtlVector< Vector > ingressEyes;
	CUtlVector< Vector > flagAreaCenters;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		const CUtlVector< CFFNavArea * > *exits = mesh->GetSpawnRoomExitAreas( t );
		if ( exits )
		{
			for ( int i = 0; i < exits->Count(); ++i )
				ingressEyes.AddToTail( ( *exits )[ i ]->GetCenter() + Vector( 0, 0, 64.0f ) );
		}
		const CUtlVector< CFFNavArea * > *flags = mesh->GetFlagAreas( t );
		if ( flags )
		{
			for ( int i = 0; i < flags->Count(); ++i )
			{
				ingressEyes.AddToTail( ( *flags )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
				flagAreaCenters.AddToTail( ( *flags )[ i ]->GetCenter() );
			}
		}
		const CUtlVector< CFFNavArea * > *caps = mesh->GetCapAreas( t );
		if ( caps )
		{
			for ( int i = 0; i < caps->Count(); ++i )
				ingressEyes.AddToTail( ( *caps )[ i ]->GetCenter() + Vector( 0, 0, 48.0f ) );
		}
	}

	// Pass 4 — auto sniper / sentry spots.
	Msg( "[CFFBotAutoTagger] Pass 4: ingressEyes=%d flagAreas=%d highGround=%d\n",
		ingressEyes.Count(), flagAreaCenters.Count(), highGroundTagged );
	autoSniperTagged = TagAutoSniperSpots( allAreas, ingressEyes, mesh );
	TagAutoSentrySpots( allAreas, flagAreaCenters, ingressEyes );
	for ( int i = 0; i < allAreas.Count(); ++i )
	{
		const unsigned int attrs = allAreas[ i ]->GetAttributesFF();
		if ( attrs & FF_NAV_AUTO_SENTRY_SPOT ) ++autoSentryTagged;
	}

	// Last-ditch fallback: if the gates rejected every high-ground area
	// (e.g., on a 1-team test map with no enemy spawn-exits computed),
	// tag every non-spawn non-water high-ground area outright. Better to
	// over-tag than to leave the sniper picker with nothing to work with.
	if ( autoSniperTagged == 0 && highGroundTagged > 0 )
	{
		Msg( "[CFFBotAutoTagger] No sniper spots passed gates — applying "
			"high-ground-only fallback (no forwardness check).\n" );
		for ( int i = 0; i < allAreas.Count(); ++i )
		{
			CFFNavArea *area = allAreas[ i ];
			const unsigned int attrs = area->GetAttributesFF();
			if ( !( attrs & FF_NAV_HIGH_GROUND ) )
				continue;
			if ( attrs & FF_NAV_SPAWN_ROOM_ANY )
				continue;
			if ( attrs & FF_NAV_UNDERWATER )
				continue;
			if ( area->GetAttributes() & ( NAV_MESH_AVOID | NAV_MESH_NAV_BLOCKER ) )
				continue;
			area->SetAttributeFF( FF_NAV_AUTO_SNIPER_SPOT );
			++autoSniperTagged;
		}
		Msg( "[CFFBotAutoTagger] Fallback tagged %d high-ground areas as AUTO_SNIPER_SPOT.\n",
			autoSniperTagged );
	}

	// Pass 5 — entity-derived tags. Independent of everything above; run last
	// so a re-tag via ff_nav_autotag refreshes them too.
	const int gearTagged   = TagHazardGear();
	const int hazardTagged = TagHazardZones();
	const int liftTagged   = TagLifts();

	// After TagLifts, which claims vertically-travelling func_doors, and after
	// the spawn-room tags exist — the gate test is "one team's spawn on one
	// side, the world on the other", and it has nothing to work with until
	// those are stamped.
	int doorGates = 0;
	const int doorTagged = TagTeamDoors( &doorGates );

	Msg( "[CFFBotAutoTagger] Entity-derived: %d area(s) at hazard gear, "
		"%d in hazard volumes, %d on lifts, %d on team doors (%d respawn gate(s)).\n",
		gearTagged, hazardTagged, liftTagged, doorTagged, doorGates );

	if ( gearTagged == 0 && FFBotLuaObjectives::CountOfClass( FFGOALCLASS_HAZARD_GEAR ) > 0 )
	{
		Warning( "[CFFBotAutoTagger] Hazard gear exists but sits on no nav area — "
			"bots cannot reach it.\n" );
	}

	Msg( "[CFFBotAutoTagger] Tagged %d water, %d underwater, %d choke, "
		"%d high-ground, %d auto-sniper-spot, %d auto-sentry-spot, "
		"%d ladder-adjacent from %d connected ladder(s) (across %d areas).\n",
		waterTagged, underwaterTagged, chokeTagged,
		highGroundTagged, autoSniperTagged, autoSentryTagged,
		ladderTagged, connectedLadders, allAreas.Count() );

	if ( TheNavMesh && TheNavMesh->GetLadders().Count() == 0 )
	{
		// Worth saying every time. Stock Source generates no ladders at all
		// outside Left 4 Dead, so a mesh built by a build without FF's ladder
		// pass has none, and there is no other symptom: bots simply never use
		// a route with a ladder in it and nothing reports why.
		Msg( "[CFFBotAutoTagger] This mesh contains NO ladders. If the map has "
			"any, regenerate with ff_nav_generate_full, or mark both ends by "
			"hand with the 'ladder' marker plus a linkfrom/linkto pair.\n" );
	}
}
