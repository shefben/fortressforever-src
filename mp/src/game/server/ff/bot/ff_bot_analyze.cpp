//========= Fortress Forever Bot =============================================//
//
// CFFBotAnalyzer — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_analyze.h"
#include "ff_bot_lua_objectives.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "nav_pathfind.h"
#include "entitylist.h"
#include "utlmap.h"
#include "engine/IEngineTrace.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_analyze( "ff_bot_analyze", "1", FCVAR_NONE,
	"Derive gameplay knowledge from the nav graph, visibility sets and world "
	"entities at map load. 0 = off (hand-authored markers only), 1 = on, "
	"2 = on and log each pass in detail." );

ConVar ff_bot_analyze_traffic_threshold( "ff_bot_analyze_traffic_threshold", "0.35", FCVAR_NONE,
	"Fraction of the busiest area's traffic an area needs before it counts as "
	"a main route. Lower tags more of the map." );

ConVar ff_bot_analyze_overlook_threshold( "ff_bot_analyze_overlook_threshold", "0.45", FCVAR_NONE,
	"Fraction of the best overlook score an area needs to count as one." );


//-----------------------------------------------------------------------------
// Tuning.
//-----------------------------------------------------------------------------

// An overlook has to see traffic from further than this to be worth anything to
// a sniper. Closer than this is just a room.
#define FFANALYZE_SNIPER_MIN_RANGE		700.0f

// ...and no further than this, or we tag skybox ledges that can technically see
// the whole map through a gap.
#define FFANALYZE_SNIPER_MAX_RANGE		4000.0f

// A derived defensive post has to be within this of what it defends.
#define FFANALYZE_DEFEND_MAX_RANGE		2000.0f

// A breakable has to shorten a route by this factor before it counts as opening
// a shortcut rather than being scenery.
#define FFANALYZE_BREACH_GAIN			1.6f

// ...and the route it opens has to be at least this long to be worth a detpack.
#define FFANALYZE_BREACH_MIN_DETOUR		1200.0f

// Padding around a trigger brush when deciding which areas it covers.
#define FFANALYZE_TRIGGER_PADDING		32.0f

// Cap on visibility work. Scoring every area against every visible area is
// O(n * visible), which on a large mesh is tens of millions of operations. We
// only care about the busy ones, so score against a sampled subset.
#define FFANALYZE_MAX_TRAFFIC_SAMPLES	256


//-----------------------------------------------------------------------------
// Last-run statistics.
//-----------------------------------------------------------------------------
static struct
{
	bool  ran;
	bool  hadVisibility;
	int   areas;
	int   cutPoints;
	int   highTraffic;
	int   overlooks;
	int   autoSnipers;
	int   autoSentries;
	int   autoDefends;
	int   autoAims;
	int   breachables;
	int   teleports;
	int   teleportLinks;
	int   pushes;
	int   pathsTraced;
	float analyzeSeconds;
} s_stats;


//=============================================================================
// Shared helpers.
//=============================================================================

//-----------------------------------------------------------------------------
// Plain-distance cost for offline analysis.
//
// Deliberately NOT CFFBotPathCost: that model is per-bot and full of transient
// terms — combat intensity, this bot's recent stuck position, its route seed,
// its class. We want the map's shape, not one bot's opinion of it on one frame.
//-----------------------------------------------------------------------------
class AnalysisCost
{
public:
	float operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder,
	                  const CFuncElevator *elevator, float length ) const
	{
		if ( fromArea == NULL )
			return 0.0f;

		CFFNavArea *ffArea = static_cast< CFFNavArea * >( area );

		// Enemy spawn rooms aren't a route for anyone. Excluding them keeps the
		// traffic map from running every path through the middle of a base.
		if ( ffArea->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) &&
		     !ffArea->HasAttributeFF( FF_NAV_SPAWN_ROOM_EXIT ) )
		{
			return -1.0f;
		}

		if ( ladder )
			return ladder->m_length;
		if ( length > 0.0f )
			return length;
		return ( area->GetCenter() - fromArea->GetCenter() ).Length();
	}
};


//-----------------------------------------------------------------------------
// Build a pointer→index map so the graph passes can use flat arrays. Nav areas
// have no spare integer field and an O(n) lookup per edge would make Tarjan
// quadratic.
//-----------------------------------------------------------------------------
static void BuildAreaIndex( CUtlMap< CNavArea *, int > &out )
{
	out.RemoveAll();
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
		out.Insert( TheNavAreas[ i ], i );
}


static int IndexOf( const CUtlMap< CNavArea *, int > &index, CNavArea *area )
{
	const int slot = index.Find( area );
	return ( slot == index.InvalidIndex() ) ? -1 : index[ slot ];
}


