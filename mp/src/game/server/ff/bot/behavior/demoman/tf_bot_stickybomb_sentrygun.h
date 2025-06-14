//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_stickybomb_sentrygun.h
// Destroy the given sentrygun with stickybombs
// Michael Booth, August 2010

#ifndef FF_BOT_STICKYBOMB_SENTRY_H
#define FF_BOT_STICKYBOMB_SENTRY_H

class CObjectSentrygun;


class CFFBotStickybombSentrygun : public Action< CFFBot >
{
public:
	CFFBotStickybombSentrygun( CObjectSentrygun *sentrygun );
	CFFBotStickybombSentrygun( CObjectSentrygun *sentrygun, float aimYaw, float aimPitch, float aimCharge );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual ActionResult< CFFBot >	OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;
	virtual QueryResultType	ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const;					// is it time to retreat?

	virtual const char *GetName( void ) const	{ return "StickybombSentrygun"; };

private:
	float m_givenYaw, m_givenPitch, m_givenCharge;
	bool m_hasGivenAim;

	bool m_isFullReloadNeeded;

	CHandle< CObjectSentrygun > m_sentrygun;

	bool m_isChargingShot;

	CountdownTimer m_searchTimer;
	bool m_hasTarget;
	Vector m_eyeAimTarget;
	Vector m_launchSpot;
	float m_chargeToLaunch;
	float m_searchPitch;
	bool IsAimOnTarget( CFFBot *me, float pitch, float yaw, float charge );
};

#endif // FF_BOT_STICKYBOMB_SENTRY_H
