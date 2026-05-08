//========= Fortress Forever Bot =============================================//
//
// CFFBotWander — Phase 3 placeholder Action. Picks a random nav area and
// walks to it; on arrival, picks another. Proves nav + path-following are
// wired correctly. Replaced by a real intention tree in Phase 5.
//
//===========================================================================//

#ifndef FF_BOT_WANDER_H
#define FF_BOT_WANDER_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotWander : public Action< CFFBot >
{
public:
	CFFBotWander();

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval ) OVERRIDE;

	// Triggered when locomotion can't make progress (e.g., bot is bumping
	// into a closed/team-restricted door). Discard current goal so the next
	// Update picks a different one — biased away from the blocked spot.
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE	{ return "Wander"; }

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_pickGoalTimer;
	Vector m_goalPos;
	bool m_hasGoal;

	// When non-zero, the next goal pick must be at least this far from
	// m_lastStuckPos. Reset to zero once a new goal is committed.
	Vector m_lastStuckPos;
	float  m_avoidStuckRadius;
};


#endif // FF_BOT_WANDER_H
