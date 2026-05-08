//========= Fortress Forever Bot =============================================//
//
// FFBotMapIntel — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_mapintel.h"
#include "ff_bot_helpers.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_door_link.h"
#include "ff_info_script.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "Path/NextBotPathFollow.h"
#include "engine/IEngineTrace.h"
#include "entitylist.h"
#include "shareddefs.h"
#include "ff_player.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"

#include "omnibot_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// WATER TAGGING: stamp FF_NAV_WATER on areas whose center is in water/slime.
// Used by path cost (drowning penalty for non-spy classes; bonus for spies
// who use water tunnels as backdoor routes).
//-----------------------------------------------------------------------------
static int TagWaterAreas( void )
{
	int n = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		const int contents = enginetrace->GetPointContents( area->GetCenter() );
		if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME ) )
		{
			area->AddFFTag( FF_NAV_WATER );
			++n;
		}
	}
	return n;
}


//-----------------------------------------------------------------------------
// SNIPER-HINT INFERENCE: rate every nav area for sniper-vantage quality.
// Heuristic:
//   - Elevation premium: count adjacent areas at lower altitude
//   - LOS to any enemy spawn area centroid (good firing line)
//   - Boost if area itself is in a spawn room corridor's mouth (battlements)
// Top scoring areas across the map get FF_NAV_SNIPER_HINT. We cap at ~5%
// of total to avoid spamming hints everywhere.
//-----------------------------------------------------------------------------
static int TagSniperHints( CFFNavMesh *mesh )
{
	if ( !mesh )
		return 0;

	const int total = TheNavAreas.Count();
	if ( total == 0 )
		return 0;

	// Collect spawn-room centroids per team for LOS check.
	Vector spawnCentroid[ 6 ];	// indexed by team
	bool   spawnPresent[ 6 ] = { false, false, false, false, false, false };
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		const CUtlVector< CFFNavArea * > *list = mesh->GetSpawnRoomAreas( t );
		if ( !list || list->Count() == 0 )
			continue;
		Vector c = vec3_origin;
		for ( int i = 0; i < list->Count(); ++i )
			c += ( *list )[ i ]->GetCenter();
		c *= ( 1.0f / (float)list->Count() );
		spawnCentroid[ t ] = c;
		spawnPresent[ t ] = true;
	}

	// Score each area.
	struct Scored { CFFNavArea *area; float score; };
	CUtlVector< Scored > scored;

	for ( int i = 0; i < total; ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		// Skip spawn rooms themselves — they're never good sniper spots
		// (defenders can't reach them, and our team's snipers would be
		// trapped behind doors).
		if ( area->HasFFTag( FF_NAV_SPAWN_ANY ) )
			continue;
		// Skip water areas — snipers would drown.
		if ( area->HasFFTag( FF_NAV_WATER ) )
			continue;

		float score = 0.0f;
		const Vector center = area->GetCenter();

		// Elevation: count adjacent areas whose center is below by > 32u.
		int adjBelow = 0;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)d );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CNavArea *neighbor = ( *adj )[ j ].area;
				if ( neighbor && ( center.z - neighbor->GetCenter().z ) > 32.0f )
					++adjBelow;
			}
		}
		score += (float)adjBelow * 6.0f;

		// LOS to any enemy spawn centroid (large bonus per visible spawn).
		// Use a hull trace from the area's center to the spawn centroid.
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( !spawnPresent[ t ] )
				continue;
			trace_t tr;
			UTIL_TraceLine( center + Vector( 0, 0, 32 ),
				spawnCentroid[ t ] + Vector( 0, 0, 32 ),
				MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction >= 0.95f )
				score += 25.0f;
		}

		if ( score > 0.0f )
		{
			Scored s = { area, score };
			scored.AddToTail( s );
		}
	}

	// Sort descending. Take top 5% as sniper hints (or top 10 if very few).
	for ( int i = 0; i < scored.Count(); ++i )
	{
		for ( int j = i + 1; j < scored.Count(); ++j )
		{
			if ( scored[ j ].score > scored[ i ].score )
			{
				Scored tmp = scored[ i ]; scored[ i ] = scored[ j ]; scored[ j ] = tmp;
			}
		}
	}

	const int targetCount = MAX( 4, total / 20 );	// 5% or 4, whichever larger
	const int actualCount = MIN( scored.Count(), targetCount );

	for ( int i = 0; i < actualCount; ++i )
	{
		scored[ i ].area->AddFFTag( FF_NAV_SNIPER_HINT );
	}
	return actualCount;
}


