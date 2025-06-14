//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_dispenser.h
// Engineer building his Dispenser near his Sentry
// Michael Booth, May 2010

#ifndef FF_BOT_ENGINEER_BUILD_DISPENSER_H
#define FF_BOT_ENGINEER_BUILD_DISPENSER_H


class CFFBotEngineerBuildDispenser : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual const char *GetName( void ) const	{ return "EngineerBuildDispenser"; };

private:
	CountdownTimer m_searchTimer;
	CountdownTimer m_getAmmoTimer;
	CountdownTimer m_repathTimer;

	int m_placementTriesLeft;
	PathFollower m_path;
};


#endif // FF_BOT_ENGINEER_BUILD_DISPENSER_H
