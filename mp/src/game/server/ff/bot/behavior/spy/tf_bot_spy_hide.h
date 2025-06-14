//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_hide.h
// Move to a hiding spot
// Michael Booth, September 2011

#ifndef FF_BOT_SPY_HIDE
#define FF_BOT_SPY_HIDE

#include "Path/NextBotPathFollow.h"

class CFFBotSpyHide : public Action< CFFBot >
{
public:
	CFFBotSpyHide( CTFPlayer *victim = NULL );
	virtual ~CFFBotSpyHide() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "SpyHide"; };

private:
	CHandle< CTFPlayer > m_initialVictim;

	HidingSpot *m_hidingSpot;
	bool FindHidingSpot( CFFBot *me );
	CountdownTimer m_findTimer;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	bool m_isAtGoal;

	float m_incursionThreshold;

	CountdownTimer m_talkTimer;
};

#endif // FF_BOT_SPY_HIDE