//-----------------------------------------------------------------------------
// SENTRY-HINT INFERENCE: areas in our defensive zone that look like good
// sentry placement — adjacent to a chokepoint, with LOS to flag area.
// Heuristic: areas within 600u of own flag, with at least 1 adjacent area
// outside that radius (= chokepoint side), and elevation either equal or
// slightly above adjacent.
//-----------------------------------------------------------------------------
static int TagSentryHints( CFFNavMesh *mesh )
{
	if ( !mesh )
		return 0;
	int total = 0;
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		const CUtlVector< CFFNavArea * > *flagAreas = mesh->GetFlagAreas( t );
		if ( !flagAreas || flagAreas->Count() == 0 )
			continue;

		// Anchor on the team's first flag area.
		CFFNavArea *flagArea = ( *flagAreas )[ 0 ];
		const Vector flagPos = flagArea->GetCenter();

		// Score nearby areas.
		struct Scored { CFFNavArea *area; float score; };
		CUtlVector< Scored > scored;

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !area )
				continue;
			if ( area->HasFFTag( FF_NAV_SPAWN_ANY ) )
				continue;
			if ( area->HasFFTag( FF_NAV_WATER ) )
				continue;
			if ( area->HasFFTag( FF_NAV_NO_BUILD ) )
				continue;

			const Vector center = area->GetCenter();
			const float distSq = ( center - flagPos ).LengthSqr();
			if ( distSq > ( 700.0f * 700.0f ) )
				continue;

			// Count outward-facing adjacencies (areas farther from flag than
			// us = facing the choke).
			int outward = 0;
			for ( int d = 0; d < NUM_DIRECTIONS; ++d )
			{
				const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)d );
				if ( !adj )
					continue;
				for ( int j = 0; j < adj->Count(); ++j )
				{
					CNavArea *n = ( *adj )[ j ].area;
					if ( n && ( n->GetCenter() - flagPos ).LengthSqr() > distSq + ( 32.0f * 32.0f ) )
						++outward;
				}
			}
			if ( outward == 0 )
				continue;	// no choke face, skip

			// LOS to flag (sentry should see the flag).
			trace_t tr;
			UTIL_TraceLine( center + Vector( 0, 0, 32 ),
				flagPos + Vector( 0, 0, 32 ),
				MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction < 0.9f )
				continue;

			Scored s = { area, (float)outward * 10.0f + ( 700.0f - sqrtf( distSq ) ) * 0.05f };
			scored.AddToTail( s );
		}

		// Take top 2 per team.
		for ( int i = 0; i < scored.Count(); ++i )
		{
			for ( int j = i + 1; j < scored.Count(); ++j )
			{
				if ( scored[ j ].score > scored[ i ].score )
				{
					Scored tmp = scored[ i ]; scored[ i ] = scored[ j ]; scored[ j ] = tmp;
				}
			}
		}
		const int take = MIN( scored.Count(), 2 );
		for ( int i = 0; i < take; ++i )
		{
			scored[ i ].area->AddFFTag( FF_NAV_SENTRY_HINT );
			++total;
		}
	}
	return total;
}


//-----------------------------------------------------------------------------
// BACKDOOR INFERENCE: water areas, plus areas with low connectivity (1 or 2
// adjacencies — narrow tunnels rather than open rooms). Spies prefer them.
//-----------------------------------------------------------------------------
static int TagBackdoorAreas( void )
{
	int n = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( area->HasFFTag( FF_NAV_SPAWN_ANY ) )
			continue;

		bool isBackdoor = false;
		if ( area->HasFFTag( FF_NAV_WATER ) )
			isBackdoor = true;
		else
		{
			int totalAdj = 0;
			for ( int d = 0; d < NUM_DIRECTIONS; ++d )
				totalAdj += area->GetAdjacentCount( (NavDirType)d );
			if ( totalAdj > 0 && totalAdj <= 2 )
				isBackdoor = true;
		}

		if ( isBackdoor )
		{
			area->AddFFTag( FF_NAV_BACKDOOR );
			++n;
		}
	}
	return n;
}


