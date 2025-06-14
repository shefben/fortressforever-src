//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_body.h
// Team Fortress NextBot body interface
// Michael Booth, May 2010

#ifndef FF_BOT_BODY_H
#define FF_BOT_BODY_H

#include "NextBot/Player/NextBotPlayerBody.h"

//----------------------------------------------------------------------------
class CFFBotBody : public PlayerBody
{
public:
	CFFBotBody( INextBot *bot ) : PlayerBody( bot )
	{
	}

	virtual ~CFFBotBody() { }

	virtual float GetHeadAimTrackingInterval( void ) const;			// return how often we should sample our target's position and velocity to update our aim tracking, to allow realistic slop in tracking
};

#endif // FF_BOT_BODY_H