//-----------------------------------------------------------------------------
// Collect every area a bot might be trying to reach: flags, capture points,
// hunted escapes, and any authored objective ground.
//-----------------------------------------------------------------------------
static void CollectObjectiveAreas( CUtlVector< CFFNavArea * > &out )
{
	const unsigned int kObjectiveBits =
		FF_NAV_FLAG_ANY | FF_NAV_CAP_ANY | FF_NAV_HUNTED_ESCAPE;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( area->HasAttributeFF( kObjectiveBits ) ||
		     area->HasAttributeFF2( FF_NAV2_CAP_NEUTRAL ) )
		{
			out.AddToTail( area );
		}
	}
}


//-----------------------------------------------------------------------------
// Areas a route can start from: every team's spawn-room thresholds. Falls back
// to spawn-room interiors on a map whose exits weren't marked.
//-----------------------------------------------------------------------------
static void CollectRouteOrigins( CFFNavMesh *mesh, CUtlVector< CFFNavArea * > &out )
{
	if ( !mesh )
		return;

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		CUtlVector< CFFNavArea * > thresholds;
		mesh->CollectSpawnRoomThresholdAreas( team, &thresholds );

		if ( thresholds.Count() > 0 )
		{
			for ( int i = 0; i < thresholds.Count(); ++i )
				out.AddToTail( thresholds[ i ] );
			continue;
		}

		const CUtlVector< CFFNavArea * > *rooms = mesh->GetSpawnRoomAreas( team );
		if ( rooms && rooms->Count() > 0 )
			out.AddToTail( ( *rooms )[ 0 ] );
	}
}


//=============================================================================
// PASS 1 — Topology. Articulation points.
//
// An articulation point (cut vertex) is a node whose removal increases the
// number of connected components. On a nav mesh that is exactly a chokepoint:
// somewhere the map funnels through, with no way around.
//
// That is a far stronger statement than the width heuristic it supplements.
// FF_NAV_CHOKE means "between 32 and 96 units across", which is true of every
// doorway, stairwell and corridor in the map — hundreds of areas, most of them
// irrelevant. A cut point is structural.
//
// Tarjan's algorithm, iterative rather than recursive: a large FF map has
// several thousand areas and a deep DFS on a corridor-shaped graph will blow
// the stack. O(V+E) either way.
//=============================================================================
static int AnalyzeTopology( void )
{
	const int count = TheNavAreas.Count();
	if ( count == 0 )
		return 0;

	CUtlMap< CNavArea *, int > index( 0, 0, DefLessFunc( CNavArea * ) );
	BuildAreaIndex( index );

	CUtlVector< int > discovery;	// DFS discovery time, -1 = unvisited
	CUtlVector< int > low;			// lowest discovery time reachable
	CUtlVector< int > parent;
	CUtlVector< bool > isCut;

	discovery.SetSize( count );
	low.SetSize( count );
	parent.SetSize( count );
	isCut.SetSize( count );

	for ( int i = 0; i < count; ++i )
	{
		discovery[ i ] = -1;
		low[ i ] = 0;
		parent[ i ] = -1;
		isCut[ i ] = false;
	}

	// Flattened adjacency, both directions. Cut vertices are an undirected
	// concept, and a one-way nav connection still means the two areas are part
	// of the same physical space.
	CUtlVector< CUtlVector< int > > adjacency;
	adjacency.SetSize( count );

	for ( int i = 0; i < count; ++i )
	{
		CNavArea *area = TheNavAreas[ i ];
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const int adjCount = area->GetAdjacentCount( (NavDirType)d );
			for ( int a = 0; a < adjCount; ++a )
			{
				CNavArea *neighbour = area->GetAdjacentArea( (NavDirType)d, a );
				const int n = IndexOf( index, neighbour );
				if ( n < 0 || n == i )
					continue;

				if ( adjacency[ i ].Find( n ) == adjacency[ i ].InvalidIndex() )
					adjacency[ i ].AddToTail( n );
				if ( adjacency[ n ].Find( i ) == adjacency[ n ].InvalidIndex() )
					adjacency[ n ].AddToTail( i );
			}
		}
	}

	int timer = 0;

	// Iterative DFS. The stack holds (node, index of next child to visit).
	struct Frame
	{
		int node;
		int childCursor;
	};
	CUtlVector< Frame > stack;

	for ( int root = 0; root < count; ++root )
	{
		if ( discovery[ root ] >= 0 )
			continue;

		int rootChildren = 0;

		Frame first;
		first.node = root;
		first.childCursor = 0;
		stack.AddToTail( first );

		discovery[ root ] = low[ root ] = timer++;

		while ( stack.Count() > 0 )
		{
			Frame &frame = stack[ stack.Count() - 1 ];
			const int node = frame.node;

			if ( frame.childCursor < adjacency[ node ].Count() )
			{
				const int child = adjacency[ node ][ frame.childCursor ];
				++frame.childCursor;

				if ( discovery[ child ] < 0 )
				{
					parent[ child ] = node;
					if ( node == root )
						++rootChildren;

					discovery[ child ] = low[ child ] = timer++;

					Frame next;
					next.node = child;
					next.childCursor = 0;
					stack.AddToTail( next );
				}
				else if ( child != parent[ node ] )
				{
					// Back edge: this subtree can reach an ancestor without
					// going through `node`, so `node` isn't cutting it off.
					if ( discovery[ child ] < low[ node ] )
						low[ node ] = discovery[ child ];
				}
			}
			else
			{
				// Finished with this node — fold its low value into its parent.
				stack.Remove( stack.Count() - 1 );

				const int p = parent[ node ];
				if ( p >= 0 )
				{
					if ( low[ node ] < low[ p ] )
						low[ p ] = low[ node ];

					// A non-root is a cut vertex when some child's subtree
					// cannot reach above it.
					if ( p != root && low[ node ] >= discovery[ p ] )
						isCut[ p ] = true;
				}
			}
		}

		// The root is a cut vertex only if it has more than one DFS child.
		if ( rootChildren > 1 )
			isCut[ root ] = true;
	}

	int tagged = 0;
	for ( int i = 0; i < count; ++i )
	{
		if ( !isCut[ i ] )
			continue;

		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );

		// A spawn-room interior is trivially a cut point (the room hangs off
		// the map by its door) and saying so is useless.
		if ( area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) &&
		     !area->HasAttributeFF( FF_NAV_SPAWN_ROOM_EXIT ) )
		{
			continue;
		}

		area->SetAttributeFF2( FF_NAV2_CUTPOINT );

		// A structural chokepoint is a chokepoint. Feed it into the existing
		// FF_NAV_CHOKE consumers — HWGuy hold positions, sticky traps, path
		// cost — which previously only ever saw the width heuristic's output.
		area->SetAttributeFF( FF_NAV_CHOKE );
		++tagged;
	}

	return tagged;
}


