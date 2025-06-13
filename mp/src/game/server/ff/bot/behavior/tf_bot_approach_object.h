//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_approach_object.h
// Move near/onto an object
// Michael Booth, February 2009

#ifndef FF_BOT_APPROACH_OBJECT_H
#define FF_BOT_APPROACH_OBJECT_H

#include "Path/NextBotPathFollow.h"

class CTFBotApproachObject : public Action< CTFBot >
{
public:
	CTFBotApproachObject( CBaseEntity *loot, float range = 10.0f );

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "ApproachObject"; };

private:
	CHandle< CBaseEntity > m_loot;		// what we are collecting
	float m_range;						// how close should we get
	PathFollower m_path;				// how we get to the loot
	CountdownTimer m_repathTimer;
};


#endif // FF_BOT_APPROACH_OBJECT_H
