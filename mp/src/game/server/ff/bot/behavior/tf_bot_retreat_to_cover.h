//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_retreat_to_cover.h
// Retreat to local cover from known threats
// Michael Booth, June 2009

#ifndef FF_BOT_RETREAT_TO_COVER_H
#define FF_BOT_RETREAT_TO_COVER_H

class CFFBotRetreatToCover : public Action< CFFBot >
{
public:
	CFFBotRetreatToCover( float hideDuration = -1.0f );
	CFFBotRetreatToCover( Action< CFFBot > *actionToChangeToOnceCoverReached );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "RetreatToCover"; };

private:
	float m_hideDuration;
	Action< CFFBot > *m_actionToChangeToOnceCoverReached;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CTFNavArea *m_coverArea;
	CountdownTimer m_waitInCoverTimer;

	CTFNavArea *FindCoverArea( CFFBot *me );
};



#endif // FF_BOT_RETREAT_TO_COVER_H