//=============================================================================
// PASS 2 — Traffic. Betweenness over spawn-to-objective routes.
//
// For every (spawn threshold, objective) pair, path between them and increment
// a counter on each area the route crosses. Normalise by the busiest area.
//
// The resulting number is the single most useful derived quantity after
// incursion distance, because almost every authoring decision is downstream of
// "where do people actually walk": where a sentry earns its keep, where a pipe
// carpet catches somebody, which corridor a defender should be watching, which
// of two ledges overlooks anything worth overlooking.
//=============================================================================
static int AnalyzeTraffic( CFFNavMesh *mesh )
{
	CUtlVector< CFFNavArea * > origins;
	CUtlVector< CFFNavArea * > objectives;

	CollectRouteOrigins( mesh, origins );
	CollectObjectiveAreas( objectives );

	if ( origins.Count() == 0 || objectives.Count() == 0 )
		return 0;

	CUtlMap< CNavArea *, int > index( 0, 0, DefLessFunc( CNavArea * ) );
	BuildAreaIndex( index );

	CUtlVector< int > visits;
	visits.SetSize( TheNavAreas.Count() );
	for ( int i = 0; i < visits.Count(); ++i )
		visits[ i ] = 0;

	AnalysisCost cost;
	int paths = 0;

	for ( int o = 0; o < origins.Count(); ++o )
	{
		for ( int g = 0; g < objectives.Count(); ++g )
		{
			CFFNavArea *start = origins[ o ];
			CFFNavArea *goal  = objectives[ g ];
			if ( start == goal )
				continue;

			if ( !NavAreaBuildPath( start, goal, NULL, cost, NULL, 0.0f, TEAM_ANY, true ) )
				continue;

			++paths;

			// Walk the parent chain back from the goal. Bounded because a
			// malformed graph could in principle produce a cycle here, and an
			// infinite loop at map load is not a debuggable failure.
			int guard = TheNavAreas.Count() + 1;
			for ( CNavArea *step = goal; step != NULL && guard-- > 0; step = step->GetParent() )
			{
				const int idx = IndexOf( index, step );
				if ( idx >= 0 )
					++visits[ idx ];
			}
		}
	}

	s_stats.pathsTraced = paths;

	int busiest = 0;
	for ( int i = 0; i < visits.Count(); ++i )
		busiest = MAX( busiest, visits[ i ] );

	if ( busiest == 0 )
		return 0;

	const float threshold = ff_bot_analyze_traffic_threshold.GetFloat();
	int tagged = 0;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		const float score = (float)visits[ i ] / (float)busiest;
		area->SetTrafficScore( score );

		if ( score >= threshold )
		{
			area->SetAttributeFF2( FF_NAV2_HIGH_TRAFFIC );
			++tagged;
		}
	}

	return tagged;
}


