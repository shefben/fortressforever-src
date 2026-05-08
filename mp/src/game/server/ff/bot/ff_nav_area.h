//========= Fortress Forever Bot =============================================//
//
// CFFNavArea — TF-style nav area for FF.
//
// Mirrors Valve's CTFNavArea (hl2_src/game/server/tf/nav_mesh/tf_nav_area.h)
// adapted for FF's 4-team setup (BLUE/RED/YELLOW/GREEN). Attribute bits use
// the FF_NAV_* prefix and follow TF's naming conventions. Per-team arrays
// use indexes [team - TEAM_BLUE] = 0..3.
//
// What you DON'T find here (vs the previous custom FF nav layer):
//   - Heuristic tags (water, backdoor, mancannon, choke, intercept lanes).
//     Those were custom decoration; gone now. Only TF-style mapper-set or
//     entity-derived attributes remain.
//   - The hot-zone "danger score" with exp decay. Replaced by TF's
//     IntervalTimer-based combat intensity (OnCombat / GetCombatIntensity).
//   - Persistence sidecar (.ffnav). Attribute persistence uses the standard
//     CNavArea Save/Load path with FF_NAV_PERSISTENT_ATTRIBUTES mask.
//
//===========================================================================//

#ifndef FF_NAV_AREA_H
#define FF_NAV_AREA_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_area.h"
#include "shareddefs.h"


// FF teams: TEAM_BLUE(2), TEAM_RED(3), TEAM_YELLOW(4), TEAM_GREEN(5).
// Per-team arrays use compact indexing: index = team - TEAM_BLUE, size 4.
#define FF_NAV_TEAM_COUNT		4
#define FF_NAV_TEAM_INDEX( t )	( ( t ) - TEAM_BLUE )


//-----------------------------------------------------------------------------
// Attribute bits. Mirrors TFNavAttributeType; renamed FF_NAV_* and extended
// for 4-team support. Bits 0..31 — keep persistent attrs in the high range
// so future additions to the entity-derived range don't shift them.
//-----------------------------------------------------------------------------
enum FFNavAttributeType
{
	FF_NAV_INVALID						= 0x00000000,

	// Generic blocking — set/cleared by mappers or runtime logic.
	FF_NAV_BLOCKED						= 0x00000001,

	// Per-team spawn rooms. Stamped at level-init by the tagger from
	// info_ff_teamspawn entities (with name-based fallback for FF Lua-driven
	// validspawn maps).
	FF_NAV_SPAWN_ROOM_BLUE				= 0x00000002,
	FF_NAV_SPAWN_ROOM_RED				= 0x00000004,
	FF_NAV_SPAWN_ROOM_YELLOW			= 0x00000008,
	FF_NAV_SPAWN_ROOM_GREEN				= 0x00000010,
	FF_NAV_SPAWN_ROOM_ANY				= 0x0000001E,

	// Spawn-room threshold area (TF's CollectAndMarkSpawnRoomExits).
	// Set on any spawn-room area with a non-spawn neighbor.
	FF_NAV_SPAWN_ROOM_EXIT				= 0x00000020,

	// Pickup hints — derived from CFFInfoScript backpack types.
	FF_NAV_HAS_AMMO						= 0x00000040,
	FF_NAV_HAS_HEALTH					= 0x00000080,
	FF_NAV_HAS_ARMOR					= 0x00000100,
	FF_NAV_HAS_GRENADES					= 0x00000200,

	// Per-team flag rest position (CFFInfoScript kFlag).
	FF_NAV_FLAG_BLUE					= 0x00000400,
	FF_NAV_FLAG_RED						= 0x00000800,
	FF_NAV_FLAG_YELLOW					= 0x00001000,
	FF_NAV_FLAG_GREEN					= 0x00002000,
	FF_NAV_FLAG_ANY						= 0x00003C00,

	// Per-team capture point (CFFInfoScript kFlagCap).
	FF_NAV_CAP_BLUE						= 0x00004000,
	FF_NAV_CAP_RED						= 0x00008000,
	FF_NAV_CAP_YELLOW					= 0x00010000,
	FF_NAV_CAP_GREEN					= 0x00020000,
	FF_NAV_CAP_ANY						= 0x0003C000,

	// Sniper / sentry hand-tagged spots (TF-style; mappers run nav_edit
	// commands to set these. Auto-detection was dropped with FFBotMapIntel).
	FF_NAV_SNIPER_SPOT					= 0x00040000,
	FF_NAV_SENTRY_SPOT					= 0x00080000,

	// Hunted-mode VIP escape destination.
	FF_NAV_HUNTED_ESCAPE				= 0x00100000,

	// Don't spawn bots in this area (mapper-set).
	FF_NAV_NO_SPAWNING					= 0x00200000,

	// Area cannot be blocked (mapper-set).
	FF_NAV_UNBLOCKABLE					= 0x00400000,

