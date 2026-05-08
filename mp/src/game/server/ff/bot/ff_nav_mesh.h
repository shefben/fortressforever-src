//========= Fortress Forever Bot =============================================//
//
// CFFNavMesh — TF-style nav mesh for FF.
//
// Mirrors Valve's CTFNavMesh (hl2_src/game/server/tf/nav_mesh/tf_nav_mesh.h)
// adapted for FF's 4-team setup. Per-team arrays (spawn rooms, spawn-room
// exits, flag rest positions, capture points) are sized FF_NAV_TEAM_COUNT
// and indexed [team - TEAM_BLUE] = 0..3.
//
// Geometry comes from a stock .nav file. Entity-derived attribute bits
// (spawn rooms, flags, caps, pickups) are stamped at OnServerActivate by
// CFFBotTagger from FF's live entity world (info_ff_teamspawn,
// CFFInfoScript) — re-derived every map load.
//
// Mapper-set persistent attributes (FF_NAV_SNIPER_SPOT, FF_NAV_SENTRY_SPOT,
// etc.) ride the standard CNavArea Save/Load path with FF_NAV_PERSISTENT_
// ATTRIBUTES mask.
//
//===========================================================================//

#ifndef FF_NAV_MESH_H
#define FF_NAV_MESH_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_mesh.h"
#include "ff_nav_area.h"


// General-purpose collector functor (ForAllAreas-style helpers). Mirrors
// TF's CTFAreaCollector.
class CFFAreaCollector
{
public:
	bool operator()( CNavArea *area )
	{
		m_vector.AddToTail( static_cast< CFFNavArea * >( area ) );
		return true;
	}
	CUtlVector< CFFNavArea * > m_vector;
};


class CFFNavMesh : public CNavMesh
{
public:
	DECLARE_CLASS( CFFNavMesh, CNavMesh );

	CFFNavMesh( void );

	virtual CNavArea *CreateArea( void ) const OVERRIDE;
	virtual unsigned int GetSubVersionNumber( void ) const OVERRIDE { return 2; }

	virtual void OnServerActivate( void ) OVERRIDE;
	virtual void OnRoundRestart( void ) OVERRIDE;

	virtual unsigned int GetGenerationTraceMask( void ) const OVERRIDE
	{
		return MASK_PLAYERSOLID_BRUSHONLY;
	}

	// FF environments are reasonably clean — let the path follower trust
	// the mesh (skip realtime jump/drop scanning of unpredictable geometry).
	virtual bool IsAuthoritative( void ) const OVERRIDE { return true; }

	// nav_generate seeding from FF spawn entities — info_ff_teamspawn rather
	// than info_player_start (which doesn't exist in FF). Same logic as before;
	// has no TF analogue.
	virtual void AddWalkableSeeds( void ) OVERRIDE;

	// Per-team accessors. team = TEAM_BLUE..TEAM_GREEN. Returns NULL on
	// invalid team. Mirrors CTFNavMesh::GetSpawnRoomAreas / GetSpawnRoomExitAreas.
	const CUtlVector< CFFNavArea * > *GetSpawnRoomAreas( int team ) const;
	const CUtlVector< CFFNavArea * > *GetSpawnRoomExitAreas( int team ) const;

	// FF goal accessors — flag rest positions, capture points. TF tracks
	// flags / caps as entities, not nav-area lists; this is FF-specific.
	const CUtlVector< CFFNavArea * > *GetFlagAreas( int team ) const;
	const CUtlVector< CFFNavArea * > *GetCapAreas( int team ) const;

	// Resupply / pickup areas — single global list across all teams.
	const CUtlVector< CFFNavArea * > *GetResupplyAreas( void ) const { return &m_resupplyAreas; }

	// Add to a per-team / global list (used by the tagger). Idempotent.
	void AddSpawnRoomArea( int team, CFFNavArea *area );
	void AddSpawnRoomExitArea( int team, CFFNavArea *area );
	void AddFlagArea( int team, CFFNavArea *area );
	void AddCapArea( int team, CFFNavArea *area );
	void AddResupplyArea( CFFNavArea *area );

	// Wipe all entity-derived per-team / resupply lists. Called from
	// OnServerActivate before re-tagging so we don't accumulate stale
	// references across map loads.
	void ClearTaggedAreaCaches( void );

	// Per-team incursion-distance flood-fill from spawn rooms outward.
	// Mirrors CTFNavMesh::ComputeIncursionDistances. Recomputes for all
	// FF teams. Run AFTER spawn-room tagging.
	void ComputeIncursionDistances( void );

	// Per-area invasion-vector recompute. Calls
	// CFFNavArea::ComputeInvasionAreaVectors on every area. Run AFTER
	// ComputeIncursionDistances.
	void ComputeInvasionAreas( void );

	// Mark spawn-room areas adjacent to non-spawn-room areas as
	// FF_NAV_SPAWN_ROOM_EXIT and add them to per-team exit lists. Mirrors
	// CTFNavMesh::CollectAndMarkSpawnRoomExits. Run AFTER spawn-room tagging.
	void CollectAndMarkSpawnRoomExits( void );

	// Populate the given vector with the largest non-spawn neighbors of
	// each FF_NAV_SPAWN_ROOM_EXIT area for the given team. The bot aims
	// here on respawn so it walks toward the actual playable region rather
	// than into a wall. Mirrors CTFNavMesh::CollectSpawnRoomThresholdAreas.
	void CollectSpawnRoomThresholdAreas( int team, CUtlVector< CFFNavArea * > *outVector ) const;

private:
	// Single-team flood-fill helper. Pulls the BFS out of the per-team loop
	// in ComputeIncursionDistances so the caller can drive seeding logic.
	void ComputeIncursionDistancesFromTeamSpawn( int team );

	// Per-team [TEAM_BLUE..TEAM_GREEN] indexed compact (team - TEAM_BLUE).
	CUtlVector< CFFNavArea * > m_spawnRoomAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_spawnRoomExitAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_flagAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_capAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_resupplyAreas;
};


inline CFFNavMesh *TheFFNavMesh( void )
{
	return reinterpret_cast< CFFNavMesh * >( TheNavMesh );
}


#endif // FF_NAV_MESH_H
