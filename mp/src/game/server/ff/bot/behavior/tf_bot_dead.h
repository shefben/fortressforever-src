//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_dead.h
// Push up daisies
// Michael Booth, May 2009

#ifndef FF_BOT_DEAD_H
#define FF_BOT_DEAD_H

#include "Path/NextBotChasePath.h"

class CFFBotDead : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "Dead"; };

private:
	IntervalTimer m_deadTimer;
};

#endif // FF_BOT_DEAD_H
