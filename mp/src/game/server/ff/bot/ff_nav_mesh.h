//========= Fortress Forever Bot =============================================//
//
// CFFNavMesh — FF-specific nav mesh. Owns CFFNavArea creation and is the
// hook point for runtime semantic tagging of areas (CFFBotTagger runs at
// OnServerActivate, after Lua has placed gameplay entities).
//
//===========================================================================//

#ifndef FF_NAV_MESH_H
#define FF_NAV_MESH_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_mesh.h"
#include "ff_nav_area.h"

#define FF_NAV_TEAM_COUNT	4		// BLUE, RED, YELLOW, GREEN — we store at index (team - TEAM_BLUE).


class CFFNavMesh : public CNavMesh
{
public:
	DECLARE_CLASS( CFFNavMesh, CNavMesh );

	CFFNavMesh( void );

	virtual CNavArea *CreateArea( void ) const OVERRIDE;	// CNavArea factory — returns CFFNavArea
	virtual unsigned int GetSubVersionNumber( void ) const OVERRIDE { return 1; }

	virtual void OnServerActivate( void ) OVERRIDE;			// invoked when server loads a new map (calls the tagger)
	virtual void OnRoundRestart( void ) OVERRIDE;			// (also re-tag on round restart in case Lua moved goals)

	// FF has no info_player_start; seed from every info_ff_teamspawn instead so
	// nav_generate samples all regions of a map (multi-team, disconnected spawns).
	virtual void AddWalkableSeeds( void ) OVERRIDE;

	// FF environments are reasonably clean — let the path follower trust the mesh.
	virtual bool IsAuthoritative( void ) const OVERRIDE { return true; }

	// Cached area collections (filled by the tagger). team = TEAM_BLUE..TEAM_GREEN.
	const CUtlVector< CFFNavArea * > *GetSpawnRoomAreas( int team ) const;
	const CUtlVector< CFFNavArea * > *GetFlagAreas( int team ) const;
	const CUtlVector< CFFNavArea * > *GetCapAreas( int team ) const;
	const CUtlVector< CFFNavArea * > *GetResupplyAreas( void ) const { return &m_resupplyAreas; }
	// Threshold/exit areas of a team's spawn rooms — areas inside the spawn
	// that are adjacent to a non-spawn area. Mirrors TFNavMesh's
	// GetSpawnRoomExitAreas. CFFBotMainAction watches these for "look toward
	// where enemies will arrive" defaults; CFFBot::Spawn aims at the nearest
	// one when leaving spawn.
	const CUtlVector< CFFNavArea * > *GetSpawnExitAreas( int team ) const;

	void ClearTaggedAreaCaches( void );
	void AddSpawnRoomArea( int team, CFFNavArea *area );
	void AddSpawnExitArea( int team, CFFNavArea *area );
	void AddFlagArea( int team, CFFNavArea *area );
	void AddCapArea( int team, CFFNavArea *area );
	void AddResupplyArea( CFFNavArea *area );

	// Compute travel distance from each team's spawn rooms to every
	// reachable nav area (TFBot's "incursion distance"). Stored on
	// CFFNavArea via SetIncursionDistance/GetIncursionDistance. Used by
	// the bot to know "how deep into enemy territory am I" — drives sniper
	// vantage selection, sentry placement, retreat decisions.
	void ComputeIncursionDistances( void );

private:
	int TeamIndex( int team ) const;	// TEAM_BLUE..TEAM_GREEN -> 0..3, or -1

	CUtlVector< CFFNavArea * > m_spawnRoomAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_spawnExitAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_flagAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_capAreas[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_resupplyAreas;
};


inline CFFNavMesh *TheFFNavMesh( void )
{
	return reinterpret_cast< CFFNavMesh * >( TheNavMesh );
}


#endif // FF_NAV_MESH_H
