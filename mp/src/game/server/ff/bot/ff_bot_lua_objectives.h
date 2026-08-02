//========= Fortress Forever Bot =============================================//
//
// FFBotLuaObjectives — the bridge between FF's Lua gameplay layer and the bots.
//
// THE PROBLEM
//
// Almost nothing about an FF map's objectives is expressed in the BSP. Flags,
// capture points, keys, gas suits, AvD phase gates — all of it is created and
// driven by Lua at runtime. A human sees this correctly because the HUD is fed
// from the same Lua state. The bots did not, because they scanned gEntList once
// at level init and never looked again.
//
// Three specific failures that produced:
//
//   1. Lua state changes were invisible. base_ad.lua (dustbowl, avanti, ksour,
//      napoli, vertigo, ...) calls flag:Restore() / flag:Remove() as the round
//      moves through its phases. On phase 1, cp2 and cp3 don't exist as far as
//      a player is concerned — but the bots had all six flags and caps tagged
//      as live from level init, and happily walked to objectives that weren't
//      there.
//
//   2. trigger_ff_script was never looked at. CFuncFFScript carries the exact
//      same GetBotGoalType() / GetBotTeamFlags() / IsActive() interface as
//      CFFInfoScript, and includes/base.lua calls SetBotGoalInfo on it. The bot
//      code only ever walked CLASS_INFOSCRIPT, so every trigger-based goal in
//      the game was invisible.
//
//   3. Touch permissions were never consulted. touchflags/disallowtouchflags
//      say which teams and classes may pick an item up. A red bot on rock2 will
//      cheerfully path across the map to a key it is physically incapable of
//      touching.
//
// THE FIX
//
// FF already contains the right event bus, fully wired, and completely dead.
// Omnibot::Notify_GoalInfo, Notify_ItemPickedUp, Notify_ItemDropped,
// Notify_ItemReturned, Notify_ItemRemove, Notify_ItemRestore and
// Notify_ItemRespawned are called from ff_item_flag.cpp and triggers.cpp at
// precisely the moments that matter — including from SetBotGoalInfo, which Lua
// calls as it spawns each entity. Every one of them opens with
//
//     if( !IsOmnibotLoaded() ) return;
//
// so with Omnibot absent the whole stream was discarded.
//
// This module taps that stream ahead of the Omnibot gate and keeps a live
// registry of every Lua-declared goal in the map, with its current state. When
// the live set changes, nav tagging is re-derived. Bot code queries the
// registry instead of walking gEntList, which is both correct and cheaper.
//
//===========================================================================//

#ifndef FF_BOT_LUA_OBJECTIVES_H
#define FF_BOT_LUA_OBJECTIVES_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "ehandle.h"
#include "tier1/utlvector.h"

class CBaseEntity;
class CBasePlayer;
class CFFNavMesh;


//-----------------------------------------------------------------------------
// What a goal entity actually IS, in our vocabulary rather than Omnibot's.
//
// Omnibot::BotGoalTypes has nine values and is the entire vocabulary Lua has
// for describing a goal. It cannot say "gas suit", "keycard" or "ball", so
// those arrive either as kNone or mislabelled as something adjacent.
//
// This enum is a superset. The first block maps 1:1 onto the Omnibot types and
// is populated straight from a declared botgoaltype. The second block can only
// ever be reached by inference — model, then entity name — and is the reason
// this classifier exists at all.
//
// Prior art: FoxBot solved the identical problem for TFC by whitelisting three
// model paths at map load (dll.cpp, pfnKeyValue interception:
// models/flag.mdl, models/keycard.mdl, models/ball.mdl) and substring-matching
// "pack" for backpacks. Twenty years of TFC ran on that. FF's models are more
// specific than TFC's, so we can go further than FoxBot could — notably, FF's
// gas suit has its own model where TFC's was a generic item_tfgoal, which is
// why FoxBot never identified it and simply let its bots die in rock2's gas.
//-----------------------------------------------------------------------------
enum FFBotGoalClass
{
	FFGOALCLASS_UNKNOWN = 0,

	// Declared vocabulary — 1:1 with Omnibot::BotGoalTypes.
	FFGOALCLASS_FLAG,
	FFGOALCLASS_CAP,
	FFGOALCLASS_AMMO,
	FFGOALCLASS_ARMOR,
	FFGOALCLASS_HEALTH,
	FFGOALCLASS_GRENADES,
	FFGOALCLASS_HUNTED_ESCAPE,
	FFGOALCLASS_TRAINER_SPAWN,

	// Inferred-only vocabulary. Lua has no way to declare any of these.
	FFGOALCLASS_KEYCARD,		// carriable key (rock2). Flag-like.
	FFGOALCLASS_BALL,			// fortball / basketball. Flag-like.
	FFGOALCLASS_HAZARD_GEAR,	// gas suit / protective equipment
	FFGOALCLASS_BACKPACK,		// generic pack: mixed ammo/health/armor

	FFGOALCLASS_COUNT
};


//-----------------------------------------------------------------------------
// One Lua-declared goal entity, plus everything we've learned about its current
// state. Held by EHANDLE: Lua removes and restores these constantly, and a raw
// pointer would dangle the first time a map changed phase.
//-----------------------------------------------------------------------------
struct FFBotLuaGoal
{
	EHANDLE	entity;