//-----------------------------------------------------------------------------
// CHOKE-POINT INFERENCE: betweenness-centrality on the nav graph. For each
// pair of "key" areas (spawn → enemy flag/cap), compute the shortest path
// and tally each visited area's count. Areas that appear in many shortest
// paths are chokes — defenders should pre-aim there, attackers should
// expect resistance there.
//
// We use a default cost (just distance) so the result reflects geometric
// chokes rather than current-state conditions.
//-----------------------------------------------------------------------------
struct GeometricCost : public IPathCost
{
	virtual float operator()( CNavArea *area, CNavArea *fromArea,
							 const CNavLadder *ladder, const CFuncElevator *,
							 float length ) const OVERRIDE
	{
		if ( fromArea == NULL )
			return 0.0f;
		float dist;
		if ( ladder )
			dist = ladder->m_length;
		else if ( length > 0.0f )
			dist = length;
		else
			dist = ( area->GetCenter() - fromArea->GetCenter() ).Length();
		const float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );
		if ( deltaZ >= 64.0f )	// max jump
			return -1.0f;
		if ( deltaZ < -200.0f )	// max drop
			return -1.0f;
		return dist;
	}
};


static void CollectKeyAreas( CFFNavMesh *mesh, CUtlVector< CFFNavArea * > &out )
{
	out.RemoveAll();
	for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
	{
		// One spawn area per team.
		const CUtlVector< CFFNavArea * > *spawns = mesh->GetSpawnRoomAreas( t );
		if ( spawns && spawns->Count() > 0 )
			out.AddToTail( ( *spawns )[ 0 ] );

		const CUtlVector< CFFNavArea * > *flags = mesh->GetFlagAreas( t );
		if ( flags && flags->Count() > 0 )
			out.AddToTail( ( *flags )[ 0 ] );

		const CUtlVector< CFFNavArea * > *caps = mesh->GetCapAreas( t );
		if ( caps && caps->Count() > 0 )
			out.AddToTail( ( *caps )[ 0 ] );
	}
}


static int InferChokePoints( CFFNavMesh *mesh )
{
	CUtlVector< CFFNavArea * > keyAreas;
	CollectKeyAreas( mesh, keyAreas );
	if ( keyAreas.Count() < 2 )
		return 0;

	// Per-area visit count.
	CUtlMap< unsigned int, int > visitCount( DefLessFunc( unsigned int ) );

	GeometricCost cost;

	for ( int i = 0; i < keyAreas.Count(); ++i )
	{
		for ( int j = 0; j < keyAreas.Count(); ++j )
		{
			if ( i == j )
				continue;
			CFFNavArea *src = keyAreas[ i ];
			CFFNavArea *dst = keyAreas[ j ];
			CNavArea *closest = NULL;
			if ( !NavAreaBuildPath( src, dst, NULL, cost, &closest ) )
				continue;

			// Walk back via m_parent chain on dst. Increment visit count
			// for each interior area. Skip endpoints — they're keys, not
			// chokes.
			CNavArea *step = dst;
			int hops = 0;
			while ( step && step != src && hops < 1024 )
			{
				if ( step != dst )	// skip the destination key itself
				{
					unsigned int id = step->GetID();
					int idx = visitCount.Find( id );
					if ( idx == visitCount.InvalidIndex() )
						visitCount.Insert( id, 1 );
					else
						++visitCount[ idx ];
				}
				step = step->GetParent();
				++hops;
			}
		}
	}

	// Threshold: areas with visit count >= half the max get tagged.
	int maxCount = 0;
	for ( int i = visitCount.FirstInorder(); i != visitCount.InvalidIndex(); i = visitCount.NextInorder( i ) )
	{
		if ( visitCount[ i ] > maxCount )
			maxCount = visitCount[ i ];
	}
	if ( maxCount < 2 )
		return 0;
	const int threshold = MAX( 2, maxCount / 2 );

	int chokeTagged = 0;
	for ( int i = visitCount.FirstInorder(); i != visitCount.InvalidIndex(); i = visitCount.NextInorder( i ) )
	{
		if ( visitCount[ i ] < threshold )
			continue;
		CNavArea *navArea = TheNavMesh->GetNavAreaByID( visitCount.Key( i ) );
		if ( !navArea )
			continue;
		static_cast< CFFNavArea * >( navArea )->AddFFTag( FF_NAV_CHOKE );
		++chokeTagged;
	}
	return chokeTagged;
}


