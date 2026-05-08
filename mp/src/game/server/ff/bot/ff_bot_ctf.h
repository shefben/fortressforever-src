//========= Fortress Forever Bot =============================================//
//
// CFFBotCtfObjective — top-level objective Action for CTF-style maps.
//
// State machine that picks the bot's current goal each evaluation tick:
//   - STATE_CARRY_FLAG     : I'm carrying an enemy flag → run to my cap point.
//   - STATE_RETURN_OWN_FLAG: my team's flag is dropped → run to it (touching
//                            it auto-returns to base).
//   - STATE_GRAB_FLAG      : an enemy flag is at home / dropped → run to it.
//   - STATE_WANDER         : no CTF goal applicable → pick a random nav area
//                            (also the path on non-CTF maps).
//
// Per-tick threat handling, aim, fire, and stuck-mashing all happen in
// CFFBotMainAction; this Action only drives the path goal.
//
//===========================================================================//

#ifndef FF_BOT_CTF_H
#define FF_BOT_CTF_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFInfoScript;

class CFFBotCtfObjective : public Action< CFFBot >
{
public:
	CFFBotCtfObjective();

	virtual ActionResult< CFFBot >        OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot >        Update( CFFBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< CFFBot >  OnStuck( CFFBot *me ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "CtfObjective"; }

private:
	enum State
	{
		STATE_NONE,
		STATE_GRAB_FLAG,
		STATE_CARRY_FLAG,
		STATE_RETURN_OWN_FLAG,
		STATE_DEFEND_OWN_FLAG,	// engineers / defensive roles: hold near our flag
		STATE_DEFEND_AT_CAP,	// snipers, hwguys: post up at our cap point
		STATE_ESCORT_CARRIER,	// teammate is carrying enemy flag — escort them
		STATE_RETREAT,			// low HP — fall back to our spawn area
		STATE_PUSH_OBJECTIVE,	// AvD/no-flag maps: head to nearest cap point
		STATE_VIP_RUN,			// Hunted: I'm the civilian, run to escape area
		STATE_ESCORT_VIP,		// Hunted: stay near my team's civilian
		STATE_SEEK_CURE,		// I'm infected — find a friendly medic
		STATE_WANDER,
	};

	// Re-evaluate game state and pick the right state + goal position.
	// Returns the new state; fills outGoalPos and outTargetEnt accordingly.
	State EvaluateState( CFFBot *me, Vector *outGoalPos, EHANDLE *outTargetEnt ) const;

	// Pick a random reachable nav-area center for STATE_WANDER, honoring our
	// stuck-avoidance pos/radius so we don't beat our head on the same wall.
	bool PickWanderGoal( const Vector &myPos, Vector *outGoalPos ) const;

	State        m_state;
	Vector       m_goalPos;
	EHANDLE      m_targetEntity;	// the flag/cap entity we're aiming for (if any)
	PathFollower m_path;

	CountdownTimer m_evaluateTimer;	// how often to re-check game state
	CountdownTimer m_repathTimer;	// how often to recompute the path
	CountdownTimer m_wanderPickTimer;

	Vector m_lastStuckPos;
	float  m_avoidStuckRadius;

	// Track death/respawn so we can force immediate re-evaluation when the
	// bot transitions from dead → alive. Without this, post-respawn bots
	// stand around facing whatever direction the stale (pre-death) path
	// goal pointed for up to 1.25s until the next eval-timer tick.
	bool   m_wasAlive;

	// Set true on dead → alive transition. After the next successful
	// m_path.Compute, we snap eye angles toward the first path segment so
	// PlayerLocomotion::Approach (which derives forward-vs-back from
	// view-dot-waypoint) sees the view aligned with travel direction and
	// presses IN_FORWARD instead of IN_BACK. Cleared once snapped.
	// Without this, bots respawn facing whatever direction the spawn entity
	// or last activity pointed them and Approach() walks them backward into
	// containers / walls.
	bool   m_needsAngleSnap;
};

#endif // FF_BOT_CTF_H
