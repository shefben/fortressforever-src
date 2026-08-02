//========= Fortress Forever Bot =============================================//
//
// FFNavBuilder — in-game manual nav point authoring.
//
// The nav mesh gives the bots geometry. It does not give them intent: which
// doorway is a spawn gate, where an engineer *should* drop a sentry, which
// wall a demoman is supposed to blow open, where the gas suit lives on rock2.
// Some of that is derivable from entities (CFFBotTagger) and some of it is
// guessable from shape (CFFBotAutoTagger), but the rest is map knowledge that
// only a human has, and up to now the only way to give it to the bots was to
// stand in exactly the right place and type a console command per marker.
//
// This is the authoring layer for that knowledge:
//
//   ff_manual_nav_builder 1     turn the mode on; binds the numpad
//   walk / noclip to a spot     ...
//   press a numpad key          marker placed at your feet, written to disk
//
// Design notes worth knowing before changing anything here:
//
//   * Markers live in a SIDECAR, maps/<map>.ffnavpoints, not in the .nav file.
//     They have to. CFFNavMesh::OnServerActivate wipes every non-persistent
//     attribute bit and CFFBotTagger re-stamps spawn / flag / cap from live
//     entities each map load, so anything we wrote into those bits at author
//     time would be erased before a bot ever read it. The sidecar is re-applied
//     after the tagger runs instead, which also means a manual marker can
//     legitimately override or supplement what the entities say.
//
//   * The sidecar is plain text and hand-editable on purpose. Authoring data
//     that can only be edited by the tool that wrote it ages badly.
//
//   * Marker types that have no existing FF_NAV_* bit (dispenser hints, det
//     charge walls, neutral caps, hazard gear) set bits in CFFNavArea's SECOND
//     attribute word. The first word has exactly one free bit left; adding a
//     word is cheaper than renumbering an enum that's already in shipped .nav
//     files.
//
//===========================================================================//

#ifndef FF_NAV_BUILDER_H
#define FF_NAV_BUILDER_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "tier1/utlvector.h"

class CFFNavMesh;
class CFFNavArea;
class CBasePlayer;


//-----------------------------------------------------------------------------
// Marker kinds. Order is the cycle order for ff_nav_builder_next / _prev, so
// keep the commonly-placed ones near the front.
//
// "Needs team" markers take the authoring team (ff_nav_builder_team, default
// = the placing player's own team). The rest ignore it and store team 0.
//-----------------------------------------------------------------------------
enum FFNavPointType
{
	FFNAVPT_INVALID = 0,

	// --- Page 1: the nine you place most, on numpad 1-9 -------------------
	FFNAVPT_SNIPER,			// sniper holds this position          (uses yaw)
	FFNAVPT_SENTRY,			// engineer should consider an SG here  (uses yaw)
	FFNAVPT_DISPENSER,		// ...and a dispenser here
	FFNAVPT_DETPACK,		// breakable wall: blow it OPEN to make a route
	FFNAVPT_SPAWN_CORNER,	// team; four of these bound one spawn room
	FFNAVPT_DOOR,			// a door lives here — expect it shut, open it
	FFNAVPT_FLAG,			// team; flag rest position
	FFNAVPT_CAP,			// team; capture point
	FFNAVPT_HAZARD_GEAR,	// gas suit / hazard equipment (rock2)

	// --- Page 2: tactical --------------------------------------------------
	FFNAVPT_DETPACK_SEAL,	// blow it SHUT to deny a route
	FFNAVPT_PIPETRAP,		// demoman pipe carpet position         (uses yaw)
	FFNAVPT_AIM,			// look this way from here              (uses yaw)
	FFNAVPT_DEFEND,			// team; hold this ground on defense    (uses yaw)
	FFNAVPT_DANGER,			// avoid: pit, trap, known grinder
	FFNAVPT_JUMP,			// conc / rocket-jump launch position   (uses yaw)
	FFNAVPT_WATER_EXIT,		// where to leave the water (ladder foot, ramp)
	FFNAVPT_LIFT,			// lift / elevator: expect to wait for it
	FFNAVPT_CAP_NEUTRAL,	// capture point nobody owns (dustbowl-style push)

