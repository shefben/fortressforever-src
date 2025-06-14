//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_attack_flag_defenders.h
// Attack enemies that are preventing the flag from reaching its destination
// Michael Booth, May 2011

#ifndef FF_BOT_ATTACK_FLAG_DEFENDERS_H
#define FF_BOT_ATTACK_FLAG_DEFENDERS_H

#include "Path/NextBotPathFollow.h"
#include "bot/behavior/ff_bot_attack.h"


//-----------------------------------------------------------------------------
class CFFBotAttackFlagDefenders : public CFFBotAttack
{
public:
	CFFBotAttackFlagDefenders( float minDuration = -1.0f );
	virtual ~CFFBotAttackFlagDefenders() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "AttackFlagDefenders"; }

private:
	CountdownTimer m_minDurationTimer;
	CountdownTimer m_watchFlagTimer;
	CHandle< CTFPlayer > m_chasePlayer;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
};


#endif // FF_BOT_ATTACK_FLAG_DEFENDERS_H
