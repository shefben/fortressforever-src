//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_get_health.h
// Pick up any nearby health kit
// Michael Booth, May 2009

#ifndef FF_BOT_GET_HEALTH_H
#define FF_BOT_GET_HEALTH_H

#include "ff_powerup.h"

class CTFBotGetHealth : public Action< CTFBot >
{
public:
	static bool IsPossible( CTFBot *me );	// Return true if this Action has what it needs to perform right now

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual EventDesiredResult< CTFBot > OnStuck( CTFBot *me );
	virtual EventDesiredResult< CTFBot > OnMoveToSuccess( CTFBot *me, const Path *path );
	virtual EventDesiredResult< CTFBot > OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?

	virtual const char *GetName( void ) const	{ return "GetHealth"; };

private:
	PathFollower m_path;
	CHandle< CTFPowerup > m_healthKit;
	bool m_isGoalDispenser;
};


#endif // FF_BOT_GET_HEALTH_H
