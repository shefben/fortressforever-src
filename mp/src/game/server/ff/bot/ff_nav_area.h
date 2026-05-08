//========= Fortress Forever Bot =============================================//
//
// CFFNavArea — runtime semantic-tag layer over CNavArea.
//
// Geometry comes from a stock .nav file (gameplay-agnostic). Semantic
// information — what each area *means* in gameplay terms (spawn room, flag,
// capture point, resupply, no-build, ...) — is stamped into m_ffBotTags by
// CFFBotTagger at LevelInitPostEntity, by walking the live entity world
// (info_ff_teamspawn, CFFInfoScript, CFFTriggerScript). Tags are never
// serialized; they're rebuilt every map load.
//
//===========================================================================//

#ifndef FF_NAV_AREA_H
#define FF_NAV_AREA_H
#ifdef _WIN32
#pragma once
#endif

#include "nav_area.h"

// Per-team bit (one bit per FF team: BLUE=2, RED=3, YELLOW=4, GREEN=5).
// Stored as bit (1 << team), so the team number is the bit index directly.
enum FFNavTagType
{
	FF_NAV_INVALID				= 0,

	// Spawn rooms — bit 1..4 for each of the four FF teams (shifted up).
	FF_NAV_SPAWN_BLUE			= 0x00000001,
	FF_NAV_SPAWN_RED			= 0x00000002,
	FF_NAV_SPAWN_YELLOW			= 0x00000004,
	FF_NAV_SPAWN_GREEN			= 0x00000008,
	FF_NAV_SPAWN_ANY			= FF_NAV_SPAWN_BLUE | FF_NAV_SPAWN_RED | FF_NAV_SPAWN_YELLOW | FF_NAV_SPAWN_GREEN,

	// Flag rest positions (info_ff_script kFlag entities).
	FF_NAV_FLAG_BLUE			= 0x00000010,
	FF_NAV_FLAG_RED				= 0x00000020,
	FF_NAV_FLAG_YELLOW			= 0x00000040,
	FF_NAV_FLAG_GREEN			= 0x00000080,
	FF_NAV_FLAG_ANY				= FF_NAV_FLAG_BLUE | FF_NAV_FLAG_RED | FF_NAV_FLAG_YELLOW | FF_NAV_FLAG_GREEN,

	// Capture points (info_ff_script kFlagCap entities).
	FF_NAV_CAP_BLUE				= 0x00000100,
	FF_NAV_CAP_RED				= 0x00000200,
	FF_NAV_CAP_YELLOW			= 0x00000400,
	FF_NAV_CAP_GREEN			= 0x00000800,
	FF_NAV_CAP_ANY				= FF_NAV_CAP_BLUE | FF_NAV_CAP_RED | FF_NAV_CAP_YELLOW | FF_NAV_CAP_GREEN,

	// Backpacks / resupply (info_ff_script kBackPack_*).
	FF_NAV_RESUPPLY				= 0x00001000,	// any backpack-style pickup
	FF_NAV_AMMO					= 0x00002000,
	FF_NAV_ARMOR				= 0x00004000,
	FF_NAV_HEALTH				= 0x00008000,
	FF_NAV_GRENADES				= 0x00010000,

	// Mode-specific.
	FF_NAV_VIP_GOAL				= 0x00020000,	// Hunted-mode escape destination
	FF_NAV_HUNTED_ESCAPE		= FF_NAV_VIP_GOAL,

	// Mapper / heuristic hints.
	FF_NAV_NO_BUILD				= 0x00040000,
	FF_NAV_SENTRY_HINT			= 0x00080000,
	FF_NAV_SNIPER_HINT			= 0x00100000,
	FF_NAV_DETPACKABLE_DOOR		= 0x00200000,	// breakable choke for demo detpack
	FF_NAV_WATER				= 0x00400000,	// area center is in water/slime
	FF_NAV_BACKDOOR				= 0x00800000,	// stealth/sneak route — bonus for spies
	FF_NAV_MANCANNON			= 0x01000000,	// has a deployed mancannon (jump pad)
	FF_NAV_CHOKE				= 0x02000000,	// betweenness-centrality choke point
	FF_NAV_NEAR_DOOR			= 0x04000000,	// area is adjacent to a one-way door pair
	FF_NAV_INTERCEPT_LANE		= 0x08000000,	// runtime: between our flag-carrier-thief and our base
	FF_NAV_SPAWN_EXIT			= 0x10000000,	// spawn-room area adjacent to non-spawn (the doorway/threshold)
};


