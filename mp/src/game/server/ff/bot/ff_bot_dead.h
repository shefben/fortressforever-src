//========= Fortress Forever Bot =============================================//
//
// CFFBotDead — Action that holds the bot while LIFE_DEAD. On the dead → alive
// transition, replaces itself with a fresh CFFBotMainAction so every respawn
// gets a clean behavior-tree state (mirrors TFBot's CTFBotDead pattern).
//
//===========================================================================//

#ifndef FF_BOT_DEAD_H
#define FF_BOT_DEAD_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "ff_bot.h"

class CFFBotDead : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "Dead"; }
};

#endif // FF_BOT_DEAD_H
