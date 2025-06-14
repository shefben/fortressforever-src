//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_scenario_monitor.h
// Behavior layer that interrupts for scenario rules (picked up flag, drop what you're doing and capture, etc)
// Michael Booth, May 2011

#ifndef FF_BOT_SCENARIO_MONITOR_H
#define FF_BOT_SCENARIO_MONITOR_H

class CFFBotScenarioMonitor : public Action< CFFBot >
{
public:
	virtual Action< CFFBot > *InitialContainedAction( CFFBot *me );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual const char *GetName( void ) const	{ return "ScenarioMonitor"; }

private:
	CountdownTimer m_ignoreLostFlagTimer;
	CountdownTimer m_lostFlagTimer;

	virtual Action< CFFBot > *DesiredScenarioAndClassAction( CFFBot *me );
};


#endif // FF_BOT_SCENARIO_MONITOR_H
