//========= Copyright Valve Corporation, All rights reserved. ============//
// Michael Booth, September 2012

#ifndef FF_BOT_MVM_ENGINEER_BUILD_SENTRYGUN_H
#define FF_BOT_MVM_ENGINEER_BUILD_SENTRYGUN_H

class CFFBotHintSentrygun;

class CFFBotMvMEngineerBuildSentryGun : public Action< CFFBot >
{
public:
	CFFBotMvMEngineerBuildSentryGun( CFFBotHintSentrygun* pSentryHint );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "MvMEngineerBuildSentryGun"; };

private:
	CHandle< CFFBotHintSentrygun > m_sentryBuildHint;
	CHandle< CObjectSentrygun > m_sentry;

	CountdownTimer m_delayBuildTime;
	CountdownTimer m_repathTimer;
	PathFollower m_path;
};

#endif // FF_BOT_MVM_ENGINEER_BUILD_SENTRYGUN_H
