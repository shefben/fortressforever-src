//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_infiltrate.h
// Move into position behind enemy lines and wait for victims
// Michael Booth, June 2010

#ifndef FF_BOT_SPY_INFILTRATE_H
#define FF_BOT_SPY_INFILTRATE_H

#include "Path/NextBotPathFollow.h"

class CFFBotSpyInfiltrate : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );
	virtual ActionResult< CFFBot >	OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnTerritoryCaptured( CFFBot *me, int territoryID );
	virtual EventDesiredResult< CFFBot > OnTerritoryLost( CFFBot *me, int territoryID );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?

	virtual const char *GetName( void ) const	{ return "SpyInfiltrate"; };

private:
	CountdownTimer m_repathTimer;
	PathFollower m_path;

	CTFNavArea *m_hideArea;
	bool FindHidingSpot( CFFBot *me );
	CountdownTimer m_findHidingSpotTimer;

	CountdownTimer m_waitTimer;

	bool m_hasEnteredCombatZone;
};


#endif // FF_BOT_SPY_INFILTRATE_H
