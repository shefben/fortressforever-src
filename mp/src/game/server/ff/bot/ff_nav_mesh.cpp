//========= Fortress Forever Bot =============================================//
//
// CFFNavMesh — see header. TF-style nav mesh for FF.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_bot_tagger.h"
#include "ff_bot_learned_links.h"
#include "ff_bot_gamemode.h"
#include "ff_nav_builder.h"
#include "ff_bot_lua_objectives.h"
#include "shareddefs.h"
#include "entitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// FF player jump height — non-crouch-jump. Used in incursion-distance
// flood-fill to skip ledges too high to jump.
#define FF_PLAYER_JUMP_HEIGHT	45.0f


//-----------------------------------------------------------------------------
CFFNavMesh::CFFNavMesh( void )
{
}

//-----------------------------------------------------------------------------
CNavArea *CFFNavMesh::CreateArea( void ) const
{
	return new CFFNavArea;
}

//-----------------------------------------------------------------------------
// Helper: TEAM_BLUE..TEAM_GREEN -> 0..3, else -1.
static int FFNavTeamIndex( int team )
{
	if ( team >= TEAM_BLUE && team <= TEAM_GREEN )
		return team - TEAM_BLUE;
	return -1;
}

//-----------------------------------------------------------------------------
const CUtlVector< CFFNavArea * > *CFFNavMesh::GetSpawnRoomAreas( int team ) const
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_spawnRoomAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetSpawnRoomExitAreas( int team ) const
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_spawnRoomExitAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetFlagAreas( int team ) const
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_flagAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetCapAreas( int team ) const
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_capAreas[ idx ];
}

//-----------------------------------------------------------------------------
void CFFNavMesh::AddSpawnRoomArea( int team, CFFNavArea *area )
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_spawnRoomAreas[ idx ].Find( area ) == m_spawnRoomAreas[ idx ].InvalidIndex() )
		m_spawnRoomAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddSpawnRoomExitArea( int team, CFFNavArea *area )
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_spawnRoomExitAreas[ idx ].Find( area ) == m_spawnRoomExitAreas[ idx ].InvalidIndex() )
		m_spawnRoomExitAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddFlagArea( int team, CFFNavArea *area )
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_flagAreas[ idx ].Find( area ) == m_flagAreas[ idx ].InvalidIndex() )
		m_flagAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddCapArea( int team, CFFNavArea *area )
{
	const int idx = FFNavTeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_capAreas[ idx ].Find( area ) == m_capAreas[ idx ].InvalidIndex() )
		m_capAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddResupplyArea( CFFNavArea *area )
{
	if ( !area ) return;
	if ( m_resupplyAreas.Find( area ) == m_resupplyAreas.InvalidIndex() )
		m_resupplyAreas.AddToTail( area );
}

//-----------------------------------------------------------------------------
void CFFNavMesh::ClearTaggedAreaCaches( void )
{
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
	{
		m_spawnRoomAreas[ i ].RemoveAll();
		m_spawnRoomExitAreas[ i ].RemoveAll();
		m_flagAreas[ i ].RemoveAll();
		m_capAreas[ i ].RemoveAll();
	}
	m_resupplyAreas.RemoveAll();
}

//-----------------------------------------------------------------------------
void CFFNavMesh::RemoveAreaFromTaggedCaches( CFFNavArea *area )
{
	if ( !area )
		return;

	// Objective and pickup lists only.
	//
	// Spawn rooms and spawn exits are deliberately left alone. An erasure does
	// not clear the spawn-room bits — those are facts about the map that the
	// incursion-distance flood fill, the invasion vectors and the enemy-spawn
	// path refusal all depend on — so removing the area from these lists would
	// leave the list and the bit disagreeing, which is worse than either.
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
	{
		m_flagAreas[ i ].FindAndRemove( area );
		m_capAreas[ i ].FindAndRemove( area );
	}
	m_resupplyAreas.FindAndRemove( area );
}


