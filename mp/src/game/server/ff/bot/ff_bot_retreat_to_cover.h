//========= Fortress Forever Bot =============================================//
//
// CFFBotRetreatToCover — find a nearby nav area out of LOS from known
// threats and run there. Mid-level reusable action.
//
// Ported from hl2_src tf_bot_retreat_to_cover. Differences:
//   - FF nav files generally don't have analyzed PVS data, so visibility
//     uses UTIL_TraceLine between area centers instead of
//     CNavArea::IsPotentiallyVisible. Acceptable cost since the BFS
//     stops at ~1000u travel distance.
//   - Spy-cloak-while-retreating behavior is class-specific and belongs
//     in a spy-tier action, not this base retreat.
//
//===========================================================================//

#ifndef FF_BOT_RETREAT_TO_COVER_H
#define FF_BOT_RETREAT_TO_COVER_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotRetreatToCover : public Action< CFFBot >
{
public:
	CFFBotRetreatToCover( float hideDuration = -1.0f );
	CFFBotRetreatToCover( Action< CFFBot > *actionToChangeToOnceCoverReached );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "RetreatToCover"; }

private:
	float m_hideDuration;
	Action< CFFBot > *m_actionToChangeToOnceCoverReached;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CFFNavArea *m_coverArea;
	CountdownTimer m_waitInCoverTimer;

	CFFNavArea *FindCoverArea( CFFBot *me );
};

#endif // FF_BOT_RETREAT_TO_COVER_H
