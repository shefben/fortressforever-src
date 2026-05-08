//========= Fortress Forever Bot =============================================//
//
// CFFNavMesh — see header. TF-style nav mesh for FF.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_mesh.h"
#include "ff_nav_area.h"
#include "ff_bot_tagger.h"
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
			return true;
		}
	} clearer;
	ForAllAreas( clearer );

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
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
			return true;
		}
	} clearer;
	ForAllAreas( clearer );

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
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
	int seedsAdded = 0;
	int seedsViaGroundTrace = 0;
	int seedsViaRawPos = 0;

	CBaseEntity *pSpawn = NULL;
	while ( ( pSpawn = gEntList.FindEntityByClassname( pSpawn, "info_ff_teamspawn" ) ) != NULL )
	{
		++spawnsFound;

		Vector pos = pSpawn->GetAbsOrigin();
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
			// Ground trace started in solid (spawn buried in geometry, or
			// hull too tall). Fall back to the raw spawn position — the
			// sampler will project down on its first step.
			AddWalkableSeed( pos, Vector( 0.0f, 0.0f, 1.0f ) );
			++seedsViaRawPos;
			++seedsAdded;
		}
	}

	// Fallback: if no FF spawns at all, seed from the listen-server host.
	if ( seedsAdded == 0 )
	{
		CBasePlayer *host = UTIL_GetListenServerHost();
		if ( host )
		{
			Vector pos = host->GetAbsOrigin();
			pos.x = FFSnapToGrid( pos.x );
			pos.y = FFSnapToGrid( pos.y );
			AddWalkableSeed( pos, Vector( 0.0f, 0.0f, 1.0f ) );
			++seedsAdded;
			Msg( "[CFFNavMesh] No info_ff_teamspawn found; seeded from listen-server host position.\n" );
		}
	}

	Msg( "[CFFNavMesh] AddWalkableSeeds: %d info_ff_teamspawn entities, %d seeds added (%d via ground-trace, %d via raw position).\n",
		spawnsFound, seedsAdded, seedsViaGroundTrace, seedsViaRawPos );

	if ( seedsAdded == 0 )
	{
		Msg( "[CFFNavMesh] WARNING: nav_generate will fail. Stand on the floor and run 'nav_mark_walkable' followed by 'nav_generate'.\n" );
	}
}