//-----------------------------------------------------------------------------
// Everything the previous function skips, plus everything it does.
//-----------------------------------------------------------------------------
void CFFNavMesh::RemoveAreaFromAllCaches( CFFNavArea *area )
{
	if ( !area )
		return;

	RemoveAreaFromTaggedCaches( area );

	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
	{
		m_spawnRoomAreas[ i ].FindAndRemove( area );
		m_spawnRoomExitAreas[ i ].FindAndRemove( area );
	}
}

//-----------------------------------------------------------------------------
// Single-team incursion-distance flood-fill. Mirrors CTFNavMesh's same-name
// overload — Dijkstra-style BFS through the nav graph from this team's
// spawn-room areas. Each visited area gets distance = travel distance from
// the nearest spawn area.
//
// Differences vs TF:
//   - We don't use CTFGameRules MvM/Raid mode escape clauses.
//   - We use the OPEN list machinery from CNavArea (PopOpenList /
//     AddToOpenListTail) — same as TF.
//   - Skip ledges higher than FF_PLAYER_JUMP_HEIGHT to avoid pathing
//     through impossible verticals.
//-----------------------------------------------------------------------------
void CFFNavMesh::ComputeIncursionDistancesFromTeamSpawn( int team )
{
	const CUtlVector< CFFNavArea * > *spawnAreas = GetSpawnRoomAreas( team );
	if ( !spawnAreas || spawnAreas->Count() == 0 )
		return;

	CNavArea::ClearSearchLists();

	// Seed all spawn-room areas with distance 0 — bot is "at home" in any
	// of them.
	for ( int i = 0; i < spawnAreas->Count(); ++i )
	{
		CFFNavArea *area = ( *spawnAreas )[ i ];
		area->SetIncursionDistance( team, 0.0f );
		area->AddToOpenList();
		area->Mark();
		area->SetParent( NULL );
	}

	while ( !CNavArea::IsOpenListEmpty() )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( CNavArea::PopOpenList() );

		// Skip blocked areas (but allow spawn-room exits — they may be
		// blocked by setup gates that open later).
		if ( !area->HasAttributeFF( FF_NAV_SPAWN_ROOM_EXIT ) && area->IsBlocked( team ) )
			continue;

		const float curDist = area->GetIncursionDistance( team );

		for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
		{
			const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)dir );
			if ( !adj )
				continue;
			for ( int i = 0; i < adj->Count(); ++i )
			{
				const NavConnect &connect = ( *adj )[ i ];
				CFFNavArea *neighbor = static_cast< CFFNavArea * >( connect.area );
				if ( !neighbor )
					continue;

				// Skip impossible verticals.
				if ( area->ComputeAdjacentConnectionHeightChange( neighbor ) > FF_PLAYER_JUMP_HEIGHT )
					continue;

				const float newDist = curDist + connect.length;
				const float existing = neighbor->GetIncursionDistance( team );
				if ( existing < 0.0f || existing > newDist )
				{
					neighbor->SetIncursionDistance( team, newDist );
					neighbor->Mark();
					neighbor->SetParent( area );

					if ( !neighbor->IsOpen() )
						neighbor->AddToOpenListTail();
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
void CFFNavMesh::ComputeIncursionDistances( void )
{
	// Invalidate all per-team distances first.
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			area->SetIncursionDistance( t, -1.0f );
	}

	int computedTeams = 0;
	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		const CUtlVector< CFFNavArea * > *spawnAreas = GetSpawnRoomAreas( team );
		if ( !spawnAreas || spawnAreas->Count() == 0 )
			continue;

		ComputeIncursionDistancesFromTeamSpawn( team );
		++computedTeams;
	}

	Msg( "[CFFNavMesh] ComputeIncursionDistances: %d team(s) flooded across %d area(s).\n",
		computedTeams, TheNavAreas.Count() );
}

//-----------------------------------------------------------------------------
void CFFNavMesh::ComputeInvasionAreas( void )
{
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		area->ComputeInvasionAreaVectors();
	}
}