//=============================================================================
// PASS 3 — Visibility.
//
// nav_generate's analyze phase (nav_generate.cpp: BeginVisibilityComputations,
// ComputeVisibilityToMesh) already computes, for every area, which other areas
// it can see, and writes the result into the .nav file. Nothing in the FF layer
// was reading it.
//
// With traffic from pass 2, "where is a good sniper perch" stops being a shape
// heuristic and becomes a measurement: which areas can see the most of the
// ground people actually walk on, from far enough away to matter, while being
// somewhere the enemy isn't already standing.
//
// The same data gives an aim hint for free. If you know which visible area
// carries the most traffic, you know which way to look.
//=============================================================================
static int AnalyzeVisibility( int *outSnipers, int *outAims )
{
	*outSnipers = 0;
	*outAims = 0;

	if ( !TheNavMesh || !TheNavMesh->IsAnalyzed() )
		return 0;

	// Sample the busiest areas rather than all of them. Visibility scoring is
	// O(areas x samples) and the tail of the traffic distribution contributes
	// nothing but time.
	CUtlVector< CFFNavArea * > busy;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( area->GetTrafficScore() > 0.15f )
			busy.AddToTail( area );
	}

	if ( busy.Count() == 0 )
		return 0;

	// Even sampling across the sorted-by-nothing list is fine: we want coverage
	// of the busy set, not the top N specifically.
	const int stride = MAX( 1, busy.Count() / FFANALYZE_MAX_TRAFFIC_SAMPLES );

	const float minRangeSq = FFANALYZE_SNIPER_MIN_RANGE * FFANALYZE_SNIPER_MIN_RANGE;
	const float maxRangeSq = FFANALYZE_SNIPER_MAX_RANGE * FFANALYZE_SNIPER_MAX_RANGE;

	CUtlVector< float > scores;
	scores.SetSize( TheNavAreas.Count() );

	float best = 0.0f;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		scores[ i ] = 0.0f;

		// Nowhere you can stand and shoot from.
		if ( area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_UNDERWATER ) )
			continue;

		const Vector from = area->GetCenter();

		float score = 0.0f;
		float bestVisibleTraffic = 0.0f;
		Vector bestVisibleDir = vec3_origin;

		for ( int b = 0; b < busy.Count(); b += stride )
		{
			CFFNavArea *target = busy[ b ];
			if ( target == area )
				continue;

			const Vector delta = target->GetCenter() - from;
			const float distSq = delta.LengthSqr();
			if ( distSq < minRangeSq || distSq > maxRangeSq )
				continue;

			if ( !area->IsPotentiallyVisible( target ) )
				continue;

			// Weight by how busy the target is and how far away it is. Distance
			// helps because a long sightline is what makes a perch a perch —
			// seeing a busy corridor from ten feet away is just being in it.
			const float traffic = target->GetTrafficScore();
			const float rangeBonus = 1.0f + ( sqrtf( distSq ) / FFANALYZE_SNIPER_MAX_RANGE );
			score += traffic * rangeBonus;

			if ( traffic > bestVisibleTraffic )
			{
				bestVisibleTraffic = traffic;
				bestVisibleDir = delta;
			}
		}

		if ( score <= 0.0f )
			continue;

		// Being high up genuinely helps, and the auto-tagger already worked out
		// which areas are.
		if ( area->HasAttributeFF( FF_NAV_HIGH_GROUND ) )
			score *= 1.25f;

		scores[ i ] = score;
		best = MAX( best, score );

		// Aim hint. Never overwrite a hand-authored one: an author who stood
		// somewhere and faced a corridor is asserting something about how the
		// map plays that a traffic count does not capture.
		if ( bestVisibleTraffic > 0.0f && !area->HasAttributeFF2( FF_NAV2_AIM_HINT ) )
		{
			Vector dir = bestVisibleDir;
			dir.z = 0.0f;
			if ( dir.NormalizeInPlace() > 0.1f )
			{
				QAngle angles;
				VectorAngles( dir, angles );
				area->SetAimYaw( AngleNormalize( angles.y ) );
				area->SetAttributeFF2( FF_NAV2_AIM_HINT );
				++( *outAims );
			}
		}
	}

	if ( best <= 0.0f )
		return 0;

	const float threshold = ff_bot_analyze_overlook_threshold.GetFloat() * best;
	int overlooks = 0;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		if ( scores[ i ] < threshold )
			continue;

		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		area->SetAttributeFF2( FF_NAV2_OVERLOOK );
		++overlooks;

		// An overlook that is also away from where the enemy comes from is a
		// sniper perch. IsAwayFromInvasionAreas is the same test the sniper's
		// own scoring uses, so this feeds candidates it already understands.
		for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
		{
			if ( !area->IsReachableByTeam( team ) )
				continue;
			if ( !area->IsAwayFromInvasionAreas( team, 750.0f ) )
				continue;

			area->SetAttributeFF( FF_NAV_AUTO_SNIPER_SPOT );
			++( *outSnipers );
			break;
		}
	}

	return overlooks;
}


