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

	// Heuristic / runtime tags — stamped by CFFBotAutoTagger at level init,
	// never serialized. These let the bot reason about FF-specific gameplay
	// terrain without requiring per-map nav_edit work.
	FF_NAV_WATER						= 0x00800000,	// feet-level water (wading)
	FF_NAV_UNDERWATER					= 0x01000000,	// midbody+ water (must swim)
	FF_NAV_CHOKE						= 0x02000000,	// narrow corridor / chokepoint
	FF_NAV_HIGH_GROUND					= 0x04000000,	// elevated relative to neighbors
	FF_NAV_AUTO_SNIPER_SPOT				= 0x08000000,	// heuristic sniper perch
	FF_NAV_AUTO_SENTRY_SPOT				= 0x10000000,	// heuristic SG choke
	FF_NAV_NEAR_LADDER					= 0x20000000,	// adjacent to a ladder

	// FIX 10 — this area overlaps an OPENABLE blocker (func_door, respawn
	// gate, movelinear, ...). Stamped at level init by
	// CFFNavMesh::MarkDoorwayAreas.
	//
	// Matters because CNavArea::IsBlocked goes true while such a door is shut,
	// and CFFBotPathCost used to return -1 (impassable) for any blocked area.
	// That deleted the only route out of a spawn room every time its gate
	// closed: no path existed at all, so the bot just milled about. A doorway
	// area is now merely expensive, so a path still exists and
	// CFFBotMainAction::HandleDoors gets a chance to open the thing.
	FF_NAV_DOORWAY						= 0x40000000,

	// Persistent attributes saved to the .nav file. Mapper-set bits only.
	// Entity-derived AND heuristic bits are re-stamped every level load.
	FF_NAV_PERSISTENT_ATTRIBUTES		= FF_NAV_SNIPER_SPOT |
	                                      FF_NAV_SENTRY_SPOT |
	                                      FF_NAV_HUNTED_ESCAPE |
	                                      FF_NAV_NO_SPAWNING |
	                                      FF_NAV_UNBLOCKABLE,
};


//-----------------------------------------------------------------------------
// Second attribute word.
//
// FFNavAttributeType above has one free bit (0x80000000) and several concepts
// that want one. Rather than renumber an enum that already exists in saved
// .nav files, new attributes go here.
//
// Everything in this word is stamped by FFNavBuilder from the map's
// maps/<map>.ffnavpoints sidecar — hand-authored map knowledge that neither
// the entity tagger nor the shape heuristics can derive. None of it is written
// to the .nav file; the sidecar owns it and re-applies it every map load, so
// there is deliberately no FF_NAV2_PERSISTENT_ATTRIBUTES mask.
//-----------------------------------------------------------------------------
enum FFNavAttributeType2
{
	FF_NAV2_INVALID						= 0x00000000,

	// Engineer build hints. A suggestion, not a requirement — the build
	// behaviours score these highly and still fall back to their own search.
	FF_NAV2_DISPENSER_SPOT				= 0x00000001,

	// Breakable / shortcut wall: worth a demoman's det charge.
	FF_NAV2_DETPACK_SPOT				= 0x00000002,

	// Capture point with no owning team (dustbowl-style linear push maps,
	// where the same point is contested by whoever holds the round).
	FF_NAV2_CAP_NEUTRAL					= 0x00000004,

	// Hazard equipment pickup — rock2's gas suit and anything like it. Bots
	// that are asphyxiating need somewhere to run to.
	FF_NAV2_HAZARD_GEAR					= 0x00000008,

	// Author says stay out: pit, crusher, known grinder.
	FF_NAV2_DANGER						= 0x00000010,

	// Conc / rocket-jump launch position.
	FF_NAV2_JUMP_SPOT					= 0x00000020,

	// Where to leave the water. Underwater tunnels connect fine now, but
	// "which end of this pool has the ladder" is map knowledge.
	FF_NAV2_WATER_EXIT					= 0x00000040,

	// Set on every area any manual marker touches. Lets diagnostics tell
	// authored data apart from derived data at a glance.
	FF_NAV2_MANUAL						= 0x00000080,

	// Per-team defensive hold ground.
	FF_NAV2_DEFEND_BLUE					= 0x00000100,
	FF_NAV2_DEFEND_RED					= 0x00000200,
	FF_NAV2_DEFEND_YELLOW				= 0x00000400,
	FF_NAV2_DEFEND_GREEN				= 0x00000800,
	FF_NAV2_DEFEND_ANY					= 0x00000F00,

	// This area rides a moving platform (func_train, func_plat, tracktrain,
	// vertically-travelling door). Both auto-detected and hand-placeable.
	//
	// Worth knowing because a lift invalidates two assumptions the path
	// follower makes: the area's position is not constant, and arriving at it
	// may require waiting rather than moving. FoxBot needed a per-waypoint
	// tolerance for the same reason — see the note about rock2's small lifts
	// in its bot_navigate.cpp.
	FF_NAV2_LIFT						= 0x00001000,

	// This area overlaps a trigger_hurt. Auto-detected from the entity, which
	// is what makes it different from FF_NAV2_DANGER — DANGER is an author
	// saying "stay out", HAZARD_ZONE is the map saying it.
	//
	// The reason this matters is rock2. Its gas is a Lua-driven trigger_hurt
	// whose damage is switched on partway through the round; there is no
	// engine-level "the map is filling with gas" signal to read. But the
	// trigger volume itself is a real entity sitting in the world from level
	// load, so the geometry of the danger is knowable even when its schedule
	// isn't.
	FF_NAV2_HAZARD_ZONE					= 0x00002000,