//-----------------------------------------------------------------------------
// NEAR-DOOR TAGGING: for each one-way door registered in the door link
// registry, tag the nav areas adjacent to it as FF_NAV_NEAR_DOOR. Bots use
// this for: pre-aiming the doorway, applying special stuck handling
// faster, and treating the area as a low-cover entry point.
//-----------------------------------------------------------------------------
static int TagAreasNearDoors( CFFNavMesh *mesh )
{
	int tagged = 0;
	// We don't have direct access to the door registry's internal list
	// from outside, but we can re-walk doors here. The cost is small.
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.NextEnt( e ) ) != NULL )
	{
		const char *cls = e->GetClassname();
		if ( !cls )
			continue;
		if ( !FStrEq( cls, "func_door" ) &&
			 !FStrEq( cls, "func_door_rotating" ) &&
			 !FStrEq( cls, "func_movelinear" ) )
			continue;

		Vector mins = e->WorldAlignMins() + e->GetAbsOrigin();
		Vector maxs = e->WorldAlignMaxs() + e->GetAbsOrigin();
		Extent ext;
		ext.lo = mins - Vector( 96.0f, 96.0f, 32.0f );
		ext.hi = maxs + Vector( 96.0f, 96.0f, 32.0f );

		CUtlVector< CFFNavArea * > doorAreas;
		TheNavMesh->CollectAreasOverlappingExtent( ext, &doorAreas );
		for ( int i = 0; i < doorAreas.Count(); ++i )
		{
			if ( !doorAreas[ i ]->HasFFTag( FF_NAV_NEAR_DOOR ) )
			{
				doorAreas[ i ]->AddFFTag( FF_NAV_NEAR_DOOR );
				++tagged;
			}
		}
	}
	return tagged;
}


//-----------------------------------------------------------------------------
// CLASS-MASK INFERENCE (height-based): an area whose ONLY incoming
// connections require a height step greater than normal jump height is
// reachable only by self-prop classes (soldier RJ, demoman sticky-jump,
// scout jumpgun). Tag those areas with a class mask restricting traversal
// to those classes so engineers/HWGuys/civilians don't try to path there.
//
// On stock-generated nav meshes this rarely fires (Source rejects too-tall
// connections at gen-time). The framework triggers when custom meshes
// include explicit jump-link connections.
//-----------------------------------------------------------------------------
static int InferClassMasks( void )
{
	// Crouch-jump can clear ~80u; anything more requires self-prop.
	const float CROUCH_JUMP_LIMIT = 80.0f;

	const unsigned short SELF_PROP_MASK =
		( 1 << CLASS_SOLDIER ) |
		( 1 << CLASS_DEMOMAN ) |
		( 1 << CLASS_SCOUT );

	int restricted = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( area->HasFFTag( FF_NAV_SPAWN_ANY ) )
			continue;	// spawn rooms must remain class-open

		// Look at all incoming connections (use adjacency in any direction
		// — symmetric here is fine).
		bool anyWalkableEntry = false;
		bool anyEntryAtAll = false;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)d );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CNavArea *neighbor = ( *adj )[ j ].area;
				if ( !neighbor )
					continue;
				anyEntryAtAll = true;
				const float dz = neighbor->ComputeAdjacentConnectionHeightChange( area );
				// Step up from neighbor to us — if shallow, anyone walks.
				if ( dz <= CROUCH_JUMP_LIMIT )
					anyWalkableEntry = true;
			}
		}

		if ( anyEntryAtAll && !anyWalkableEntry )
		{
			// Every entry to this area requires more than a crouch-jump —
			// only self-prop classes can reasonably reach. Restrict.
			area->SetClassMask( SELF_PROP_MASK );
			++restricted;
		}
	}
	return restricted;
}