//=============================================================================
// PASS 4 — Defensive posts and sentry ground.
//
// A defender wants to be somewhere the attackers must pass, with sight of it,
// close enough to the thing being defended to matter, and on our side of it.
// Every one of those is now measurable:
//
//   must pass   -> FF_NAV2_CUTPOINT or high traffic  (passes 1 and 2)
//   sight of it -> visibility sets                   (pass 3)
//   close to    -> distance to the objective area
//   our side    -> incursion distance
//
// Results go into the same FF_NAV2_DEFEND_<team> bits the hand-authored posts
// use, so FFBotGameMode::ResolveDefendPosition picks them up with no change,
// and an authored post still wins because it is checked first and scored by
// proximity.
//=============================================================================
static int AnalyzeDefense( int *outSentries )
{
	*outSentries = 0;

	CUtlVector< CFFNavArea * > objectives;
	CollectObjectiveAreas( objectives );
	if ( objectives.Count() == 0 )
		return 0;

	int tagged = 0;

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		const int defendBit = CFFNavArea::DefendAttributeForTeam( team );
		if ( defendBit == 0 )
			continue;

		// What does this team defend? Its own flag and its own capture points.
		const unsigned int ownFlag = CFFNavArea::FlagAttributeForTeam( team );
		const unsigned int ownCap  = CFFNavArea::CapAttributeForTeam( team );

		CUtlVector< CFFNavArea * > mine;
		for ( int i = 0; i < objectives.Count(); ++i )
		{
			if ( objectives[ i ]->HasAttributeFF( ownFlag | ownCap ) )
				mine.AddToTail( objectives[ i ] );
		}

		if ( mine.Count() == 0 )
			continue;

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );

			// Has to be somewhere attackers must come through.
			if ( !area->HasAttributeFF2( FF_NAV2_CUTPOINT | FF_NAV2_HIGH_TRAFFIC ) )
				continue;

			// Not inside anyone's spawn, not underwater.
			if ( area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_UNDERWATER ) )
				continue;

			// Reachable by us, or we can't get there to defend it.
			if ( !area->IsReachableByTeam( team ) )
				continue;

			// Near enough to something we own to be defending it.
			CFFNavArea *nearest = NULL;
			float nearestDistSq = FLT_MAX;
			for ( int m = 0; m < mine.Count(); ++m )
			{
				const float dSq = ( mine[ m ]->GetCenter() - area->GetCenter() ).LengthSqr();
				if ( dSq < nearestDistSq )
				{
					nearestDistSq = dSq;
					nearest = mine[ m ];
				}
			}

			if ( !nearest || nearestDistSq > ( FFANALYZE_DEFEND_MAX_RANGE * FFANALYZE_DEFEND_MAX_RANGE ) )
				continue;

			// On our side of the objective. Incursion distance from our own
			// spawn is lower for us than for the attackers, and a post deeper
			// into enemy ground than the thing it protects is not a post, it's
			// a push.
			const float myIncursion = area->GetIncursionDistance( team );
			const float objIncursion = nearest->GetIncursionDistance( team );
			if ( myIncursion >= 0.0f && objIncursion >= 0.0f && myIncursion > objIncursion * 1.5f )
				continue;

			area->SetAttributeFF2( defendBit );
			++tagged;

			// A cut point in our own half, with a sightline, is where a sentry
			// gun does its work. Feed the existing engineer consumer.
			if ( area->HasAttributeFF2( FF_NAV2_CUTPOINT ) &&
			     !area->HasAttributeFF( FF_NAV_SENTRY_SPOT ) )
			{
				area->SetAttributeFF( FF_NAV_AUTO_SENTRY_SPOT );
				++( *outSentries );
			}
		}
	}

	return tagged;
}


//=============================================================================
// PASS 5 — World entities nobody was reading.
//
// Before this the bot layer looked at six world classnames. The BSP contains a
// great deal more that maps directly onto gameplay knowledge.
//=============================================================================

