//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_behavior.h
// Team Fortress NextBot behaviors
// Michael Booth, February 2009

#ifndef FF_BOT_BEHAVIOR_H
#define FF_BOT_BEHAVIOR_H

#include "Path/NextBotPathFollow.h"

class CFFBotMainAction : public Action< CFFBot >
{
public:
	virtual Action< CFFBot > *InitialContainedAction( CFFBot *me );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnKilled( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );
	virtual EventDesiredResult< CFFBot > OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result = NULL );
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	virtual EventDesiredResult< CFFBot > OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;
	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?
	virtual QueryResultType	ShouldHurry( const INextBot *me ) const;							// are we in a hurry?

	virtual Vector SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const;		// given a subject, return the world space position we should aim at
	virtual QueryResultType IsPositionAllowed( const INextBot *me, const Vector &pos ) const;

	virtual const CKnownEntity *	SelectMoreDangerousThreat( const INextBot *me, 
															   const CBaseCombatCharacter *subject,
															   const CKnownEntity *threat1, 
															   const CKnownEntity *threat2 ) const;	// return the more dangerous of the two threats to 'subject', or NULL if we have no opinion

	virtual const char *GetName( void ) const	{ return "MainAction"; };

private:
	CountdownTimer m_reloadTimer;
	mutable CountdownTimer m_aimAdjustTimer;
	mutable float m_aimErrorRadius;
	mutable float m_aimErrorAngle;

	float m_yawRate;
	float m_priorYaw;
	IntervalTimer m_steadyTimer;

	int m_nextDisguise;

	bool m_isWaitingForFullReload;

	void FireWeaponAtEnemy( CFFBot *me );

	CHandle< CBaseEntity > m_lastTouch;
	float m_lastTouchTime;

	bool IsImmediateThreat( const CBaseCombatCharacter *subject, const CKnownEntity *threat ) const;
	const CKnownEntity *SelectCloserThreat( CFFBot *me, const CKnownEntity *threat1, const CKnownEntity *threat2 ) const;
	const CKnownEntity *GetHealerOfThreat( const CKnownEntity *threat ) const;

	const CKnownEntity *SelectMoreDangerousThreatInternal( const INextBot *me, 
														   const CBaseCombatCharacter *subject,
														   const CKnownEntity *threat1, 
														   const CKnownEntity *threat2 ) const;


	void Dodge( CFFBot *me );

	IntervalTimer m_undergroundTimer;
};



#endif // FF_BOT_BEHAVIOR_H