//-----------------------------------------------------------------------------
void FFBotMapIntel::RunLevelInit( CFFNavMesh *mesh )
{
	if ( !mesh || !mesh->IsLoaded() )
		return;

	const int waterTagged = TagWaterAreas();
	const int sniperTagged = TagSniperHints( mesh );
	const int sentryTagged = TagSentryHints( mesh );
	const int backdoorTagged = TagBackdoorAreas();
	const int chokeTagged = InferChokePoints( mesh );
	const int nearDoorTagged = TagAreasNearDoors( mesh );
	const int classRestricted = InferClassMasks();

	Msg( "[FFBotMapIntel] Inferred tags: %d water, %d sniper-hint, %d sentry-hint, %d backdoor, %d choke, %d near-door, %d class-restricted.\n",
		waterTagged, sniperTagged, sentryTagged, backdoorTagged, chokeTagged, nearDoorTagged, classRestricted );
}


//-----------------------------------------------------------------------------
// ENEMY SENTRY REGISTRY — refreshed each Tick. Iterates all sentry guns,
// notes their position + owning team. Path cost queries this for "would
// stepping into this nav area put me in line with an enemy SG?".
//-----------------------------------------------------------------------------
static CUtlVector< FFBotMapIntel::SentryInfo > s_enemySentries;


void FFBotMapIntel::Tick( void )
{
	s_enemySentries.RemoveAll();

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_SentryGun" ) ) != NULL )
	{
		CFFSentryGun *sg = dynamic_cast< CFFSentryGun * >( e );
		if ( !sg )
			continue;
		if ( sg->GetHealth() <= 0 )
			continue;
		SentryInfo info;
		info.pos = sg->WorldSpaceCenter();
		info.team = sg->GetTeamNumber();
		s_enemySentries.AddToTail( info );
	}

	// Tag mancannons on nav. Refreshed each tick because they expire.
	// First clear stale tags.
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( area )
			area->RemoveFFTag( FF_NAV_MANCANNON );
	}
	CBaseEntity *jp = NULL;
	while ( ( jp = gEntList.FindEntityByClassname( jp, "FF_ManCannon" ) ) != NULL )
	{
		CNavArea *area = TheNavMesh->GetNearestNavArea( jp->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY );
		if ( area )
			static_cast< CFFNavArea * >( area )->AddFFTag( FF_NAV_MANCANNON );
	}

	// Cap-state-aware mesh: if our flag is being walked out, tag the
	// intercept lane so defenders converge.
	RefreshInterceptLanes();

	// Hot-zone promotion (currently a hook; danger score is the truth).
	RefreshHotZones();
}


const CUtlVector< FFBotMapIntel::SentryInfo > &FFBotMapIntel::GetEnemySentries( int /*myTeam*/ )
{
	return s_enemySentries;
}


CFFNavArea *FFBotMapIntel::FindNearestSniperHint( const Vector &fromPos )
{
	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( !area->HasFFTag( FF_NAV_SNIPER_HINT ) )
			continue;
		const float dSq = ( area->GetCenter() - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}
	return best;
}


CFFNavArea *FFBotMapIntel::FindUnoccupiedSniperHint( int myTeam, const Vector &fromPos,
													  float exclusionRadius )
{
	// Build list of friendly sniper positions for occupancy check. Iterate
	// players this time (vs the SG-entity list FindUnoccupiedSentryHint
	// uses) — snipers don't drop a buildable to mark their post.
	CUtlVector< Vector > friendlySniperPositions;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *p = UTIL_PlayerByIndex( i );
		if ( !p || !p->IsAlive() )
			continue;
		if ( p->GetTeamNumber() != myTeam )
			continue;
		// Walk the FF cast to check class.
		CFFPlayer *ffp = ToFFPlayer( p );
		if ( !ffp || ffp->GetClassSlot() != CLASS_SNIPER )
			continue;
		friendlySniperPositions.AddToTail( ffp->GetAbsOrigin() );
	}

	const float exclSq = exclusionRadius * exclusionRadius;

	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area || !area->HasFFTag( FF_NAV_SNIPER_HINT ) )
			continue;

		const Vector center = area->GetCenter();

		bool occupied = false;
		for ( int s = 0; s < friendlySniperPositions.Count(); ++s )
		{
			if ( ( friendlySniperPositions[ s ] - center ).LengthSqr() < exclSq )
			{
				occupied = true;
				break;
			}
		}
		if ( occupied )
			continue;

		const float dSq = ( center - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}
	return best;
}