//-----------------------------------------------------------------------------
static void CollectAreasInBounds( const Vector &mins, const Vector &maxs,
                                  CUtlVector< CFFNavArea * > &out )
{
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		const Vector c = area->GetCenter();
		if ( c.x < mins.x || c.x > maxs.x ) continue;
		if ( c.y < mins.y || c.y > maxs.y ) continue;
		if ( c.z < mins.z || c.z > maxs.z ) continue;
		out.AddToTail( area );
	}
}


static void EntityBoundsPadded( CBaseEntity *ent, float padding, Vector *mins, Vector *maxs )
{
	ent->CollisionProp()->WorldSpaceAABB( mins, maxs );
	*mins -= Vector( padding, padding, padding );
	*maxs += Vector( padding, padding, padding );
}


//-----------------------------------------------------------------------------
// Breakable walls.
//
// "Which wall does a demoman blow open to make a route" was the flagship
// example of knowledge only a human had. It isn't: a breakable is a shortcut
// exactly when destroying it shortens a path, and that is a graph query.
//
// The test: take the nav areas on either side of the brush. If they are already
// well connected, the breakable is scenery or a window. If getting from one to
// the other currently means a long detour, breaking it opens a route.
//-----------------------------------------------------------------------------
static int AnalyzeBreakables( void )
{
	static const char * const kBreakableClasses[] = {
		"func_breakable",
		"func_breakable_surf",
		"func_wall_toggle",
	};

	AnalysisCost cost;
	int tagged = 0;

	for ( int c = 0; c < (int)ARRAYSIZE( kBreakableClasses ); ++c )
	{
		CBaseEntity *ent = NULL;
		while ( ( ent = gEntList.FindEntityByClassname( ent, kBreakableClasses[ c ] ) ) != NULL )
		{
			Vector mins, maxs;
			EntityBoundsPadded( ent, 96.0f, &mins, &maxs );

			CUtlVector< CFFNavArea * > touching;
			CollectAreasInBounds( mins, maxs, touching );

			// Needs areas on both sides to be a route at all.
			if ( touching.Count() < 2 )
				continue;

			// Take the two furthest-apart touching areas as the two "sides".
			CFFNavArea *sideA = NULL;
			CFFNavArea *sideB = NULL;
			float furthest = 0.0f;
			for ( int i = 0; i < touching.Count(); ++i )
			{
				for ( int j = i + 1; j < touching.Count(); ++j )
				{
					const float dSq = ( touching[ i ]->GetCenter() - touching[ j ]->GetCenter() ).LengthSqr();
					if ( dSq > furthest )
					{
						furthest = dSq;
						sideA = touching[ i ];
						sideB = touching[ j ];
					}
				}
			}

			if ( !sideA || !sideB )
				continue;

			const float straightLine = sqrtf( furthest );

			// How far apart are they by the routes that exist now?
			if ( !NavAreaBuildPath( sideA, sideB, NULL, cost, NULL, 0.0f, TEAM_ANY, true ) )
			{
				// No route at all — breaking this connects two parts of the map
				// that currently aren't. That is unambiguously a breach point.
				for ( int i = 0; i < touching.Count(); ++i )
				{
					touching[ i ]->SetAttributeFF2( FF_NAV2_BREACHABLE );
					if ( !touching[ i ]->HasAttributeFF2( FF_NAV2_DETPACK_SEAL ) )
						touching[ i ]->SetAttributeFF2( FF_NAV2_DETPACK_SPOT );
				}
				++tagged;
				continue;
			}

			// Walk the path we found and measure it.
			float travel = 0.0f;
			int guard = TheNavAreas.Count() + 1;
			for ( CNavArea *step = sideB; step != NULL && guard-- > 0; step = step->GetParent() )
			{
				CNavArea *prev = step->GetParent();
				if ( prev )
					travel += ( step->GetCenter() - prev->GetCenter() ).Length();
			}

			// A worthwhile breach: the detour is long in absolute terms AND
			// much longer than going straight through.
			if ( travel < FFANALYZE_BREACH_MIN_DETOUR )
				continue;
			if ( straightLine <= 1.0f || ( travel / straightLine ) < FFANALYZE_BREACH_GAIN )
				continue;

			for ( int i = 0; i < touching.Count(); ++i )
			{
				touching[ i ]->SetAttributeFF2( FF_NAV2_BREACHABLE );
				if ( !touching[ i ]->HasAttributeFF2( FF_NAV2_DETPACK_SEAL ) )
					touching[ i ]->SetAttributeFF2( FF_NAV2_DETPACK_SPOT );
			}
			++tagged;
		}
	}

	return tagged;
}


