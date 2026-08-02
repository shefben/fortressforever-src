//========= Fortress Forever Bot =============================================//
//
// FFBotGameMode — what kind of map is this, what should a bot do on it, and
// which bot should be doing it.
//
// THE PROBLEM
//
// CFFBotCtfObjective was the only objective driver in the bot code and its
// states are CTF states: grab the enemy flag, carry it to our cap, return ours
// when it drops. base_ctf is 254 of the 599 shipped map scripts, so that is
// right for the plurality and wrong for everything else. Attack/defend, control
// point, invade, hunted and fortball maps all fell through the CTF ladder into
// "walk to a cap" or, failing that, wander.
//
// Three separate things were missing, and they are separate:
//
//   1. Nobody asked what kind of map this is. Every bot on every map ran the
//      same ladder.
//   2. Nothing understood that objectives can be SEQUENCED. rock2's flag is
//      behind a door the keycard opens; dustbowl's cp2 does not exist until
//      cp1 falls. A bot that only knows "walk to the nearest live goal" will
//      stand at a locked door for the rest of the round.
//   3. Nobody decided who defends. Class choice biased towards having at least
//      one engineer and one medic and was otherwise random, so a team of eight
//      was eight attackers. On an attack/defend map that loses the round no
//      matter how good the navigation is.
//
// WHAT THIS DOES ABOUT IT
//
// Mode detection reads the live goal registry rather than the map name or the
// script name. Both of those are available and both are wrong: map names are a
// naming convention, not data, and the script tells you which base file it
// included, not what state the round is in. What the registry knows — how many
// flags exist, which teams may touch them, which teams own capture points — is
// the actual shape of the game being played, and it updates when Lua changes
// its mind mid-round.
//
// Objective resolution is one mode-agnostic priority ladder. It replaces the
// three separate fallbacks at the bottom of the CTF state machine.
//
// Sequencing is deliberately NOT a hand-authored prerequisite table. There is
// no way to write one that covers 599 map scripts, and a wrong table is worse
// than none. Instead there are two general signals:
//
//   * Rank. A keycard outranks a flag, always. A keycard exists in order to
//     gate something; nothing else is what a keycard is for. This one rule is
//     the whole of rock2's sequencing and it needs no map knowledge.
//   * Failure. If a bot cannot compute a path to an objective, or spends long
//     enough pursuing one without getting closer, that objective is blacklisted
//     for that bot for a while and the ladder moves on. This is the general
//     case: whatever the prerequisite is, not being able to get there is how it
//     manifests, and it is observable without knowing what it was.
//
// Role assignment is a per-team quota, not a per-bot preference. The quota
// comes from the mode — an attack/defend defender team wants most of its bots
// holding ground, the attacking team wants almost none — and the bots closest
// to the thing being defended are the ones that get the defensive role.
//
//===========================================================================//

#ifndef FF_BOT_GAMEMODE_H
#define FF_BOT_GAMEMODE_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

class CFFBot;
class CBaseEntity;


//-----------------------------------------------------------------------------
// What kind of round is being played.
//
// Derived from the live goal registry, not from the map name. Re-derived
// whenever the registry's live set changes, so an attack/defend map that moves
// from phase to phase is re-read rather than assumed.
//-----------------------------------------------------------------------------
enum FFBotGameModeType
{
	FFGAMEMODE_UNKNOWN = 0,

	// Two or more teams each own a flag and a capture point. The classic.
	FFGAMEMODE_CTF,

	// One side has something to take or hold and the other side does not.
	// Capture points are declared for one team only.
	FFGAMEMODE_ATTACK_DEFEND,

	// Capture points nobody owns, contested by everyone. Dustbowl-style linear
	// push, king-of-the-hill, and anything else built on neutral caps.
	FFGAMEMODE_CONTROL_POINT,

	// Neutral flag (touchable by all) carried to team-owned capture points.
	FFGAMEMODE_INVADE,

	// A civilian has to reach an escape zone and everyone else has an opinion
	// about that.
	FFGAMEMODE_HUNTED,

	// Ball-carrying scoring maps.
	FFGAMEMODE_FORTBALL,

	// No live objectives at all. Deathmatch, conc maps, surf maps, anything
	// where the point is the shooting.
	FFGAMEMODE_DEATHMATCH,

	FFGAMEMODE_COUNT
};


//-----------------------------------------------------------------------------
// What a bot has been told to do about the map, as opposed to about the enemy
// standing in front of it.
//
// Quota-assigned per team; see AssignRoles. A bot's role is stable for at
// least FFBOT_ROLE_MIN_HOLD seconds so bots don't oscillate between attacking
// and defending every time someone dies.
//-----------------------------------------------------------------------------
enum FFBotRoleType
{
	FFROLE_OFFENSE = 0,	// go and take the thing
	FFROLE_DEFENSE,		// hold the thing we have
	FFROLE_SUPPORT,		// escort / heal / build; follows whoever needs it

