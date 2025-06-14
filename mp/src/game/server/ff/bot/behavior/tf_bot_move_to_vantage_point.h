//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_move_to_vantage_point.h
// Move to a position where at least one enemy is visible
// Michael Booth, November 2009

#ifndef FF_BOT_MOVE_TO_VANTAGE_POINT_H
#define FF_BOT_MOVE_TO_VANTAGE_POINT_H

#include "Path/NextBotChasePath.h"

class CFFBotMoveToVantagePoint : public Action< CFFBot >
{
public:
	CFFBotMoveToVantagePoint( float maxTravelDistance = 2000.0f );
	virtual ~CFFBotMoveToVantagePoint() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual const char *GetName( void ) const	{ return "MoveToVantagePoint"; };

private:
	float m_maxTravelDistance;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CTFNavArea *m_vantageArea;
};

#endif // FF_BOT_MOVE_TO_VANTAGE_POINT_H