class CFFNavArea : public CNavArea
{
public:
	DECLARE_CLASS( CFFNavArea, CNavArea );

	CFFNavArea( void );

	void	AddFFTag( int tags )				{ m_ffBotTags |= tags; }
	void	RemoveFFTag( int tags )				{ m_ffBotTags &= ~tags; }
	bool	HasFFTag( int tags ) const			{ return ( m_ffBotTags & tags ) != 0; }
	int		GetFFTags( void ) const				{ return m_ffBotTags; }
	void	ClearFFTags( void )					{ m_ffBotTags = 0; }

	// Convenience: spawn-room bit for a given FF team (TEAM_BLUE..TEAM_GREEN).
	static int SpawnTagForTeam( int team );
	static int FlagTagForTeam( int team );
	static int CapTagForTeam( int team );

	// Hot-zone heatmap. Each player death increments by 1. Decays
	// exponentially in GetDangerScore so areas where action is happening
	// glow hot, calm areas drift back to zero. Used by path cost (small
	// penalty) and defenders (pre-aim toward hottest area).
	void	IncrementDanger( float amount = 1.0f );
	float	GetDangerScore( void ) const;	// lazy-decayed read

	void	ClearDanger( void )					{ m_dangerScore = 0.0f; m_dangerUpdateTime = 0.0f; }

	// Class restriction mask. Each bit = one CLASS_* slot. 0 means "no
	// restriction; all classes welcome". Non-zero means "only the listed
	// classes should path through here". Inferred at level init from
	// height differentials (e.g., ledges only reachable via self-prop
	// jumps would be marked CLASS_SOLDIER | CLASS_DEMOMAN).
	unsigned short	GetClassMask( void ) const		{ return m_classMask; }
	void			SetClassMask( unsigned short mask ) { m_classMask = mask; }
	bool			IsClassAllowed( int classSlot ) const
	{
		if ( m_classMask == 0 )
			return true;
		return ( m_classMask & ( 1 << classSlot ) ) != 0;
	}

	// Per-team incursion distance. Travel distance (along nav graph) from
	// the given team's spawn rooms to this area. Lower = closer to the
	// team's home, higher = deeper into enemy territory. Mirrors TFBot's
	// CTFNavArea::m_distanceFromSpawnRoom. Used by:
	//  - invasion-area computation (areas adjacent to here with HIGHER
	//    incursion distance for team T → enemies of T are pushing in from
	//    those areas, so we should look toward them).
	//  - sniper vantage scoring (prefer high incursion = forward position).
	//  - sentry placement (prefer low-medium = defensive but not in spawn).
	//  - retreat decisions (compare current incursion to home's).
	// -1.0f = unreachable from that team's spawn (or not yet computed).
	float	GetIncursionDistance( int team ) const;
	void	SetIncursionDistance( int team, float dist );

	// Per-area invasion vectors. For team T, list the adjacent areas that
	// have HIGHER incursion distance for the *enemy* of T — meaning enemies
	// of T (i.e., team T's foes) approach this area from those directions.
	// Computed once per map by CFFNavMesh::ComputeInvasionAreas after
	// incursion distances are flooded. Used by the bot's default look
	// behavior to keep view aimed at where threats will appear.
	const CUtlVector< CFFNavArea * > &GetEnemyInvasionAreaVector( int myTeam ) const;
	void					ComputeInvasionAreaVectors( void );

private:
	unsigned int m_ffBotTags;
	mutable float m_dangerScore;
	mutable float m_dangerUpdateTime;
	unsigned short m_classMask;

	// One incursion distance per team slot (TEAM_BLUE..TEAM_GREEN → index
	// team-2). Default -1.0 = unreachable / unset.
	float m_incursionDistance[ 4 ];

	// One invasion-area vector per team slot. m_invasionAreaVector[t] =
	// areas the bot should look toward when watching for team t's enemies.
	CUtlVector< CFFNavArea * > m_invasionAreaVector[ 4 ];
};


#endif // FF_NAV_AREA_H