//-----------------------------------------------------------------------------
// Teleports.
//
// A trigger_teleport moves a player somewhere the nav mesh has no edge for, and
// no amount of walkable-space sampling will ever find it — the two ends may be
// on opposite sides of the map. The destination is a named entity we can look
// up, so this is a free, exact nav connection.
//-----------------------------------------------------------------------------
static int AnalyzeTeleports( int *outLinks )
{
	*outLinks = 0;
	int tagged = 0;

	CBaseEntity *ent = NULL;
	while ( ( ent = gEntList.FindEntityByClassname( ent, "trigger_teleport" ) ) != NULL )
	{
		Vector mins, maxs;
		EntityBoundsPadded( ent, FFANALYZE_TRIGGER_PADDING, &mins, &maxs );

		CUtlVector< CFFNavArea * > entrances;
		CollectAreasInBounds( mins, maxs, entrances );
		if ( entrances.Count() == 0 )
			continue;

		for ( int i = 0; i < entrances.Count(); ++i )
			entrances[ i ]->SetAttributeFF2( FF_NAV2_TELEPORT );
		++tagged;

		// Where does it go? CBaseEntity's target is the destination entity's
		// name for a teleport, which is the whole point of the entity.
		const string_t targetName = ent->m_target;
		if ( targetName == NULL_STRING )
			continue;

		CBaseEntity *destination = gEntList.FindEntityByName( NULL, targetName );
		if ( !destination || !TheNavMesh )
			continue;

		CNavArea *exitArea = TheNavMesh->GetNearestNavArea( destination->GetAbsOrigin(),
			false, 256.0f, false, true, TEAM_ANY );
		if ( !exitArea )
			continue;

		static_cast< CFFNavArea * >( exitArea )->SetAttributeFF2( FF_NAV2_TELEPORT );

		// One-way, because a teleport is. Connecting the return trip would send
		// bots walking into the exit expecting to come out at the entrance.
		for ( int i = 0; i < entrances.Count(); ++i )
		{
			CFFNavArea *from = entrances[ i ];
			if ( from == exitArea )
				continue;

			bool already = false;
			for ( int d = 0; d < NUM_DIRECTIONS; ++d )
			{
				if ( from->IsConnected( exitArea, (NavDirType)d ) )
				{
					already = true;
					break;
				}
			}
			if ( already )
				continue;

			const Vector delta = exitArea->GetCenter() - from->GetCenter();
			NavDirType dir;
			if ( fabsf( delta.x ) > fabsf( delta.y ) )
				dir = ( delta.x > 0.0f ) ? EAST : WEST;
			else
				dir = ( delta.y > 0.0f ) ? SOUTH : NORTH;

			from->ConnectTo( exitArea, dir );
			++( *outLinks );
		}
	}

	return tagged;
}


//-----------------------------------------------------------------------------
// Push volumes. Movement the mesh cannot see, and frequently the intended way
// across a gap.
//-----------------------------------------------------------------------------
static int AnalyzePushes( void )
{
	int tagged = 0;

	CBaseEntity *ent = NULL;
	while ( ( ent = gEntList.FindEntityByClassname( ent, "trigger_push" ) ) != NULL )
	{
		Vector mins, maxs;
		EntityBoundsPadded( ent, FFANALYZE_TRIGGER_PADDING, &mins, &maxs );

		CUtlVector< CFFNavArea * > touching;
		CollectAreasInBounds( mins, maxs, touching );

		for ( int i = 0; i < touching.Count(); ++i )
		{
			touching[ i ]->SetAttributeFF2( FF_NAV2_PUSH );
			++tagged;
		}
	}

	return tagged;
}


//=============================================================================
// Entry points.
//=============================================================================

void CFFBotAnalyzer::ClearDerived( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
		return;

	// Only bits this module owns outright. FF_NAV2_DETPACK_SPOT, the DEFEND_*
	// bits and FF_NAV2_AIM_HINT are shared with the manual builder, which we do
	// not re-run, so wiping them here would silently drop hand-authored
	// markers. They are instead written conditionally above, never overwriting
	// an existing value.
	const unsigned int kOwned2 =
		FF_NAV2_CUTPOINT | FF_NAV2_HIGH_TRAFFIC | FF_NAV2_OVERLOOK |
		FF_NAV2_BREACHABLE | FF_NAV2_TELEPORT | FF_NAV2_PUSH;

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		area->ClearAttributeFF2( kOwned2 );
		area->SetTrafficScore( 0.0f );
	}
}


