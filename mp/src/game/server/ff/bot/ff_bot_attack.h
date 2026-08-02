//========= Fortress Forever Bot =============================================//
//
// CFFBotAttack — chase-the-threat sub-action.
//
// Mirrors TFBot's CTFBotAttack. Aim and fire are handled per-tick at
// CFFBotMainAction; this action only drives positioning. Uses ChasePath
// (LEAD_SUBJECT) when the target is visible (predicts target movement) and
// PathFollower to last-known position when LOS is lost. Equips the best
// weapon for the current threat each tick.
//
//===========================================================================//

#ifndef FF_BOT_ATTACK_H
#define FF_BOT_ATTACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "Path/NextBotChasePath.h"
#include "ff_bot.h"

class CFFBotAttack : public Action< CFFBot >
{
public:
	CFFBotAttack( void );
	virtual ~CFFBotAttack() { }

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "Attack"; }

private:
	PathFollower m_path;			// path to last-known position when LOS lost
	ChasePath m_chasePath;			// lead-the-subject path while visible
	CountdownTimer m_repathTimer;	// throttle re-paths to LKP
	CountdownTimer m_loseSightTimer;	// give up if threat unseen this long

	// Cover-peek state. Once we're in firing range with LOS, we cycle:
	//   peek phase  — exposed, fire for ~1s
	//   cover phase — hidden behind nav geometry for ~2s, then peek again
	// Without this the bot just stands wherever they stopped chasing and
	// eats incoming fire.
	bool           m_inCoverPhase;
	Vector         m_coverPos;
	CountdownTimer m_coverPhaseTimer;
	PathFollower   m_coverPath;
};

#endif // FF_BOT_ATTACK_H
