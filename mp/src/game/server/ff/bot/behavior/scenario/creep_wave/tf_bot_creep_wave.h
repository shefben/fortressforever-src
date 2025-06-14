//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_creep_wave.h
// Move in a "creep wave" to the next available control point to capture it
// Michael Booth, August 2010

#ifndef FF_BOT_CREEP_WAVE_H
#define FF_BOT_CREEP_WAVE_H

#ifdef FF_CREEP_MODE

#include "Path/NextBotPathFollow.h"
#include "Path/NextBotChasePath.h"


CFFBot *FindNearestEnemyCreep( CFFBot *me );


//-----------------------------------------------------------------------------
class CFFBotCreepWave : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnKilled( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnUnStuck( CFFBot *me );

	virtual const char *GetName( void ) const	{ return "CreepWave"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	IntervalTimer m_stuckTimer;
};



//-----------------------------------------------------------------------------
class CFFBotCreepAttack : public Action< CFFBot >
{
public:
	CFFBotCreepAttack( CFFPlayer *victim );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "CreepAttack"; };

private:
	CHandle< CFFPlayer > m_victim;
};


#endif // FF_CREEP_MODE

#endif // FF_BOT_CREEP_WAVE_H