	FFROLE_COUNT
};


//-----------------------------------------------------------------------------
// One resolved objective. `entity` may be NULL for a positional objective (a
// defend area, a wander target); `pos` is always valid.
//-----------------------------------------------------------------------------
enum FFBotObjectiveKind
{
	FFOBJ_NONE = 0,
	FFOBJ_TAKE_ITEM,		// walk onto a carriable and pick it up
	FFOBJ_DELIVER_ITEM,		// we are carrying one; take it to the cap
	FFOBJ_CAPTURE_POINT,	// stand in / touch a capture trigger
	FFOBJ_DEFEND_POINT,		// hold ground here
	FFOBJ_ESCORT,			// stay near this entity
	FFOBJ_ESCAPE,			// hunted VIP exit
};

struct FFBotObjective
{
	CBaseEntity *entity;	// may be NULL
	Vector       pos;
	int          kind;		// FFBotObjectiveKind
	int          goalClass;	// FFBotGoalClass of `entity`, or FFGOALCLASS_UNKNOWN
	const char  *why;		// static string, for ff_bot_gamemode_report
};


// Minimum time a bot keeps a role before the quota is allowed to move it.
#define FFBOT_ROLE_MIN_HOLD		15.0f


namespace FFBotGameMode
{
	//-------------------------------------------------------------------------
	// Lifecycle.
	//-------------------------------------------------------------------------

	// Called from CFFNavMesh::OnServerActivate, after FFBotLuaObjectives has
	// had its first look at the map.
	void OnMapLoad( void );

	// Per-frame from FFBotManager_Tick. Re-derives the mode when the goal
	// registry moves, ages out objective blacklists, and re-runs the role
	// quota. All three are internally throttled.
	void Tick( void );


	//-------------------------------------------------------------------------
	// Mode.
	//-------------------------------------------------------------------------

	int         Get( void );					// FFBotGameModeType
	const char *Name( int mode );

	// Attack/defend sides. Both return false in every other mode, so callers
	// can ask unconditionally.
	bool IsTeamAttacker( int team );
	bool IsTeamDefender( int team );

	// Force a re-derivation now rather than at the next throttle window. Used
	// by FFBotLuaObjectives when the live goal set changes.
	void InvalidateMode( void );


	//-------------------------------------------------------------------------
	// Objective resolution.
	//
	// One ladder for every mode. Returns false only when the map has no live
	// objective this bot can act on at all, in which case the caller should
	// wander.
	//-------------------------------------------------------------------------
	bool ResolveObjective( CFFBot *me, FFBotObjective *out );

	// Where should a defender for this team stand? Prefers hand-authored
	// FF_NAV2_DEFEND_* ground, then the thing actually being defended (own
	// flag / own cap / the contested neutral cap), then a choke between it and
	// the nearest enemy approach.
	bool ResolveDefendPosition( CFFBot *me, Vector *out );


	//-------------------------------------------------------------------------
	// Objective gating — the general prerequisite detector.
	//
	// Nothing here knows what a prerequisite IS. It only knows that a bot has
	// stopped being able to make progress on something, which is how every
	// unmet prerequisite in the game presents itself.
	//-------------------------------------------------------------------------

	// This bot could not path to `ent`, or gave up on it. Blacklists it for
	// this bot only, for FFBOT_OBJECTIVE_BLACKLIST_TIME seconds.
	void NoteObjectiveFailure( CFFBot *me, CBaseEntity *ent, const char *why );

	// Called every time the bot re-evaluates while pursuing `ent`, with the
	// straight-line distance remaining. Blacklists the objective when the bot
	// has failed to reduce that distance for long enough.
	void NoteObjectiveProgress( CFFBot *me, CBaseEntity *ent, float distanceRemaining );

	bool IsObjectiveBlacklisted( CFFBot *me, CBaseEntity *ent );

	// Drop this bot's blacklist. Called on respawn: a locked door may well
	// have opened while we were dead.
	void ClearObjectiveBlacklist( CFFBot *me );


	//-------------------------------------------------------------------------
	// Roles.
	//-------------------------------------------------------------------------

	// How many of `teamSize` bots this team's mode wants holding ground.
	int  GetDesiredDefenderCount( int team, int teamSize );

	// Re-run the per-team quota. Throttled internally; safe to call often.
	void AssignRoles( void );

	// Pick a class for a bot joining `team`, taking the mode and the team's
	// current offense/defense balance into account. Falls back to the caller's
	// own logic by returning 0 when it has no opinion (no team, no mode).
	int  PickClassForTeamNeed( int team );

	const char *RoleName( int role );


	//-------------------------------------------------------------------------
	// Diagnostics.
	//-------------------------------------------------------------------------
	void PrintReport( void );
}


#endif // FF_BOT_GAMEMODE_H