//-----------------------------------------------------------------------------
// For each spawn-room area, set FF_NAV_SPAWN_ROOM_EXIT if it has any
// non-spawn-room neighbor — that area is the "doorway out". Mirrors
// CTFNavMesh::CollectAndMarkSpawnRoomExits, generalized to 4 teams.
//-----------------------------------------------------------------------------
void CFFNavMesh::CollectAndMarkSpawnRoomExits( void )
{
	for ( int i = 0; i < FF_NAV_TEAM_COUNT; ++i )
		m_spawnRoomExitAreas[ i ].RemoveAll();

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) )
			continue;

		bool isExit = false;
		for ( int dir = 0; dir < NUM_DIRECTIONS && !isExit; ++dir )
		{
			const NavConnectVector *adj = area->GetAdjacentAreas( (NavDirType)dir );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CFFNavArea *neighbor = static_cast< CFFNavArea * >( ( *adj )[ j ].area );
				if ( neighbor && !neighbor->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY ) )
				{
					isExit = true;
					break;
				}
			}
		}

		if ( isExit )
		{
			area->SetAttributeFF( FF_NAV_SPAWN_ROOM_EXIT );

			// Publish to per-team exit lists. Each spawn-tagged area
			// belongs to every team whose SPAWN_ROOM_<team> bit is set.
			for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
			{
				if ( area->HasAttributeFF( CFFNavArea::SpawnRoomAttributeForTeam( t ) ) )
					AddSpawnRoomExitArea( t, area );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// For each spawn-room exit area for `team`, find its largest non-spawn
// neighbor and add to outVector. The bot aims at one of these on respawn
// to walk through the doorway into the playable region. Mirrors
// CTFNavMesh::CollectSpawnRoomThresholdAreas.
//-----------------------------------------------------------------------------
void CFFNavMesh::CollectSpawnRoomThresholdAreas( int team, CUtlVector< CFFNavArea * > *outVector ) const
{
	if ( !outVector )
		return;

	const CUtlVector< CFFNavArea * > *exitAreas = GetSpawnRoomExitAreas( team );
	if ( !exitAreas )
		return;

	for ( int i = 0; i < exitAreas->Count(); ++i )
	{
		CFFNavArea *exit = ( *exitAreas )[ i ];
		if ( !exit )
			continue;

		// Find the largest non-spawn-room neighbor of this exit.
		CFFNavArea *biggest = NULL;
		float biggestSize = 0.0f;
		for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
		{
			const NavConnectVector *adj = exit->GetAdjacentAreas( (NavDirType)dir );
			if ( !adj )
				continue;
			for ( int j = 0; j < adj->Count(); ++j )
			{
				CFFNavArea *cand = static_cast< CFFNavArea * >( ( *adj )[ j ].area );
				if ( !cand )
					continue;
				if ( cand->HasAttributeFF( FF_NAV_SPAWN_ROOM_ANY | FF_NAV_SPAWN_ROOM_EXIT ) )
					continue;	// still "inside" spawn from this team's POV

				const float size = cand->GetSizeX() * cand->GetSizeY();
				if ( size > biggestSize )
				{
					biggestSize = size;
					biggest = cand;
				}
			}
		}

		if ( biggest && outVector->Find( biggest ) == outVector->InvalidIndex() )
			outVector->AddToTail( biggest );
	}
}

//-----------------------------------------------------------------------------
// Map load / round restart. Clear all attribute bits + per-team caches,
// re-stamp from live entities. Persistent attributes already loaded by
// CFFNavArea::Load survive (they're on the area, not in the cache lists,
// and ClearAllAttributesFF would wipe them — so we don't call that).
//-----------------------------------------------------------------------------
void CFFNavMesh::OnServerActivate( void )
{
	BaseClass::OnServerActivate();

	// Clear NON-PERSISTENT attribute bits before re-tagging. Persistent
	// attributes (sniper spots, sentry spots, etc.) come back from the .nav
	// file and shouldn't be wiped.
	struct ClearTransient
	{
		bool operator()( CNavArea *a )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( a );
			area->ClearAttributeFF( ~FF_NAV_PERSISTENT_ATTRIBUTES );
			// Word 2 is entirely FFNavBuilder's; the sidecar re-stamps it.
			area->ClearAllAttributesFF2();
			return true;
		}
	} clearer;
	ForAllAreas( clearer );

	// Read the hand-authored marker sidecar BEFORE tagging: the tagger's
	// derivation pass applies it, and a manual spawn room has to exist before
	// spawn-exit collection and the incursion flood-fill run off it.
	FFNavBuilder::OnMapLoad();

	// Reconcile the Lua goal registry. Most of it arrived already, during
	// entity spawn, via Omnibot::Notify_GoalInfo; this sweeps up anything that
	// acquired a goal type without going through SetBotGoalInfo.
	FFBotLuaObjectives::OnMapLoad();

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
	MarkDoorwayAreas();

	// FIX 11 — apply connections learned from watching players move on this
	// map in previous sessions. Must run after the mesh is loaded and before
	// bots start pathing.
	FFBotLearnedLinks::OnMapLoad();

	// What kind of game is this? Derived from the goal registry, so it has to
	// run after FFBotLuaObjectives::OnMapLoad has swept the map.
	FFBotGameMode::OnMapLoad();
}

void CFFNavMesh::OnRoundRestart( void )
{
	BaseClass::OnRoundRestart();

	// Lua sometimes moves goal entities at round-start; re-tag.
	struct ClearTransient
	{
		bool operator()( CNavArea *a )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( a );
			area->ClearAttributeFF( ~FF_NAV_PERSISTENT_ATTRIBUTES );
			// Word 2 is entirely FFNavBuilder's; the sidecar re-stamps it.
			area->ClearAllAttributesFF2();
			return true;
		}
	} clearer;
	ForAllAreas( clearer );

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
	MarkDoorwayAreas();
}

