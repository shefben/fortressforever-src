//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_dispenser.h
// Engineer building his Dispenser near his Sentry
// Michael Booth, May 2010

#ifndef FF_BOT_ENGINEER_BUILD_DISPENSER_H
#define FF_BOT_ENGINEER_BUILD_DISPENSER_H


class CTFBotEngineerBuildDispenser : public Action< CTFBot >
{
public:
	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );
	virtual void					OnEnd( CTFBot *me, Action< CTFBot > *nextAction );

	virtual ActionResult< CTFBot >	OnResume( CTFBot *me, Action< CTFBot > *interruptingAction );

	virtual const char *GetName( void ) const	{ return "EngineerBuildDispenser"; };

private:
	CountdownTimer m_searchTimer;
	CountdownTimer m_getAmmoTimer;
	CountdownTimer m_repathTimer;

	int m_placementTriesLeft;
	PathFollower m_path;
};


#endif // FF_BOT_ENGINEER_BUILD_DISPENSER_H
