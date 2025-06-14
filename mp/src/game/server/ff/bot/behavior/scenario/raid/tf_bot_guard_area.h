//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_guard_area.h
// Defend an area against intruders
// Michael Booth, October 2009

#ifdef FF_RAID_MODE

#ifndef FF_BOT_GUARD_AREA_H
#define FF_BOT_GUARD_AREA_H

#include "Path/NextBotChasePath.h"

class CFFBotGuardArea : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?

	virtual EventDesiredResult< CFFBot > OnCommandApproach( CFFBot *me, const Vector &pos, float range );

	virtual const char *GetName( void ) const	{ return "GuardArea"; };

private:
	ChasePath m_chasePath;
	PathFollower m_pathToPoint;
	PathFollower m_pathToVantageArea;
	CountdownTimer m_vocalizeTimer;
	CountdownTimer m_repathTimer;
};

#endif // FF_RAID_MODE

#endif // FF_BOT_GUARD_AREA_H
