//========= Fortress Forever Bot =============================================//
//
// CFFNavMesh — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_nav_mesh.h"
#include "ff_bot_tagger.h"
#include "shareddefs.h"
#include "entitylist.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


CFFNavMesh::CFFNavMesh( void )
{
}

//-----------------------------------------------------------------------------
CNavArea *CFFNavMesh::CreateArea( void ) const
{
	return new CFFNavArea;
}

//-----------------------------------------------------------------------------
int CFFNavMesh::TeamIndex( int team ) const
{
	if ( team >= TEAM_BLUE && team <= TEAM_GREEN )
		return team - TEAM_BLUE;
	return -1;
}

//-----------------------------------------------------------------------------
const CUtlVector< CFFNavArea * > *CFFNavMesh::GetSpawnRoomAreas( int team ) const
{
	int idx = TeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_spawnRoomAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetFlagAreas( int team ) const
{
	int idx = TeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_flagAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetCapAreas( int team ) const
{
	int idx = TeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_capAreas[ idx ];
}

const CUtlVector< CFFNavArea * > *CFFNavMesh::GetSpawnExitAreas( int team ) const
{
	int idx = TeamIndex( team );
	if ( idx < 0 ) return NULL;
	return &m_spawnExitAreas[ idx ];
}

//-----------------------------------------------------------------------------
void CFFNavMesh::AddSpawnRoomArea( int team, CFFNavArea *area )
{
	int idx = TeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_spawnRoomAreas[ idx ].Find( area ) == m_spawnRoomAreas[ idx ].InvalidIndex() )
		m_spawnRoomAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddSpawnExitArea( int team, CFFNavArea *area )
{
	int idx = TeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_spawnExitAreas[ idx ].Find( area ) == m_spawnExitAreas[ idx ].InvalidIndex() )
		m_spawnExitAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddFlagArea( int team, CFFNavArea *area )
{
	int idx = TeamIndex( team );
	if ( idx < 0 || !area ) return;
	if ( m_flagAreas[ idx ].Find( area ) == m_flagAreas[ idx ].InvalidIndex() )
		m_flagAreas[ idx ].AddToTail( area );
}

void CFFNavMesh::AddCapArea( int team, CFFNavArea *area )
{
	int idx = TeamIndex( team );
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
		m_spawnExitAreas[ i ].RemoveAll();
		m_flagAreas[ i ].RemoveAll();
		m_capAreas[ i ].RemoveAll();
	}
	m_resupplyAreas.RemoveAll();
}


//-----------------------------------------------------------------------------
// Per-team incursion-distance flood-fill from spawn rooms outward across
// the nav graph. Mirrors TFBot's CTFNavMesh::ComputeIncursionDistances.
// Each area gets a dist-from-spawn for each team, used downstream by
// invasion-area computation, sniper vantage scoring, and "how deep am I?"
// queries.
//
// Algorithm: for each team, BFS from that team's spawn-room areas. Each
// neighbor's distance = current + edge length. Already-visited areas are
// updated only if we found a shorter path.
//-----------------------------------------------------------------------------
void CFFNavMesh::ComputeIncursionDistances( void )
{
	// Reset all distances first.
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		for ( int t = TEAM_BLUE; t <= TEAM_GREEN; ++t )
		{
			area->SetIncursionDistance( t, -1.0f );
		}
	}

	for ( int team = TEAM_BLUE; team <= TEAM_GREEN; ++team )
	{
		const CUtlVector< CFFNavArea * > *spawnAreas = GetSpawnRoomAreas( team );
		if ( !spawnAreas || spawnAreas->Count() == 0 )
			continue;

		// Open list — simple priority queue via insertion-sort (nav graphs
		// are small enough that we don't need a real heap).
		CUtlVector< CFFNavArea * > openList;

		for ( int i = 0; i < spawnAreas->Count(); ++i )
		{
			CFFNavArea *area = ( *spawnAreas )[ i ];
			area->SetIncursionDistance( team, 0.0f );
			openList.AddToTail( area );
		}

		while ( openList.Count() > 0 )
		{
			// Find the area with smallest distance (poor man's PQ).
			int bestIdx = 0;
			float bestDist = openList[ 0 ]->GetIncursionDistance( team );
			for ( int i = 1; i < openList.Count(); ++i )
			{
				const float d = openList[ i ]->GetIncursionDistance( team );
				if ( d < bestDist )
				{
					bestDist = d;
					bestIdx = i;
				}
			}
			CFFNavArea *current = openList[ bestIdx ];
			openList.Remove( bestIdx );

			const float curDist = current->GetIncursionDistance( team );

			for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
			{
				const NavConnectVector *adj = current->GetAdjacentAreas( (NavDirType)dir );
				if ( !adj )
					continue;
				for ( int i = 0; i < adj->Count(); ++i )
				{
					CFFNavArea *neighbor = static_cast< CFFNavArea * >( ( *adj )[ i ].area );
					if ( !neighbor )
						continue;

					const float edgeLen = ( neighbor->GetCenter() - current->GetCenter() ).Length();
					const float newDist = curDist + edgeLen;
					const float existingDist = neighbor->GetIncursionDistance( team );
					if ( existingDist < 0.0f || newDist < existingDist )
					{
						neighbor->SetIncursionDistance( team, newDist );
						if ( openList.Find( neighbor ) == openList.InvalidIndex() )
							openList.AddToTail( neighbor );
					}
				}
			}
		}
	}

	// Now compute invasion area vectors per area, using the freshly
	// flooded incursion distances.
	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
		area->ComputeInvasionAreaVectors();
	}

	Msg( "[CFFNavMesh] Computed incursion distances + invasion vectors for %d area(s) across %d teams.\n",
		TheNavAreas.Count(), 4 );
}

//-----------------------------------------------------------------------------
void CFFNavMesh::OnServerActivate( void )
{
	BaseClass::OnServerActivate();

	// Clear stale tags from any prior map / round.
	struct Clearer { bool operator()( CNavArea *a ) { static_cast< CFFNavArea * >( a )->ClearFFTags(); return true; } } clearer;
	ForAllAreas( clearer );

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
}

void CFFNavMesh::OnRoundRestart( void )
{
	BaseClass::OnRoundRestart();

	// Lua sometimes moves goal entities at round-start; re-tag.
	struct Clearer { bool operator()( CNavArea *a ) { static_cast< CFFNavArea * >( a )->ClearFFTags(); return true; } } clearer;
	ForAllAreas( clearer );

	ClearTaggedAreaCaches();
	CFFBotTagger::TagAreasFromEntities( this );
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
		pos.x = SnapToGrid( pos.x );
		pos.y = SnapToGrid( pos.y );

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
			// Ground trace started in solid (spawn buried in geometry, or hull
			// too tall for the spawn's headroom). Fall back to the raw spawn
			// position — sampler will project down on its first step.
			AddWalkableSeed( pos, Vector( 0.0f, 0.0f, 1.0f ) );
			++seedsViaRawPos;
			++seedsAdded;
		}
	}

	// If there were no FF spawns at all (which shouldn't happen on a real map),
	// fall back to the listen-server host's feet so nav_generate at least runs.
	if ( seedsAdded == 0 )
	{
		CBasePlayer *host = UTIL_GetListenServerHost();
		if ( host )
		{
			Vector pos = host->GetAbsOrigin();
			pos.x = SnapToGrid( pos.x );
			pos.y = SnapToGrid( pos.y );
			Vector normal( 0.0f, 0.0f, 1.0f );
			AddWalkableSeed( pos, normal );
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
