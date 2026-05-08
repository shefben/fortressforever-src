//========= Fortress Forever Bot =============================================//
//
// CFFBotAttack — Phase 4 combat action.
//
// Aims at the bot's primary known threat and fires the held weapon. Also
// strafes toward the threat if out of range; otherwise stops to fire.
// Returns DONE when no primary threat remains so the suspended parent
// behavior (Wander, eventually CTF/etc.) can resume.
//
//===========================================================================//

#ifndef FF_BOT_ATTACK_H
#define FF_BOT_ATTACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFBotAttack : public Action< CFFBot >
{
public:
	CFFBotAttack( void );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "Attack"; }

private:
	PathFollower m_chasePath;
	CountdownTimer m_repathTimer;
	CountdownTimer m_loseSightTimer;	// give up if threat unseen this long
};

#endif // FF_BOT_ATTACK_H
