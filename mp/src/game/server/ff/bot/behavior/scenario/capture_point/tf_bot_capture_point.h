//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_capture_point.h
// Move to and try to capture the next point
// Michael Booth, February 2009

#ifndef FF_BOT_CAPTURE_POINT_H
#define FF_BOT_CAPTURE_POINT_H

#include "Path/NextBotPathFollow.h"

class CFFBotCapturePoint : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual EventDesiredResult< CFFBot > OnTerritoryContested( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;					// is it time to retreat?
	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "CapturePoint"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
};

#endif // FF_BOT_CAPTURE_POINT_H
