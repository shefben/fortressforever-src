//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_lurk.h
// Wait for victims
// Michael Booth, September 2011

#ifndef FF_BOT_SPY_LURK_H
#define FF_BOT_SPY_LURK_H

#include "Path/NextBotPathFollow.h"

class CFFBotSpyLurk : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "SpyLurk"; };

private:
	CountdownTimer m_lurkTimer;
};


#endif // FF_BOT_SPY_LURK_H
