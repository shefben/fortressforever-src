//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_escort_flag_carrier.h
// Escort the flag carrier to their destination
// Michael Booth, May 2011

#ifndef FF_BOT_ESCORT_FLAG_CARRIER_H
#define FF_BOT_ESCORT_FLAG_CARRIER_H


#include "Path/NextBotPathFollow.h"
#include "bot/behavior/ff_bot_melee_attack.h"


//-----------------------------------------------------------------------------
class CFFBotEscortFlagCarrier : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "EscortFlagCarrier"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CFFBotMeleeAttack m_meleeAttackAction;
};


#endif // FF_BOT_ESCORT_FLAG_CARRIER_H
