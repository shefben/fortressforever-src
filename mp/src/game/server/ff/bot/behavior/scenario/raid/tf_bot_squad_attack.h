//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_squad_attack.h
// Move and attack as a small, cohesive, group
// Michael Booth, October 2009

#ifndef FF_BOT_SQUAD_ATTACK_H
#define FF_BOT_SQUAD_ATTACK_H

#ifdef FF_RAID_MODE

#include "Path/NextBotPathFollow.h"
#include "Path/NextBotChasePath.h"


//-----------------------------------------------------------------------------
class CFFBotSquadAttack : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	QueryResultType	ShouldRetreat( const INextBot *me ) const;

	virtual const char *GetName( void ) const	{ return "SquadPatrol"; };

private:
	CountdownTimer m_vocalizeTimer;
	PathFollower m_path;
	ChasePath m_chasePath;
	CHandle< CTFPlayer > m_victim;
	CountdownTimer m_victimConsiderTimer;

	CFFBot *GetSquadLeader( CFFBot *me ) const;
};

inline QueryResultType CFFBotSquadAttack::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}

#endif // FF_RAID_MODE

#endif // FF_BOT_SQUAD_ATTACK_H