CFFNavArea *FFBotMapIntel::FindNearestSentryHint( const Vector &fromPos )
{
	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( !area->HasFFTag( FF_NAV_SENTRY_HINT ) )
			continue;
		const float dSq = ( area->GetCenter() - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}
	return best;
}


CFFNavArea *FFBotMapIntel::FindUnoccupiedSentryHint( int myTeam, const Vector &fromPos,
													 float exclusionRadius )
{
	// Build a quick list of friendly sentry positions for occupancy check.
	CUtlVector< Vector > friendlySGPositions;
	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassname( e, "FF_SentryGun" ) ) != NULL )
	{
		if ( e->GetTeamNumber() == myTeam )
			friendlySGPositions.AddToTail( e->GetAbsOrigin() );
	}

	const float exclSq = exclusionRadius * exclusionRadius;

	CFFNavArea *best = NULL;
	float bestDistSq = FLT_MAX;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( !area->HasFFTag( FF_NAV_SENTRY_HINT ) )
			continue;

		const Vector center = area->GetCenter();

		// Skip if a friendly SG is already parked within exclusion radius.
		bool occupied = false;
		for ( int s = 0; s < friendlySGPositions.Count(); ++s )
		{
			if ( ( friendlySGPositions[ s ] - center ).LengthSqr() < exclSq )
			{
				occupied = true;
				break;
			}
		}
		if ( occupied )
			continue;

		const float dSq = ( center - fromPos ).LengthSqr();
		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			best = area;
		}
	}
	return best;
}


//-----------------------------------------------------------------------------
// INTERCEPT LANE: when an enemy is carrying our flag, build a path from the
// carrier back to their spawn (their goal) and tag all areas along that
// path with FF_NAV_INTERCEPT_LANE. Defender path cost gives ×0.7 discount
// on those areas, so they sprint to intercept.
//
// Tags are cleared at the start of each refresh so stale paths don't
// linger. Throttled to ~1.5 Hz so it doesn't dominate frame time on big
// maps.
//-----------------------------------------------------------------------------
static CountdownTimer s_interceptRefreshTimer;
static CUtlVector< unsigned int > s_taggedInterceptAreas;

struct InterceptCost : public IPathCost
{
	virtual float operator()( CNavArea *area, CNavArea *fromArea,
							 const CNavLadder *ladder, const CFuncElevator *,
							 float length ) const OVERRIDE
	{
		if ( fromArea == NULL )
			return 0.0f;
		float dist;
		if ( ladder )			dist = ladder->m_length;
		else if ( length > 0 )	dist = length;
		else					dist = ( area->GetCenter() - fromArea->GetCenter() ).Length();
		const float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );
		if ( deltaZ >= 64.0f || deltaZ < -200.0f )
			return -1.0f;
		return dist;
	}
};

