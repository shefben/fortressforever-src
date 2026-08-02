//========= Fortress Forever Bot =============================================//
//
// CFFBotTaunt — stand still and (eventually) play a taunt animation.
//
// Ported from hl2_src tf_bot_taunt. FF doesn't have a TF-style HandleTauntCommand
// or TF_COND_TAUNTING — until FF's gesture/voice taunt API is wired up, this
// action is just a stand-still timer so callers can use it as a behavioral gate.
//
//===========================================================================//

#ifndef FF_BOT_TAUNT_H
#define FF_BOT_TAUNT_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "ff_bot.h"

class CFFBotTaunt : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "Taunt"; }

private:
	CountdownTimer m_startDelay;
	CountdownTimer m_tauntEndTimer;
	bool m_didTaunt;
};

#endif // FF_BOT_TAUNT_H
