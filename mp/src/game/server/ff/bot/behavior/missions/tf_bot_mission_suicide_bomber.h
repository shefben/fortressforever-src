//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_mission_suicide_bomber.h
// Move to target and explode
// Michael Booth, October 2011

#ifndef FF_BOT_MISSION_SUICIDE_BOMBER_H
#define FF_BOT_MISSION_SUICIDE_BOMBER_H

#include "Path/NextBotPathFollow.h"

class CFFBotMissionSuicideBomber : public Action< CFFBot >
{
public:
	CFFBotMissionSuicideBomber( void );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnKilled( CFFBot *me, const CTakeDamageInfo &info );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "MissionSuicideBomber"; };

private:
	CHandle< CBaseEntity > m_victim;	// the victim we are trying to destroy
	Vector m_lastKnownVictimPosition;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CountdownTimer m_talkTimer;
	CountdownTimer m_detonateTimer;

	void StartDetonate( CFFBot *me, bool bWasSuccessful = false, bool bWasKilled = false );
	void Detonate( CFFBot *me );
	bool m_bHasDetonated;
	bool m_bWasSuccessful;
	bool m_bWasKilled;

	int m_consecutivePathFailures;

	Vector m_vecDetLocation;
};


#endif // FF_BOT_MISSION_SUICIDE_BOMBER_H
