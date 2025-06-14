//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_payload_guard.h
// Guard the payload and keep the attackers from getting near it
// Michael Booth, April 2010

#ifndef FF_BOT_PAYLOAD_GUARD_H
#define FF_BOT_PAYLOAD_GUARD_H

#include "Path/NextBotPathFollow.h"

class CFFBotPayloadGuard : public Action< CFFBot >
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

	virtual const char *GetName( void ) const	{ return "PayloadGuard"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	
	Vector m_vantagePoint;
	CountdownTimer m_vantagePointTimer;
	Vector FindVantagePoint( CFFBot *me, CBaseEntity *cart );

	CountdownTimer m_moveToBlockTimer;

};

#endif // FF_BOT_PAYLOAD_GUARD_H
