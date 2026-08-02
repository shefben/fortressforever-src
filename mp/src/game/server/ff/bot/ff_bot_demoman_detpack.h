//========= Fortress Forever Bot =============================================//
//
// CFFBotDemomanDetpack — offensive detpack placement.
//
// FF-only ability with no TF analogue. The demoman drops a 5-second-fused
// detpack at an enemy chokepoint then retreats to safe distance to let it
// blow. Used to clear sentry nests, breach detpackable doors, and deny
// chokes during pushes.
//
// State machine:
//   APPROACH  — walking to chosen choke
//   PLACE     — at the spot; Command_BuildDetpack and wait for the
//                buildable to spawn
//   RETREAT   — detpack exists; sprint >= FFBOT_DETPACK_SAFE_DIST away
//   COOK      — at safe distance; hold position until detpack detonates
//
// Used opportunistically (suspended into) when:
//   - Demoman class
//   - Has a buildable detpack available (no current detpack out, alive)
//   - Not in active combat (suspends to attack first if a threat exists)
//   - Cooldown elapsed since last placement
//
//===========================================================================//

#ifndef FF_BOT_DEMOMAN_DETPACK_H
#define FF_BOT_DEMOMAN_DETPACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotDemomanDetpack : public Action< CFFBot >
{
public:
	CFFBotDemomanDetpack( const Vector &targetPos );

	// Static gate: caller checks this before suspending to us.
	static bool IsPossible( CFFBot *me );

	// Static target picker — returns vec3_origin if no good choke exists.
	// Picks an area in enemy territory that's a real choke (FF_NAV_SENTRY_SPOT
	// preferred, otherwise highest-incursion reachable area within ~2500u).
	static Vector PickTargetChoke( CFFBot *me );

	virtual ActionResult< CFFBot >        OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot >        Update( CFFBot *me, float interval ) OVERRIDE;
	virtual const char                   *GetName( void ) const OVERRIDE { return "DemomanDetpack"; }

	// Don't retreat — we're committed to the placement run.
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;

private:
	enum State
	{
		STATE_APPROACH,
		STATE_PLACE,
		STATE_RETREAT,
		STATE_COOK,
	};

	State           m_state;
	Vector          m_targetPos;
	Vector          m_detpackPos;	// where we actually placed; used for retreat distance
	PathFollower    m_path;
	CountdownTimer  m_repathTimer;
	CountdownTimer  m_giveupTimer;	// abort the whole action if it stalls
	CountdownTimer  m_placeAttemptTimer;
};

#endif // FF_BOT_DEMOMAN_DETPACK_H