	int		goalType;		// Omnibot::BotGoalTypes as declared by Lua
	int		goalClass;		// FFBotGoalClass — declared, or inferred
	int		teamFlags;		// raw bot team-flag bitfield from Lua
	bool	isTrigger;		// CFuncFFScript rather than CFFInfoScript
	bool	classInferred;	// true = we guessed this, Lua didn't say

	// Live state, refreshed by events and by the reconcile pass.
	bool	isLive;			// active, not removed — a real objective right now
	bool	isCarried;
	bool	isDropped;

	Vector	worldPos;		// where it is at this instant
	Vector	homePos;		// where it was when Lua first declared it
};


namespace FFBotLuaObjectives
{
	//-------------------------------------------------------------------------
	// Event sinks. Called from omnibot_interface.cpp, ahead of the
	// IsOmnibotLoaded() early-out that would otherwise swallow them.
	//
	// OnGoalInfo fires from CFFInfoScript::SetBotGoalInfo and
	// CFuncFFScript::SetBotGoalInfo, which Lua calls while spawning entities.
	// That is the moment the map tells us what everything is, and it happens
	// during level load — before the nav mesh has been activated, so this must
	// be safe to call with no mesh present.
	//-------------------------------------------------------------------------
	void OnGoalInfo( CBaseEntity *ent, int goalType, int teamFlags );

	// One sink for pickup / drop / return / respawn / remove / restore. They
	// all mean the same thing to us: this entity's state is no longer what we
	// cached, go and look.
	void OnGoalStateChanged( CBaseEntity *ent );


	//-------------------------------------------------------------------------
	// Lifecycle.
	//-------------------------------------------------------------------------

	// Reconcile cached state, purge dead handles, sweep for goals that never
	// announced themselves, and re-tag the nav mesh if the live set moved.
	// Called every frame from FFBotManager_Tick; internally throttled.
	void Tick( void );

	// Called from CFFNavMesh::OnServerActivate. Does NOT clear the registry —
	// by this point Lua has already spawned its entities and told us about
	// them. Map changes are detected inside OnGoalInfo instead.
	void OnMapLoad( void );

	// Force a full rescan of gEntList for goal entities. Belt and braces for
	// anything that acquired a goal type without going through SetBotGoalInfo.
	void Rescan( void );


	//-------------------------------------------------------------------------
	// Queries. These replace the repeated gEntList walks that used to live in
	// ff_bot_helpers / ff_bot_ctf / ff_bot_intel.
	//-------------------------------------------------------------------------

	int                 Count( void );
	const FFBotLuaGoal *Get( int index );

	// Is this goal declared for `ffTeam`? A goal with no team flags at all is
	// declared for everyone, which is how every consumer has always read it.
	//
	// Public because game-mode detection has to reason about ownership across
	// the whole registry — "which teams may take this flag" is exactly the
	// question that tells CTF apart from invade.
	bool GoalIsForTeam( const FFBotLuaGoal *goal, int ffTeam );

	// Nearest live goal of a type. team < 0 matches any; otherwise the goal
	// must be declared for that team. If forBot is non-NULL the goal must also
	// be one that bot is permitted to touch.
	CBaseEntity *FindNearestGoal( int goalType, int team, const Vector &from,
	                              CBasePlayer *forBot );

	void CollectGoals( int goalType, int team, CUtlVector< CBaseEntity * > *out );

	// Same, keyed on FFBotGoalClass rather than the Omnibot type. This is the
	// query to use for anything the declared vocabulary can't express — gas
	// suits, keycards, balls.
	CBaseEntity *FindNearestOfClass( int goalClass, int team, const Vector &from,
	                                 CBasePlayer *forBot );

	void CollectOfClass( int goalClass, int team, CUtlVector< CBaseEntity * > *out );

	int         CountOfClass( int goalClass );
	const char *GoalClassName( int goalClass );

	// True when this class behaves like a flag: carriable, and taking it to
	// somewhere else is the point. Covers FLAG, KEYCARD and BALL, which are
	// mechanically identical and differ only in what the map calls them.
	bool IsFlagLikeClass( int goalClass );

	// Is this entity currently a real objective? False for removed, inactive,
	// or unknown entities.
	bool IsGoalLive( CBaseEntity *ent );

	// Does this bot pass the entity's touchflags / disallowtouchflags?
	// True for anything that isn't a CFFInfoScript — trigger permissions are
	// evaluated by a Lua allowed() callback we can't run from here.
	bool CanBotTouch( CBaseEntity *goalEnt, CBasePlayer *bot );

	// The objective the map itself is pointing this player at, via Lua's
	// UpdateObjectiveIcon / UpdateTeamObjectiveIcon. This is exactly what the
	// HUD arrow shows a human, so when a map sets it, it outranks anything we
	// would work out for ourselves. NULL when the map doesn't use it.
	CBaseEntity *GetScriptedObjective( CBasePlayer *player );

	void PrintReport( void );
}


#endif // FF_BOT_LUA_OBJECTIVES_H