void FFBotMapIntel::RefreshInterceptLanes( void )
{
	if ( !s_interceptRefreshTimer.HasStarted() )
	{
		s_interceptRefreshTimer.Start( 0.7f );
		return;
	}
	if ( !s_interceptRefreshTimer.IsElapsed() )
		return;
	s_interceptRefreshTimer.Start( 0.7f );

	// Clear previously-tagged areas.
	for ( int i = 0; i < s_taggedInterceptAreas.Count(); ++i )
	{
		CNavArea *navArea = TheNavMesh->GetNavAreaByID( s_taggedInterceptAreas[ i ] );
		if ( navArea )
			static_cast< CFFNavArea * >( navArea )->RemoveFFTag( FF_NAV_INTERCEPT_LANE );
	}
	s_taggedInterceptAreas.RemoveAll();

	// Walk all kFlag entities. For any flag in carried state where the
	// carrier is on the OPPOSING team to the flag's owner, build a path
	// from the carrier to the flag's home base and tag that path.
	CFFNavMesh *mesh = TheFFNavMesh();
	if ( !mesh || !mesh->IsLoaded() )
		return;

	InterceptCost cost;

	CBaseEntity *e = NULL;
	while ( ( e = gEntList.FindEntityByClassT( e, CLASS_INFOSCRIPT ) ) != NULL )
	{
		CFFInfoScript *flag = static_cast< CFFInfoScript * >( e );
		if ( flag->GetBotGoalType() != Omnibot::kFlag )
			continue;
		if ( !flag->IsCarried() )
			continue;
		CBaseEntity *carrier = flag->GetCarrier();
		if ( !carrier )
			continue;

		// Find the team this flag belongs to (the team it can NOT be
		// touched by — its owner).
		int ownerTeam = -1;
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			if ( FFBotHelpers::IsBotsOwnFlag( t, flag ) )
			{
				ownerTeam = t;
				break;
			}
		}
		if ( ownerTeam < 0 )
			continue;
		// Carrier must be on a different team (sanity).
		if ( carrier->GetTeamNumber() == ownerTeam )
			continue;

		// Endpoint nav areas.
		CNavArea *startNav = TheNavMesh->GetNearestNavArea(
			carrier->GetAbsOrigin(), false, 256.0f, false, true, TEAM_ANY );
		if ( !startNav )
			continue;
		const CUtlVector< CFFNavArea * > *carrierTeamSpawns =
			mesh->GetSpawnRoomAreas( carrier->GetTeamNumber() );
		if ( !carrierTeamSpawns || carrierTeamSpawns->Count() == 0 )
			continue;
		CFFNavArea *endNav = ( *carrierTeamSpawns )[ 0 ];

		// Find the path.
		CNavArea *closest = NULL;
		if ( !NavAreaBuildPath( static_cast< CFFNavArea * >( startNav ),
								endNav, NULL, cost, &closest ) )
			continue;

		// Walk back via m_parent and tag.
		CNavArea *step = endNav;
		int hops = 0;
		while ( step && step != startNav && hops < 1024 )
		{
			CFFNavArea *ffa = static_cast< CFFNavArea * >( step );
			if ( !ffa->HasFFTag( FF_NAV_INTERCEPT_LANE ) )
			{
				ffa->AddFFTag( FF_NAV_INTERCEPT_LANE );
				s_taggedInterceptAreas.AddToTail( ffa->GetID() );
			}
			step = step->GetParent();
			++hops;
		}
	}
}


//-----------------------------------------------------------------------------
// HOT-ZONE PROMOTION: areas whose decayed danger score is above a threshold
// get FF_NAV_INTERCEPT_LANE-style transient tag. We reuse the existing
// danger score (already set by death events) but expose a faster boolean
// path for callers that don't want to do the lazy decay each query.
//
// (We tag with a transient bit rather than a separate dedicated bit since
// "this is currently a hot zone" overlaps semantically with "this lane is
// where action is happening" — both rate ×0.7 / ×1.0 modifiers in cost.)
//-----------------------------------------------------------------------------
static CountdownTimer s_hotZoneRefreshTimer;

void FFBotMapIntel::RefreshHotZones( void )
{
	if ( !s_hotZoneRefreshTimer.HasStarted() )
	{
		s_hotZoneRefreshTimer.Start( 2.0f );
		return;
	}
	if ( !s_hotZoneRefreshTimer.IsElapsed() )
		return;
	s_hotZoneRefreshTimer.Start( 2.0f );

	// We don't need a separate tag bit; the danger score itself is the
	// truth source. This is a hook that future "promote to choke / sniper
	// hint" logic can attach to. For now: when an area's danger score
	// climbs over 6.0 and it's tagged FF_NAV_CHOKE, the choke is
	// effectively "live" — no extra action needed since path cost already
	// applies danger * 8 flat penalty.
	//
	// Left in place as a forward hook; current implementation is a no-op
	// beyond what GetDangerScore() naturally drives.
}


bool FFBotMapIntel::IsExposedToEnemySentry( int myTeam, const Vector &areaCenter, float maxRange )
{
	const float maxRangeSq = maxRange * maxRange;
	for ( int i = 0; i < s_enemySentries.Count(); ++i )
	{
		const SentryInfo &s = s_enemySentries[ i ];
		if ( s.team == myTeam )
			continue;
		const float dSq = ( s.pos - areaCenter ).LengthSqr();
		if ( dSq > maxRangeSq )
			continue;
		// LOS check: does the sentry have a clean view of the area's center?
		trace_t tr;
		UTIL_TraceLine( s.pos, areaCenter + Vector( 0, 0, 32 ),
			MASK_OPAQUE, NULL, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.95f )
			return true;
	}
	return false;
}
