//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_payload_block.h
// Prevent the other team from moving the cart
// Michael Booth, April 2010

#ifndef FF_BOT_PAYLOAD_BLOCK_H
#define FF_BOT_PAYLOAD_BLOCK_H

#include "Path/NextBotPathFollow.h"

class CFFBotPayloadBlock : public Action< CFFBot >
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

	virtual const char *GetName( void ) const	{ return "PayloadBlock"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CountdownTimer m_giveUpTimer;
};

#endif // FF_BOT_PAYLOAD_BLOCK_H