//-----------------------------------------------------------------------------
// FIX 10 — stamp FF_NAV_DOORWAY on areas overlapping an openable blocker.
//
// CNavArea::IsBlocked flips to true whenever a door brush occupies the area,
// and CFFBotPathCost treated any blocked area as impassable (-1). On a map
// whose spawn gate is shut, that removed the ONLY route out of the spawn room
// from the graph — no path existed at all, which is why bots milled around
// inside spawn instead of pathing anywhere.
//
// With this bit set, path cost charges a heavy penalty instead of refusing, so
// a route still exists and CFFBotMainAction::HandleDoors can walk into the
// door and open it.
//-----------------------------------------------------------------------------
void CFFNavMesh::MarkDoorwayAreas( void )
{
	static const char * const kOpenable[] = {
		"func_door",
		"func_door_rotating",
		"prop_door_rotating",
		"func_movelinear",
		"func_brush",
		"func_wall_toggle",
	};

	int doorsFound = 0;
	int areasMarked = 0;

	for ( int c = 0; c < ARRAYSIZE( kOpenable ); ++c )
	{
		CBaseEntity *door = NULL;
		while ( ( door = gEntList.FindEntityByClassname( door, kOpenable[ c ] ) ) != NULL )
		{
			++doorsFound;

			// Collect the door's world bounds with a margin, so the areas on
			// BOTH sides of the threshold get marked, not just the sliver the
			// brush itself sits in.
			Extent extent;
			extent.Init( door );

			const float kMargin = 32.0f;
			extent.lo -= Vector( kMargin, kMargin, kMargin );
			extent.hi += Vector( kMargin, kMargin, kMargin );

			CUtlVector< CNavArea * > overlapping;
			CollectAreasOverlappingExtent( extent, &overlapping );

			for ( int i = 0; i < overlapping.Count(); ++i )
			{
				CFFNavArea *area = static_cast< CFFNavArea * >( overlapping[ i ] );
				if ( !area || area->HasAttributeFF( FF_NAV_DOORWAY ) )
					continue;
				area->SetAttributeFF( FF_NAV_DOORWAY );
				++areasMarked;
			}
		}
	}

	Msg( "[CFFNavMesh] MarkDoorwayAreas: %d openable entities, %d areas tagged FF_NAV_DOORWAY.\n",
		doorsFound, areasMarked );
}

