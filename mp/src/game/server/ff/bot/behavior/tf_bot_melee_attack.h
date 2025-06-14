//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_melee_attack.h
// Attack a threat with out melee weapon
// Michael Booth, February 2009

#ifndef FF_BOT_MELEE_ATTACK_H
#define FF_BOT_MELEE_ATTACK_H

#include "Path/NextBotChasePath.h"

class CFFBotMeleeAttack : public Action< CFFBot >
{
public:
	CFFBotMeleeAttack( float giveUpRange = -1.0f );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "MeleeAttack"; };

private:
	float m_giveUpRange;			// if non-negative and if threat is farther than this, give up our melee attack
	ChasePath m_path;
};

#endif // FF_BOT_MELEE_ATTACK_H