	// --- Page 3: occasional & connectivity ----------------------------------
	FFNAVPT_RESUPPLY,		// ammo / health / armor available here
	FFNAVPT_NO_SPAWNING,	// don't pick this area as a bot spawn point
	FFNAVPT_ESCAPE,			// hunted-mode VIP escape destination

	// Nav CONNECTIONS rather than nav attributes. Everything above tags an
	// area; these two describe an edge between two of them.
	//
	// This is the one thing the builder could not express, and the gap mattered:
	// underwater tunnels, one-way drops, gaps that need a running jump, and any
	// doorway that happened to be shut when nav_generate ran all produce areas
	// that are adjacent in the world and unconnected in the graph. A* then
	// refuses the route entirely, and no amount of attribute tagging fixes it
	// because there is no edge to tag.
	//
	// Placed in pairs, consumed in file order: each FROM takes the next TO after
	// it. Bidirectional by default; ff_nav_builder_link_oneway makes the pair a
	// one-way edge, which is what a drop off a ledge actually is.
	FFNAVPT_LINK_FROM,		// start of a connection this mesh is missing
	FFNAVPT_LINK_TO,		// ...and where it comes out

	// --- Page 4: traversal furniture & maintenance --------------------------
	//
	// Ladders and doors were already detected and tagged, and both were
	// invisible and unauthorable: no marker type, no entry in
	// ff_nav_visualize. "The ladder in 2fort has no nav points on it" and "the
	// ladder was never tagged" looked identical, and where the detection did
	// miss one there was no way to say so.
	FFNAVPT_LADDER,			// ladder endpoint — climb here
	FFNAVPT_GRENADES,		// grenade resupply, as distinct from ammo
	FFNAVPT_DOOR_TEAM,		// team; a door that only opens for one team
	FFNAVPT_DOOR_ONEWAY,	// team; a respawn gate: out only, owners only

	// Not a nav point at all: an ANTI-point.
	//
	// Everything the tagger, the auto-tagger and the analyzer derive is
	// recomputed from scratch at every map load, which means deleting one of
	// their tags is not something that can persist — it is back the next time
	// the map loads. This marker records the deletion instead, and is applied
	// after every derivation pass, so what it clears stays clear.
	FFNAVPT_ERASE,

	FFNAVPT_COUNT
};


// Numpad digits 1-9 map to one page at a time. Three pages covers every type
// with room to spare, and page 1 is deliberately the same nine keys the tool
// had before pages existed, so existing muscle memory still works.
#define FFNAVPT_SLOTS_PER_PAGE	9
#define FFNAVPT_PAGE_COUNT		4


//-----------------------------------------------------------------------------
// One authored marker. Stored by world position rather than nav area ID: area
// IDs are invalidated by nav_generate, and the whole point of this file is to
// survive a mesh regeneration so the map knowledge doesn't have to be redone.
//-----------------------------------------------------------------------------
struct FFNavPoint
{
	int    type;		// FFNavPointType
	int    team;		// TEAM_BLUE..TEAM_GREEN, or 0 for team-agnostic types
	Vector pos;
	float  radius;		// 0 = the one area under the point; >0 = all within

	// Facing, in degrees, captured from where you were looking when you placed
	// it. Meaningless for most types and ignored by them; for the handful that
	// are about direction rather than position — sniper, sentry, aim, pipetrap,
	// defend, jump — it IS the payload. "Stand here" and "stand here looking
	// down that corridor" are different instructions, and only one of them is
	// expressible with a position.
	float  yaw;
};


namespace FFNavBuilder
{
	//-------------------------------------------------------------------------
	// Lifecycle.
	//-------------------------------------------------------------------------

	// Read maps/<map>.ffnavpoints. Called from CFFNavMesh::OnServerActivate
	// before the tagger runs.
	void OnMapLoad( void );

	// Stamp every loaded marker onto the mesh. Called from
	// CFFBotTagger::TagAreasFromEntities after the entity pass and BEFORE
	// spawn-exit collection / incursion distances, so a hand-authored spawn
	// room feeds those computations exactly like an entity-derived one.
	void ApplyToMesh( CFFNavMesh *mesh );

