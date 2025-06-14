//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_wander.h
// Wanderering/idle enemies for Squad Co-op mode
// Michael Booth, October 2009

#ifndef FF_BOT_WANDER_H
#define FF_BOT_WANDER_H

#ifdef FF_RAID_MODE

//-----------------------------------------------------------------------------
class CFFBotWander : public Action< CFFBot >
{
public:
	CFFBotWander( void );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );
	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );

	virtual EventDesiredResult< CFFBot > OnCommandAttack( CFFBot *me, CBaseEntity *victim );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?
	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?

	virtual const char *GetName( void ) const	{ return "Wander"; };

private:
	CountdownTimer m_visionTimer;
	CountdownTimer m_vocalizeTimer;
};


inline QueryResultType CFFBotWander::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;
}


inline QueryResultType CFFBotWander::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}


#endif FF_RAID_MODE

#endif // FF_BOT_WANDER_H
