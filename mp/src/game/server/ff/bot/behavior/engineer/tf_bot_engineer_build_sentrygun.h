//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_sentrygun.h
// Engineer building his Sentry gun
// Michael Booth, May 2010

#ifndef FF_BOT_ENGINEER_BUILD_SENTRYGUN_H
#define FF_BOT_ENGINEER_BUILD_SENTRYGUN_H

class CFFBotHintSentrygun;


class CFFBotEngineerBuildSentryGun : public Action< CFFBot >
{
public:
	CFFBotEngineerBuildSentryGun( void );
	CFFBotEngineerBuildSentryGun( CFFBotHintSentrygun *sentryBuildHint );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual const char *GetName( void ) const	{ return "EngineerBuildSentryGun"; };

private:
	CountdownTimer m_searchTimer;
	CountdownTimer m_giveUpTimer;
	CountdownTimer m_getAmmoTimer;
	CountdownTimer m_repathTimer;
	CountdownTimer m_buildTeleporterExitTimer;

	int m_sentryTriesLeft;
	PathFollower m_path;

	CFFBotHintSentrygun *m_sentryBuildHint;
	Vector m_sentryBuildLocation;

	int m_wanderWay;
	bool m_needToAimSentry;
	Vector m_sentryBuildAimTarget;
};


#endif // FF_BOT_ENGINEER_BUILD_SENTRYGUN_H