	// Apply the 'erase' markers. Called LAST, after the auto-tagger and the
	// analyzer, because the whole point is to remove what those derived — an
	// erasure applied before them is overwritten by them within the same map
	// load, which looks exactly like the delete not working.
	//
	// Separate from ApplyToMesh rather than a case inside it for that reason
	// alone: it is not a different kind of marker, it is a different point in
	// the pipeline.
	int ApplyErasures( CFFNavMesh *mesh );

	// Per-frame. Draws the authoring overlay while the mode is on; does
	// nothing at all when it's off. Called from FFBotManager_Tick.
	void Tick( void );


	//-------------------------------------------------------------------------
	// Authoring.
	//-------------------------------------------------------------------------

	// Place a marker at the player's feet (traced to ground, so noclip works).
	// team < 0 means "use the authoring team". Applies immediately to the live
	// mesh and writes the sidecar. Returns false and prints why on failure.
	bool Place( CBasePlayer *player, int type, int team );

	// Remove the SINGLE nearest marker within radius of pos. Returns 1 if one
	// went away, 0 if there was nothing in range.
	//
	// Nearest rather than all-within-radius because deletion is usually a
	// correction: you placed one marker slightly wrong and want that one gone.
	// In a doorway with a door marker, a defend marker and two spawn corners
	// inside 96 units of each other, "delete everything near me" takes four
	// markers when you meant one, and three of them were fine.
	int  DeleteNearest( const Vector &pos, float radius );

	// Remove EVERY marker within radius. The old behaviour, still wanted for
	// clearing an area out wholesale.
	int  DeleteAllNear( const Vector &pos, float radius );

	void Save( void );
	void Clear( void );
	void PrintReport( void );

	// Re-read the sidecar and re-stamp. Use after hand-editing the file.
	void Reload( void );


	//-------------------------------------------------------------------------
	// Type / team helpers, shared by the console commands and the overlay.
	//-------------------------------------------------------------------------
	int         TypeFromName( const char *name );
	const char *NameForType( int type );
	const char *DescriptionForType( int type );
	bool        TypeNeedsTeam( int type );
	bool        TypeUsesYaw( int type );
	void        ColorForType( int type, int *r, int *g, int *b );

	int  GetSelectedType( void );
	void SetSelectedType( int type );
	void CycleSelectedType( int delta );

	// Numpad paging. Slot is 1..FFNAVPT_SLOTS_PER_PAGE; returns FFNAVPT_INVALID
	// for a slot the current page doesn't fill.
	int         GetPage( void );
	void        SetPage( int page );
	void        CyclePage( int delta );
	int         TypeForSlot( int page, int slot );
	const char *PageName( int page );
	void        PrintPage( void );

	// Resolves ff_nav_builder_team, falling back to the player's own team.
	int  ResolveAuthoringTeam( CBasePlayer *player );


	//-------------------------------------------------------------------------
	// Queries for bot behaviours.
	//
	// Marker attributes are readable straight off CFFNavArea (see the FF_NAV2_*
	// bits in ff_nav_area.h); these are for the cases where a behaviour wants
	// the authored world position rather than an area.
	//-------------------------------------------------------------------------

	// Nearest marker of the given type to 'from'. team < 0 matches any team.
	// Returns false if there are none.
	bool FindNearestPoint( int type, int team, const Vector &from, Vector *out );

	// As above, plus the recorded facing. For the types where the yaw is the
	// payload rather than a nicety — aim hints above all, where the position
	// only says where the instruction applies and the direction is the whole
	// instruction.
	bool FindNearestPointWithYaw( int type, int team, const Vector &from,
	                              Vector *outPos, float *outYaw );

	// All markers of a type, appended to outVector.
	void CollectPoints( int type, int team, CUtlVector< Vector > *outVector );

	// How many markers of a type exist on this map.
	int  CountPoints( int type );

	// How many markers of any type exist on this map.
	int  TotalCount( void );
}


#endif // FF_NAV_BUILDER_H