	// Demoman: blow this SHUT to deny a route, as opposed to
	// FF_NAV2_DETPACK_SPOT which means blow it OPEN to make one.
	//
	// FoxBot has carried this distinction since TFC (W_FL_TFC_DETPACK_CLEAR
	// vs W_FL_TFC_DETPACK_SEAL) and it is not cosmetic: the two produce
	// opposite behaviour from the same position, and conflating them means a
	// demoman opening the route the defence just paid to close.
	FF_NAV2_DETPACK_SEAL				= 0x00004000,

	// Demoman pipe-trap position — where to lay a pipe carpet, which is a
	// different question from where to stand while doing it.
	FF_NAV2_PIPETRAP					= 0x00008000,

	// "Look this way from here." An aim hint is about facing, not position:
	// the marker's yaw is the payload, the area is just where it applies.
	// The yaw itself lives in CFFNavArea::m_aimYaw.
	FF_NAV2_AIM_HINT					= 0x00010000,

	//-------------------------------------------------------------------------
	// Derived by CFFBotAnalyzer at map load. Everything below is computed from
	// the nav graph, the visibility sets nav_generate already builds, or world
	// entities nobody was reading — no hand authoring, no map knowledge.
	//-------------------------------------------------------------------------

	// Articulation point: removing this area would disconnect the nav graph.
	//
	// This is what a chokepoint actually IS, and it is a different claim from
	// FF_NAV_CHOKE, which only means "this area is between 32 and 96 units
	// wide". That width test tags every corridor and doorway in the map whether
	// or not it matters, and misses a wide choke completely.
	FF_NAV2_CUTPOINT					= 0x00020000,

	// This area lies on a large fraction of the routes between spawn rooms and
	// objectives. Derived by running the pathfinder over every
	// spawn-threshold-to-objective pair and counting.
	//
	// "Where does the traffic go" answers most of the questions an author
	// answers by hand — where a sentry earns its keep, where a pipe carpet
	// catches people, which corridor is worth watching.
	FF_NAV2_HIGH_TRAFFIC				= 0x00040000,

	// Sees an unusual amount of high-traffic ground. Scored from the per-area
	// visibility sets that nav_generate already computes and writes into the
	// .nav file, and which nothing was reading.
	FF_NAV2_OVERLOOK					= 0x00080000,

	// A breakable brush sits here AND destroying it measurably shortens the
	// route between two parts of the map. The second half is the important one:
	// most breakables are scenery, and the test for "this one opens a shortcut"
	// is a graph query rather than a guess.
	FF_NAV2_BREACHABLE					= 0x00100000,

	// A trigger_teleport's entrance. Its exit is a real nav connection that no
	// amount of walkable-space sampling could ever find.
	FF_NAV2_TELEPORT					= 0x00200000,

	// A trigger_push overlaps this area — movement the mesh cannot see, and a
	// launcher that gets a player somewhere walking wouldn't.
	FF_NAV2_PUSH						= 0x00400000,
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

	// Second attribute word — FF_NAV2_* bits, authored via FFNavBuilder.
	// Deliberately a separate API rather than a 64-bit widening of the first:
	// every existing call site means "the FF_NAV_* word" and silently
	// accepting an FF_NAV2_* constant there would be a no-op that compiles.
	void			SetAttributeFF2( int flags )		{ m_attributeFlags2 |= flags; }
	void			ClearAttributeFF2( int flags )		{ m_attributeFlags2 &= ~flags; }
	bool			HasAttributeFF2( int flags ) const	{ return ( m_attributeFlags2 & flags ) != 0; }
	unsigned int	GetAttributesFF2( void ) const		{ return m_attributeFlags2; }
	void			ClearAllAttributesFF2( void )		{ m_attributeFlags2 = 0; }

	// Convenience: per-team spawn-room / flag / cap / defend bit lookup.
	static int		SpawnRoomAttributeForTeam( int team );
	static int		FlagAttributeForTeam( int team );
	static int		CapAttributeForTeam( int team );
	static int		DefendAttributeForTeam( int team );

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

	// Facing, in degrees, for FF_NAV2_AIM_HINT. Either hand-authored (the yaw
	// you were looking at when you placed the marker) or derived by
	// CFFBotAnalyzer from where the traffic comes from.
	//
	// Lives on the area rather than being looked up from the marker sidecar
	// because a derived hint has no marker to look up, and the consumer should
	// not have to care which kind it got.
	float			GetAimYaw( void ) const		{ return m_aimYaw; }
	void			SetAimYaw( float yaw )		{ m_aimYaw = yaw; }

	// 0..1. How much of the spawn-to-objective traffic passes through here, as
	// a fraction of the busiest area on the map. Derived; never serialized.
	float			GetTrafficScore( void ) const	{ return m_trafficScore; }
	void			SetTrafficScore( float s )		{ m_trafficScore = s; }

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
	unsigned int m_attributeFlags2;

	float m_aimYaw;			// FF_NAV2_AIM_HINT payload
	float m_trafficScore;	// 0..1, derived betweenness

	// Per-team [TEAM_BLUE..TEAM_GREEN] indexed compact (team - TEAM_BLUE).
	float m_distanceFromSpawnRoom[ FF_NAV_TEAM_COUNT ];
	CUtlVector< CFFNavArea * > m_invasionAreaVector[ FF_NAV_TEAM_COUNT ];

	mutable float m_combatIntensity;
	mutable IntervalTimer m_combatTimer;

	unsigned short m_classMask;
};


#endif // FF_NAV_AREA_H
