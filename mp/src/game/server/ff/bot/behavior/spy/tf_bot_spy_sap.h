//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_sap.h
// Sap nearby enemy buildings
// Michael Booth, June 2010

#ifndef FF_BOT_SPY_SAP_H
#define FF_BOT_SPY_SAP_H

#include "Path/NextBotPathFollow.h"

class CFFBotSpySap : public Action< CFFBot >
{
public:
	CFFBotSpySap( CBaseObject *sapTarget );
	virtual ~CFFBotSpySap() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual ActionResult< CFFBot >	OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );

	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?
	virtual QueryResultType	ShouldRetreat( const INextBot *me ) const;							// is it time to retreat?
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const;		// use this to signal the enemy we are focusing on, so we dont avoid them

	virtual const char *GetName( void ) const	{ return "SpySap"; };

private:
	CHandle< CBaseObject > m_sapTarget;

	CountdownTimer m_repathTimer;
	PathFollower m_path;

	CBaseObject *GetNearestKnownSappableTarget( CFFBot *me );
	bool AreAllDangerousSentriesSapped( CFFBot *me ) const;
};

#endif // FF_BOT_SPY_SAP_H
