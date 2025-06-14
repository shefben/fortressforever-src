//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_fetch_flag.h
// Go get the flag!
// Michael Booth, May 2011

#ifndef FF_BOT_FETCH_FLAG_H
#define FF_BOT_FETCH_FLAG_H

#include "Path/NextBotPathFollow.h"


//-----------------------------------------------------------------------------
class CFFBotFetchFlag : public Action< CFFBot >
{
public:
	#define TEMPORARY_FLAG_FETCH true
	CFFBotFetchFlag( bool isTemporary = false );
	virtual ~CFFBotFetchFlag() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;
	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;

	virtual const char *GetName( void ) const	{ return "FetchFlag"; };

private:
	bool m_isTemporary;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
};


#endif // FF_BOT_FETCH_FLAG_H
