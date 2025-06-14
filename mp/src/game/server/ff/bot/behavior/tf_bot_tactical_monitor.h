//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_tactical_monitor.h
// Behavior layer that interrupts for ammo/health/retreat/etc
// Michael Booth, June 2009

#ifndef FF_BOT_TACTICAL_MONITOR_H
#define FF_BOT_TACTICAL_MONITOR_H

class CObjectTeleporter;

class CFFBotTacticalMonitor : public Action< CFFBot >
{
public:
	virtual Action< CFFBot > *InitialContainedAction( CFFBot *me );

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );

	virtual EventDesiredResult< CFFBot > OnNavAreaChanged( CFFBot *me, CNavArea *newArea, CNavArea *oldArea );
	virtual EventDesiredResult< CFFBot > OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );

	// @note Tom Bui: Currently used for the training stuff, but once we get that interface down, we will turn that
	// into a proper API
	virtual EventDesiredResult< CFFBot > OnCommandString( CFFBot *me, const char *command );

	virtual const char *GetName( void ) const	{ return "TacticalMonitor"; }

private:
	CountdownTimer m_maintainTimer;

	CountdownTimer m_acknowledgeAttentionTimer;
	CountdownTimer m_acknowledgeRetryTimer;
	CountdownTimer m_attentionTimer;

	CountdownTimer m_stickyBombCheckTimer;
	void MonitorArmedStickyBombs( CFFBot *me );

	bool ShouldOpportunisticallyTeleport( CFFBot *me ) const;
	CObjectTeleporter *FindNearbyTeleporter( CFFBot *me );
	CountdownTimer m_findTeleporterTimer;

	void AvoidBumpingEnemies( CFFBot *me );
};



#endif // FF_BOT_TACTICAL_MONITOR_H