	// Persistent attributes saved to the .nav file. Mapper-set bits only.
	// Entity-derived bits (spawn rooms, flags, caps, pickups) are re-stamped
	// every level load and never written.
	FF_NAV_PERSISTENT_ATTRIBUTES		= FF_NAV_SNIPER_SPOT |
	                                      FF_NAV_SENTRY_SPOT |
	                                      FF_NAV_HUNTED_ESCAPE |
	                                      FF_NAV_NO_SPAWNING |
	                                      FF_NAV_UNBLOCKABLE,
};


class CFFNavArea : public CNavArea
{
public:
	DECLARE_CLASS( CFFNavArea, CNavArea );

	CFFNavArea( void );

	virtual void OnServerActivate( void );
	virtual void OnRoundRestart( void );

	virtual void Save( CUtlBuffer &fileBuffer, unsigned int version ) const;
	virtual NavErrorType Load( CUtlBuffer &fileBuffer, unsigned int version, unsigned int subVersion );

	// Attribute API — mirrors CTFNavArea::SetAttributeTF / HasAttributeTF.
	void			SetAttributeFF( int flags )			{ m_attributeFlags |= flags; }
	void			ClearAttributeFF( int flags )		{ m_attributeFlags &= ~flags; }
	bool			HasAttributeFF( int flags ) const	{ return ( m_attributeFlags & flags ) != 0; }
	unsigned int	GetAttributesFF( void ) const		{ return m_attributeFlags; }
	void			ClearAllAttributesFF( void )		{ m_attributeFlags = 0; }

	// Convenience: per-team spawn-room / flag / cap bit lookup.
	static int		SpawnRoomAttributeForTeam( int team );
	static int		FlagAttributeForTeam( int team );
	static int		CapAttributeForTeam( int team );

	// Per-team incursion distance — travel distance from the team's spawn
	// room(s) to this area along the nav graph. -1 means unreachable from
	// that team's spawn or not yet computed. Mirrors CTFNavArea.
	float			GetIncursionDistance( int team ) const;
	void			SetIncursionDistance( int team, float dist );
	bool			IsReachableByTeam( int team ) const;

	// Adjacent area with the largest INCREASE in incursion distance for the
	// given team — i.e., the direction that pushes deeper into enemy
	// territory. Mirrors CTFNavArea::GetNextIncursionArea.
	CFFNavArea *GetNextIncursionArea( int team ) const;

	// Populate a vector with adjacent areas that have a LOWER incursion
	// distance for the given team (closer to home).
	void CollectPriorIncursionAreas( int team, CUtlVector< CFFNavArea * > *outVector );

	// Populate a vector with adjacent areas that have a HIGHER incursion
	// distance for the given team (deeper into enemy territory).
	void CollectNextIncursionAreas( int team, CUtlVector< CFFNavArea * > *outVector );

	// For team myTeam, list adjacent areas the enemy is invading from.
	const CUtlVector< CFFNavArea * > &GetEnemyInvasionAreaVector( int myTeam ) const;

	// True if this area is at least safetyRange units from every invasion
	// area for myTeam. Mirrors CTFNavArea::IsAwayFromInvasionAreas — used
	// to score sniper vantage candidates.
	bool IsAwayFromInvasionAreas( int myTeam, float safetyRange = 1000.0f ) const;

	// Recompute m_invasionAreaVector from current incursion distances.
	// Called by CFFNavMesh::ComputeInvasionAreas after the per-team
	// incursion-distance flood-fill finishes.
	void ComputeInvasionAreaVectors( void );

	// Combat intensity. OnCombat() spikes the score; GetCombatIntensity
	// reads with exponential decay (rate from tf-style cvars). Used by
	// path cost (slight penalty for hot zones) and defenders (look toward
	// active combat). Replaces our custom danger-score system.
	void	OnCombat( void );
	float	GetCombatIntensity( void ) const;
	bool	IsInCombat( void ) const;

	// FF-specific: per-area class restriction mask. 0 = all classes welcome,
	// non-zero = only listed classes can path through. No TF analogue.
	unsigned short	GetClassMask( void ) const		{ return m_classMask; }
	void			SetClassMask( unsigned short m ){ m_classMask = m; }
	bool			IsClassAllowed( int classSlot ) const
	{
		if ( m_classMask == 0 )
			return true;
		return ( m_classMask & ( 1 << classSlot ) ) != 0;
	}

private:
	friend class CFFNavMesh;

	unsigned int m_attributeFlags;

	// Per-team [TEAM_BLUE..TEAM_GREEN] indexed compact (team - TEAM_BLUE).
	float m_distanceFromSpawnRoom[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_invasionAreaVector[ FF_NAV_TEAM_COUNT ];

	mutable float m_combatIntensity;
	mutable IntervalTimer m_combatTimer;

	unsigned short m_classMask;
};


#endif // FF_NAV_AREA_H
