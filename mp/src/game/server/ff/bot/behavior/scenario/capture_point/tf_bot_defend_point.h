//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_defend_point.h
// Move to and defend current point from capture
// Michael Booth, February 2009

#ifndef FF_BOT_DEFEND_POINT_H
#define FF_BOT_DEFEND_POINT_H

#include "Path/NextBotPathFollow.h"
#include "Path/NextBotChasePath.h"

class CFFBotDefendPoint : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual EventDesiredResult< CFFBot > OnTerritoryContested( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual const char *GetName( void ) const	{ return "DefendPoint"; };

private:
	PathFollower m_path;				// for moving to a defense position
	ChasePath m_chasePath;				// for chasing enemies

	CountdownTimer m_repathTimer;
	CountdownTimer m_lookAroundTimer;
	CountdownTimer m_idleTimer;

	CTFNavArea *m_defenseArea;
	CTFNavArea *SelectAreaToDefendFrom( CFFBot *me );

	bool IsPointThreatened( CFFBot *me );
	bool WillBlockCapture( CFFBot *me ) const;
	bool m_isAllowedToRoam;
};


#endif // FF_BOT_DEFEND_POINT_H
