//========= Fortress Forever Bot =============================================//
//
// CFFBotSeekAndDestroy — patrol the map, engage enemies on sight.
//
// Default fallback when no objective behavior wants to drive the bot. Picks
// a random "interesting place" (enemy spawn threshold, enemy flag rest area,
// enemy capture point) and walks there; suspends to CFFBotAttack on threat.
//
// Ported from hl2_src tf_bot_seek_and_destroy. Differences:
//   - No control-point logic (FF doesn't use TF's territory system)
//   - Goal pool draws from CFFNavMesh's per-team flag/cap area lists
//
//===========================================================================//

#ifndef FF_BOT_SEEK_AND_DESTROY_H
#define FF_BOT_SEEK_AND_DESTROY_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"
#include "ff_nav_area.h"

class CFFBotSeekAndDestroy : public Action< CFFBot >
{
public:
	CFFBotSeekAndDestroy( float duration = -1.0f );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual ActionResult< CFFBot > OnResume( CFFBot *me, Action< CFFBot > *interruptingAction ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "SeekAndDestroy"; }

private:
	CFFNavArea *m_goalArea;
	CFFNavArea *ChooseGoalArea( CFFBot *me );
	void RecomputeSeekPath( CFFBot *me );

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_giveUpTimer;
};

#endif // FF_BOT_SEEK_AND_DESTROY_H