//-----------------------------------------------------------------------------
// Diagnostic dump for ff_bot_nav_report.
//
// The failure this exists to surface: if nav generation ran with the spawn
// doors shut, the spawn room is a DISCONNECTED nav island. Then
// CollectAndMarkSpawnRoomExits finds no non-spawn neighbour, so there are zero
// exit areas and zero threshold areas, so CFFBot::Spawn can't compute an exit
// direction and no path to any objective can be built. Every one of those is
// silent at runtime; a bot with no path looks exactly like a stuck bot.
//
// If a team reports "exits=0" here, the nav mesh is the problem, not the AI.
//-----------------------------------------------------------------------------
void CFFNavMesh::PrintNavReport( void ) const
{
	Msg( "==== FF bot nav report ====\n" );

	if ( !IsLoaded() )
	{
		Msg( "  NO NAV MESH LOADED. Run nav_generate.\n" );
		return;
	}

	int doorwayAreas = 0;
	int blockedAreas = 0;
	int waterAreas = 0;
	int underwaterAreas = 0;
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		if ( !area )
			continue;
		if ( area->HasAttributeFF( FF_NAV_DOORWAY ) )
			++doorwayAreas;
		if ( area->IsBlocked( TEAM_ANY ) )
			++blockedAreas;
		if ( area->HasAttributeFF( FF_NAV_WATER ) )
			++waterAreas;
		if ( area->HasAttributeFF( FF_NAV_UNDERWATER ) )
			++underwaterAreas;
	}

	Msg( "  areas=%d  doorway=%d  currently-blocked=%d\n",
		TheNavAreas.Count(), doorwayAreas, blockedAreas );

	// Vertical routing. A map with visible ladders but ladders=0 means nav
	// generation never found them — regenerate with nav_generate. Bots
	// physically cannot path up or down a ladder that isn't in this list.
	const int ladderCount = const_cast< CFFNavMesh * >( this )->GetLadders().Count();
	Msg( "  ladders=%d  water areas=%d  underwater areas=%d%s\n",
		ladderCount, waterAreas, underwaterAreas,
		( ladderCount == 0 ) ? "   <-- NO LADDERS IN MESH (regenerate if this map has any)" : "" );

	static const char * const kTeamNames[] = { "BLUE", "RED", "YELLOW", "GREEN" };

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		const CUtlVector< CFFNavArea * > *spawns = GetSpawnRoomAreas( team );
		const CUtlVector< CFFNavArea * > *exits  = GetSpawnRoomExitAreas( team );
		if ( !spawns || spawns->Count() == 0 )
			continue;

		CUtlVector< CFFNavArea * > thresholds;
		CollectSpawnRoomThresholdAreas( team, &thresholds );

		// How much of the map is reachable from this team's spawn? A tiny
		// number here with a large area count is the disconnected-island
		// signature.
		int reachable = 0;
		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( area && area->IsReachableByTeam( team ) )
				++reachable;
		}

		Msg( "  %-6s spawn=%d exits=%d thresholds=%d reachable=%d/%d%s\n",
			kTeamNames[ team - TEAM_BLUE ],
			spawns->Count(),
			exits ? exits->Count() : 0,
			thresholds.Count(),
			reachable, TheNavAreas.Count(),
			( exits && exits->Count() == 0 ) ? "   <-- SPAWN ROOM IS A DISCONNECTED NAV ISLAND" : "" );
	}

	Msg( "  flags/caps:" );
	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		const CUtlVector< CFFNavArea * > *flags = GetFlagAreas( team );
		const CUtlVector< CFFNavArea * > *caps  = GetCapAreas( team );
		if ( ( !flags || flags->Count() == 0 ) && ( !caps || caps->Count() == 0 ) )
			continue;
		Msg( " %s(flag=%d cap=%d)", kTeamNames[ team - TEAM_BLUE ],
			flags ? flags->Count() : 0, caps ? caps->Count() : 0 );
	}
	Msg( "\n  resupply areas=%d\n", m_resupplyAreas.Count() );
	Msg( "===========================\n" );
}

//-----------------------------------------------------------------------------
// Helper: snap one coordinate to the nav generation grid (matches CNavMesh).
static float FFSnapToGrid( float v )
{
	const float kGridSize = GenerationStepSize;
	const float snapped = floorf( v / kGridSize + 0.5f ) * kGridSize;
	return snapped;
}

