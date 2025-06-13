//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_nav_ent_wait.h
// Wait for awhile, as directed by nav entity
// Michael Booth, September 2009

#ifndef FF_BOT_NAV_ENT_WAIT_H
#define FF_BOT_NAV_ENT_WAIT_H

#include "NextBot/NavMeshEntities/func_nav_prerequisite.h"


class CTFBotNavEntWait : public Action< CTFBot >
{
public:
	CTFBotNavEntWait( const CFuncNavPrerequisite *prereq );

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "NavEntWait"; };

private:
	CHandle< CFuncNavPrerequisite > m_prereq;
	CountdownTimer m_timer;
};


#endif // FF_BOT_NAV_ENT_WAIT_H