void CFFBotAnalyzer::AnalyzeAll( CFFNavMesh *mesh )
{
	if ( ff_bot_analyze.GetInt() <= 0 )
		return;
	if ( !mesh || !mesh->IsLoaded() || TheNavAreas.Count() == 0 )
		return;

	const float startTime = Plat_FloatTime();

	Q_memset( &s_stats, 0, sizeof( s_stats ) );
	s_stats.ran = true;
	s_stats.areas = TheNavAreas.Count();
	s_stats.hadVisibility = ( TheNavMesh != NULL && TheNavMesh->IsAnalyzed() );

	ClearDerived( mesh );

	s_stats.cutPoints   = AnalyzeTopology();
	s_stats.highTraffic = AnalyzeTraffic( mesh );
	s_stats.overlooks   = AnalyzeVisibility( &s_stats.autoSnipers, &s_stats.autoAims );
	s_stats.autoDefends = AnalyzeDefense( &s_stats.autoSentries );
	s_stats.breachables = AnalyzeBreakables();
	s_stats.teleports   = AnalyzeTeleports( &s_stats.teleportLinks );
	s_stats.pushes      = AnalyzePushes();

	s_stats.analyzeSeconds = Plat_FloatTime() - startTime;

	Msg( "[CFFBotAnalyzer] %d areas in %.2fs: %d cut points, %d high traffic "
	     "(%d routes), %d overlooks, %d sniper, %d sentry, %d defend, %d aim, "
	     "%d breach, %d teleport (%d links), %d push.\n",
		s_stats.areas, s_stats.analyzeSeconds,
		s_stats.cutPoints, s_stats.highTraffic, s_stats.pathsTraced,
		s_stats.overlooks, s_stats.autoSnipers, s_stats.autoSentries,
		s_stats.autoDefends, s_stats.autoAims, s_stats.breachables,
		s_stats.teleports, s_stats.teleportLinks, s_stats.pushes );

	if ( !s_stats.hadVisibility )
	{
		Warning( "[CFFBotAnalyzer] This .nav has no visibility data, so no "
		         "overlooks, sniper perches or aim hints could be derived. "
		         "Re-run nav_generate - the analyze phase that builds it is "
		         "part of generation.\n" );
	}
}


void CFFBotAnalyzer::PrintReport( void )
{
	Msg( "==== FF bot map analysis ====\n" );

	if ( !s_stats.ran )
	{
		Msg( "  Has not run. ff_bot_analyze is %d; run ff_nav_analyze.\n",
			ff_bot_analyze.GetInt() );
		Msg( "=============================\n" );
		return;
	}

	Msg( "  %d nav areas analyzed in %.2fs\n", s_stats.areas, s_stats.analyzeSeconds );
	Msg( "  visibility data present: %s\n", s_stats.hadVisibility ? "yes" : "NO" );
	Msg( "\n" );
	Msg( "  pass 1  topology     %5d cut points (structural chokepoints)\n", s_stats.cutPoints );
	Msg( "  pass 2  traffic      %5d high-traffic areas, from %d traced routes\n",
		s_stats.highTraffic, s_stats.pathsTraced );
	Msg( "  pass 3  visibility   %5d overlooks -> %d sniper perches, %d aim hints\n",
		s_stats.overlooks, s_stats.autoSnipers, s_stats.autoAims );
	Msg( "  pass 4  defense      %5d defensive posts -> %d sentry spots\n",
		s_stats.autoDefends, s_stats.autoSentries );
	Msg( "  pass 5  entities     %5d breachable walls, %d teleports (%d nav links), %d push volumes\n",
		s_stats.breachables, s_stats.teleports, s_stats.teleportLinks, s_stats.pushes );

	if ( s_stats.highTraffic == 0 )
	{
		Msg( "\n  NO TRAFFIC DATA. Passes 3 and 4 depend on it and will have\n"
		     "  produced nothing. Traffic needs spawn-room thresholds AND\n"
		     "  objective areas - check ff_bot_nav_report and ff_bot_lua_report.\n" );
	}

	if ( !s_stats.hadVisibility )
	{
		Msg( "\n  NO VISIBILITY DATA in this .nav. Re-run nav_generate.\n" );
	}

	Msg( "=============================\n" );
}


//=============================================================================
// Console commands.
//=============================================================================

CON_COMMAND_F( ff_nav_analyze,
	"Re-run the automatic gameplay analysis (topology, traffic, visibility, "
	"defense, entities) on the current nav mesh.",
	FCVAR_CHEAT )
{
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
	{
		Msg( "[ff_nav_analyze] Nav mesh not loaded.\n" );
		return;
	}

	CFFBotAnalyzer::AnalyzeAll( mesh );
	CFFBotAnalyzer::PrintReport();
}


CON_COMMAND_F( ff_nav_analyze_report,
	"Show what the last automatic map analysis derived.",
	FCVAR_CHEAT )
{
	CFFBotAnalyzer::PrintReport();
}