//-----------------------------------------------------------------------------
// nav_generate seeds the sampler from "walkable seed positions". CNavMesh's
// default looks up GetPlayerSpawnName() (info_player_start) — which doesn't
// exist in FF. Walk every info_ff_teamspawn so the sampler reaches all team
// regions (and disconnected spawn rooms on multi-team maps).
//-----------------------------------------------------------------------------
void CFFNavMesh::AddWalkableSeeds( void )
{
	int spawnsFound = 0;
	int objectiveEntsSeeded = 0;
	int itemEntsSeeded = 0;
	int seedsAdded = 0;
	int seedsViaGroundTrace = 0;
	int seedsViaRawPos = 0;

	auto trySeed = [&]( const Vector &raw ) -> void
	{
		Vector pos = raw;
		pos.x = FFSnapToGrid( pos.x );
		pos.y = FFSnapToGrid( pos.y );
		Vector groundPos = pos;
		Vector normal( 0.0f, 0.0f, 1.0f );
		if ( FindGroundForNode( &groundPos, &normal ) )
		{
			AddWalkableSeed( groundPos, normal );
			++seedsViaGroundTrace;
			++seedsAdded;
		}
		else
		{
			AddWalkableSeed( pos, Vector( 0.0f, 0.0f, 1.0f ) );
			++seedsViaRawPos;
			++seedsAdded;
		}
	};

	// 1. Team spawns — the primary seed (one for each spawn doorway).
	{
		CBaseEntity *pSpawn = NULL;
		while ( ( pSpawn = gEntList.FindEntityByClassname( pSpawn, "info_ff_teamspawn" ) ) != NULL )
		{
			++spawnsFound;
			trySeed( pSpawn->GetAbsOrigin() );
		}
	}

	// 2. Objective entities — flag, cap, backpack, hunted-escape, etc.
	// Most FF maps put these in disjoint regions of the map (own base vs
	// enemy base). Seeding from each one ensures nav_generate's sampler
	// doesn't miss whole basements / sewers / battlements that aren't
	// reachable from a spawn-room flood-fill alone.
	{
		CBaseEntity *pEnt = NULL;
		while ( ( pEnt = gEntList.FindEntityByClassT( pEnt, CLASS_INFOSCRIPT ) ) != NULL )
		{
			++objectiveEntsSeeded;
			trySeed( pEnt->GetAbsOrigin() );
		}
	}

	// 3. Item entities — packs / health drops. Same rationale: any
	// gameplay-relevant point is a place we want bot nav coverage for.
	static const char * const kItemClasses[] = {
		"ff_item_backpack",
		"ff_item_healthdrop",
		"item_healthkit",
		"item_battery",
	};
	for ( int c = 0; c < ARRAYSIZE( kItemClasses ); ++c )
	{
		CBaseEntity *pEnt = NULL;
		while ( ( pEnt = gEntList.FindEntityByClassname( pEnt, kItemClasses[ c ] ) ) != NULL )
		{
			++itemEntsSeeded;
			trySeed( pEnt->GetAbsOrigin() );
		}
	}

	// 4. Fallback: listen-server host position.
	if ( seedsAdded == 0 )
	{
		CBasePlayer *host = UTIL_GetListenServerHost();
		if ( host )
		{
			trySeed( host->GetAbsOrigin() );
			Msg( "[CFFNavMesh] No FF entities found; seeded from listen-server host position.\n" );
		}
	}

	Msg( "[CFFNavMesh] AddWalkableSeeds: %d teamspawns, %d objectives, %d items; "
		"%d seeds added (%d ground-trace, %d raw).\n",
		spawnsFound, objectiveEntsSeeded, itemEntsSeeded,
		seedsAdded, seedsViaGroundTrace, seedsViaRawPos );

	if ( seedsAdded == 0 )
	{
		Msg( "[CFFNavMesh] WARNING: nav_generate will fail. Stand on the floor and run 'nav_mark_walkable' followed by 'nav_generate'.\n" );
	}
}
