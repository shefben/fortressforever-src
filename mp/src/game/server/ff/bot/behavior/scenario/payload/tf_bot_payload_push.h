//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_payload_push.h
// Push the cart to the goal
// Michael Booth, April 2010

#ifndef FF_BOT_PAYLOAD_PUSH_H
#define FF_BOT_PAYLOAD_PUSH_H

#include "Path/NextBotPathFollow.h"

class CFFBotPayloadPush : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;					// is it time to retreat?
	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "PayloadPush"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	float m_hideAngle;
};

#endif // FF_BOT_PAYLOAD_PUSH_H
