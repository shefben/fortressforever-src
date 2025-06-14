//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_mob_rush.h
// A member of a rushing mob of melee attackers
// Michael Booth, October 2009

#ifndef FF_BOT_MOB_RUSH_H
#define FF_BOT_MOB_RUSH_H

#ifdef FF_RAID_MODE

#include "Path/NextBotChasePath.h"


//-----------------------------------------------------------------------------
class CFFBotMobRush : public Action< CFFBot >
{
public:
	CFFBotMobRush( CFFPlayer *victim, float reactionTime = 0.0f );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );
	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	QueryResultType	ShouldRetreat( const INextBot *me ) const;

	virtual const char *GetName( void ) const	{ return "MobRush"; };

private:
	CHandle< CFFPlayer > m_victim;
	CountdownTimer m_reactionTimer;
	CountdownTimer m_tauntTimer;
	CountdownTimer m_vocalizeTimer;
	ChasePath m_path;
};

#endif // FF_RAID_MODE

#endif // FF_BOT_MOB_RUSH_H
