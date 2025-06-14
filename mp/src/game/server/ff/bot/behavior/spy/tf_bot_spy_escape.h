//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_escape.h
// Flee!
// Michael Booth, June 2010

#ifndef FF_BOT_SPY_ESCAPE_H
#define FF_BOT_SPY_ESCAPE_H

#include "Path/NextBotPathFollow.h"

class CFFBotSpyEscape : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "SpyEscape"; };

private:
};

#endif // FF_BOT_SPY_ESCAPE_H
